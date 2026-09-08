/*=============================================================================
    Name    : avi.c
    Purpose : routines for playing AVI files

    Created 1/9/1999 by jdorie, khent, jjoire
    Copyright Relic Entertainment, Inc.  All rights reserved.
=============================================================================*/
/* TC 2003-10-01:
 * Ever so silently, we replace AVI functions with dummy functions when
 * compiling on platforms other than Windows.  Shh...don't tell the game what
 * we're doing...
 */

#include "Animatic.h"
#include "avi.h"
#include "Debug.h"
#include "File.h"
#include "glinc.h"
#include "NIS.h"
#include "Subtitle.h"
#include "Tutor.h"
#include "utility.h"
#include "Universe.h"
#include "render.h"
#include "SoundEvent.h"

//nova: this allows MSVC to compile this file
#ifdef _WIN32
 	#define sleep(x) _sleep((x) * 1000)
#endif

#ifdef HW_ENABLE_MOVIES_WIN32
    #include <windows.h>
    #include <Vfw.h>
    #include "wave.h"
#endif

#ifdef HW_ENABLE_MOVIES
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/imgutils.h>
#endif


/*=============================================================================
    Switches
=============================================================================*/


extern bool32 fullScreen;
extern void* ghMainWindow;
extern int MAIN_WindowWidth;
extern int MAIN_WindowHeight;
extern bool32 systemActive;

bool32 g_bMoreFrames;

int aviDonePlaying = 1;
int aviIsPlaying = 0;
int aviHasAudio = 0;

// Set to 1 if you want to watch the Intros.
udword aviPlayIntros = 0;

#ifdef HW_ENABLE_MOVIES
static AVFormatContext *pFormatCtx = NULL;
static AVCodecContext *pCodecCtx = NULL;
static AVStream *streamPointer = NULL;
static const AVCodec *pCodec = NULL;
static AVFrame *pFrame = NULL;
static int videoStream = -1;
static struct SwsContext *imageConvertContext = NULL;
#endif

int aviMovieExpandFactor = 1;

void aviFileExit(void);

#ifdef HW_ENABLE_MOVIES_WIN32	/* Disable AVI code outside of Windows. */

PAVISTREAM    g_VidStream;      //the AVI video stream
AVISTREAMINFO g_VidStreamInfo;  //info about the AVI stream

PAVISTREAM    g_AudStream;      //the AVI audio stream
AVISTREAMINFO g_AudStreamInfo;  //info about the AVI stream

long          g_dwCurrFrame;    //current frame of the AVI
long          g_dwCurrSample;   //current sample of the AVI
PGETFRAME     g_pFrame;         //an object to hold info about the video frame when we get it

double g_FramesPerSec;
double g_SamplesPerSec;

HBITMAP g_hBitmap = 0;
WORD*   g_pBitmap = 0;

MMRESULT g_timerHandle = NULL;

int aviVerifyResult(HRESULT Result)
{
    return (Result == 0 ? 1 : 0);
}


BOOL aviGetNextFrame(BITMAPINFO** ppbmi)
{
    *ppbmi = (LPBITMAPINFO)AVIStreamGetFrame(g_pFrame, g_dwCurrFrame);
    g_dwCurrFrame++;

    //any more frames left?
    return !(g_dwCurrFrame >= g_VidStreamInfo.dwLength);
}

void aviResetStream(void)
{
    g_dwCurrFrame  = 0;
    g_dwCurrSample = 0;
}


void aviShowFrame(BITMAPINFO* pMap)
{
    HDC hdc, hdcTemp;
    int XbmSize, YbmSize;
    int xOffset, yOffset;

    XbmSize = pMap->bmiHeader.biWidth;
    YbmSize = pMap->bmiHeader.biHeight;

    hdc = GetDC(ghMainWindow);
    g_hBitmap = CreateDIBSection(hdc, pMap, DIB_RGB_COLORS, (VOID**)&g_pBitmap, NULL, NULL);
    SetDIBits(hdc, g_hBitmap, 0, YbmSize, (BYTE*)(&pMap->bmiColors), pMap, DIB_RGB_COLORS);

    hdcTemp = CreateCompatibleDC(hdc);
    SelectObject(hdcTemp, g_hBitmap);

    if (!fullScreen &&
        (XbmSize != MAIN_WindowWidth || YbmSize != MAIN_WindowHeight))
    {
        xOffset = (MAIN_WindowWidth  - XbmSize) / 2;
        yOffset = (MAIN_WindowHeight - YbmSize) / 2;
    }
    else
    {
        xOffset = 0;
        yOffset = 0;
    }
    BitBlt(hdc, xOffset, yOffset, XbmSize, YbmSize, hdcTemp, 0, 0, SRCCOPY);

    DeleteDC(hdcTemp);
    ReleaseDC(ghMainWindow, hdc);

    DeleteObject(g_hBitmap);
}

