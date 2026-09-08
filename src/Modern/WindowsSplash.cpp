#include "WindowsSplash.h"

#if defined(_WIN32)

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "stb_image.h"

namespace
{
struct SplashState
{
    HWND window = nullptr;
    HBITMAP artwork = nullptr;
    HFONT headingFont = nullptr;
    HFONT bodyFont = nullptr;
    int artworkWidth = 0;
    int artworkHeight = 0;
    float progress = 0.02f;
    std::string stage = "INITIALIZING COMMAND SYSTEMS";
};

SplashState splash;

std::wstring executableDirectory()
{
    wchar_t path[32768] = {};
    DWORD length = GetModuleFileNameW(nullptr, path,
        static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (length == 0 || length >= sizeof(path) / sizeof(path[0]))
        return std::wstring();
    std::wstring result(path, length);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() :
           result.substr(0, slash);
}

bool readFile(const std::wstring &path, std::vector<unsigned char> &bytes)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size = {};
    bool ok = GetFileSizeEx(file, &size) != FALSE && size.QuadPart > 0 &&
              size.QuadPart <= 32ll * 1024ll * 1024ll;
    if (ok)
    {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()),
                      &read, nullptr) != FALSE && read == bytes.size();
    }
    CloseHandle(file);
    if (!ok) bytes.clear();
    return ok;
}