BITMAPINFO* g_pbmi = NULL;

void CALLBACK aviTimeProc(UINT uid, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	if (g_bMoreFrames)
	{
		g_bMoreFrames = aviGetNextFrame(&g_pbmi);
	}
	else
	{
		aviDonePlaying = TRUE;
	}
}

#endif	/* _WIN32 */

#ifdef HW_ENABLE_MOVIES
static GLint strtex;
static int texinit = 0;
static int strtexWidth = 0;
static int strtexHeight = 0;

static void Draw_Stretch (int x, int y, int w, int h, int cols, int rows, char *data)
{
    if (!texinit || strtexWidth != cols || strtexHeight != rows) {
        if (texinit) glDeleteTextures(1, &strtex);
        glGenTextures(1, &strtex);
        glBindTexture(GL_TEXTURE_2D, strtex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, cols, rows, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, data);
        strtexWidth = cols;
        strtexHeight = rows;
        texinit = 1;
    } else {
        glBindTexture(GL_TEXTURE_2D, strtex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cols, rows, GL_RGB,
                        GL_UNSIGNED_BYTE, data);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);

    glBegin (GL_TRIANGLE_STRIP);
    glTexCoord2f (0.0f, 1.0f); glVertex2f (x, y);
    glTexCoord2f (1.0f, 1.0f); glVertex2f (x+w, y);
    glTexCoord2f (0.0f, 0.0f); glVertex2f (x, y+h);
    glTexCoord2f (1.0f, 0.0f); glVertex2f (x+w, y+h);
    glEnd ();
}

void aviDisplayFrame( AVFrame *pFrameRGB, int w, int h )
{
    animAviSetup(TRUE);

    // Preserve each source movie's encoded aspect ratio and center it.
    float videoAspect = (float)w / (float)h;
    float screenAspect = (float)MAIN_WindowWidth / (float)MAIN_WindowHeight;

    int imageHeight, imageWidth;

    if (screenAspect >= videoAspect)
    {
        imageHeight = MAIN_WindowHeight;
        imageWidth = (int)(MAIN_WindowHeight * videoAspect);
    }
    else
    {
        // screen is taller than the video content, use full screen width
        imageHeight = (int)(MAIN_WindowWidth / videoAspect);
        imageWidth = (int)(MAIN_WindowWidth);
    }

    int x = (MAIN_WindowWidth - imageWidth) / 2;
    int y = (MAIN_WindowHeight - imageHeight) / 2;

    Draw_Stretch(x, y, imageWidth, imageHeight, w, h, pFrameRGB->data[0]);

    animAviSetup(FALSE);
}

#endif

void aviSubUpdate(void) {

    int index;

    for (index = 0; index < SUB_NumberRegions; index++)
    {
        if ( index == STR_LetterboxBar)
        {                                           //if this is the subtitle region
            tutDrawTextPointers(&subRegion[STR_LetterboxBar].rect);//draw any active pointers there may be
        }
        if (subRegion[index].bEnabled && subRegion[index].cardIndex > 0)
        {
            if (index == STR_NIS)
            {
                subTimeElapsed = &thisNisPlaying->timeElapsed;
            }
            else
            {
                subTimeElapsed = &universe.totaltimeelapsed;
            }

            // disable dropshadow for movie subtitles since they are somehow buggy
            // and it seems like they were supposed to be drawn differently
            // TODO: figure out why it is buggy
            
            subcard *card;
            int index2;
            for (index2 = 0, card = subRegion[index].card; index2 < subRegion[index].cardIndex; index2++, card++)
            {
                if (card->bDropShadow)
                {
                    card->bDropShadow = FALSE;
                }
            }

            subTitlesDraw(&subRegion[index]);
        }
    }

}

void aviPlayLoop()
{
#ifdef HW_ENABLE_MOVIES
    AVFrame *rgbFrame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    int frameNumber = 0;
    int64_t firstTimestamp = AV_NOPTS_VALUE;
    double fallbackFrameSeconds = 1.0 / 15.0;
    double startSeconds = (double)SDL_GetPerformanceCounter() /
                          (double)SDL_GetPerformanceFrequency();
    bool32 skipped = FALSE;
    if (rgbFrame == NULL || packet == NULL || pCodecCtx == NULL ||
        av_image_alloc(rgbFrame->data, rgbFrame->linesize,
                       pCodecCtx->width, pCodecCtx->height,
                       AV_PIX_FMT_RGB24, 1) < 0)
    {
        dbgMessage("aviPlayLoop: unable to allocate movie conversion frame");
        if (packet != NULL) av_packet_free(&packet);
        if (rgbFrame != NULL) av_frame_free(&rgbFrame);
        return;
    }
    imageConvertContext = sws_getContext(
        pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_RGB24,
        SWS_BICUBIC, NULL, NULL, NULL);
    if (imageConvertContext == NULL)
    {
        dbgMessage("aviPlayLoop: unable to initialize movie scaler");
        av_freep(&rgbFrame->data[0]);
        av_frame_free(&rgbFrame);
        av_packet_free(&packet);
        return;
    }
    if (streamPointer != NULL)
    {
        AVRational rate = av_guess_frame_rate(pFormatCtx, streamPointer, NULL);
        if (rate.num > 0 && rate.den > 0)
            fallbackFrameSeconds = (double)rate.den / (double)rate.num;
    }

    while (!skipped)
    {
        int readResult = av_read_frame(pFormatCtx, packet);
        if (readResult < 0)
        {
            avcodec_send_packet(pCodecCtx, NULL);
        }
        else if (packet->stream_index == videoStream)
        {
            avcodec_send_packet(pCodecCtx, packet);
        }
        av_packet_unref(packet);

        for (;;)
        {
            int decodeResult = avcodec_receive_frame(pCodecCtx, pFrame);
            if (decodeResult == AVERROR(EAGAIN)) break;
            if (decodeResult == AVERROR_EOF)
            {
                skipped = TRUE;
                break;
            }
            if (decodeResult < 0)
            {
                dbgMessage("aviPlayLoop: movie decoder returned an error");
                skipped = TRUE;
                break;
            }

            sws_scale(imageConvertContext,
                      (const uint8_t * const *)pFrame->data,
                      pFrame->linesize, 0, pCodecCtx->height,
                      rgbFrame->data, rgbFrame->linesize);
            animAviDecode(frameNumber);
            soundEventUpdate();
            speechEventUpdate();
            rndClearToBlack();
            aviDisplayFrame(rgbFrame, pCodecCtx->width, pCodecCtx->height);
            aviSubUpdate();
            rndFlush();

            {
                SDL_Event event;
                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT || event.type == SDL_KEYDOWN ||
                        event.type == SDL_MOUSEBUTTONDOWN)
                    {
                        skipped = TRUE;
                        break;
                    }
                }
            }
            if (skipped) break;

            {
                int64_t timestamp = pFrame->best_effort_timestamp;
                double targetSeconds;
                if (timestamp != AV_NOPTS_VALUE && streamPointer != NULL)
                {
                    if (firstTimestamp == AV_NOPTS_VALUE)
                        firstTimestamp = timestamp;
                    targetSeconds = (double)(timestamp - firstTimestamp) *
                                    av_q2d(streamPointer->time_base);
                }
                else
                {
                    targetSeconds = frameNumber * fallbackFrameSeconds;
                }
                while (!skipped)
                {
                    double now = (double)SDL_GetPerformanceCounter() /
                                 (double)SDL_GetPerformanceFrequency();
                    if (now - startSeconds >= targetSeconds) break;
                    SDL_Delay(1);
                }
            }
            ++frameNumber;
        }
        if (readResult < 0) break;
    }

    aviDonePlaying = TRUE;
    sws_freeContext(imageConvertContext);
    imageConvertContext = NULL;
    av_freep(&rgbFrame->data[0]);
    av_frame_free(&rgbFrame);
    av_packet_free(&packet);
#endif //  HW_ENABLE_MOVIES
}

int aviStart(char* filename)
{

#ifdef HW_ENABLE_MOVIES
    int result;
    char error[AV_ERROR_MAX_STRING_SIZE];
    videoStream = -1;

    if (avformat_open_input(&pFormatCtx, filename, NULL, NULL) < 0) {
        dbgMessagef("aviStart: unable to open movie: %s", filename);
        return FALSE;
    }
    result = avformat_find_stream_info(pFormatCtx, NULL);
    if (result < 0)
    {
        dbgMessagef("aviStart: stream inspection failed for %s", filename);
        aviFileExit();
        return FALSE;
    }
    videoStream = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO,
                                      -1, -1, &pCodec, 0);
    if (videoStream < 0 || pCodec == NULL) {
        dbgMessagef("aviStart: no supported video stream in %s", filename);
        aviFileExit();
        return FALSE;
    }
    streamPointer = pFormatCtx->streams[videoStream];
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (pCodecCtx == NULL) {
        dbgMessage("aviStart: unable to allocate decoder context");
        aviFileExit();
        return FALSE;
    }
    result = avcodec_parameters_to_context(pCodecCtx, streamPointer->codecpar);
    if (result < 0) {
        av_strerror(result, error, sizeof(error));
        dbgMessagef("aviStart: codec parameters failed: %s", error);
        aviFileExit();
        return FALSE;
    }
    result = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (result < 0) {
        av_strerror(result, error, sizeof(error));
        dbgMessagef("aviStart: cannot open %s decoder: %s", pCodec->name, error);
        aviFileExit();
        return FALSE;
    }
    pFrame = av_frame_alloc();
    if (pFrame == NULL) {
        dbgMessage("aviStart: unable to allocate decoded frame");
        aviFileExit();
        return FALSE;
    }
    dbgMessagef("aviStart: playing %s with FFmpeg decoder %s (%dx%d)",
                filename, pCodec->name, pCodecCtx->width, pCodecCtx->height);