bool loadArtwork()
{
    std::wstring base = executableDirectory();
    std::wstring path = base + L"\\UI\\startup_splash.png";
    std::vector<unsigned char> file;
    if (!readFile(path, file))
    {
        path = base + L"\\..\\..\\..\\..\\assets\\UI\\startup_splash.png";
        if (!readFile(path, file)) return false;
    }
    int channels = 0;
    stbi_uc *rgba = stbi_load_from_memory(file.data(),
        static_cast<int>(file.size()), &splash.artworkWidth,
        &splash.artworkHeight, &channels, 4);
    if (rgba == nullptr || splash.artworkWidth <= 0 ||
        splash.artworkHeight <= 0)
    {
        if (rgba != nullptr) stbi_image_free(rgba);
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = splash.artworkWidth;
    info.bmiHeader.biHeight = -splash.artworkHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *dibPixels = nullptr;
    HDC screen = GetDC(nullptr);
    splash.artwork = CreateDIBSection(screen, &info, DIB_RGB_COLORS,
                                      &dibPixels, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (splash.artwork == nullptr || dibPixels == nullptr)
    {
        stbi_image_free(rgba);
        return false;
    }
    unsigned char *bgra = static_cast<unsigned char *>(dibPixels);
    const size_t count = static_cast<size_t>(splash.artworkWidth) *
                         splash.artworkHeight;
    for (size_t index = 0; index < count; ++index)
    {
        bgra[index * 4 + 0] = rgba[index * 4 + 2];
        bgra[index * 4 + 1] = rgba[index * 4 + 1];
        bgra[index * 4 + 2] = rgba[index * 4 + 0];
        bgra[index * 4 + 3] = 255;
    }
    stbi_image_free(rgba);
    return true;
}

float stageProgress(const char *stage)
{
    if (stage == nullptr) return splash.progress;
    if (strstr(stage, "single-instance") != nullptr) return 0.10f;
    if (strstr(stage, "loading saved") != nullptr) return 0.18f;
    if (strstr(stage, "command line") != nullptr) return 0.28f;
    if (strstr(stage, "pre-initialization begin") != nullptr) return 0.36f;
    if (strstr(stage, "pre-initialization complete") != nullptr) return 0.54f;
    if (strstr(stage, "window initialization begin") != nullptr) return 0.60f;
    if (strstr(stage, "window initialization complete") != nullptr) return 0.72f;
    if (strstr(stage, "game-system initialization begin") != nullptr) return 0.78f;
    if (strstr(stage, "game-system initialization complete") != nullptr) return 0.96f;
    if (strstr(stage, "entering main loop") != nullptr) return 1.0f;
    return splash.progress;
}

LRESULT CALLBACK splashWindowProcedure(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint = {};
        HDC target = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        FillRect(target, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (splash.artwork != nullptr)
        {
            HDC source = CreateCompatibleDC(target);
            HGDIOBJ old = SelectObject(source, splash.artwork);
            SetStretchBltMode(target, HALFTONE);
            StretchBlt(target, 0, 0, client.right, client.bottom,
                       source, 0, 0, splash.artworkWidth,
                       splash.artworkHeight, SRCCOPY);
            SelectObject(source, old);
            DeleteDC(source);
        }

        const int margin = std::max(
            22, static_cast<int>(client.right / 28));
        RECT band = { 0, client.bottom - client.bottom / 4,
                      client.right, client.bottom };
        HBRUSH bandBrush = CreateSolidBrush(RGB(4, 8, 12));
        FillRect(target, &band, bandBrush);
        DeleteObject(bandBrush);
        SetBkMode(target, TRANSPARENT);
        SetTextColor(target, RGB(228, 222, 201));
        SelectObject(target, splash.headingFont);
        RECT heading = { margin, band.top + 18, client.right - margin,
                         band.top + 64 };
        DrawTextW(target, L"HOMEWORLD / MODERN", -1, &heading,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SetTextColor(target, RGB(130, 155, 158));
        SelectObject(target, splash.bodyFont);
        wchar_t stageWide[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, splash.stage.c_str(), -1,
                            stageWide, 256);
        RECT status = { margin, band.top + 63, client.right - margin,
                        band.top + 100 };
        DrawTextW(target, stageWide, -1, &status,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT track = { margin, client.bottom - 28,
                       client.right - margin, client.bottom - 22 };
        HBRUSH trackBrush = CreateSolidBrush(RGB(34, 48, 54));
        FillRect(target, &track, trackBrush);
        DeleteObject(trackBrush);
        RECT value = track;
        value.right = value.left + static_cast<LONG>(
            (value.right - value.left) * splash.progress);
        HBRUSH valueBrush = CreateSolidBrush(RGB(222, 162, 62));
        FillRect(target, &value, valueBrush);
        DeleteObject(valueBrush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void pumpMessages()
{
    MSG message = {};
    while (PeekMessageW(&message, splash.window, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
}

extern "C" void hwWindowsSplashStart(void)
{
    if (splash.window != nullptr) return;
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = splashWindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"HomeworldModernStartupSplash";
    RegisterClassW(&windowClass);
    loadArtwork();
    const int desktopWidth = GetSystemMetrics(SM_CXSCREEN);
    const int desktopHeight = GetSystemMetrics(SM_CYSCREEN);
    int width = std::min(1120, desktopWidth * 3 / 5);
    int height = width * 9 / 16;
    if (height > desktopHeight * 4 / 5)
    {
        height = desktopHeight * 4 / 5;
        width = height * 16 / 9;
    }
    splash.headingFont = CreateFontW(
        -std::max(22, height / 22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Bahnschrift SemiCondensed");
    splash.bodyFont = CreateFontW(
        -std::max(13, height / 40), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Bahnschrift");
    splash.window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST, windowClass.lpszClassName,
        L"Homeworld Modern", WS_POPUP,
        (desktopWidth - width) / 2, (desktopHeight - height) / 2,
        width, height, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (splash.window != nullptr)
    {
        ShowWindow(splash.window, SW_SHOWNORMAL);
        UpdateWindow(splash.window);
        pumpMessages();
    }
}

extern "C" void hwWindowsSplashUpdate(const char *stage)
{
    if (stage != nullptr)
    {
        splash.stage = stage;
        std::transform(splash.stage.begin(), splash.stage.end(),
                       splash.stage.begin(), [](unsigned char character)
                       { return static_cast<char>(std::toupper(character)); });
        splash.progress = std::max(splash.progress, stageProgress(stage));
    }
    if (splash.window != nullptr)
    {
        InvalidateRect(splash.window, nullptr, FALSE);
        UpdateWindow(splash.window);
        pumpMessages();
    }
}

extern "C" void hwWindowsSplashFinish(void)
{
    if (splash.window != nullptr)
    {
        DestroyWindow(splash.window);
        splash.window = nullptr;
    }
    if (splash.artwork != nullptr)
    {
        DeleteObject(splash.artwork);
        splash.artwork = nullptr;
    }
    if (splash.headingFont != nullptr)
    {
        DeleteObject(splash.headingFont);
        splash.headingFont = nullptr;
    }
    if (splash.bodyFont != nullptr)
    {
        DeleteObject(splash.bodyFont);
        splash.bodyFont = nullptr;
    }
}

#else

extern "C" void hwWindowsSplashStart(void) {}
extern "C" void hwWindowsSplashUpdate(const char *) {}
extern "C" void hwWindowsSplashFinish(void) {}

#endif