#endif // HW_ENABLE_MOVIES
    return TRUE;
}


int aviGetSamples(void* pBuf, long* pNumSamples, long nBufSize)
{
#ifdef HW_ENABLE_MOVIES_WIN32
    HRESULT Res;
    long nSampRead;

	if(aviHasAudio == FALSE) return FALSE;

	if(aviIsPlaying == FALSE)
	{
		memset(pBuf,0,nBufSize);
		return TRUE;
	}

    Res = AVIStreamRead(g_AudStream, g_dwCurrSample, *pNumSamples, pBuf, nBufSize, NULL, &nSampRead);
    if (!aviVerifyResult(Res))
    {
        return FALSE;
    }

    *pNumSamples = nSampRead;
    g_dwCurrSample += nSampRead;

	return TRUE;
#else
    return 0;
#endif
}

void aviFileExit (void){

#ifdef HW_ENABLE_MOVIES
    if (imageConvertContext != NULL)
    {
        sws_freeContext(imageConvertContext);
        imageConvertContext = NULL;
    }
    if (pFrame != NULL) av_frame_free(&pFrame);
    if (pCodecCtx != NULL) avcodec_free_context(&pCodecCtx);
    if (pFormatCtx != NULL) avformat_close_input(&pFormatCtx);
    pCodec = NULL;
    streamPointer = NULL;
    videoStream = -1;
#endif

}
	
void aviSetScreen(int w, int h){

    int xOfs, yOfs;

    xOfs = (MAIN_WindowWidth  - 640) / 2;
    yOfs = (MAIN_WindowHeight - 480) / 2;

dbgMessagef("aviSetScreen: xOfs=%d yOfs=%d", xOfs, yOfs);

}

int aviStop(void)
{
#ifdef HW_ENABLE_MOVIES
    aviFileExit();
    if (texinit)
    {
        glDeleteTextures(1, &strtex);
        texinit = 0;
        strtexWidth = strtexHeight = 0;
    }
#endif
    return 1;
}


bool32 aviPlay(char* filename)
{
#if AVI_VERBOSE_LEVEL >= 2
dbgMessage("aviPlay:Entering");
#endif
    char  fullname[1024];

//TODO  Include Windows file structure. 

    strcpy(fullname, filePathPrepend(filename, FF_HomeworldDataPath));

    //try Homeworld\Data\Movies first
    if (!aviStart(fullname))
    {
        //try current directory next
        if (!aviStart(filename))
        {
            return FALSE;
        }
    }

//taskFreezeAll();

//    aviSetScreen(pCodecCtx->width,pCodecCtx->height);

//    nisFadeToSet(a , 0, b);

    g_bMoreFrames  = TRUE;
    aviIsPlaying   = TRUE;
    aviDonePlaying = FALSE;
    aviPlayLoop();

    aviStop();
    aviIsPlaying = FALSE;
    
    return 1;
}

int aviInit()
{

//#ifdef HW_ENABLE_MOVIES
    //av_register_all(); //can be ignored in ffmpeg 4+
//#endif

    return 1;

}


int aviCleanup()
{
#ifdef HW_ENABLE_MOVIES_WIN32
	//cleanup audio
	if(EndWave() < 0) return FALSE;

    return TRUE;
#else
    return 1;
#endif
}

void aviIntroPlay()
{
    int intro;
    utilPlayingIntro = TRUE;

    for (intro = 0;intro < 4;intro ++) {

        switch (intro) {
            case 0:
                /*binkInit(-1);*/
                aviInit();
//                intro++;
                break;
            case 1:
                /*binkPlay("Movies\\sierra.bik", NULL, NULL, S_RGB555, TRUE, ANIM00_Sierra);*/
                if (aviPlayIntros) {
#ifdef _WIN32
                    aviPlay("Movies\\sierra.bik");
#else
                    aviPlay("Movies/sierra.bik");
#endif
//                intro++;
                }
                break;
            case 2:
                /*binkPlay("Movies\\relicintro.bik", NULL, NULL, S_RGB555, TRUE, ANIM00_Relic);*/
                if (aviPlayIntros) {
#ifdef _WIN32
                    aviPlay("Movies\\relicintro.bik");
#else
                    aviPlay("Movies/relicintro.bik");
#endif
//                    intro++;
                  }
                  break;
            case 3:
                /*binkCleanup();*/
                mainCleanupAfterVideo();
//                intro++;
                break;
//            case 4:
//                goto DONE_INTROS;
        }
    }
}
