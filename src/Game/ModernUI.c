#include "ModernUI.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#include "FontReg.h"
#include "FEColour.h"
#include "File.h"
#include "Formation.h"
#include "Globals.h"
#include "Camera.h"
#include "ClassDefs.h"
#include "ColPick.h"
#include "InfoOverlay.h"
#include "Gun.h"
#include "KeyBindings.h"
#include "ManagerDock.h"
#include "Memory.h"
#include "ModernMissionBackdrop.h"
#include "NIS.h"
#include "ObjTypes.h"
#include "Options.h"
#include "Select.h"
#include "Sensors.h"
#include "ShipView.h"
#include "SinglePlayer.h"
#include "SpaceObj.h"
#include "TaskBar.h"
#include "UIControls.h"
#include "Universe.h"
#include "font.h"
#include "main.h"
#include "mainrgn.h"
#include "mouse.h"
#include "prim2d.h"
#include "render.h"
#include "soundlow.h"
#include "texreg.h"
#include "uiscale.h"
#include "utility.h"

#include "ModernGraphics.h"

#define MUI_VIDEO_TAB_COUNT 4
#define MUI_AUDIO_TAB_COUNT 2
#define MUI_MAIN_SETTINGS_COUNT 4
#define MUI_KEYS_VISIBLE_ROWS 19
#define MUI_COLOR_TAB_COUNT 6
#define MUI_ROW_COUNT 5
#define MUI_GAMEPLAY_ROW_COUNT 6
#define MUI_ART_COUNT 6

typedef struct ModernUIVideoState
{
    bool32 active;
    bool32 inGame;
    sdword tab;
    sdword savedFullScreen;
    sdword savedExclusive;
    sdword savedWidth;
    sdword savedHeight;
    sdword savedRefresh;
    sdword savedFrameCap;
    sdword savedUIScale;
} ModernUIVideoState;

typedef struct ModernUIGameplayState
{
    bool32 active;
    bool32 inGame;
    sdword savedMouseSensitivity;
    sdword savedCursorScalePercent;
    udword savedUnlimitedFuel;
    udword savedPauseOrders;
    udword savedShipRecoil;
    udword savedSuperSalvagers;
    udword savedCaptureBuildAll;
    udword savedResourceMultiplier;
} ModernUIGameplayState;

typedef struct ModernUIAudioState
{
    bool32 active;
    bool32 inGame;
    sdword tab;
} ModernUIAudioState;

typedef struct ModernUIKeysState
{
    bool32 active;
    bool32 inGame;
    regionhandle inputRegion;
    sdword selected;
    sdword scroll;
    bool32 capturing;
    udword invalidUntil;
} ModernUIKeysState;

typedef struct ModernUIColorState
{
    bool32 active;
    sdword mode;
    color base;
    color stripe;
    color engine;
    color nav;
    color resourceBeam;
    color hyperspace;
    bool32 engineOverride;
    bool32 navOverride;
    bool32 resourceBeamOverride;
    bool32 hyperspaceOverride;
} ModernUIColorState;

typedef struct ModernUIDossierState
{
    bool32 active;
    regionhandle region;
    ShipType shipType;
    ShipRace shipRace;
    ShipClass shipClass;
    sdword selectionCount;
    bool32 mixedSelection;
    real32 health;
    real32 maxHealth;
    real32 mass;
    real32 maxVelocity;
    sdword buildCost;
    sdword firepower;
    sdword coverage;
    char shipName[96];
    char maneuverability[48];
    udword imageTexture;
    sdword imageWidth;
    sdword imageHeight;
    bool32 imageAvailable;
} ModernUIDossierState;

#define MUI_LIGHTING_ROW_COUNT 10
#define MUI_LIGHTING_KEY_ROW_COUNT 11
#define MUI_LIGHTING_LIGHT_ROW_COUNT 14
#define MUI_LIGHTING_SHADOW_ROW_COUNT 10
#define MUI_LIGHTING_MAX_ROW_COUNT 14
#define MUI_LIGHTING_PAGE_MISSION 0
#define MUI_LIGHTING_PAGE_LIGHTS 1
#define MUI_LIGHTING_PAGE_TUNING 2
#define MUI_LIGHTING_PAGE_SHADOWS 3
#define MUI_LIGHTING_SYSTEM_KEY 0
#define MUI_LIGHTING_SYSTEM_AMBIENT 1
#define MUI_LIGHTING_SYSTEM_COUNT 2
#define MUI_PI 3.14159265358979323846f
#define MUI_COMMAND_ACTION_COUNT 6
#define MUI_COMMAND_ROSTER_ROWS 5
#define MUI_COMMAND_ROSTER_COLUMNS 2
#define MUI_COMMAND_ROSTER_PAGE_CAPACITY (MUI_COMMAND_ROSTER_ROWS * MUI_COMMAND_ROSTER_COLUMNS)
#define MUI_COMMAND_ICON_ROWS 2

typedef struct ModernUICommandSummary
{
    udword signature;
    sdword count;
    bool32 mixed;
    ShipType primaryType;
    sdword totalFirepower;
    real32 averageFirepower;
    real32 averageSpeed;
    real32 averageCoverage;
} ModernUICommandSummary;

typedef struct ModernUICommandState
{
    regionhandle leftRegion;
    regionhandle rightRegion;
    regionhandle iconRegion;
    regionhandle ruleRegion;
    sdword rosterScroll;
    sdword iconScroll;
    bool32 infoOverlayWasRunning;
    bool32 backdropAttempted;
    udword backdropTexture;
    sdword backdropWidth;
    sdword backdropHeight;
    ModernUICommandSummary summary;
} ModernUICommandState;

typedef struct ModernUICommandLayout
{
    rectangle left;
    rectangle right;
    rectangle iconTray;
    rectangle topRule;
    rectangle stats;
    rectangle selectionName;
    rectangle action[MUI_COMMAND_ACTION_COUNT];
    rectangle dossier;
    rectangle roster;
    rectangle rosterUp;
    rectangle rosterDown;
    rectangle hyperspace;
    bool32 hyperspaceAvailable;
} ModernUICommandLayout;

typedef struct ModernUILightingState
{
    bool32 active;
    regionhandle region;
    regionhandle markerRegion;
    sdword selectedRow;
    sdword page;
    sdword selectedLight;
    bool32 soloKey;
    sdword soloSkyAmbient;
    sdword soloEnvironment;
    sdword soloAuthored;
    sdword soloFx;
    char status[128];
} ModernUILightingState;

typedef struct ModernUILayout
{
    rectangle canvas;
    rectangle rail;
    rectangle content;
    rectangle preview;
    rectangle tabs[MUI_COLOR_TAB_COUNT];
    rectangle rows[MUI_GAMEPLAY_ROW_COUNT];
    rectangle cancel;
    rectangle apply;
} ModernUILayout;

typedef enum ModernUIScreenKind
{
    MUI_SCREEN_GAMEPLAY,
    MUI_SCREEN_FRONTEND,
    MUI_SCREEN_MULTIPLAYER,
    MUI_SCREEN_INGAME,
    MUI_SCREEN_POPUP
} ModernUIScreenKind;

typedef struct ModernUIScreenLayout
{
    ModernUIScreenKind kind;
    rectangle outer;
    rectangle content;
} ModernUIScreenLayout;

typedef struct ModernUIArt
{
    bool32 attempted;
    udword texture;
    sdword width;
    sdword height;
} ModernUIArt;

typedef enum ModernUIArtKind
{
    MUI_ART_GALAXY,
    MUI_ART_CAMPAIGN,
    MUI_ART_FLEET,
    MUI_ART_SYSTEMS,
    MUI_ART_ARCHIVE,
    MUI_ART_PRODUCTION
} ModernUIArtKind;

static ModernUIVideoState muiVideo;
static ModernUIGameplayState muiGameplay;
static ModernUIAudioState muiAudio;
static ModernUIKeysState muiKeys;
static ModernUIColorState muiColor;
static ModernUIDossierState muiDossier;
static ModernUILightingState muiLighting;
static ModernUICommandState muiCommand;
static sdword muiMainSelection;
static sdword muiMainSettingsSelection;
static real32 muiMainSettingsExpansion;
static udword muiMainSettingsLastTick;
static bool32 muiMainSettingsKeyboardOpen;
static fescreen *muiMainScreen;
static sdword muiConnectionSelection;
static fescreen *muiConnectionScreen;
static fonthandle muiHeadingFont;
static fonthandle muiBodyFont;
static fonthandle muiCompactFont;
static fonthandle muiTinyFont;
static ModernUIArt muiArtwork[MUI_ART_COUNT];
static udword muiHueSatTexture;
static udword muiValueTexture;
static sdword muiValueHue = -1;
static sdword muiValueSaturation = -1;

/* Existing manager activity flags.  Keep the per-ship tactical strip out of
   the right-side manager dock while Build/Research/Launch is open. */
extern bool32 cmActive;
extern bool32 rmGUIActive;
extern bool32 lmActive;

static const char *muiArtworkFiles[MUI_ART_COUNT] = {
    "menu_backdrop.png",
    "campaign_plate.png",
    "fleet_plate.png",
    "systems_plate.png",
    "archive_plate.png",
    "production_plate.png"
};

static const color muiInk = colRGB(7, 12, 17);
static const color muiSurface = colRGBA(15, 25, 32, 238);
static const color muiSurfaceRaised = colRGBA(24, 36, 43, 244);
static const color muiLine = colRGB(86, 111, 120);
static const color muiLineSoft = colRGBA(72, 94, 102, 118);
static const color muiPaper = colRGB(225, 222, 206);
static const color muiMuted = colRGB(143, 157, 157);
static const color muiSignal = colRGB(222, 162, 62);
static const color muiSignalSoft = colRGBA(222, 162, 62, 42);
static const color muiCool = colRGB(105, 184, 196);
static const color muiDisabled = colRGB(74, 82, 84);
static const color muiDanger = colRGB(190, 82, 68);

static void muiDrawButtonRect(const rectangle *rect, const char *label,
                              bool32 primary, bool32 hovered);
static void muiApplyRendererLive(void);

void modernUIApplyPalette(void)
{
    FEC_ListItemTranslucent = muiSignalSoft;
    FEC_ListItemTranslucentBorder = muiSignal;
    FEC_ListItemStandard = muiMuted;
    FEC_ListItemSelected = muiPaper;
    FEC_ListItemInactive = muiDisabled;
    FEC_ListItemInvalid = colRGB(151, 83, 70);
    FEC_Background = muiInk;
    TB_SelectedColor = muiPaper;
    TB_HyperspaceColor = muiSignal;
    TB_CompleteColor = muiCool;
    TB_IncompleteColor = muiMuted;
}

static sdword muiClamp(sdword value, sdword low, sdword high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool32 muiPointIn(const rectangle *rect, sdword x, sdword y)
{
    return x >= rect->x0 && x < rect->x1 && y >= rect->y0 && y < rect->y1;
}

static bool32 muiAtomIsButtonLike(const featom *atom)
{
    if (atom == NULL) return FALSE;
    return atom->type == FA_Button || atom->type == FA_ToggleButton ||
           atom->type == FA_CheckBox || atom->type == FA_RadioButton ||
           atom->type == FA_BitmapButton || atom->type == FA_DragButton;
}

static bool32 muiBuildActionAtom(const featom *atom)
{
    /* The construction FIB lays four very wide 1999-era action buttons across
       the bottom edge.  They dominate the manager bounds even though the live
       build content is much narrower.  Small bitmap/location buttons are not
       action buttons and must stay with the content. */
    return muiAtomIsButtonLike(atom) && atom->loadedWidth >= 90 &&
           atom->loadedHeight <= 56;
}

static bool32 muiBuildActionCaption(const fescreen *screen, const featom *atom)
{
    sdword index;
    sdword cx;
    sdword cy;
    if (screen == NULL || atom == NULL || atom->type != FA_StaticText)
        return FALSE;
    cx = atom->loadedX + atom->loadedWidth / 2;
    cy = atom->loadedY + atom->loadedHeight / 2;
    for (index = 1; index < screen->nAtoms; ++index)
    {
        const featom *button = &screen->atoms[index];
        if (!muiBuildActionAtom(button)) continue;
        if (cx >= button->loadedX &&
            cx < button->loadedX + button->loadedWidth &&
            cy >= button->loadedY &&
            cy < button->loadedY + button->loadedHeight)
            return TRUE;
    }
    return FALSE;
}

static rectangle muiRect(sdword x0, sdword y0, sdword x1, sdword y1)
{
    rectangle result;
    result.x0 = x0;
    result.y0 = y0;
    result.x1 = x1;
    result.y1 = y1;
    return result;
}

static void muiFill(const rectangle *rect, color c)
{
    rectangle copy = *rect;
    if (colAlpha(c) < 255)
    {
        primRectTranslucent2(&copy, c);
    }
    else
    {
        primRectSolid2(&copy, c);
    }
}
static void muiText(sdword x, sdword y, color c, const char *text,
                    fonthandle font)
{
    fonthandle old = fontMakeCurrent(font);
    fontPrint(x, y, c, (char *)text);
    fontMakeCurrent(old);
}

static void muiTextRight(sdword x, sdword y, color c, const char *text,
                         fonthandle font)
{
    fonthandle old = fontMakeCurrent(font);
    fontPrint(x - fontWidth((char *)text), y, c, (char *)text);
    fontMakeCurrent(old);
}

static void muiTextCentered(const rectangle *rect, color c, const char *text,
                            fonthandle font)
{
    fonthandle old = fontMakeCurrent(font);
    sdword width = fontWidth((char *)text);
    sdword height = fontHeight((char *)text);
    fontPrint((rect->x0 + rect->x1 - width) / 2,
              (rect->y0 + rect->y1 - height) / 2,
              c, (char *)text);
    fontMakeCurrent(old);
}

static void muiEnsureFonts(void)
{
    if (muiBodyFont == 0)
    {
        muiBodyFont = frFontRegister("simplixssk_13.hff");
        /* Both faces ship with the original Homeworld data. */
        muiHeadingFont = frFontRegister("eurosewideheavy_i17.hff");
        if (muiHeadingFont == 0)
            muiHeadingFont = muiBodyFont;

        /* Gameplay chrome needs to be able to shrink text before it ever
           resorts to clipping.  These are existing Homeworld fonts; the
           front-end/menu layout is not changed by using them here. */
        muiCompactFont = frFontRegister("HW_EuroseCond_11.hff");
        if (muiCompactFont == 0)
            muiCompactFont = frFontRegister("hw_eurosecond_11.hff");
        if (muiCompactFont == 0)
            muiCompactFont = frFontRegister("Arial_12.hff");
        if (muiCompactFont == 0)
            muiCompactFont = muiBodyFont;

        muiTinyFont = frFontRegister("Small_Fonts_8.hff");
        if (muiTinyFont == 0)
            muiTinyFont = muiCompactFont;
    }
}

static bool32 muiNameIs(const char *name, const char *candidate)
{
    return name != NULL && strcmp(name, candidate) == 0;
}

static bool32 muiNameStartsWith(const char *name, const char *prefix)
{
    return name != NULL && strncmp(name, prefix, strlen(prefix)) == 0;
}

static bool32 muiGameplayScreen(const char *name)
{
    static const char *screens[] = {
        "Construction_manager", "Research_Manager", "Launch_Manager",
        "Task_Bar", "Sensors_manager", "FleetIntel",
        "Single_Player_Objective", "Say_Chatting_Screen",
        "Allies_Chatting_Screen", "RightClickMenu",
        "Group_Formations_submenu", "Group_Tactics_submenu",
        "PlayerListRightClick", "PlayerListRightClickAlly",
        "Trader_Interface", "HyperspaceRollCall", "Horse_Race",
        "Horse_Race_Single", "Horse_Race_NonNetwork", "AbortLoadConfirm"
    };
    sdword index;
    for (index = 0; index < (sdword)(sizeof(screens) / sizeof(screens[0])); ++index)
    {
        if (muiNameIs(name, screens[index])) return TRUE;
    }
    return FALSE;
}

static bool32 muiManagerScreen(const char *name)
{
    return muiNameIs(name, "Construction_manager") ||
           muiNameIs(name, "Research_Manager") ||
           muiNameIs(name, "Launch_Manager");
}

static bool32 muiPopupScreen(const fescreen *screen)
{
    const char *name;
    if (screen == NULL || screen->nAtoms == 0) return FALSE;
    name = screen->name;
    if (bitTest(screen->atoms[0].flags, FAF_Popup)) return TRUE;
    return (name != NULL &&
            (strstr(name, "Popup") != NULL ||
             strstr(name, "Message_Box") != NULL ||
             strstr(name, "_Alert") != NULL ||
             strstr(name, "Invalid_Key") != NULL ||
             strstr(name, "Press_Key") != NULL ||
             strstr(name, "Countdown_") != NULL ||
             muiNameIs(name, "Quit_game") || muiNameIs(name, "Quit_game2") ||
             muiNameIs(name, "SP_Game_Over") || muiNameIs(name, "Player_Drop")));
}

static bool32 muiInGameScreen(const char *name)
{
    static const char *screens[] = {
        "Custom_options", "Game_options", "Audio", "Gameplay", "Computer",
        "Keys", "Advanced_speech", "InGameEqualizer",
        "InGameVideo_Explanation", "Game_record_save", "Game_lesson_load",
        "Delete_recorded_game_popup", "DeleteGamePopup", "DeleteGamePopup2",
        "In_Game_Key_Pool", "In_Game_Invalid_Key", "SP_Game_Over",
        "Player_Drop"
    };
    sdword index;
    if (muiNameStartsWith(name, "In_game_")) return TRUE;
    for (index = 0; index < (sdword)(sizeof(screens) / sizeof(screens[0])); ++index)
    {
        if (muiNameIs(name, screens[index])) return TRUE;
    }
    return FALSE;
}

static bool32 muiMultiplayerScreen(const char *name)
{
    static const char *screens[] = {
        "Internet_Login", "LAN_Login", "LLAN_Login", "Channel_Chat",
        "LChannel_Chat", "Available_Channels", "Create_Channel",
        "Available_Games", "LAvailable_Games", "Player_Options",
        "Player_Wait", "LPlayer_Wait", "Captain_Wait", "LCaptain_Wait",
        "Basic_Options", "Advanced_Options", "Resource_Options",
        "Skirmish_Basic", "Skirmish_Advanced", "Skirmish_Resource",
        "Game_Password", "LGame_Password"
    };
    sdword index;
    for (index = 0; index < (sdword)(sizeof(screens) / sizeof(screens[0])); ++index)
        if (muiNameIs(name, screens[index])) return TRUE;
    return FALSE;
}

static ModernUIScreenKind muiScreenKind(const fescreen *screen)
{
    if (screen == NULL || muiGameplayScreen(screen->name))
        return MUI_SCREEN_GAMEPLAY;
    if (muiPopupScreen(screen)) return MUI_SCREEN_POPUP;
    if (muiInGameScreen(screen->name)) return MUI_SCREEN_INGAME;
    if (muiMultiplayerScreen(screen->name)) return MUI_SCREEN_MULTIPLAYER;
    return MUI_SCREEN_FRONTEND;
}

static ModernUIScreenLayout muiScreenLayout(ModernUIScreenKind kind)
{
    ModernUIScreenLayout layout;
    sdword margin = uiScaleSize(22);
    sdword width;
    sdword height;
    layout.kind = kind;
    layout.outer = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
    layout.content = layout.outer;

    if (kind == MUI_SCREEN_FRONTEND || kind == MUI_SCREEN_MULTIPLAYER)
    {
        layout.content.x0 = MAIN_WindowWidth >= uiScaleSize(900) ?
            MAIN_WindowWidth * (kind == MUI_SCREEN_MULTIPLAYER ? 23 : 39) / 100 : margin;
        layout.content.x1 = MAIN_WindowWidth - margin;
        layout.content.y0 = muiClamp(MAIN_WindowHeight / 11,
                                     uiScaleSize(76), uiScaleSize(132));
        layout.content.y1 = MAIN_WindowHeight - margin;
    }
    else if (kind == MUI_SCREEN_INGAME)
    {
        width = muiClamp(MAIN_WindowWidth * 46 / 100,
                         uiScaleSize(430), uiScaleSize(760));
        height = muiClamp(MAIN_WindowHeight * 86 / 100,
                          uiScaleSize(360), MAIN_WindowHeight - margin * 2);
        layout.outer = muiRect(margin, (MAIN_WindowHeight - height) / 2,
                               margin + width,
                               (MAIN_WindowHeight - height) / 2 + height);
        layout.content = muiRect(layout.outer.x0 + uiScaleSize(18),
                                 layout.outer.y0 + uiScaleSize(66),
                                 layout.outer.x1 - uiScaleSize(18),
                                 layout.outer.y1 - uiScaleSize(18));
    }
    else if (kind == MUI_SCREEN_POPUP)
    {
        width = muiClamp(MAIN_WindowWidth * 44 / 100,
                         uiScaleSize(360), uiScaleSize(720));
        height = muiClamp(MAIN_WindowHeight * 48 / 100,
                          uiScaleSize(230), uiScaleSize(520));
        layout.outer = muiRect((MAIN_WindowWidth - width) / 2,
                               (MAIN_WindowHeight - height) / 2,
                               (MAIN_WindowWidth + width) / 2,
                               (MAIN_WindowHeight + height) / 2);
        layout.content = muiRect(layout.outer.x0 + uiScaleSize(20),
                                 layout.outer.y0 + uiScaleSize(62),
                                 layout.outer.x1 - uiScaleSize(20),
                                 layout.outer.y1 - uiScaleSize(20));
    }
    return layout;
}

static fescreen *muiScreenForRegion(regionhandle region)
{
    sdword index;
    featom *baseAtom;
    if (region == NULL) return NULL;
    baseAtom = (featom *)region->userID;
    for (index = feStackIndex; index >= 0; --index)
    {
        fescreen *screen = feStack[index].screen;
        if (screen != NULL && screen->nAtoms > 0 && &screen->atoms[0] == baseAtom)
            return screen;
    }
    return NULL;
}

static void muiHumanizeTitle(const char *name, char *result, size_t count)
{
    size_t index = 0;
    if (count == 0) return;
    if (name == NULL) name = "INTERFACE";
    while (*name != '\0' && index + 1 < count)
    {
        char c = *name++;
        if (c == '_') c = ' ';
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        result[index++] = c;
    }
    result[index] = '\0';
}

static ModernUIArtKind muiArtworkForScreen(const char *name)
{
    udword hash = 2166136261u;
    const char *cursor = name;
    if (name == NULL) return MUI_ART_GALAXY;
    if (strstr(name, "Single") != NULL || strstr(name, "Mission") != NULL ||
        strstr(name, "Tutorial") != NULL || strstr(name, "Campaign") != NULL)
        return MUI_ART_CAMPAIGN;
    if (strstr(name, "Multi") != NULL || strstr(name, "Fleet") != NULL ||
        strstr(name, "Build") != NULL || strstr(name, "Research") != NULL ||
        strstr(name, "Launch") != NULL)
        return MUI_ART_FLEET;
    if (strstr(name, "Video") != NULL || strstr(name, "Options") != NULL ||
        strstr(name, "Computer") != NULL || strstr(name, "Audio") != NULL ||
        strstr(name, "Gameplay") != NULL || strstr(name, "Key") != NULL)
        return MUI_ART_SYSTEMS;
    if (strstr(name, "Credit") != NULL || strstr(name, "Archive") != NULL ||
        strstr(name, "Record") != NULL)
        return MUI_ART_ARCHIVE;
    /* Stable variety for the remaining screens.  This is deliberately keyed
       by screen name so the art never changes while a screen is visible. */
    while (cursor != NULL && *cursor != '\0')
    {
        hash ^= (udword)(unsigned char)*cursor++;
        hash *= 16777619u;
    }
    return (ModernUIArtKind)(MUI_ART_CAMPAIGN + hash % 4u);
}

static bool32 muiEnsureArtwork(ModernUIArtKind kind)
{
    ModernUIArt *art;
    char path[1024];
    char *basePath;
    void *fileData = NULL;
    size_t fileSize = 0;
    int width;
    int height;
    int channels;
    stbi_uc *pixels;
    if (kind < 0 || kind >= MUI_ART_COUNT) kind = MUI_ART_GALAXY;
    art = &muiArtwork[kind];
    if (art->attempted) return art->texture != 0;
    art->attempted = TRUE;
    basePath = SDL_GetBasePath();
    path[0] = '\0';
    if (basePath != NULL)
    {
        snprintf(path, sizeof(path), "%sUI/%s", basePath,
                 muiArtworkFiles[kind]);
        fileData = SDL_LoadFile(path, &fileSize);
        if (fileData == NULL)
        {
            /* Developer-tree fallback: Release/ -> windows-x64/ -> build/
               -> out/ -> source root. */
            snprintf(path, sizeof(path),
                     "%s../../../../assets/UI/%s", basePath,
                     muiArtworkFiles[kind]);
            fileData = SDL_LoadFile(path, &fileSize);
        }
        SDL_free(basePath);
    }
    if (fileData == NULL)
    {
        snprintf(path, sizeof(path), "assets/UI/%s", muiArtworkFiles[kind]);
        fileData = SDL_LoadFile(path, &fileSize);
    }
    if (fileData == NULL)
    {
        snprintf(path, sizeof(path), "UI/%s", muiArtworkFiles[kind]);
        fileData = SDL_LoadFile(path, &fileSize);
    }
    if (fileData == NULL)
    {
        fprintf(stderr, "[ModernUI] Optional interface artwork not found: %s\n", path);
        return FALSE;
    }
    pixels = stbi_load_from_memory((const stbi_uc *)fileData, (int)fileSize,
                                   &width, &height, &channels, 4);
    SDL_free(fileData);
    if (pixels == NULL || width <= 0 || height <= 0)
    {
        if (pixels != NULL) stbi_image_free(pixels);
        fprintf(stderr, "[ModernUI] Could not decode interface artwork.\n");
        return FALSE;
    }
    art->texture = trRGBTextureCreate((color *)pixels, width, height, TRUE);
    art->width = width;
    art->height = height;
    stbi_image_free(pixels);
    if (art->texture != 0)
    {
        fprintf(stderr, "[ModernUI] Interface artwork loaded: %s (%dx%d).\n",
                path, width, height);
    }
    return art->texture != 0;
}

static void muiDrawArtworkInRect(ModernUIArtKind kind,
                                 const rectangle *target)
{
    ModernUIArt *art;
    rectangle rect;
    real32 u0 = 0.0f;
    real32 u1 = 1.0f;
    real32 v0 = 0.0f;
    real32 v1 = 1.0f;
    real32 imageAspect;
    real32 screenAspect;
    sdword oldTexture;
    udword oldMode;
    if (target == NULL)
        return;
    rect = *target;
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) return;
    if (!muiEnsureArtwork(kind))
    {
        muiFill(&rect, muiInk);
        return;
    }
    art = &muiArtwork[kind];
    imageAspect = (real32)art->width / (real32)art->height;
    screenAspect = (real32)(rect.x1 - rect.x0) /
                   (real32)(rect.y1 - rect.y0);
    if (screenAspect > imageAspect)
    {
        real32 visible = imageAspect / screenAspect;
        v0 = (1.0f - visible) * 0.5f;
        v1 = 1.0f - v0;
    }
    else
    {
        real32 visible = screenAspect / imageAspect;
        u0 = (1.0f - visible) * 0.5f;
        u1 = 1.0f - u0;
    }
    trRGBTextureMakeCurrent(art->texture);
    oldTexture = rndTextureEnable(TRUE);
    oldMode = rndTextureEnvironment(RTE_Replace);
    glBegin(GL_TRIANGLE_STRIP);
    glColor3ub(255, 255, 255);
    glTexCoord2f(u0, v0); glVertex2f(primScreenToGLX(rect.x0), primScreenToGLY(rect.y0));
    glTexCoord2f(u0, v1); glVertex2f(primScreenToGLX(rect.x0), primScreenToGLY(rect.y1));
    glTexCoord2f(u1, v0); glVertex2f(primScreenToGLX(rect.x1), primScreenToGLY(rect.y0));
    glTexCoord2f(u1, v1); glVertex2f(primScreenToGLX(rect.x1), primScreenToGLY(rect.y1));
    glEnd();
    rndTextureEnvironment(oldMode);
    rndTextureEnable(oldTexture);
}

static void muiDrawBackdrop(ModernUIArtKind kind)
{
    rectangle rect = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
    muiDrawArtworkInRect(kind, &rect);
}

static bool32 muiLoadOptionalUITexture(const char *relativePath,
                                       udword *texture,
                                       sdword *widthOut,
                                       sdword *heightOut)
{
    char path[1024];
    char *basePath;
    void *fileData = NULL;
    size_t fileSize = 0;
    stbi_uc *pixels;
    int width, height, channels;

    if (relativePath == NULL || texture == NULL || widthOut == NULL ||
        heightOut == NULL)
    {
        return FALSE;
    }

    basePath = SDL_GetBasePath();
    if (basePath != NULL)
    {
        snprintf(path, sizeof(path), "%sUI/%s", basePath, relativePath);
        fileData = SDL_LoadFile(path, &fileSize);
        if (fileData == NULL)
        {
            snprintf(path, sizeof(path), "%s../../../../assets/UI/%s",
                     basePath, relativePath);
            fileData = SDL_LoadFile(path, &fileSize);
        }
        SDL_free(basePath);
    }
    if (fileData == NULL)
    {
        snprintf(path, sizeof(path), "assets/UI/%s", relativePath);
        fileData = SDL_LoadFile(path, &fileSize);
    }
    if (fileData == NULL)
    {
        snprintf(path, sizeof(path), "UI/%s", relativePath);
        fileData = SDL_LoadFile(path, &fileSize);
    }
    if (fileData == NULL)
    {
        return FALSE;
    }

    pixels = stbi_load_from_memory((const stbi_uc *)fileData, (int)fileSize,
                                   &width, &height, &channels, 4);
    SDL_free(fileData);
    if (pixels == NULL || width <= 0 || height <= 0)
    {
        if (pixels != NULL) stbi_image_free(pixels);
        return FALSE;
    }
    *texture = trRGBTextureCreate((color *)pixels, width, height, TRUE);
    *widthOut = width;
    *heightOut = height;
    stbi_image_free(pixels);
    return *texture != 0;
}

static void muiDrawTextureInRect(udword texture, sdword imageWidth,
                                 sdword imageHeight,
                                 const rectangle *target)
{
    rectangle rect;
    real32 u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
    real32 imageAspect, screenAspect;
    sdword oldTexture;
    udword oldMode;

    if (texture == 0 || target == NULL || imageWidth <= 0 || imageHeight <= 0)
    {
        return;
    }
    rect = *target;
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) return;

    imageAspect = (real32)imageWidth / (real32)imageHeight;
    screenAspect = (real32)(rect.x1 - rect.x0) /
                   (real32)(rect.y1 - rect.y0);
    if (screenAspect > imageAspect)
    {
        real32 visible = imageAspect / screenAspect;
        v0 = (1.0f - visible) * 0.5f;
        v1 = 1.0f - v0;
    }
    else
    {
        real32 visible = screenAspect / imageAspect;
        u0 = (1.0f - visible) * 0.5f;
        u1 = 1.0f - u0;
    }

    trRGBTextureMakeCurrent(texture);
    oldTexture = rndTextureEnable(TRUE);
    oldMode = rndTextureEnvironment(RTE_Replace);
    glBegin(GL_TRIANGLE_STRIP);
    glColor3ub(255, 255, 255);
    glTexCoord2f(u0, v0); glVertex2f(primScreenToGLX(rect.x0), primScreenToGLY(rect.y0));
    glTexCoord2f(u0, v1); glVertex2f(primScreenToGLX(rect.x0), primScreenToGLY(rect.y1));
    glTexCoord2f(u1, v0); glVertex2f(primScreenToGLX(rect.x1), primScreenToGLY(rect.y0));
    glTexCoord2f(u1, v1); glVertex2f(primScreenToGLX(rect.x1), primScreenToGLY(rect.y1));
    glEnd();
    rndTextureEnvironment(oldMode);
    rndTextureEnable(oldTexture);
}

static void muiDossierReleaseImage(void)
{
    if (muiDossier.imageTexture != 0)
    {
        trRGBTextureDelete(muiDossier.imageTexture);
    }
    muiDossier.imageTexture = 0;
    muiDossier.imageWidth = 0;
    muiDossier.imageHeight = 0;
    muiDossier.imageAvailable = FALSE;
}

static void muiDossierLoadArchiveImage(const Ship *ship)
{
    char relativePath[256];
    const char *raceFolder;
    const char *shipTypeName;

    muiDossierReleaseImage();
    if (ship == NULL || universe.curPlayerPtr == NULL ||
        muiDossier.selectionCount != 1)
    {
        return;
    }

    /* A manual plate is shown only for a native hull of the player's current
       fleet race.  Captured foreign ships intentionally stay text-only. */
    if (ship->shiprace != universe.curPlayerPtr->race)
    {
        return;
    }
    if (ship->shiprace == R1)
    {
        raceFolder = "R1";
    }
    else if (ship->shiprace == R2)
    {
        raceFolder = "R2";
    }
    else
    {
        return;
    }

    shipTypeName = ShipTypeToStr(ship->shiptype);
    if (shipTypeName == NULL || shipTypeName[0] == '\0')
    {
        return;
    }
    snprintf(relativePath, sizeof(relativePath), "Dossier/%s/%s.png",
             raceFolder, shipTypeName);
    muiDossier.imageAvailable = muiLoadOptionalUITexture(
        relativePath, &muiDossier.imageTexture,
        &muiDossier.imageWidth, &muiDossier.imageHeight);
}

static const char *muiDossierRaceName(ShipRace race)
{
    switch (race)
    {
        case R1: return "KUSHAN";
        case R2: return "TAIIDAN";
        case P1: return "TURANIC";
        case P2: return "KADESH";
        case P3: return "BENTUSI";
        case Traders: return "TRADER";
        default: return "UNCLASSIFIED";
    }
}

/* Condensed, modernized summaries based on the original 1999 Fleet Archive
 * ship-data section.  Unknown/non-player craft fall back to a class record so
 * the panel remains useful for every valid selection. */
static const char *muiDossierBrief(ShipType type)
{
    switch (type)
    {
        case LightInterceptor:
            return "A fast, inexpensive reconnaissance fighter. Extreme agility makes it ideal for diversion, rapid scouting and light harassment.";
        case HeavyInterceptor:
            return "A dedicated fighter killer with heavier guns than the Scout. Interceptor wings can also threaten capital ships through concentrated fire.";
        case AttackBomber:
            return "A strike fighter built around compact plasma bombs. Slow ordnance limits anti-fighter use, but it is devastating against frigates and capital hulls.";
        case CloakedFighter:
            return "A stealth fighter using cloaking sails for surveillance and ambush. It must reveal itself to fire, creating a brief window of vulnerability.";
        case LightDefender:
        case HeavyDefender:
            return "A defensive fighter whose gimballed weapons and powerful attitude thrusters provide exceptional firing coverage around protected formations.";
        case DefenseFighter:
            return "A mobile defensive platform that intercepts incoming fire with its emitter dome, trading conventional armament for fleet protection.";
        case LightCorvette:
            return "A durable corvette with a fast-tracking turret capable of engaging fighters at speed. Its heavier chassis sacrifices fighter-class agility.";
        case HeavyCorvette:
            return "A heavily armored twin-turret corvette able to track multiple fighters while remaining a credible threat to capital ships.";
        case MultiGunCorvette:
            return "Six articulating turrets give this corvette superb coverage against numerous fast targets, making it lethal to clustered fighter wings.";
        case RepairCorvette:
            return "A field-support craft for repairing and refueling strike craft. Heavy armor helps it survive beside the units it services.";
        case SalCapCorvette:
            return "A towing and salvage craft adapted to capture disabled enemy ships and return them to a carrier or mothership for refitting.";
        case MinelayerCorvette:
            return "A corvette able to seed drifting mines or deploy a defensive mine wall. Its area-denial role is particularly dangerous to capital formations.";
        case StandardFrigate:
            return "The fleet's direct-combat frigate: armored, heavily armed and optimized for concentrated forward fire against other capital ships.";
        case IonCannonFrigate:
            return "A frigate-scale ion weapon platform. Its spinal beam delivers exceptional anti-capital damage at the cost of broad firing coverage.";
        case AdvanceSupportFrigate:
            return "A mobile strike-craft service platform with docking points for repair and refueling. It is durable, but should not operate without escort.";
        case DDDFrigate:
            return "A carrier for autonomous defense drones. The deployed screen creates dense anti-fighter coverage around the parent frigate.";
        case DFGFrigate:
            return "A frigate devoted to projecting a defensive field, protecting nearby ships from incoming fire rather than mounting an offensive battery.";
        case MissileDestroyer:
            return "A guided-missile capital ship effective against both strike craft and heavy targets, with internal manufacturing for sustained salvos.";
        case StandardDestroyer:
            return "A first-choice capital-ship hunter combining twin ion cannons, heavy turrets and enough mobility to react quickly within a battle line.";
        case Carrier:
            return "A mobile construction and docking center able to build frigates and service large strike-craft groups while providing its own deck-gun defense.";
        case HeavyCruiser:
            return "The fleet's largest conventional warship. Multiple ion cannon mounts and heavy turrets make its arrival a decisive battlefield event.";
        case Mothership:
            return "Fleet command, navigation, foundry and cryogenic ark in one immense hull. It is the industrial and strategic center of the expedition.";
        case ResourceCollector:
            return "A molecular extraction vessel that converts asteroids, dust and gas into the resource units required for fleet construction.";
        case ResourceController:
            return "A forward resource drop-off and strike-craft service node that reduces collector transit time and extends operational range.";
        case CloakGenerator:
            return "A capital utility ship that projects a temporary cloaking field large enough to conceal nearby fleet elements.";
        case GravWellGenerator:
            return "A specialized generator whose gravity field arrests strike craft inside its radius, controlling space at the cost of limited operating life.";
        case Probe:
            return "A one-use reconnaissance vehicle with an extremely powerful short-duration engine. Once deployed at range, it becomes stationary.";
        case ProximitySensor:
            return "A compact remote sensor package capable of detecting cloaked vessels and extending the fleet's local warning network.";
        case SensorArray:
            return "A large sensor platform that expands fleet awareness, revealing distant contacts and resource concentrations across the battlespace.";
        case ResearchShip:
            return "A complete mobile science facility. Multiple research ships link together, expanding capability and aggregate structural resilience.";
        default:
            return "A fleet-recognized hull recorded in the command archive. Tactical role and performance are summarized from its loaded ship specification.";
    }
}

static sdword muiWrappedText(sdword x, sdword y, sdword width,
                             const char *text, color c, fonthandle font,
                             sdword maxLines)
{
    char line[256];
    char word[96];
    char test[352];
    const char *cursor = text;
    sdword lineCount = 0;
    sdword lineHeight;
    fonthandle old = fontMakeCurrent(font);
    line[0] = '\0';
    lineHeight = fontHeight("M") + uiScaleSize(3);

    while (*cursor != '\0' && lineCount < maxLines)
    {
        size_t wordLength = 0;
        while (*cursor == ' ') ++cursor;
        while (cursor[wordLength] != '\0' && cursor[wordLength] != ' ' &&
               wordLength + 1 < sizeof(word))
        {
            ++wordLength;
        }
        if (wordLength == 0) break;
        memcpy(word, cursor, wordLength);
        word[wordLength] = '\0';
        cursor += wordLength;
        snprintf(test, sizeof(test), "%s%s%s", line,
                 line[0] != '\0' ? " " : "", word);
        if (line[0] != '\0' && fontWidth(test) > width)
        {
            fontPrint(x, y + lineCount * lineHeight, c, line);
            ++lineCount;
            line[0] = '\0';
            if (lineCount >= maxLines) break;
        }
        if (line[0] != '\0') strncat(line, " ", sizeof(line) - strlen(line) - 1);
        strncat(line, word, sizeof(line) - strlen(line) - 1);
    }
    if (line[0] != '\0' && lineCount < maxLines)
    {
        fontPrint(x, y + lineCount * lineHeight, c, line);
        ++lineCount;
    }
    fontMakeCurrent(old);
    return lineCount * lineHeight;
}

static rectangle muiDossierCloseRect(void)
{
    rectangle panel;
    mdGetPanelRect(&panel);
    return muiRect(panel.x0 + uiScaleSize(12),
                   panel.y1 - uiScaleSize(44),
                   panel.x1 - uiScaleSize(12),
                   panel.y1 - uiScaleSize(10));
}

static void muiDrawDossier(regionhandle region)
{
    rectangle panel = region->rect;
    rectangle wash = panel;
    rectangle accent = panel;
    rectangle rule;
    rectangle close = muiDossierCloseRect();
    rectangle imageRect;
    char text[160];
    sdword x = panel.x0 + uiScaleSize(16);
    sdword right = panel.x1 - uiScaleSize(16);
    sdword y;
    bool32 hasImage = muiDossier.imageAvailable && muiDossier.imageTexture != 0;
    (void)region;

    muiEnsureFonts();
    muiDrawArtworkInRect(MUI_ART_ARCHIVE, &panel);
    muiFill(&wash, colRGBA(9, 17, 23, 226));
    primRectOutline2(&panel, 1, muiLine);
    accent.x1 = accent.x0 + uiScaleSize(3);
    muiFill(&accent, muiSignal);

    muiText(x, panel.y0 + uiScaleSize(14), muiSignal,
            "FLEET ARCHIVE / SHIP DOSSIER", muiBodyFont);
    muiText(x, panel.y0 + uiScaleSize(38), muiPaper,
            muiDossier.shipName, muiHeadingFont);
    snprintf(text, sizeof(text), "%s / %s CLASS",
             muiDossierRaceName(muiDossier.shipRace),
             ShipClassToNiceStr(muiDossier.shipClass));
    muiText(x, panel.y0 + uiScaleSize(67), muiCool, text, muiBodyFont);

    if (muiDossier.selectionCount > 1)
    {
        snprintf(text, sizeof(text), "%d SHIPS SELECTED%s",
                 muiDossier.selectionCount,
                 muiDossier.mixedSelection ? " / MIXED GROUP / PRIMARY ENTRY" : " / GROUP ENTRY");
        muiText(x, panel.y0 + uiScaleSize(87), muiMuted, text, muiBodyFont);
    }
    else if (hasImage)
    {
        muiText(x, panel.y0 + uiScaleSize(87), muiMuted,
                "ORIGINAL FLEET MANUAL ARCHIVE PLATE", muiBodyFont);
    }
    else
    {
        muiText(x, panel.y0 + uiScaleSize(87), muiMuted,
                "TEXT RECORD / CAPTURED OR UNCATALOGUED HULL", muiBodyFont);
    }

    rule = muiRect(x, panel.y0 + uiScaleSize(108), right,
                   panel.y0 + uiScaleSize(109));
    muiFill(&rule, muiLineSoft);

    if (hasImage)
    {
        sdword imageWidth = uiScaleSize(154);
        sdword imageHeight = uiScaleSize(126);
        imageRect = muiRect(right - imageWidth,
                            panel.y0 + uiScaleSize(120),
                            right,
                            panel.y0 + uiScaleSize(120) + imageHeight);
        muiFill(&imageRect, muiInk);
        muiDrawTextureInRect(muiDossier.imageTexture,
                             muiDossier.imageWidth,
                             muiDossier.imageHeight,
                             &imageRect);
        primRectOutline2(&imageRect, 1, muiLine);

        y = panel.y0 + uiScaleSize(122);
        snprintf(text, sizeof(text), "HULL  %.0f / %.0f",
                 muiDossier.health, muiDossier.maxHealth);
        muiText(x, y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(22);
        snprintf(text, sizeof(text), "MASS  %.0f t", muiDossier.mass);
        muiText(x, y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(22);
        snprintf(text, sizeof(text), "VELOCITY  %.0f m/s",
                 muiDossier.maxVelocity);
        muiText(x, y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(22);
        snprintf(text, sizeof(text), "FIREPOWER  %d", muiDossier.firepower);
        muiText(x, y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(22);
        snprintf(text, sizeof(text), "COVERAGE  %d%%", muiDossier.coverage);
        muiText(x, y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(22);
        snprintf(text, sizeof(text), "COST  %d RU", muiDossier.buildCost);
        muiText(x, y, muiPaper, text, muiBodyFont);
    }
    else
    {
        y = panel.y0 + uiScaleSize(122);
        snprintf(text, sizeof(text), "HULL  %.0f / %.0f", muiDossier.health,
                 muiDossier.maxHealth);
        muiText(x, y, muiPaper, text, muiBodyFont);
        snprintf(text, sizeof(text), "MASS  %.0f t", muiDossier.mass);
        muiText(x + uiScaleSize(184), y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(24);
        snprintf(text, sizeof(text), "VELOCITY  %.0f m/s", muiDossier.maxVelocity);
        muiText(x, y, muiPaper, text, muiBodyFont);
        snprintf(text, sizeof(text), "COST  %d RU", muiDossier.buildCost);
        muiText(x + uiScaleSize(184), y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(24);
        snprintf(text, sizeof(text), "FIREPOWER  %d", muiDossier.firepower);
        muiText(x, y, muiPaper, text, muiBodyFont);
        snprintf(text, sizeof(text), "COVERAGE  %d%%", muiDossier.coverage);
        muiText(x + uiScaleSize(184), y, muiPaper, text, muiBodyFont);
        y += uiScaleSize(24);
        snprintf(text, sizeof(text), "MANEUVERABILITY  %s",
                 muiDossier.maneuverability);
        muiText(x, y, muiPaper, text, muiBodyFont);
    }

    rule = muiRect(x, panel.y0 + uiScaleSize(258), right,
                   panel.y0 + uiScaleSize(259));
    muiFill(&rule, muiLineSoft);
    muiText(x, panel.y0 + uiScaleSize(272), muiSignal,
            "ARCHIVE ASSESSMENT", muiBodyFont);
    snprintf(text, sizeof(text), "MANEUVERABILITY  %s", muiDossier.maneuverability);
    muiTextRight(right, panel.y0 + uiScaleSize(272), muiCool, text, muiBodyFont);
    muiWrappedText(x, panel.y0 + uiScaleSize(296), right - x,
                   muiDossierBrief(muiDossier.shipType), muiMuted,
                   muiBodyFont, 5);

    muiDrawButtonRect(&close, "CLOSE DOSSIER", FALSE,
                      muiPointIn(&close, mouseCursorX(), mouseCursorY()));
}

static udword muiDossierProcess(regionhandle region, smemsize ID,
                                udword event, udword data)
{
    rectangle close;
    (void)region;
    (void)data;
    if (event == RPE_KeyDown && ID == ESCKEY)
    {
        modernUICloseShipDossier();
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft)
    {
        close = muiDossierCloseRect();
        if (muiPointIn(&close, mouseCursorX(), mouseCursorY()))
        {
            modernUICloseShipDossier();
            return RPR_Redraw;
        }
    }
    return 0;
}

void modernUICloseShipDossier(void)
{
    regionhandle region = muiDossier.region;
    muiDossier.active = FALSE;
    muiDossier.region = NULL;
    muiDossierReleaseImage();
    if (region != NULL)
    {
        regRegionDelete(region);
    }
}

void modernUIShowShipDossier(Ship *ship, sdword selectionCount,
                             bool32 mixedSelection)
{
    ShipStaticInfo *info;
    rectangle panel;
    if (ship == NULL || ship->staticinfo == NULL || ghMainRegion == NULL)
    {
        return;
    }
    info = ship->staticinfo;
    modernUICloseLightingEditor();
    mdCloseAllManagers();
    muiEnsureFonts();

    muiDossier.active = TRUE;
    muiDossier.shipType = ship->shiptype;
    muiDossier.shipRace = ship->shiprace;
    muiDossier.shipClass = info->shipclass;
    muiDossier.selectionCount = selectionCount;
    muiDossier.mixedSelection = mixedSelection;
    muiDossier.health = ship->health;
    muiDossier.maxHealth = info->maxhealth;
    muiDossier.mass = info->staticheader.mass;
    muiDossier.maxVelocity = info->staticheader.maxvelocity;
    if (singlePlayerGame && opSuperSalvagers &&
        ship->playerowner == universe.curPlayerPtr &&
        ship->shiptype == SalCapCorvette)
    {
        /* Dossier numbers report effective gameplay values while the canonical
           simulation health bar remains normalized to the original hull. */
        muiDossier.health *= 3.0f;
        muiDossier.maxHealth *= 3.0f;
        muiDossier.maxVelocity *= 2.0f;
    }
    muiDossier.buildCost = info->buildCost;
    /* The legacy cached fields are never populated for a number of ships in
       current data sets.  Derive the dossier values from the same weapon and
       geometry calculators used by the original ship-view statistics. */
    muiDossier.firepower = info->svFirePower != 0 ? info->svFirePower :
        (sdword)(gunShipFirePower(info, Neutral) + 0.5f);
    muiDossier.coverage = svShipCoverage(info);
    snprintf(muiDossier.shipName, sizeof(muiDossier.shipName), "%s",
             ShipTypeToNiceStr(ship->shiptype));
    svShipManeuverability(info, muiDossier.maneuverability);
    muiDossierLoadArchiveImage(ship);

    if (muiDossier.region == NULL)
    {
        mdGetPanelRect(&panel);
        muiDossier.region = regChildAlloc(
            ghMainRegion, (smemsize)&muiDossier, panel.x0, panel.y0,
            panel.x1 - panel.x0, panel.y1 - panel.y0, 0,
            RPE_PressLeft | RPE_ReleaseLeft | RPE_DrawEveryFrame);
        regDrawFunctionSet(muiDossier.region, muiDrawDossier);
        regFunctionSet(muiDossier.region, muiDossierProcess);
        regKeyChildAlloc(muiDossier.region, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiDossierProcess, 1, ESCKEY);
    }
    regSiblingMoveToFront(muiDossier.region);
    regRecursiveSetDirty(muiDossier.region);
    fprintf(stderr, "[ModernUI] Ship dossier opened for %s (%d selected).\n",
            muiDossier.shipName, selectionCount);
}


static bool32 muiCommandEnsureBackdrop(void)
{
    if (muiCommand.backdropAttempted)
    {
        return muiCommand.backdropTexture != 0;
    }
    muiCommand.backdropAttempted = TRUE;
    return muiLoadOptionalUITexture(
        "CommandBar/tactical_backdrop.png", &muiCommand.backdropTexture,
        &muiCommand.backdropWidth, &muiCommand.backdropHeight);
}

static void muiCommandDrawBackdropSegment(const rectangle *target)
{
    sdword oldTexture;
    udword oldMode;
    real32 u0;
    real32 u1;
    rectangle tint;

    if (target == NULL || target->x1 <= target->x0 || target->y1 <= target->y0)
    {
        return;
    }
    if (!muiCommandEnsureBackdrop())
    {
        muiFill(target, muiSurface);
        return;
    }

    /* Map one continuous cinematic strip across the entire lower command
       deck. Left/right panels therefore show different portions of the same
       artwork instead of repeating the image independently. */
    u0 = (real32)target->x0 / (real32)(MAIN_WindowWidth > 0 ? MAIN_WindowWidth : 1);
    u1 = (real32)target->x1 / (real32)(MAIN_WindowWidth > 0 ? MAIN_WindowWidth : 1);
    trRGBTextureMakeCurrent(muiCommand.backdropTexture);
    oldTexture = rndTextureEnable(TRUE);
    oldMode = rndTextureEnvironment(RTE_Replace);
    glBegin(GL_TRIANGLE_STRIP);
    glColor3ub(255, 255, 255);
    glTexCoord2f(u0, 0.0f); glVertex2f(primScreenToGLX(target->x0), primScreenToGLY(target->y0));
    glTexCoord2f(u0, 1.0f); glVertex2f(primScreenToGLX(target->x0), primScreenToGLY(target->y1));
    glTexCoord2f(u1, 0.0f); glVertex2f(primScreenToGLX(target->x1), primScreenToGLY(target->y0));
    glTexCoord2f(u1, 1.0f); glVertex2f(primScreenToGLX(target->x1), primScreenToGLY(target->y1));
    glEnd();
    rndTextureEnvironment(oldMode);
    rndTextureEnable(oldTexture);

    /* Bring the photographic plate into the same luminance family as the
       vanilla center taskbar. Controls remain readable and the art reads as
       material under glass, not wallpaper. */
    tint = *target;
    muiFill(&tint, colRGBA(15, 25, 32, 214));
}

static bool32 muiCommandBarVisible(void)
{
    return gameIsRunning &&
           (mrRenderMainScreen || (smSensorsActive && !smFleetIntel)) &&
           !nisIsRunning && tbInfoBarVisible();
}

static bool32 muiCommandHyperspaceAvailable(void)
{
    return singlePlayerGame && singlePlayerGameInfo.playerCanHyperspace;
}

static void muiCommandIconTrayMetrics(const rectangle *right, sdword count,
                                      rectangle *tray, sdword *iconSize,
                                      sdword *columns, sdword *gapOut,
                                      sdword *capacityOut)
{
    rectangle center;
    sdword gap = uiScaleSize(2);
    sdword padding = uiScaleSize(2);
    sdword preferred = uiScaleSize(20);
    sdword minimum = uiScaleSize(12);
    sdword navWidth = 0;
    sdword layoutGap = uiScaleSize(3);
    sdword margin = uiScaleSize(3);
    sdword rowHeight = uiScaleSize(20);
    sdword trayWidth;
    sdword maxTrayWidth;
    sdword availableWidth;
    sdword size;
    sdword cols;
    sdword capacity;

    if (tray != NULL) *tray = muiRect(0, 0, 0, 0);
    if (iconSize != NULL) *iconSize = 0;
    if (columns != NULL) *columns = 0;
    if (gapOut != NULL) *gapOut = gap;
    if (capacityOut != NULL) *capacityOut = 0;
    if (right == NULL || cmActive || rmGUIActive || lmActive)
        return;

    /* The user's placement is inside the bottom taskbar itself: the compact
       per-ship tray occupies the upper-right portion of the CENTER deck,
       immediately left of SELECTED SHIPS.  The lower Sensors/Build/Research/
       Launch/Resources row remains full width below it.  TaskBar.c reserves
       this same upper-right rectangle from the objectives list. */
    tbGetInfoBarRect(&center);
    if (center.x1 <= center.x0 || center.y1 <= center.y0) return;

    if (rowHeight < 14) rowHeight = 14;
    if (rowHeight > (center.y1 - center.y0) / 2)
        rowHeight = (center.y1 - center.y0) / 2;

    trayWidth = (center.x1 - center.x0) * 32 / 100;
    if (trayWidth > uiScaleSize(300)) trayWidth = uiScaleSize(300);
    if (trayWidth < uiScaleSize(160)) trayWidth = uiScaleSize(160);
    maxTrayWidth = (center.x1 - center.x0) * 45 / 100;
    if (trayWidth > maxTrayWidth) trayWidth = maxTrayWidth;
    if (trayWidth <= minimum + padding * 2) return;

    if (tray != NULL)
    {
        tray->x1 = center.x1 - margin;
        tray->x0 = tray->x1 - trayWidth;
        tray->y0 = center.y0 + margin;
        tray->y1 = center.y1 - margin - rowHeight - layoutGap;
        if (tray->y1 <= tray->y0)
            *tray = muiRect(0, 0, 0, 0);
    }

    /* Keep the placement stable when the selection is empty.  Only the cards
       disappear; the taskbar geometry never jumps around as selections change. */
    if (count <= 0) return;

    availableWidth = trayWidth - padding * 2;
    if (availableWidth <= minimum) return;

    size = preferred;
    if (size > availableWidth) size = availableWidth;
    if (size < minimum) size = minimum;
    cols = (availableWidth + gap) / (size + gap);
    if (cols < 1) cols = 1;
    capacity = cols * MUI_COMMAND_ICON_ROWS;

    if (count > capacity)
    {
        navWidth = uiScaleSize(18);
        availableWidth -= navWidth;
        if (availableWidth < minimum) availableWidth = minimum;
        cols = (availableWidth + gap) / (size + gap);
        if (cols < 1) cols = 1;
        capacity = cols * MUI_COMMAND_ICON_ROWS;
    }

    if (iconSize != NULL) *iconSize = size;
    if (columns != NULL) *columns = cols;
    if (capacityOut != NULL) *capacityOut = capacity;
}

static rectangle muiCommandIconRect(const rectangle *tray, sdword index,
                                    sdword iconSize, sdword columns,
                                    sdword gap)
{
    sdword padding = uiScaleSize(2);
    sdword row = columns > 0 ? index / columns : 0;
    sdword col = columns > 0 ? index % columns : 0;
    sdword gridHeight = MUI_COMMAND_ICON_ROWS * iconSize +
                        (MUI_COMMAND_ICON_ROWS - 1) * gap;
    sdword availableHeight = tray->y1 - tray->y0 - padding * 2;
    sdword verticalInset = availableHeight > gridHeight
        ? (availableHeight - gridHeight) / 2 : 0;
    sdword x0 = tray->x0 + padding + col * (iconSize + gap);
    sdword y0 = tray->y0 + padding + verticalInset + row * (iconSize + gap);
    return muiRect(x0, y0, x0 + iconSize, y0 + iconSize);
}

static void muiCommandLayoutCompute(ModernUICommandLayout *layout)
{
    rectangle infoRect;
    sdword margin = uiScaleSize(6);
    sdword gap = uiScaleSize(4);
    sdword hudHeight = uiScaleSize(108);
    sdword infoHeight = 0;
    sdword leftWidth;
    sdword rightWidth;
    sdword statsWidth;
    sdword dossierWidth;
    sdword middleLeft;
    sdword middleRight;
    sdword nameHeight = uiScaleSize(24);
    sdword actionTop;
    sdword actionHeight;
    sdword actionWidth;
    sdword rowHeight;
    sdword actionColumns;
    sdword actionRows;
    sdword col;
    sdword row;
    sdword index;
    sdword hyperHeight = 0;

    memset(layout, 0, sizeof(*layout));
    tbGetInfoBarRect(&infoRect);
    if (infoRect.y0 > 0 && infoRect.y0 < MAIN_WindowHeight)
    {
        infoHeight = MAIN_WindowHeight - infoRect.y0;
    }
    /* Match the original center bar's top edge whenever its geometry is
       sane.  This makes the three pieces read as one measured instrument
       panel instead of three unrelated overlays. */
    if (infoHeight >= uiScaleSize(58) && infoHeight <= uiScaleSize(168))
    {
        hudHeight = infoHeight;
    }

    leftWidth = (MAIN_WindowWidth * 34) / 100;
    if (leftWidth > uiScaleSize(448)) leftWidth = uiScaleSize(448);
    if (leftWidth < uiScaleSize(300)) leftWidth = uiScaleSize(300);
    if (leftWidth > MAIN_WindowWidth / 2) leftWidth = MAIN_WindowWidth / 2;

    rightWidth = (MAIN_WindowWidth * 21) / 100;
    if (rightWidth > uiScaleSize(300)) rightWidth = uiScaleSize(300);
    if (rightWidth < uiScaleSize(190)) rightWidth = uiScaleSize(190);
    if (rightWidth > MAIN_WindowWidth / 3) rightWidth = MAIN_WindowWidth / 3;

    /* Respect the real vanilla taskbar footprint.  On wide displays this
       leaves the intended breathing room; on narrow displays the side decks
       shrink instead of covering the original center controls. */
    if (infoRect.x1 > infoRect.x0 &&
        infoRect.x0 >= uiScaleSize(80) &&
        infoRect.x1 <= MAIN_WindowWidth - uiScaleSize(80))
    {
        sdword leftAvailable = infoRect.x0 - gap;
        sdword rightAvailable = MAIN_WindowWidth - infoRect.x1 - gap;
        if (leftAvailable > uiScaleSize(120) && leftWidth > leftAvailable)
            leftWidth = leftAvailable;
        if (rightAvailable > uiScaleSize(120) && rightWidth > rightAvailable)
            rightWidth = rightAvailable;
    }

    layout->left = muiRect(0, MAIN_WindowHeight - hudHeight,
                           leftWidth, MAIN_WindowHeight);
    layout->right = muiRect(MAIN_WindowWidth - rightWidth,
                            MAIN_WindowHeight - hudHeight,
                            MAIN_WindowWidth, MAIN_WindowHeight);
    muiCommandIconTrayMetrics(&layout->right, selSelected.numShips,
                              &layout->iconTray, NULL, NULL, NULL, NULL);
    layout->topRule = muiRect(0, layout->left.y0,
                              MAIN_WindowWidth, layout->left.y0 + uiScaleSize(1));

    statsWidth = (leftWidth * 27) / 100;
    dossierWidth = (leftWidth * 14) / 100;
    layout->stats = muiRect(layout->left.x0 + margin,
                            layout->left.y0 + margin,
                            layout->left.x0 + margin + statsWidth,
                            layout->left.y1 - margin);
    layout->dossier = muiRect(layout->left.x1 - margin - dossierWidth,
                              layout->left.y0 + margin,
                              layout->left.x1 - margin,
                              layout->left.y1 - margin);
    middleLeft = layout->stats.x1 + gap;
    middleRight = layout->dossier.x0 - gap;
    layout->selectionName = muiRect(middleLeft,
                                    layout->left.y0 + margin,
                                    middleRight,
                                    layout->left.y0 + margin + nameHeight);
    actionTop = layout->selectionName.y1 + gap;
    /* Dense tactical deck: use the available width before spending another
       row of vertical space.  The captions use a deliberately small common
       face, so all six actions fit as a clean 3x2 matrix without truncation. */
    actionColumns = (middleRight - middleLeft >= uiScaleSize(132)) ? 3 : 2;
    actionRows = (MUI_COMMAND_ACTION_COUNT + actionColumns - 1) / actionColumns;
    actionHeight = (layout->left.y1 - margin - actionTop -
                    gap * (actionRows - 1)) / actionRows;
    actionWidth = (middleRight - middleLeft -
                   gap * (actionColumns - 1)) / actionColumns;
    for (index = 0; index < MUI_COMMAND_ACTION_COUNT; ++index)
    {
        row = index / actionColumns;
        col = index % actionColumns;
        layout->action[index] = muiRect(
            middleLeft + col * (actionWidth + gap),
            actionTop + row * (actionHeight + gap),
            middleLeft + col * (actionWidth + gap) + actionWidth,
            actionTop + row * (actionHeight + gap) + actionHeight);
    }

    layout->hyperspaceAvailable = muiCommandHyperspaceAvailable();
    if (layout->hyperspaceAvailable)
    {
        hyperHeight = uiScaleSize(18);
        layout->hyperspace = muiRect(layout->right.x0 + margin,
                                     layout->right.y1 - margin - hyperHeight,
                                     layout->right.x1 - margin,
                                     layout->right.y1 - margin);
    }
    layout->roster = muiRect(layout->right.x0 + margin,
                             layout->right.y0 + margin,
                             layout->right.x1 - margin,
                             layout->hyperspaceAvailable ?
                                 layout->hyperspace.y0 - gap :
                                 layout->right.y1 - margin);

    rowHeight = (layout->roster.y1 - layout->roster.y0 - uiScaleSize(22)) /
                MUI_COMMAND_ROSTER_ROWS;
    if (rowHeight < uiScaleSize(12)) rowHeight = uiScaleSize(12);
    layout->rosterUp = muiRect(layout->roster.x1 - uiScaleSize(18),
                               layout->roster.y0 + uiScaleSize(22),
                               layout->roster.x1,
                               layout->roster.y0 + uiScaleSize(22) + rowHeight * 2);
    layout->rosterDown = muiRect(layout->roster.x1 - uiScaleSize(18),
                                 layout->rosterUp.y1,
                                 layout->roster.x1,
                                 layout->roster.y1);
}

static udword muiCommandSelectionSignature(void)
{
    udword signature = 2166136261u;
    sdword index;
    signature ^= (udword)selSelected.numShips;
    signature *= 16777619u;
    for (index = 0; index < selSelected.numShips; ++index)
    {
        smemsize value = (smemsize)selSelected.ShipPtr[index];
        signature ^= (udword)value;
#ifdef _X86_64
        signature ^= (udword)(((uqword)value) >> 32);
#else
        signature ^= (udword)(value >> 16);
#endif
        signature *= 16777619u;
    }
    return signature;
}

static void muiCommandSummaryUpdate(void)
{
    udword signature = muiCommandSelectionSignature();
    sdword index;
    real32 firepowerSum = 0.0f;
    real32 speedSum = 0.0f;
    real32 coverageSum = 0.0f;

    if (muiCommand.summary.signature == signature &&
        muiCommand.summary.count == selSelected.numShips)
    {
        return;
    }

    memset(&muiCommand.summary, 0, sizeof(muiCommand.summary));
    muiCommand.summary.signature = signature;
    muiCommand.summary.count = selSelected.numShips;
    muiCommand.rosterScroll = 0;
    muiCommand.iconScroll = 0;
    if (selSelected.numShips <= 0)
    {
        return;
    }

    muiCommand.summary.primaryType = selSelected.ShipPtr[0]->shiptype;
    for (index = 0; index < selSelected.numShips; ++index)
    {
        Ship *ship = selSelected.ShipPtr[index];
        ShipStaticInfo *info = ship != NULL ? ship->staticinfo : NULL;
        real32 firepower = 0.0f;
        if (ship == NULL || info == NULL)
        {
            continue;
        }
        if (ship->shiptype != muiCommand.summary.primaryType)
        {
            muiCommand.summary.mixed = TRUE;
        }
        firepower = info->svFirePower != 0 ? (real32)info->svFirePower :
                    gunShipFirePower(info, Neutral);
        firepowerSum += firepower;
        if (singlePlayerGame && opSuperSalvagers &&
            ship->playerowner == universe.curPlayerPtr &&
            ship->shiptype == SalCapCorvette)
            speedSum += info->staticheader.maxvelocity * 2.0f;
        else
            speedSum += info->staticheader.maxvelocity;
        coverageSum += (real32)svShipCoverage(info);
    }

    muiCommand.summary.totalFirepower = (sdword)(firepowerSum + 0.5f);
    muiCommand.summary.averageFirepower = firepowerSum /
                                          (real32)selSelected.numShips;
    muiCommand.summary.averageSpeed = speedSum /
                                      (real32)selSelected.numShips;
    muiCommand.summary.averageCoverage = coverageSum /
                                         (real32)selSelected.numShips;
}

static real32 muiCommandAverageHealthFraction(void)
{
    real32 total = 0.0f;
    sdword valid = 0;
    sdword index;

    /* Health changes continuously without changing the selection signature,
       so do not cache it in muiCommand.summary.  Sample the live ships for
       the HUD each frame instead.  In Homeworld's ship stats, maxhealth is
       also the value presented as armor, making this a compact live
       HEALTH/ARMOR condition meter rather than another static dossier field. */
    for (index = 0; index < selSelected.numShips; ++index)
    {
        Ship *ship = selSelected.ShipPtr[index];
        real32 fraction;
        if (ship == NULL || ship->staticinfo == NULL ||
            ship->staticinfo->maxhealth <= 0.0f)
            continue;
        fraction = ship->health / ship->staticinfo->maxhealth;
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        total += fraction;
        ++valid;
    }
    return valid > 0 ? total / (real32)valid : 0.0f;
}

static sdword muiCommandBuildRosterGroups(ShipType *types, sdword *counts,
                                           sdword capacity)
{
    sdword typeCounts[TOTAL_NUM_SHIPS];
    sdword index;
    sdword groupCount = 0;
    memset(typeCounts, 0, sizeof(typeCounts));
    if (types == NULL || counts == NULL || capacity <= 0) return 0;

    for (index = 0; index < selSelected.numShips; ++index)
    {
        Ship *ship = selSelected.ShipPtr[index];
        sdword type;
        if (ship == NULL) continue;
        type = (sdword)ship->shiptype;
        if (type >= 0 && type < TOTAL_NUM_SHIPS) ++typeCounts[type];
    }
    /* Enumeration order is stable and roughly follows the ship catalogue, so
       grouped selections do not jump around merely because click order did. */
    for (index = 0; index < TOTAL_NUM_SHIPS && groupCount < capacity; ++index)
    {
        if (typeCounts[index] <= 0) continue;
        types[groupCount] = (ShipType)index;
        counts[groupCount] = typeCounts[index];
        ++groupCount;
    }
    return groupCount;
}

static void muiCommandApplySelection(MaxSelection *selection)
{
    if (selection == NULL) return;
    selSelected = *selection;
    if (selSelected.numShips > 0)
        selCentrePointCompute();
    ioUpdateShipTotals();
    muiCommand.summary.signature = 0;
    muiCommand.rosterScroll = 0;
    muiCommand.iconScroll = 0;
}

static void muiCommandSelectOnlyType(ShipType type)
{
    MaxSelection filtered;
    memset(&filtered, 0, sizeof(filtered));
    filtered.timeLastStatus = selSelected.timeLastStatus;
    selSelectionCopyByType(&filtered, &selSelected, type);
    if (filtered.numShips > 0)
        muiCommandApplySelection(&filtered);
}

static void muiCommandSelectOnlyShip(Ship *ship)
{
    MaxSelection single;
    if (ship == NULL || ship->playerowner != universe.curPlayerPtr) return;
    memset(&single, 0, sizeof(single));
    single.timeLastStatus = selSelected.timeLastStatus;
    single.numShips = 1;
    single.ShipPtr[0] = ship;
    muiCommandApplySelection(&single);
}

static void muiCommandRemoveShipFromSelection(Ship *ship)
{
    MaxSelection reduced;
    if (ship == NULL) return;
    reduced = selSelected;
    selSelectionRemoveSingleShip(&reduced, ship);
    if (reduced.numShips == selSelected.numShips) return;
    muiCommandApplySelection(&reduced);
}

static void muiCommandRemoveTypeFromSelection(ShipType type)
{
    MaxSelection reduced;
    sdword index;
    reduced = selSelected;
    for (index = reduced.numShips - 1; index >= 0; --index)
    {
        Ship *ship = reduced.ShipPtr[index];
        if (ship != NULL && ship->shiptype == type)
            selSelectionRemoveSingleShip(&reduced, ship);
    }
    if (reduced.numShips == selSelected.numShips) return;
    muiCommandApplySelection(&reduced);
}

static void muiCommandDrawShipGlyph(const rectangle *rect, const Ship *ship,
                                    color c)
{
    sdword cx;
    sdword cy;
    sdword w;
    sdword h;
    sdword x0;
    sdword x1;
    sdword y0;
    sdword y1;
    sdword shipClass;
    if (rect == NULL || ship == NULL || ship->staticinfo == NULL) return;
    w = rect->x1 - rect->x0;
    h = rect->y1 - rect->y0;
    if (w < 4 || h < 4) return;
    cx = (rect->x0 + rect->x1) / 2;
    cy = (rect->y0 + rect->y1) / 2;
    x0 = rect->x0 + 1;
    x1 = rect->x1 - 1;
    y0 = rect->y0 + 1;
    y1 = rect->y1 - 1;
    shipClass = ship->staticinfo->shipclass;

    switch (shipClass)
    {
        case CLASS_Fighter:
            primLine2(cx, y0, x1, y1, c);
            primLine2(x1, y1, cx, cy + h / 5, c);
            primLine2(cx, cy + h / 5, x0, y1, c);
            primLine2(x0, y1, cx, y0, c);
            break;
        case CLASS_Corvette:
            primLine2(cx, y0, x1, cy, c);
            primLine2(x1, cy, cx, y1, c);
            primLine2(cx, y1, x0, cy, c);
            primLine2(x0, cy, cx, y0, c);
            primLine2(x0, cy, x1, cy, c);
            break;
        case CLASS_Frigate:
            primLine2(x0, cy, cx - w / 6, y0, c);
            primLine2(cx - w / 6, y0, x1, cy, c);
            primLine2(x1, cy, cx - w / 6, y1, c);
            primLine2(cx - w / 6, y1, x0, cy, c);
            break;
        case CLASS_Destroyer:
            primLine2(x0, y0 + h / 4, x1 - w / 4, y0 + h / 4, c);
            primLine2(x1 - w / 4, y0 + h / 4, x1, cy, c);
            primLine2(x1, cy, x1 - w / 4, y1 - h / 4, c);
            primLine2(x1 - w / 4, y1 - h / 4, x0, y1 - h / 4, c);
            primLine2(x0, y1 - h / 4, x0, y0 + h / 4, c);
            break;
        case CLASS_Carrier:
            primRectOutline2((rectangle *)rect, 1, c);
            primLine2(cx, y0, cx, y1, c);
            primLine2(x0, cy, x1, cy, c);
            break;
        case CLASS_HeavyCruiser:
            primLine2(x0 + w / 5, y0, x1 - w / 5, y0, c);
            primLine2(x1 - w / 5, y0, x1, cy, c);
            primLine2(x1, cy, x1 - w / 5, y1, c);
            primLine2(x1 - w / 5, y1, x0 + w / 5, y1, c);
            primLine2(x0 + w / 5, y1, x0, cy, c);
            primLine2(x0, cy, x0 + w / 5, y0, c);
            break;
        case CLASS_Mothership:
            primLine2(cx - w / 6, y0, cx + w / 6, y0, c);
            primLine2(cx + w / 6, y0, x1, cy, c);
            primLine2(x1, cy, cx + w / 6, y1, c);
            primLine2(cx + w / 6, y1, cx - w / 6, y1, c);
            primLine2(cx - w / 6, y1, x0, cy, c);
            primLine2(x0, cy, cx - w / 6, y0, c);
            primLine2(cx, y0, cx, y1, c);
            break;
        case CLASS_Resource:
            primRectOutline2((rectangle *)rect, 1, c);
            primLine2(x0, cy, x1, cy, c);
            primLine2(cx, y0, cx, y1, c);
            break;
        default:
            primLine2(cx, y0, x1, cy, c);
            primLine2(x1, cy, cx, y1, c);
            primLine2(cx, y1, x0, cy, c);
            primLine2(x0, cy, cx, y0, c);
            break;
    }
}

static void muiCommandDrawIconTray(const ModernUICommandLayout *layout)
{
    sdword iconSize;
    sdword columns;
    sdword gap;
    sdword capacity;
    sdword pageCount;
    sdword pageIndex;
    sdword start;
    sdword visible;
    sdword slot;
    rectangle tray;
    if (layout == NULL) return;
    tray = layout->iconTray;
    muiCommandIconTrayMetrics(&layout->right, selSelected.numShips,
                              &tray, &iconSize, &columns, &gap, &capacity);
    if (tray.x1 <= tray.x0 || tray.y1 <= tray.y0) return;

    muiCommandDrawBackdropSegment(&tray);
    primRectOutline2(&tray, 1, muiLineSoft);

    if (selSelected.numShips <= 0 || iconSize <= 0 || capacity <= 0) return;

    pageCount = (selSelected.numShips + capacity - 1) / capacity;
    if (pageCount < 1) pageCount = 1;
    if (muiCommand.iconScroll < 0) muiCommand.iconScroll = 0;
    if (muiCommand.iconScroll >= pageCount)
        muiCommand.iconScroll = pageCount - 1;
    pageIndex = muiCommand.iconScroll;
    start = pageIndex * capacity;
    visible = selSelected.numShips - start;
    if (visible > capacity) visible = capacity;

    for (slot = 0; slot < visible; ++slot)
    {
        sdword index = start + slot;
        Ship *ship = selSelected.ShipPtr[index];
        rectangle card;
        rectangle glyph;
        rectangle healthTrack;
        rectangle healthFill;
        bool32 hovered;
        real32 healthFraction;
        color healthColor;
        if (ship == NULL || ship->staticinfo == NULL) continue;
        card = muiCommandIconRect(&tray, slot, iconSize, columns, gap);
        if (card.y1 > tray.y1) continue;
        hovered = muiPointIn(&card, mouseCursorX(), mouseCursorY());
        muiFill(&card, hovered ? muiSignalSoft : colRGBA(15, 25, 32, 220));
        primRectOutline2(&card, 1, hovered ? muiSignal : muiLine);

        glyph = card;
        glyph.x0 += uiScaleSize(3);
        glyph.x1 -= uiScaleSize(3);
        glyph.y0 += uiScaleSize(3);
        glyph.y1 -= uiScaleSize(iconSize >= uiScaleSize(18) ? 8 : 5);
        muiCommandDrawShipGlyph(&glyph, ship, hovered ? muiPaper : muiCool);

        healthTrack = card;
        healthTrack.x0 += 1;
        healthTrack.x1 -= 1;
        healthTrack.y0 = card.y1 - (iconSize >= uiScaleSize(18) ? uiScaleSize(4) : 2);
        healthTrack.y1 = card.y1 - 1;
        muiFill(&healthTrack, colRGBA(4, 8, 10, 230));
        healthFraction = ship->staticinfo->maxhealth > 0.0f
            ? ship->health / ship->staticinfo->maxhealth : 0.0f;
        if (healthFraction < 0.0f) healthFraction = 0.0f;
        if (healthFraction > 1.0f) healthFraction = 1.0f;
        healthColor = healthFraction > 0.50f ? SEL_ShipHealthGreen :
                      (healthFraction > 0.25f ? SEL_ShipHealthYellow :
                                                SEL_ShipHealthRed);
        healthFill = healthTrack;
        healthFill.x1 = healthFill.x0 +
            (sdword)((real32)(healthTrack.x1 - healthTrack.x0) * healthFraction);
        if (healthFill.x1 > healthFill.x0) muiFill(&healthFill, healthColor);
    }

    if (pageCount > 1)
    {
        rectangle rail = muiRect(tray.x1 - uiScaleSize(17), tray.y0 + 1,
                                 tray.x1 - 1, tray.y1 - 1);
        rectangle upper = rail;
        rectangle lower = rail;
        rectangle pageRect = rail;
        char pageText[24];
        sdword cx = (rail.x0 + rail.x1) / 2;
        color upColor = pageIndex > 0 ? muiCool : muiDisabled;
        color downColor = pageIndex + 1 < pageCount ? muiCool : muiDisabled;
        upper.y1 = upper.y0 + (rail.y1 - rail.y0) / 3;
        lower.y0 = rail.y1 - (rail.y1 - rail.y0) / 3;
        pageRect.y0 = upper.y1;
        pageRect.y1 = lower.y0;
        muiFill(&rail, colRGBA(7, 12, 17, 210));
        primLine2(cx - uiScaleSize(3), upper.y0 + uiScaleSize(7),
                  cx, upper.y0 + uiScaleSize(4), upColor);
        primLine2(cx, upper.y0 + uiScaleSize(4),
                  cx + uiScaleSize(3), upper.y0 + uiScaleSize(7), upColor);
        primLine2(cx - uiScaleSize(3), lower.y1 - uiScaleSize(7),
                  cx, lower.y1 - uiScaleSize(4), downColor);
        primLine2(cx, lower.y1 - uiScaleSize(4),
                  cx + uiScaleSize(3), lower.y1 - uiScaleSize(7), downColor);
        snprintf(pageText, sizeof(pageText), "%d/%d", pageIndex + 1, pageCount);
        muiTextCentered(&pageRect, muiMuted, pageText, muiTinyFont);
    }
}

static void muiCommandIconRegionDraw(regionhandle region)
{
    ModernUICommandLayout layout;
    if (!muiCommandBarVisible()) return;
    muiEnsureFonts();
    muiCommandLayoutCompute(&layout);
    region->rect = layout.iconTray;
    muiCommandSummaryUpdate();
    muiCommandDrawIconTray(&layout);
}

static void muiFitText(char *buffer, size_t bufferSize, const char *text,
                       sdword maxWidth, fonthandle font)
{
    fonthandle old;
    size_t length;
    if (buffer == NULL || bufferSize == 0) return;
    buffer[0] = 0;
    if (text == NULL || maxWidth <= 0) return;
    snprintf(buffer, bufferSize, "%s", text);
    old = fontMakeCurrent(font);
    if (fontWidth(buffer) > maxWidth)
    {
        length = strlen(buffer);
        while (length > 0 && fontWidth(buffer) > maxWidth)
        {
            --length;
            buffer[length] = 0;
        }
        if (length >= 3)
        {
            while (length > 3)
            {
                buffer[length - 1] = 0;
                --length;
                buffer[length - 1] = '.';
                buffer[length - 2] = '.';
                buffer[length - 3] = '.';
                if (fontWidth(buffer) <= maxWidth) break;
                buffer[length - 1] = 0;
            }
        }
    }
    fontMakeCurrent(old);
}
static void muiTextLeftFittedCentered(const rectangle *rect, color c,
                                      const char *text, fonthandle font,
                                      sdword horizontalPadding)
{
    char buffer[128];
    fonthandle old;
    sdword available;
    sdword height;
    sdword y;
    if (rect == NULL) return;
    if (horizontalPadding < 0) horizontalPadding = 0;
    available = rect->x1 - rect->x0 - horizontalPadding * 2;
    if (available <= 0) return;
    muiFitText(buffer, sizeof(buffer), text, available, font);
    if (buffer[0] == 0) return;
    old = fontMakeCurrent(font);
    height = fontHeight(NULL);
    fontMakeCurrent(old);
    y = rect->y0 + ((rect->y1 - rect->y0) - height) / 2;
    muiText(rect->x0 + horizontalPadding, y, c, buffer, font);
}

static fonthandle muiBestFitFont(const char *text, sdword maxWidth,
                                 fonthandle primary, fonthandle compact,
                                 fonthandle tiny)
{
    fonthandle old;

    if (text == NULL || maxWidth <= 0) return primary;
    old = fontCurrentGet();

    fontMakeCurrent(primary);
    if (fontWidth((char *)text) <= maxWidth)
    {
        fontMakeCurrent(old);
        return primary;
    }
    if (compact != 0)
    {
        fontMakeCurrent(compact);
        if (fontWidth((char *)text) <= maxWidth)
        {
            fontMakeCurrent(old);
            return compact;
        }
    }
    if (tiny != 0)
    {
        fontMakeCurrent(tiny);
        if (fontWidth((char *)text) <= maxWidth)
        {
            fontMakeCurrent(old);
            return tiny;
        }
    }
    fontMakeCurrent(old);
    return tiny != 0 ? tiny : (compact != 0 ? compact : primary);
}

static void muiTextLeftAdaptiveFull(const rectangle *rect, color c,
                                    const char *text, fonthandle primary,
                                    fonthandle compact, fonthandle tiny,
                                    sdword horizontalPadding)
{
    fonthandle font;
    fonthandle old;
    sdword available;
    sdword height;
    sdword y;
    if (rect == NULL || text == NULL) return;
    if (horizontalPadding < 0) horizontalPadding = 0;
    available = rect->x1 - rect->x0 - horizontalPadding * 2;
    if (available <= 0) return;
    font = muiBestFitFont(text, available, primary, compact, tiny);
    old = fontMakeCurrent(font);
    height = fontHeight(NULL);
    fontMakeCurrent(old);
    y = rect->y0 + ((rect->y1 - rect->y0) - height) / 2;
    muiText(rect->x0 + horizontalPadding, y, c, text, font);
}

static void muiTextCenteredAdaptiveFull(const rectangle *rect, color c,
                                        const char *text, fonthandle primary,
                                        fonthandle compact, fonthandle tiny,
                                        sdword horizontalPadding)
{
    fonthandle chosen;
    sdword available;
    if (rect == NULL || text == NULL) return;
    if (horizontalPadding < 0) horizontalPadding = 0;
    available = rect->x1 - rect->x0 - horizontalPadding * 2;
    if (available <= 0) return;
    chosen = muiBestFitFont(text, available, primary, compact, tiny);
    /* No ellipsis here.  Gameplay buttons are laid out around their complete
       caption and the font is reduced only when necessary. */
    muiTextCentered(rect, c, text, chosen);
}

static void muiCommandMeter(const rectangle *rect, sdword y, sdword rowHeight,
                            const char *label, const char *value,
                            real32 fraction)
{
    rectangle track;
    rectangle fill;
    sdword trackHeight = uiScaleSize(2);
    sdword lineY;
    sdword textY;
    sdword width = rect->x1 - rect->x0;
    sdword valueWidth;
    sdword labelWidth;
    sdword textHeight;
    fonthandle old;
    fonthandle labelFont;
    fonthandle valueFont;

    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;
    /* This part of the HUD is intentionally dense.  Never abbreviate a stat
       into FIRE.../COV...; choose a smaller face and keep the whole word. */
    valueFont = muiBestFitFont(value, width / 2,
                               muiCompactFont, muiTinyFont, muiTinyFont);
    old = fontMakeCurrent(valueFont);
    valueWidth = fontWidth((char *)value);
    fontMakeCurrent(old);
    labelWidth = width - valueWidth - uiScaleSize(8);
    if (labelWidth < uiScaleSize(18)) labelWidth = uiScaleSize(18);
    labelFont = muiBestFitFont(label, labelWidth,
                               muiCompactFont, muiTinyFont, muiTinyFont);
    if (trackHeight < 1) trackHeight = 1;
    if (rowHeight < trackHeight + 1) rowHeight = trackHeight + 1;
    lineY = y + rowHeight - trackHeight;
    old = fontMakeCurrent(labelFont);
    textHeight = fontHeight(NULL);
    fontMakeCurrent(old);
    textY = y + ((lineY - y) - textHeight) / 2;
    if (textY < y) textY = y;
    muiText(rect->x0, textY, muiMuted, label, labelFont);
    muiTextRight(rect->x1, textY, muiPaper, value, valueFont);
    track = muiRect(rect->x0, lineY, rect->x1, lineY + trackHeight);
    muiFill(&track, muiLineSoft);
    fill = track;
    fill.x1 = fill.x0 + (sdword)((real32)width * fraction);
    if (fill.x1 > fill.x0) muiFill(&fill, muiSignal);
}

static void muiCommandButton(const rectangle *rect, const char *label,
                             bool32 enabled, bool32 danger)
{
    bool32 hovered = enabled && muiPointIn(rect, mouseCursorX(), mouseCursorY());
    color border = enabled ? (danger ? muiDanger : muiLine) : muiDisabled;
    color textColor = enabled ? (danger ? colRGB(224, 139, 117) : muiPaper) : muiDisabled;
    rectangle top = *rect;

    muiFill(rect, enabled ? (hovered ? muiSurfaceRaised : muiSurface) :
                          colRGBA(13, 20, 25, 190));
    primRectOutline2((rectangle *)rect, 1, hovered ? (danger ? muiDanger : muiSignal) : border);
    top.y1 = top.y0 + uiScaleSize(2);
    if (enabled) muiFill(&top, danger ? muiDanger : (hovered ? muiSignal : muiLineSoft));
    /* One common small face keeps FORMATIONS/TACTICS/CANCEL/etc visually
       balanced and guarantees complete captions in the compact 3x2 grid. */
    muiTextCentered(rect, textColor, label, muiTinyFont);
}

static void muiCommandDrawDossierButton(const rectangle *rect, bool32 enabled)
{
    bool32 hovered = enabled && muiPointIn(rect, mouseCursorX(), mouseCursorY());
    color c = enabled ? (hovered ? muiSignal : muiCool) : muiDisabled;
    sdword cx = (rect->x0 + rect->x1) / 2;
    sdword cy = rect->y0 + (rect->y1 - rect->y0) * 42 / 100;
    sdword r = uiScaleSize(13);
    rectangle labelRect = *rect;

    muiFill(rect, enabled ? (hovered ? muiSurfaceRaised : muiSurface) :
                          colRGBA(13, 20, 25, 190));
    primRectOutline2((rectangle *)rect, 1, enabled ? (hovered ? muiSignal : muiLine) : muiDisabled);
    primLine2(cx, cy - r, cx + r, cy, c);
    primLine2(cx + r, cy, cx, cy + r, c);
    primLine2(cx, cy + r, cx - r, cy, c);
    primLine2(cx - r, cy, cx, cy - r, c);
    primLine2(cx - r / 2, cy - uiScaleSize(3), cx + r / 2,
              cy - uiScaleSize(3), c);
    primLine2(cx - r / 2, cy + uiScaleSize(3), cx + r / 2,
              cy + uiScaleSize(3), c);
    labelRect.y0 = rect->y1 - uiScaleSize(27);
    muiTextCentered(&labelRect, enabled ? muiPaper : muiDisabled,
                    "DOSSIER", muiTinyFont);
}

static void muiCommandLeftDraw(regionhandle region)
{
    ModernUICommandLayout layout;
    char value[64];
    char selectionName[128];
    sdword y;
    sdword meterStep;
    sdword meterHeight;
    bool32 hasSelection;
    bool32 singleSelection;
    real32 healthFraction;
    rectangle divider;

    if (!muiCommandBarVisible()) return;
    muiEnsureFonts();
    muiCommandLayoutCompute(&layout);
    region->rect = layout.left;
    muiCommandSummaryUpdate();
    hasSelection = muiCommand.summary.count > 0;
    singleSelection = muiCommand.summary.count == 1 &&
                      selSelected.ShipPtr[0] != NULL &&
                      selSelected.ShipPtr[0]->playerowner == universe.curPlayerPtr;

    muiCommandDrawBackdropSegment(&layout.left);
    primRectOutline2(&layout.left, 1, muiLineSoft);

    /* Divide the live stats panel itself instead of assuming a 108px HUD.
       Four compact rows now fit deterministically: the three established
       tactical stats plus the requested live HEALTH/ARMOR condition. */
    meterStep = (layout.stats.y1 - layout.stats.y0) / 4;
    if (meterStep < 1) meterStep = 1;
    y = layout.stats.y0;
    meterHeight = meterStep;
    if (hasSelection)
        snprintf(value, sizeof(value), "%d", muiCommand.summary.totalFirepower);
    else
        snprintf(value, sizeof(value), "--");
    muiCommandMeter(&layout.stats, y, meterHeight, "FIREPOWER", value,
                    hasSelection ? muiCommand.summary.averageFirepower / 19000.0f : 0.0f);
    y += meterStep;
    if (hasSelection)
        snprintf(value, sizeof(value), "%.0f m/s", muiCommand.summary.averageSpeed);
    else
        snprintf(value, sizeof(value), "--");
    muiCommandMeter(&layout.stats, y, meterStep, "AVG SPEED", value,
                    hasSelection ? muiCommand.summary.averageSpeed / 1000.0f : 0.0f);
    y += meterStep;
    if (hasSelection)
        snprintf(value, sizeof(value), "%.0f%%", muiCommand.summary.averageCoverage);
    else
        snprintf(value, sizeof(value), "--");
    muiCommandMeter(&layout.stats, y, meterStep, "COVERAGE", value,
                    hasSelection ? muiCommand.summary.averageCoverage / 100.0f : 0.0f);
    y += meterStep;
    healthFraction = hasSelection ? muiCommandAverageHealthFraction() : 0.0f;
    if (hasSelection)
        snprintf(value, sizeof(value), "%.0f%%", healthFraction * 100.0f);
    else
        snprintf(value, sizeof(value), "--");
    muiCommandMeter(&layout.stats, y, layout.stats.y1 - y,
                    "HEALTH/ARMOR", value, healthFraction);

    muiFill(&layout.selectionName, hasSelection ? muiSurfaceRaised : muiSurface);
    primRectOutline2(&layout.selectionName, 1, hasSelection ? muiLine : muiLineSoft);
    if (!hasSelection)
    {
        snprintf(selectionName, sizeof(selectionName), "NO SELECTION");
    }
    else if (muiCommand.summary.mixed)
    {
        snprintf(selectionName, sizeof(selectionName), "MIXED");
    }
    else if (muiCommand.summary.count > 1)
    {
        snprintf(selectionName, sizeof(selectionName), "%s x %d",
                 ShipTypeToNiceStr(muiCommand.summary.primaryType),
                 muiCommand.summary.count);
    }
    else
    {
        snprintf(selectionName, sizeof(selectionName), "%s",
                 ShipTypeToNiceStr(muiCommand.summary.primaryType));
    }
    muiTextLeftAdaptiveFull(&layout.selectionName,
                            hasSelection ? muiPaper : muiMuted,
                            selectionName, muiCompactFont,
                            muiTinyFont, muiTinyFont, uiScaleSize(5));

    muiCommandButton(&layout.action[0], "FORMATIONS", hasSelection &&
                     muiCommand.summary.count >= MIN_SHIPS_IN_FORMATION, FALSE);
    muiCommandButton(&layout.action[1], "TACTICS", hasSelection, FALSE);
    muiCommandButton(&layout.action[2], "CANCEL", hasSelection, FALSE);
    muiCommandButton(&layout.action[3], "DOCK", hasSelection, FALSE);
    muiCommandButton(&layout.action[4], "RETIRE", hasSelection, FALSE);
    muiCommandButton(&layout.action[5], "SCUTTLE", hasSelection, TRUE);
    muiCommandDrawDossierButton(&layout.dossier, singleSelection);

    divider = muiRect(layout.stats.x1 + uiScaleSize(2), layout.left.y0 + uiScaleSize(8),
                      layout.stats.x1 + uiScaleSize(3), layout.left.y1 - uiScaleSize(8));
    muiFill(&divider, muiLineSoft);
}

static udword muiCommandLeftProcess(regionhandle region, smemsize ID,
                                    udword event, udword data)
{
    ModernUICommandLayout layout;
    sdword popupY;
    (void)region;
    (void)ID;
    (void)data;

    if (!muiCommandBarVisible())
    {
        return 0;
    }
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft)
    {
        return 0;
    }
    muiCommandLayoutCompute(&layout);
    muiCommandSummaryUpdate();
    if (muiPointIn(&layout.dossier, mouseCursorX(), mouseCursorY()))
    {
        if (selSelected.numShips == 1 && selSelected.ShipPtr[0] != NULL &&
            selSelected.ShipPtr[0]->playerowner == universe.curPlayerPtr)
        {
            modernUIShowShipDossier(selSelected.ShipPtr[0], 1, FALSE);
        }
        return RPR_Redraw;
    }
    if (selSelected.numShips <= 0) return 0;

    popupY = layout.action[0].y0 - uiScaleSize(4);
    if (muiPointIn(&layout.action[0], mouseCursorX(), mouseCursorY()))
    {
        mrOpenFormationSubmenuAt(layout.action[0].x0, popupY);
    }
    else if (muiPointIn(&layout.action[1], mouseCursorX(), mouseCursorY()))
    {
        mrOpenTacticsSubmenuAt(layout.action[1].x0, popupY);
    }
    else if (muiPointIn(&layout.action[2], mouseCursorX(), mouseCursorY()))
    {
        mrCancel(NULL, NULL);
    }
    else if (muiPointIn(&layout.action[3], mouseCursorX(), mouseCursorY()))
    {
        mrDockingOrders(NULL, NULL);
    }
    else if (muiPointIn(&layout.action[4], mouseCursorX(), mouseCursorY()))
    {
        mrRetire(NULL, NULL);
    }
    else if (muiPointIn(&layout.action[5], mouseCursorX(), mouseCursorY()))
    {
        mrScuttle(NULL, NULL);
    }
    return RPR_Redraw;
}

static void muiCommandRightDraw(regionhandle region)
{
    ModernUICommandLayout layout;
    rectangle headerRule;
    rectangle rowRect;
    rectangle rosterList;
    ShipType groupTypes[TOTAL_NUM_SHIPS];
    sdword groupCounts[TOTAL_NUM_SHIPS];
    char text[128];
    sdword groupCount;
    sdword pageCount;
    sdword pageIndex;
    sdword start;
    sdword visibleCount;
    sdword slot;
    sdword row;
    sdword column;
    sdword rowHeight;
    sdword rowsThisPage;
    sdword columnWidth;
    sdword columnGap = uiScaleSize(8);
    sdword arrowX;
    color arrowColor;

    if (!muiCommandBarVisible()) return;
    /* The lower-right roster supersedes the legacy upper-right Info Overlay. */
    if (ioRunning) ioDisable();
    muiEnsureFonts();
    muiCommandLayoutCompute(&layout);
    region->rect = layout.right;
    muiCommandSummaryUpdate();
    groupCount = muiCommandBuildRosterGroups(groupTypes, groupCounts,
                                              TOTAL_NUM_SHIPS);
    pageCount = groupCount > 0
        ? (groupCount + MUI_COMMAND_ROSTER_PAGE_CAPACITY - 1) /
          MUI_COMMAND_ROSTER_PAGE_CAPACITY : 1;
    if (muiCommand.rosterScroll < 0) muiCommand.rosterScroll = 0;
    if (muiCommand.rosterScroll >= pageCount) muiCommand.rosterScroll = pageCount - 1;
    pageIndex = muiCommand.rosterScroll;

    muiCommandDrawBackdropSegment(&layout.right);
    primRectOutline2(&layout.right, 1, muiLineSoft);
    muiText(layout.roster.x0 + uiScaleSize(5), layout.roster.y0 + uiScaleSize(3),
            muiSignal, "SELECTED SHIPS", muiBodyFont);
    snprintf(text, sizeof(text), "%d", muiCommand.summary.count);
    muiTextRight(layout.roster.x1 - uiScaleSize(5),
                 layout.roster.y0 + uiScaleSize(3), muiMuted, text, muiBodyFont);
    headerRule = muiRect(layout.roster.x0, layout.roster.y0 + uiScaleSize(20),
                         layout.roster.x1, layout.roster.y0 + uiScaleSize(21));
    muiFill(&headerRule, muiLineSoft);

    if (groupCount <= 0)
    {
        muiText(layout.roster.x0 + uiScaleSize(5),
                layout.roster.y0 + uiScaleSize(35), muiMuted,
                "NO ACTIVE SELECTION", muiBodyFont);
    }
    else
    {
        start = pageIndex * MUI_COMMAND_ROSTER_PAGE_CAPACITY;
        visibleCount = groupCount - start;
        if (visibleCount > MUI_COMMAND_ROSTER_PAGE_CAPACITY)
            visibleCount = MUI_COMMAND_ROSTER_PAGE_CAPACITY;
        rowsThisPage = (visibleCount + MUI_COMMAND_ROSTER_COLUMNS - 1) /
                       MUI_COMMAND_ROSTER_COLUMNS;
        if (rowsThisPage < 1) rowsThisPage = 1;
        if (rowsThisPage > MUI_COMMAND_ROSTER_ROWS) rowsThisPage = MUI_COMMAND_ROSTER_ROWS;
        rowHeight = (layout.roster.y1 - (layout.roster.y0 + uiScaleSize(22))) /
                    MUI_COMMAND_ROSTER_ROWS;
        if (rowHeight < uiScaleSize(12)) rowHeight = uiScaleSize(12);
        rosterList = muiRect(layout.roster.x0 + uiScaleSize(4),
                             layout.roster.y0 + uiScaleSize(23),
                             layout.roster.x1 - (pageCount > 1 ? uiScaleSize(20) : uiScaleSize(4)),
                             layout.roster.y1);
        columnWidth = (rosterList.x1 - rosterList.x0 - columnGap) / 2;
        if (columnWidth < uiScaleSize(36)) columnWidth = uiScaleSize(36);
        if (visibleCount > rowsThisPage)
        {
            rectangle columnRule = muiRect(rosterList.x0 + columnWidth + columnGap / 2,
                                           rosterList.y0 + uiScaleSize(2),
                                           rosterList.x0 + columnWidth + columnGap / 2 + 1,
                                           rosterList.y1 - uiScaleSize(2));
            muiFill(&columnRule, muiLineSoft);
        }

        /* Balance each page across both columns so the roster uses its full
           width even with only a handful of ship types.  Counts are attached
           to the type instead of repeating identical hulls. */
        for (slot = 0; slot < visibleCount; ++slot)
        {
            sdword groupIndex = start + slot;
            column = slot >= rowsThisPage ? 1 : 0;
            row = column ? slot - rowsThisPage : slot;
            rowRect = muiRect(rosterList.x0 + column * (columnWidth + columnGap),
                              rosterList.y0 + row * rowHeight,
                              rosterList.x0 + column * (columnWidth + columnGap) + columnWidth,
                              rosterList.y0 + (row + 1) * rowHeight);
            if (muiPointIn(&rowRect, mouseCursorX(), mouseCursorY()))
            {
                rectangle hover = rowRect;
                muiFill(&hover, muiSignalSoft);
                hover.x1 = hover.x0 + uiScaleSize(2);
                muiFill(&hover, muiSignal);
            }
            snprintf(text, sizeof(text), "%s x %d",
                     ShipTypeToNiceStr(groupTypes[groupIndex]),
                     groupCounts[groupIndex]);
            muiTextLeftAdaptiveFull(&rowRect, muiPaper, text,
                                    muiCompactFont, muiTinyFont,
                                    muiTinyFont, uiScaleSize(3));
            if (row < rowsThisPage - 1)
            {
                headerRule = muiRect(rowRect.x0, rowRect.y1 - uiScaleSize(1),
                                     rowRect.x1, rowRect.y1);
                muiFill(&headerRule, muiLineSoft);
            }
        }
        if (pageCount > 1)
        {
            arrowX = (layout.rosterUp.x0 + layout.rosterUp.x1) / 2;
            arrowColor = pageIndex > 0 ? muiCool : muiDisabled;
            primLine2(arrowX - uiScaleSize(4), layout.rosterUp.y0 + uiScaleSize(12),
                      arrowX, layout.rosterUp.y0 + uiScaleSize(8), arrowColor);
            primLine2(arrowX, layout.rosterUp.y0 + uiScaleSize(8),
                      arrowX + uiScaleSize(4), layout.rosterUp.y0 + uiScaleSize(12), arrowColor);
            arrowColor = pageIndex + 1 < pageCount ? muiCool : muiDisabled;
            primLine2(arrowX - uiScaleSize(4), layout.rosterDown.y1 - uiScaleSize(12),
                      arrowX, layout.rosterDown.y1 - uiScaleSize(8), arrowColor);
            primLine2(arrowX, layout.rosterDown.y1 - uiScaleSize(8),
                      arrowX + uiScaleSize(4), layout.rosterDown.y1 - uiScaleSize(12), arrowColor);
        }
    }

    if (layout.hyperspaceAvailable)
    {
        bool32 hovered = muiPointIn(&layout.hyperspace, mouseCursorX(), mouseCursorY());
        color border = hovered ? muiPaper : muiSignal;
        muiFill(&layout.hyperspace, hovered ? colRGBA(48, 37, 19, 242) :
                                             colRGBA(29, 24, 16, 232));
        primRectOutline2(&layout.hyperspace, 1, border);
        muiTextCentered(&layout.hyperspace,
                        hovered ? muiPaper : muiSignal,
                        "HYPERSPACE", muiTinyFont);
    }
}

static udword muiCommandRightProcess(regionhandle region, smemsize ID,
                                     udword event, udword data)
{
    ModernUICommandLayout layout;
    rectangle rosterList;
    rectangle rowRect;
    rectangle tray;
    ShipType groupTypes[TOTAL_NUM_SHIPS];
    sdword groupCounts[TOTAL_NUM_SHIPS];
    sdword groupCount;
    sdword pageCount;
    sdword maxPage;
    sdword start;
    sdword visibleCount;
    sdword rowsThisPage;
    sdword rowHeight;
    sdword columnWidth;
    sdword columnGap = uiScaleSize(8);
    sdword slot;
    sdword row;
    sdword column;
    sdword iconSize;
    sdword iconColumns;
    sdword iconGap;
    sdword iconCapacity;
    sdword iconPageCount;
    sdword iconMaxPage;
    sdword iconStart;
    sdword iconVisible;
    (void)ID;
    (void)data;

    if (!muiCommandBarVisible()) return 0;
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    muiCommandLayoutCompute(&layout);
    muiCommandSummaryUpdate();
    groupCount = muiCommandBuildRosterGroups(groupTypes, groupCounts,
                                              TOTAL_NUM_SHIPS);
    pageCount = groupCount > 0
        ? (groupCount + MUI_COMMAND_ROSTER_PAGE_CAPACITY - 1) /
          MUI_COMMAND_ROSTER_PAGE_CAPACITY : 1;
    maxPage = pageCount - 1;

    muiCommandIconTrayMetrics(&layout.right, selSelected.numShips,
                              &tray, &iconSize, &iconColumns, &iconGap,
                              &iconCapacity);
    iconPageCount = iconCapacity > 0
        ? (selSelected.numShips + iconCapacity - 1) / iconCapacity : 1;
    if (iconPageCount < 1) iconPageCount = 1;
    iconMaxPage = iconPageCount - 1;
    if (muiCommand.iconScroll < 0) muiCommand.iconScroll = 0;
    if (muiCommand.iconScroll > iconMaxPage) muiCommand.iconScroll = iconMaxPage;

    if ((event == RPE_WheelUp || event == RPE_WheelDown) &&
        iconSize > 0 && muiPointIn(&tray, mouseCursorX(), mouseCursorY()))
    {
        if (event == RPE_WheelUp && muiCommand.iconScroll > 0)
            --muiCommand.iconScroll;
        else if (event == RPE_WheelDown && muiCommand.iconScroll < iconMaxPage)
            ++muiCommand.iconScroll;
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }

    if ((event == RPE_WheelUp || event == RPE_WheelDown) &&
        muiPointIn(&layout.roster, mouseCursorX(), mouseCursorY()))
    {
        if (event == RPE_WheelUp && muiCommand.rosterScroll > 0)
            --muiCommand.rosterScroll;
        else if (event == RPE_WheelDown && muiCommand.rosterScroll < maxPage)
            ++muiCommand.rosterScroll;
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft) return 0;

    /* Homeworld-2-style individual selection strip.  Every selected ship is
       represented by its own live card; clicking a card makes that ship the
       entire selection. */
    if (iconSize > 0 && muiPointIn(&tray, mouseCursorX(), mouseCursorY()))
    {
        if (iconPageCount > 1 &&
            mouseCursorX() >= tray.x1 - uiScaleSize(18))
        {
            sdword third = (tray.y1 - tray.y0) / 3;
            if (mouseCursorY() < tray.y0 + third && muiCommand.iconScroll > 0)
                --muiCommand.iconScroll;
            else if (mouseCursorY() >= tray.y1 - third &&
                     muiCommand.iconScroll < iconMaxPage)
                ++muiCommand.iconScroll;
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }

        iconStart = muiCommand.iconScroll * iconCapacity;
        iconVisible = selSelected.numShips - iconStart;
        if (iconVisible > iconCapacity) iconVisible = iconCapacity;
        for (slot = 0; slot < iconVisible; ++slot)
        {
            sdword index = iconStart + slot;
            rectangle iconRect = muiCommandIconRect(
                &tray, slot, iconSize, iconColumns, iconGap);
            Ship *ship = selSelected.ShipPtr[index];
            if (iconRect.y1 <= tray.y1 &&
                muiPointIn(&iconRect, mouseCursorX(), mouseCursorY()))
            {
                /* RTX-0069: Ctrl-clicking an entry in the Selected Ships tray
                   removes that exact ship from the live selection.  Plain click
                   retains the established select-only behavior. */
                if (keyIsHit(CONTROLKEY))
                    muiCommandRemoveShipFromSelection(ship);
                else
                    muiCommandSelectOnlyShip(ship);
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
    }

    /* The grouped xN roster is a real selection filter, not just a readout.
       Clicking HEAVY CORVETTE x 9, for example, drops every other type and
       retains only those nine ships. */
    if (groupCount > 0)
    {
        start = muiCommand.rosterScroll * MUI_COMMAND_ROSTER_PAGE_CAPACITY;
        visibleCount = groupCount - start;
        if (visibleCount > MUI_COMMAND_ROSTER_PAGE_CAPACITY)
            visibleCount = MUI_COMMAND_ROSTER_PAGE_CAPACITY;
        rowsThisPage = (visibleCount + MUI_COMMAND_ROSTER_COLUMNS - 1) /
                       MUI_COMMAND_ROSTER_COLUMNS;
        if (rowsThisPage < 1) rowsThisPage = 1;
        if (rowsThisPage > MUI_COMMAND_ROSTER_ROWS)
            rowsThisPage = MUI_COMMAND_ROSTER_ROWS;
        rowHeight = (layout.roster.y1 - (layout.roster.y0 + uiScaleSize(22))) /
                    MUI_COMMAND_ROSTER_ROWS;
        if (rowHeight < uiScaleSize(12)) rowHeight = uiScaleSize(12);
        rosterList = muiRect(layout.roster.x0 + uiScaleSize(4),
                             layout.roster.y0 + uiScaleSize(23),
                             layout.roster.x1 -
                                 (pageCount > 1 ? uiScaleSize(20) : uiScaleSize(4)),
                             layout.roster.y1);
        columnWidth = (rosterList.x1 - rosterList.x0 - columnGap) / 2;
        if (columnWidth < uiScaleSize(36)) columnWidth = uiScaleSize(36);
        for (slot = 0; slot < visibleCount; ++slot)
        {
            sdword groupIndex = start + slot;
            column = slot >= rowsThisPage ? 1 : 0;
            row = column ? slot - rowsThisPage : slot;
            rowRect = muiRect(
                rosterList.x0 + column * (columnWidth + columnGap),
                rosterList.y0 + row * rowHeight,
                rosterList.x0 + column * (columnWidth + columnGap) + columnWidth,
                rosterList.y0 + (row + 1) * rowHeight);
            if (muiPointIn(&rowRect, mouseCursorX(), mouseCursorY()))
            {
                /* RTX-0075: grouped rows follow the 3-D modifier contract.
                   Shift-click is inert; Ctrl-click removes every selected ship
                   of the grouped type; plain click isolates that type. */
                if (keyIsHit(SHIFTKEY))
                {
                    /* intentionally no selection change */
                }
                else if (keyIsHit(CONTROLKEY))
                {
                    muiCommandRemoveTypeFromSelection(groupTypes[groupIndex]);
                }
                else
                {
                    muiCommandSelectOnlyType(groupTypes[groupIndex]);
                }
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
    }

    if (pageCount > 1 && muiPointIn(&layout.rosterUp, mouseCursorX(), mouseCursorY()))
    {
        if (muiCommand.rosterScroll > 0) --muiCommand.rosterScroll;
        return RPR_Redraw;
    }
    if (pageCount > 1 && muiPointIn(&layout.rosterDown, mouseCursorX(), mouseCursorY()))
    {
        if (muiCommand.rosterScroll < maxPage) ++muiCommand.rosterScroll;
        return RPR_Redraw;
    }
    if (layout.hyperspaceAvailable &&
        muiPointIn(&layout.hyperspace, mouseCursorX(), mouseCursorY()))
    {
        spHyperspaceButtonPushed();
        return RPR_Redraw;
    }
    return 0;
}

static void muiCommandRuleDraw(regionhandle region)
{
    ModernUICommandLayout layout;
    rectangle leftAccent;
    rectangle rightAccent;

    if (!muiCommandBarVisible()) return;
    muiCommandLayoutCompute(&layout);
    region->rect = layout.topRule;
    muiFill(&layout.topRule, muiLineSoft);
    leftAccent = layout.topRule;
    leftAccent.x1 = layout.left.x1;
    muiFill(&leftAccent, muiSignal);
    rightAccent = layout.topRule;
    rightAccent.x0 = layout.iconTray.x1 > layout.iconTray.x0
        ? layout.iconTray.x0 : layout.right.x0;
    muiFill(&rightAccent, muiSignal);
}

void modernUICommandBarStartup(void)
{
    ModernUICommandLayout layout;
    if (ghMainRegion == NULL || muiCommand.leftRegion != NULL)
    {
        return;
    }
    memset(&muiCommand, 0, sizeof(muiCommand));
    muiCommand.infoOverlayWasRunning = ioDisable();
    muiCommandLayoutCompute(&layout);
    muiCommandEnsureBackdrop();

    muiCommand.leftRegion = regChildAlloc(
        ghMainRegion, (smemsize)&muiCommand, layout.left.x0, layout.left.y0,
        layout.left.x1 - layout.left.x0, layout.left.y1 - layout.left.y0, 0,
        RPE_PressLeft | RPE_ReleaseLeft | RPE_DrawEveryFrame);
    regDrawFunctionSet(muiCommand.leftRegion, muiCommandLeftDraw);
    regFunctionSet(muiCommand.leftRegion, muiCommandLeftProcess);

    muiCommand.rightRegion = regChildAlloc(
        ghMainRegion, (smemsize)&muiCommand, layout.right.x0, layout.right.y0,
        layout.right.x1 - layout.right.x0, layout.right.y1 - layout.right.y0, 0,
        RPE_PressLeft | RPE_ReleaseLeft | RPE_WheelUp | RPE_WheelDown |
        RPE_DrawEveryFrame);
    regDrawFunctionSet(muiCommand.rightRegion, muiCommandRightDraw);
    regFunctionSet(muiCommand.rightRegion, muiCommandRightProcess);

    if (layout.iconTray.x1 > layout.iconTray.x0 &&
        layout.iconTray.y1 > layout.iconTray.y0)
    {
        muiCommand.iconRegion = regChildAlloc(
            ghMainRegion, (smemsize)&muiCommand,
            layout.iconTray.x0, layout.iconTray.y0,
            layout.iconTray.x1 - layout.iconTray.x0,
            layout.iconTray.y1 - layout.iconTray.y0, 0,
            RPE_PressLeft | RPE_ReleaseLeft | RPE_WheelUp | RPE_WheelDown |
            RPE_DrawEveryFrame);
        regDrawFunctionSet(muiCommand.iconRegion, muiCommandIconRegionDraw);
        regFunctionSet(muiCommand.iconRegion, muiCommandRightProcess);
    }

    muiCommand.ruleRegion = regChildAlloc(
        ghMainRegion, (smemsize)&muiCommand, layout.topRule.x0, layout.topRule.y0,
        layout.topRule.x1 - layout.topRule.x0,
        layout.topRule.y1 - layout.topRule.y0, 0, RPE_DrawEveryFrame);
    regDrawFunctionSet(muiCommand.ruleRegion, muiCommandRuleDraw);

    regSiblingMoveToFront(muiCommand.leftRegion);
    regSiblingMoveToFront(muiCommand.rightRegion);
    if (muiCommand.iconRegion != NULL)
        regSiblingMoveToFront(muiCommand.iconRegion);
    regSiblingMoveToFront(muiCommand.ruleRegion);
    fprintf(stderr, "[ModernUI] Professional tactical command surface enabled.\n");
}

void modernUICommandBarBringToFront(void)
{
    if (muiCommand.leftRegion != NULL)
        regSiblingMoveToFront(muiCommand.leftRegion);
    if (muiCommand.rightRegion != NULL)
        regSiblingMoveToFront(muiCommand.rightRegion);
    if (muiCommand.iconRegion != NULL)
        regSiblingMoveToFront(muiCommand.iconRegion);
    if (muiCommand.ruleRegion != NULL)
        regSiblingMoveToFront(muiCommand.ruleRegion);

    /* Sensor Manager creates its viewport after these overlays.  Reassert
       their z-order so an already-open gameplay panel remains usable there. */
    if (muiDossier.region != NULL)
        regSiblingMoveToFront(muiDossier.region);
    if (muiLighting.markerRegion != NULL)
        regSiblingMoveToFront(muiLighting.markerRegion);
    if (muiLighting.region != NULL)
        regSiblingMoveToFront(muiLighting.region);
}

void modernUICommandBarShutdown(void)
{
    regionhandle left = muiCommand.leftRegion;
    regionhandle right = muiCommand.rightRegion;
    regionhandle icon = muiCommand.iconRegion;
    regionhandle rule = muiCommand.ruleRegion;
    bool32 restoreInfoOverlay = muiCommand.infoOverlayWasRunning;
    udword backdropTexture = muiCommand.backdropTexture;
    memset(&muiCommand, 0, sizeof(muiCommand));
    if (left != NULL) regRegionDelete(left);
    if (right != NULL) regRegionDelete(right);
    if (icon != NULL) regRegionDelete(icon);
    if (rule != NULL) regRegionDelete(rule);
    if (backdropTexture != 0) trRGBTextureDelete(backdropTexture);
    if (restoreInfoOverlay) ioEnable();
}

static bool32 muiExportVisualSettings(void)
{
    char path[1024];
    char exportRoot[1024];
    char exportDirectory[1024];
    char *basePath = SDL_GetBasePath();
    FILE *stream;
    int pathLength;
    int rootLength;
    int directoryLength;
    if (basePath == NULL)
    {
        fprintf(stderr, "[ModernUI] Visual export failed: SDL_GetBasePath returned NULL.\n");
        return FALSE;
    }
#ifdef _WIN32
    rootLength = snprintf(exportRoot, sizeof(exportRoot), "%sRTXExports", basePath);
    directoryLength = snprintf(exportDirectory, sizeof(exportDirectory),
                               "%sRTXExports\\Global", basePath);
    pathLength = snprintf(path, sizeof(path),
                          "%sRTXExports\\Global\\RTX-Visual-Settings.txt",
                          basePath);
#else
    rootLength = snprintf(exportRoot, sizeof(exportRoot), "%sRTXExports", basePath);
    directoryLength = snprintf(exportDirectory, sizeof(exportDirectory),
                               "%sRTXExports/Global", basePath);
    pathLength = snprintf(path, sizeof(path),
                          "%sRTXExports/Global/RTX-Visual-Settings.txt",
                          basePath);
#endif
    SDL_free(basePath);
    if (rootLength < 0 || (size_t)rootLength >= sizeof(exportRoot) ||
        directoryLength < 0 || (size_t)directoryLength >= sizeof(exportDirectory) ||
        pathLength < 0 || (size_t)pathLength >= sizeof(path))
    {
        fprintf(stderr, "[ModernUI] Visual export failed: game-directory path is too long.\n");
        return FALSE;
    }

    /* File.c's recursive directory helper normalizes absolute Windows paths to
       '/' but later searches only for '\\'.  When both RTXExports and Global
       are absent it therefore attempts to mkdir the nested path in one shot.
       Create each new export level explicitly; each call has an existing parent. */
    if (!fileMakeDirectory(exportRoot))
    {
        fprintf(stderr, "[ModernUI] Visual export failed: cannot create export root %s.\n",
                exportRoot);
        return FALSE;
    }
    if (!fileMakeDirectory(exportDirectory))
    {
        fprintf(stderr, "[ModernUI] Visual export failed: cannot create export directory %s.\n",
                exportDirectory);
        return FALSE;
    }
    stream = fopen(path, "wt");
    if (stream == NULL)
    {
        fprintf(stderr, "[ModernUI] Visual export failed: cannot open %s.\n", path);
        return FALSE;
    }
    fprintf(stream, "HW1RTX_VISUAL_SETTINGS 1\n");
    fprintf(stream, "# RTX-0079 export: current graphics + Shift+F12 shader/tuning state.\n");
    fprintf(stream, "# Intentionally excluded: resolution, refresh rate, anti-aliasing/DLSS mode, frame generation.\n\n");
    fprintf(stream, "[display]\n");
    fprintf(stream, "fullscreen %d\n", fullScreen ? 1 : 0);
    fprintf(stream, "exclusiveFullscreen %d\n", mainExclusiveFullscreen ? 1 : 0);
    fprintf(stream, "frameRateLimit %d\n", mainFrameRateLimit);
    fprintf(stream, "vsync %d\n", mainVSync);
    fprintf(stream, "uiScalePercent %d\n\n", mainUIScalePercent);
    fprintf(stream, "[path_tracing]\n");
    fprintf(stream, "raytracing %d\n", mainRaytracing);
    fprintf(stream, "pathTracingBounces %d\n", mainPathTracingBounces);
    fprintf(stream, "pathTracingSamples %d\n", mainPathTracingSamples);
    fprintf(stream, "generatedNormalStrengthPercent %d\n", mainGeneratedNormalStrengthPercent);
    fprintf(stream, "fxLightingStrengthPercent %d\n\n", mainFxLightingStrengthPercent);
    fprintf(stream, "[image_quality]\n");
    fprintf(stream, "brightnessPercent %d\n", opBrightnessVal);
    fprintf(stream, "effectDensityMode %d\n", opEffectsVal);
    fprintf(stream, "effectBudget %d\n\n", opNumEffects);
    fprintf(stream, "[post_effects]\n");
    fprintf(stream, "chromaticAberrationPercent %d\n", mainChromaticAberrationPercent);
    fprintf(stream, "motionBlurPercent %d\n", mainMotionBlurPercent);
    fprintf(stream, "filmGrainPercent %d\n", mainFilmGrainPercent);
    fprintf(stream, "godRaysPercent %d\n", mainGodRaysPercent);
    fprintf(stream, "bloomPercent %d\n\n", mainBloomPercent);
    fprintf(stream, "[shift_f12_global_tuning]\n");
    fprintf(stream, "primarySunLightingPercent %d\n", mainPrimarySunLightingPercent);
    fprintf(stream, "skyAmbientLightingPercent %d\n", mainSkyAmbientLightingPercent);
    fprintf(stream, "environmentLightingPercent %d\n", mainEnvironmentLightingPercent);
    fprintf(stream, "authoredLightingPercent %d\n", mainAuthoredLightingPercent);
    fprintf(stream, "fxEmissiveLightingPercent %d\n", mainFxLightingStrengthPercent);
    fprintf(stream, "surfaceReflectivityPercent %d\n", mainSurfaceReflectivityPercent);
    fprintf(stream, "surfaceRoughnessPercent %d\n", mainSurfaceRoughnessPercent);
    fprintf(stream, "generatedNormalsPercent %d\n", mainGeneratedNormalStrengthPercent);
    fprintf(stream, "pathLightExposurePercent %d\n", mainLightingExposurePercent);
    fprintf(stream, "outputDitherPercent %d\n\n", mainOutputDitherPercent);
    fprintf(stream, "[raytraced_shadows]\n");
    fprintf(stream, "shadowStrengthPercent %d\n", mainShadowStrengthPercent);
    fprintf(stream, "sunAngularRadiusDegrees %.2f\n", (float)mainSunShadowAngularRadiusCentidegrees / 100.0f);
    fprintf(stream, "sunShadowSamples %d\n", mainSunShadowSamples);
    fprintf(stream, "localShadowSamples %d\n", mainLocalShadowSamples);
    fprintf(stream, "localAngularRadiusDegrees %.2f\n", (float)mainLocalShadowAngularRadiusCentidegrees / 100.0f);
    fprintf(stream, "receiverBiasWorld %.2f\n", (float)mainShadowReceiverBiasHundredths / 100.0f);
    fprintf(stream, "normalBiasWorld %.2f\n", (float)mainShadowNormalBiasHundredths / 100.0f);
    fprintf(stream, "contactShadowStrengthPercent %d\n", mainContactShadowStrengthPercent);
    fprintf(stream, "contactShadowDistance %d\n", mainContactShadowDistance);
    fprintf(stream, "maximumShadowDistance %d\n", mainShadowMaximumDistance);
    if (fflush(stream) != 0 || ferror(stream))
    {
        fprintf(stderr, "[ModernUI] Visual export failed while writing %s.\n", path);
        fclose(stream);
        return FALSE;
    }
    if (fclose(stream) != 0)
    {
        fprintf(stderr, "[ModernUI] Visual export failed while closing %s.\n", path);
        return FALSE;
    }
    fprintf(stderr, "[ModernUI] Visual/shader settings exported to %s.\n", path);
    return TRUE;
}

static void muiLightingSetStatus(const char *text);

static bool32 muiSaveAndExportMissionLighting(void)
{
    bool32 saved;
    bool32 exported;
    fprintf(stderr, "[ModernUI] Save+Export: user-settings mission save begin.\n");
    saved = modernMissionBackdropLightingAuthoringSave();
    fprintf(stderr, "[ModernUI] Save+Export: user-settings mission save %s.\n",
            saved ? "complete" : "failed");
    fprintf(stderr, "[ModernUI] Save+Export: game-directory mission export begin.\n");
    exported = modernMissionBackdropLightingAuthoringExport();
    fprintf(stderr, "[ModernUI] Save+Export: game-directory mission export %s.\n",
            exported ? "complete" : "failed");
    if (saved && exported)
    {
        muiLightingSetStatus("MISSION SAVED + EXPORTED TO GAME/RTXExports/Missions");
        return TRUE;
    }
    if (saved)
        muiLightingSetStatus("MISSION SAVED; PORTABLE EXPORT FAILED - CHECK LOG");
    else if (exported)
        muiLightingSetStatus("MISSION EXPORTED; USER-SETTINGS SAVE FAILED");
    else
        muiLightingSetStatus("MISSION SAVE + EXPORT FAILED - check log/paths");
    return FALSE;
}

static sdword muiMissionLightSelectableCount(
    const ModernMissionLightingAuthoringState *state)
{
    return state != NULL && state->available ?
        MUI_LIGHTING_SYSTEM_COUNT + state->authoredLightCount : 0;
}

static ModernMissionAuthoredLight *muiMissionSelectedAuthoredLight(
    ModernMissionLightingAuthoringState *state)
{
    sdword index;
    if (state == NULL) return NULL;
    index = muiLighting.selectedLight - MUI_LIGHTING_SYSTEM_COUNT;
    if (index < 0 || index >= state->authoredLightCount) return NULL;
    return &state->authoredLights[index];
}

static rectangle muiLightingPanelRect(void)
{
    sdword margin = uiScaleSize(22);
    sdword width = muiClamp(MAIN_WindowWidth * 48 / 100,
                            uiScaleSize(620), uiScaleSize(780));
    sdword height = uiScaleSize(760);
    if (width > MAIN_WindowWidth - margin * 2)
        width = MAIN_WindowWidth - margin * 2;
    if (height > MAIN_WindowHeight - margin * 2)
        height = MAIN_WindowHeight - margin * 2;
    return muiRect(MAIN_WindowWidth - margin - width, margin,
                   MAIN_WindowWidth - margin, margin + height);
}

static void muiLightingLayout(sdword rowCount,
                              rectangle rows[MUI_LIGHTING_MAX_ROW_COUNT],
                              rectangle *missionTab, rectangle *lightsTab,
                              rectangle *tuningTab, rectangle *shadowTab,
                              rectangle footer[4])
{
    rectangle panel = muiLightingPanelRect();
    sdword innerX0 = panel.x0 + uiScaleSize(14);
    sdword innerX1 = panel.x1 - uiScaleSize(14);
    sdword tabTop = panel.y0 + uiScaleSize(92);
    sdword tabBottom = tabTop + uiScaleSize(32);
    sdword top = panel.y0 + uiScaleSize(154);
    sdword footerTop = panel.y1 - uiScaleSize(54);
    sdword available = footerTop - uiScaleSize(10) - top;
    sdword rowHeight = rowCount > 0 ? available / rowCount : available;
    sdword tabGap = uiScaleSize(6);
    sdword tabWidth = (innerX1 - innerX0 - tabGap * 3) / 4;
    sdword footerGap = uiScaleSize(6);
    sdword footerWidth = (innerX1 - innerX0 - footerGap * 3) / 4;
    sdword index;

    *missionTab = muiRect(innerX0, tabTop, innerX0 + tabWidth, tabBottom);
    *lightsTab = muiRect(missionTab->x1 + tabGap, tabTop,
                         missionTab->x1 + tabGap + tabWidth, tabBottom);
    *tuningTab = muiRect(lightsTab->x1 + tabGap, tabTop,
                         lightsTab->x1 + tabGap + tabWidth, tabBottom);
    *shadowTab = muiRect(tuningTab->x1 + tabGap, tabTop, innerX1, tabBottom);
    for (index = 0; index < rowCount; ++index)
    {
        rows[index] = muiRect(innerX0, top + index * rowHeight,
                              innerX1, top + (index + 1) * rowHeight);
    }
    for (index = 0; index < 4; ++index)
    {
        sdword x0 = innerX0 + index * (footerWidth + footerGap);
        footer[index] = muiRect(x0, footerTop, x0 + footerWidth,
                                panel.y1 - uiScaleSize(12));
    }
}

static sdword *muiLightingValue(sdword row, sdword *low, sdword *high,
                               sdword *step)
{
    *step = 5;
    switch (row)
    {
        case 0:
            *low = 0; *high = 200;
            return &mainPrimarySunLightingPercent;
        case 1:
            *low = 0; *high = 200;
            return &mainSkyAmbientLightingPercent;
        case 2:
            *low = 0; *high = 200;
            return &mainEnvironmentLightingPercent;
        case 3:
            *low = 0; *high = 200;
            return &mainAuthoredLightingPercent;
        case 4:
            *low = 0; *high = 200;
            return &mainFxLightingStrengthPercent;
        case 5:
            *low = 0; *high = 200;
            return &mainSurfaceReflectivityPercent;
        case 6:
            *low = 0; *high = 100;
            return &mainSurfaceRoughnessPercent;
        case 7:
            *low = 0; *high = 400;
            return &mainGeneratedNormalStrengthPercent;
        case 8:
            *low = 50; *high = 200;
            return &mainLightingExposurePercent;
        default:
            *low = 0; *high = 200;
            return &mainOutputDitherPercent;
    }
}

static void muiLightingApply(void)
{
    mainPrimarySunLightingPercent = muiClamp(
        mainPrimarySunLightingPercent, 0, 200);
    mainSkyAmbientLightingPercent = muiClamp(
        mainSkyAmbientLightingPercent, 0, 200);
    mainEnvironmentLightingPercent = muiClamp(
        mainEnvironmentLightingPercent, 0, 200);
    mainAuthoredLightingPercent = muiClamp(
        mainAuthoredLightingPercent, 0, 200);
    mainFxLightingStrengthPercent = muiClamp(
        mainFxLightingStrengthPercent, 0, 200);
    mainSurfaceReflectivityPercent = muiClamp(
        mainSurfaceReflectivityPercent, 0, 200);
    mainSurfaceRoughnessPercent = muiClamp(
        mainSurfaceRoughnessPercent, 0, 100);
    mainGeneratedNormalStrengthPercent = muiClamp(
        mainGeneratedNormalStrengthPercent, 0, 400);
    mainLightingExposurePercent = muiClamp(
        mainLightingExposurePercent, 50, 200);
    mainOutputDitherPercent = muiClamp(mainOutputDitherPercent, 0, 200);
    muiApplyRendererLive();
}

static void muiLightingAdjust(sdword row, sdword direction)
{
    sdword low, high, step;
    sdword *value = muiLightingValue(row, &low, &high, &step);
    *value = muiClamp(*value + direction * step, low, high);
    muiLightingApply();
}

static void muiLightingReset(void)
{
    mainPrimarySunLightingPercent = 100;
    mainSkyAmbientLightingPercent = 100;
    mainEnvironmentLightingPercent = 100;
    mainAuthoredLightingPercent = 100;
    mainFxLightingStrengthPercent = 100;
    mainSurfaceReflectivityPercent = 125;
    mainSurfaceRoughnessPercent = 38;
    mainGeneratedNormalStrengthPercent = 100;
    mainLightingExposurePercent = 100;
    mainOutputDitherPercent = 100;
    muiLightingApply();
}

static sdword muiLightingRowCountForPage(sdword page)
{
    if (page == MUI_LIGHTING_PAGE_MISSION) return MUI_LIGHTING_KEY_ROW_COUNT;
    if (page == MUI_LIGHTING_PAGE_LIGHTS) return MUI_LIGHTING_LIGHT_ROW_COUNT;
    if (page == MUI_LIGHTING_PAGE_SHADOWS) return MUI_LIGHTING_SHADOW_ROW_COUNT;
    return MUI_LIGHTING_ROW_COUNT;
}

static sdword *muiShadowValue(sdword row, sdword *low, sdword *high, sdword *step)
{
    switch (row)
    {
        case 0: *low=0; *high=200; *step=5; return &mainShadowStrengthPercent;
        case 1: *low=0; *high=500; *step=5; return &mainSunShadowAngularRadiusCentidegrees;
        case 2: *low=1; *high=16; *step=1; return &mainSunShadowSamples;
        case 3: *low=1; *high=8; *step=1; return &mainLocalShadowSamples;
        case 4: *low=0; *high=1000; *step=10; return &mainLocalShadowAngularRadiusCentidegrees;
        case 5: *low=1; *high=200; *step=1; return &mainShadowReceiverBiasHundredths;
        case 6: *low=0; *high=400; *step=1; return &mainShadowNormalBiasHundredths;
        case 7: *low=0; *high=200; *step=5; return &mainContactShadowStrengthPercent;
        case 8: *low=0; *high=10000; *step=100; return &mainContactShadowDistance;
        default:*low=10000; *high=1000000; *step=10000; return &mainShadowMaximumDistance;
    }
}

static void muiShadowApply(void)
{
    sdword low, high, step, row;
    for (row = 0; row < MUI_LIGHTING_SHADOW_ROW_COUNT; ++row)
    {
        sdword *value = muiShadowValue(row, &low, &high, &step);
        *value = muiClamp(*value, low, high);
    }
    muiApplyRendererLive();
}

static void muiShadowAdjust(sdword row, sdword direction)
{
    sdword low, high, step;
    sdword *value = muiShadowValue(row, &low, &high, &step);
    *value = muiClamp(*value + direction * step, low, high);
    muiShadowApply();
}

static void muiShadowReset(void)
{
    mainShadowStrengthPercent = 100;
    mainSunShadowAngularRadiusCentidegrees = 35;
    mainSunShadowSamples = 6;
    mainLocalShadowSamples = 2;
    mainLocalShadowAngularRadiusCentidegrees = 20;
    mainShadowReceiverBiasHundredths = 5;
    mainShadowNormalBiasHundredths = 8;
    mainContactShadowStrengthPercent = 100;
    mainContactShadowDistance = 1200;
    mainShadowMaximumDistance = 500000;
    muiShadowApply();
}

static void muiLightingSetStatus(const char *text)
{
    if (text == NULL) text = "";
    snprintf(muiLighting.status, sizeof(muiLighting.status), "%s", text);
}

static void muiLightingSetPage(sdword page)
{
    if (page != MUI_LIGHTING_PAGE_MISSION &&
        page != MUI_LIGHTING_PAGE_LIGHTS &&
        page != MUI_LIGHTING_PAGE_TUNING &&
        page != MUI_LIGHTING_PAGE_SHADOWS)
    {
        page = MUI_LIGHTING_PAGE_MISSION;
    }
    if (muiLighting.soloKey && page != MUI_LIGHTING_PAGE_MISSION)
    {
        mainSkyAmbientLightingPercent = muiLighting.soloSkyAmbient;
        mainEnvironmentLightingPercent = muiLighting.soloEnvironment;
        mainAuthoredLightingPercent = muiLighting.soloAuthored;
        mainFxLightingStrengthPercent = muiLighting.soloFx;
        muiLighting.soloKey = FALSE;
        muiLightingApply();
    }
    muiLighting.page = page;
    muiLighting.selectedRow = 0;
}

static void muiLightingSoloKeySet(bool32 enabled)
{
    if (enabled && !muiLighting.soloKey)
    {
        muiLighting.soloSkyAmbient = mainSkyAmbientLightingPercent;
        muiLighting.soloEnvironment = mainEnvironmentLightingPercent;
        muiLighting.soloAuthored = mainAuthoredLightingPercent;
        muiLighting.soloFx = mainFxLightingStrengthPercent;
        mainSkyAmbientLightingPercent = 0;
        mainEnvironmentLightingPercent = 0;
        mainAuthoredLightingPercent = 0;
        mainFxLightingStrengthPercent = 0;
        muiLighting.soloKey = TRUE;
        muiLightingSetStatus("SOLO KEY ACTIVE - all non-key lighting muted temporarily");
        muiLightingApply();
    }
    else if (!enabled && muiLighting.soloKey)
    {
        mainSkyAmbientLightingPercent = muiLighting.soloSkyAmbient;
        mainEnvironmentLightingPercent = muiLighting.soloEnvironment;
        mainAuthoredLightingPercent = muiLighting.soloAuthored;
        mainFxLightingStrengthPercent = muiLighting.soloFx;
        muiLighting.soloKey = FALSE;
        muiLightingSetStatus("Solo key disabled - previous lighting restored");
        muiLightingApply();
    }
}

static void muiMissionLightingAngles(const real32 direction[3],
                                     real32 *azimuthDegrees,
                                     real32 *elevationDegrees)
{
    real32 y = direction[1];
    if (y < -1.0f) y = -1.0f;
    if (y > 1.0f) y = 1.0f;
    *azimuthDegrees = (real32)(atan2(direction[0], -direction[2]) *
                               (180.0 / MUI_PI));
    *elevationDegrees = (real32)(asin(y) * (180.0 / MUI_PI));
}

static void muiMissionLightingDirection(real32 azimuthDegrees,
                                        real32 elevationDegrees,
                                        real32 direction[3])
{
    const real32 azimuth = azimuthDegrees * (MUI_PI / 180.0f);
    const real32 elevation = elevationDegrees * (MUI_PI / 180.0f);
    const real32 cosElevation = (real32)cos(elevation);
    direction[0] = cosElevation * (real32)sin(azimuth);
    direction[1] = (real32)sin(elevation);
    direction[2] = -cosElevation * (real32)cos(azimuth);
}

static bool32 muiMissionLightingAdjust(sdword row, sdword direction)
{
    ModernMissionLightingAuthoringState state;
    real32 azimuth;
    real32 elevation;
    if (!modernMissionBackdropLightingAuthoringGet(&state))
    {
        muiLightingSetStatus("Mission HDR sky is not ready yet");
        return FALSE;
    }
    muiMissionLightingAngles(state.direction, &azimuth, &elevation);
    switch (row)
    {
        case 0:
            azimuth += (real32)direction;
            if (azimuth > 180.0f) azimuth -= 360.0f;
            if (azimuth < -180.0f) azimuth += 360.0f;
            muiMissionLightingDirection(azimuth, elevation, state.direction);
            break;
        case 1:
            elevation += (real32)direction;
            if (elevation > 89.0f) elevation = 89.0f;
            if (elevation < -89.0f) elevation = -89.0f;
            muiMissionLightingDirection(azimuth, elevation, state.direction);
            break;
        case 2:
            state.sunIntensity += (real32)direction * 0.05f;
            if (state.sunIntensity < 0.0f) state.sunIntensity = 0.0f;
            if (state.sunIntensity > 4.0f) state.sunIntensity = 4.0f;
            break;
        case 3:
        case 4:
        case 5:
        {
            sdword channel = row - 3;
            state.sourceColorScale[channel] += (real32)direction * 0.05f;
            if (state.sourceColorScale[channel] < 0.0f)
                state.sourceColorScale[channel] = 0.0f;
            if (state.sourceColorScale[channel] > 4.0f)
                state.sourceColorScale[channel] = 4.0f;
            break;
        }
        case 6:
            state.ambientEnabled = TRUE;
            state.ambientScale += (real32)direction * 0.05f;
            if (state.ambientScale < 0.0f) state.ambientScale = 0.0f;
            if (state.ambientScale > 4.0f) state.ambientScale = 4.0f;
            break;
        case 7:
            state.ambientEnabled = TRUE;
            state.ambientColorOverride = !state.ambientColorOverride;
            break;
        default:
        {
            sdword channel = row - 8;
            if (channel < 0 || channel > 2) return FALSE;
            state.ambientEnabled = TRUE;
            state.ambientColorOverride = TRUE;
            state.ambientColor[channel] += (real32)direction * 0.05f;
            if (state.ambientColor[channel] < 0.0f)
                state.ambientColor[channel] = 0.0f;
            if (state.ambientColor[channel] > 4.0f)
                state.ambientColor[channel] = 4.0f;
            break;
        }
    }
    if (row <= 5) state.sourceValid = TRUE;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("LIVE / UNSAVED - press S or SAVE MISSION when satisfied");
    return TRUE;
}

static bool32 muiLightingCursorWorldRay(real32 origin[3], real32 direction[3])
{
    const real32 *view = (const real32 *)&rndCameraMatrix;
    real32 ndcX;
    real32 ndcY;
    real32 tanHalfFov;
    real32 aspect;
    real32 viewX;
    real32 viewY;
    real32 viewZ = -1.0f;
    real32 length;
    real32 worldX;
    real32 worldY;
    real32 worldZ;
    if (mrCamera == NULL || MAIN_WindowWidth <= 0 || MAIN_WindowHeight <= 0)
        return FALSE;
    ndcX = ((real32)mouseCursorX() / (real32)MAIN_WindowWidth) * 2.0f - 1.0f;
    ndcY = 1.0f - ((real32)mouseCursorY() / (real32)MAIN_WindowHeight) * 2.0f;
    aspect = rndAspectRatio > 0.01f ? rndAspectRatio :
        (real32)MAIN_WindowWidth / (real32)MAIN_WindowHeight;
    tanHalfFov = (real32)tan(muiClamp((sdword)mrCamera->fieldofview, 1, 179) *
                                (MUI_PI / 360.0f));
    viewX = ndcX * tanHalfFov * aspect;
    viewY = ndcY * tanHalfFov;
    length = (real32)sqrt(viewX * viewX + viewY * viewY + viewZ * viewZ);
    if (length <= 0.000001f) return FALSE;
    viewX /= length;
    viewY /= length;
    viewZ /= length;

    /* Invert the world->view rotation with its transpose, matching the
       mission-key placement convention already validated in RTX-0076. */
    worldX = view[0] * viewX + view[1] * viewY + view[2] * viewZ;
    worldY = view[4] * viewX + view[5] * viewY + view[6] * viewZ;
    worldZ = view[8] * viewX + view[9] * viewY + view[10] * viewZ;
    length = (real32)sqrt(worldX * worldX + worldY * worldY + worldZ * worldZ);
    if (length <= 0.000001f) return FALSE;
    direction[0] = worldX / length;
    direction[1] = worldY / length;
    direction[2] = worldZ / length;
    if (origin != NULL)
    {
        origin[0] = mrCamera->eyeposition.x;
        origin[1] = mrCamera->eyeposition.y;
        origin[2] = mrCamera->eyeposition.z;
    }
    return TRUE;
}

static bool32 muiMissionLightingPlaceKeyAtCursor(void)
{
    ModernMissionLightingAuthoringState state;
    real32 direction[3];
    if (!modernMissionBackdropLightingAuthoringGet(&state) ||
        !muiLightingCursorWorldRay(NULL, direction))
    {
        muiLightingSetStatus("Cannot place key until the mission sky/camera is ready");
        return FALSE;
    }
    state.direction[0] = direction[0];
    state.direction[1] = direction[1];
    state.direction[2] = direction[2];
    state.sourceValid = TRUE;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("KEY PLACED AT CURSOR / UNSAVED");
    return TRUE;
}

static void muiMissionLightDefaults(ModernMissionAuthoredLight *light)
{
    real32 radius = mrCamera != NULL ? mrCamera->distance * 2.0f : 25000.0f;
    if (radius < 5000.0f) radius = 5000.0f;
    if (radius > 250000.0f) radius = 250000.0f;
    memset(light, 0, sizeof(*light));
    light->enabled = TRUE;
    light->type = MODERN_MISSION_LIGHT_POINT;
    if (mrCamera != NULL)
    {
        light->position[0] = mrCamera->lookatpoint.x;
        light->position[1] = mrCamera->lookatpoint.y;
        light->position[2] = mrCamera->lookatpoint.z;
    }
    light->direction[0] = 0.0f;
    light->direction[1] = 0.0f;
    light->direction[2] = -1.0f;
    light->color[0] = light->color[1] = light->color[2] = 1.0f;
    light->intensity = 1.0f;
    light->radius = radius;
    light->coneAngle = 25.0f;
    light->edgeAngle = 10.0f;
}

static bool32 muiMissionLightAdd(void)
{
    ModernMissionLightingAuthoringState state;
    if (!modernMissionBackdropLightingAuthoringGet(&state))
    {
        muiLightingSetStatus("Mission HDR sky is not ready yet");
        return FALSE;
    }
    if (state.authoredLightCount >= MODERN_MISSION_AUTHORED_LIGHT_MAX)
    {
        muiLightingSetStatus("CUSTOM LIGHT LIMIT REACHED (16)");
        return FALSE;
    }
    muiLighting.selectedLight = MUI_LIGHTING_SYSTEM_COUNT + state.authoredLightCount;
    muiMissionLightDefaults(&state.authoredLights[state.authoredLightCount]);
    ++state.authoredLightCount;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("POINT LIGHT ADDED / P places it at cursor");
    return TRUE;
}

static bool32 muiMissionLightDelete(void)
{
    ModernMissionLightingAuthoringState state;
    ModernMissionAuthoredLight *light;
    sdword authoredIndex;
    sdword index;
    sdword total;
    if (!modernMissionBackdropLightingAuthoringGet(&state))
    {
        muiLightingSetStatus("NO MISSION LIGHT TO DELETE");
        return FALSE;
    }
    total = muiMissionLightSelectableCount(&state);
    if (total <= 0) return FALSE;
    if (muiLighting.selectedLight < 0) muiLighting.selectedLight = 0;
    if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;
    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY)
    {
        state.sourceValid = FALSE;
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("PRIMARY KEY REMOVED / RESET restores detected key");
        return TRUE;
    }
    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_AMBIENT)
    {
        state.ambientEnabled = FALSE;
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("SKY AMBIENT REMOVED / RESET restores HDR ambient");
        return TRUE;
    }
    light = muiMissionSelectedAuthoredLight(&state);
    if (light == NULL) return FALSE;
    authoredIndex = muiLighting.selectedLight - MUI_LIGHTING_SYSTEM_COUNT;
    for (index = authoredIndex; index + 1 < state.authoredLightCount; ++index)
        state.authoredLights[index] = state.authoredLights[index + 1];
    --state.authoredLightCount;
    total = MUI_LIGHTING_SYSTEM_COUNT + state.authoredLightCount;
    if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("CUSTOM/MAP LIGHT DELETED / UNSAVED");
    return TRUE;
}

static real32 muiMissionLightYawDegrees(const real32 direction[3])
{
    real32 yaw = (real32)(atan2(direction[0], -direction[2]) * (180.0 / MUI_PI));
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw < -180.0f) yaw += 360.0f;
    return yaw;
}

static void muiMissionLightSetYawDegrees(ModernMissionAuthoredLight *light, real32 yawDegrees)
{
    real32 horizontal;
    real32 radians;
    real32 length;
    if (light == NULL) return;
    horizontal = (real32)sqrt(light->direction[0] * light->direction[0] +
                              light->direction[2] * light->direction[2]);
    if (horizontal < 0.000001f)
    {
        horizontal = (real32)sqrt(max(0.0f, 1.0f - light->direction[1] * light->direction[1]));
        if (horizontal < 0.000001f) horizontal = 1.0f;
    }
    radians = yawDegrees * (MUI_PI / 180.0f);
    light->direction[0] = (real32)sin(radians) * horizontal;
    light->direction[2] = -(real32)cos(radians) * horizontal;
    length = (real32)sqrt(light->direction[0] * light->direction[0] +
                          light->direction[1] * light->direction[1] +
                          light->direction[2] * light->direction[2]);
    if (length > 0.000001f)
    {
        light->direction[0] /= length;
        light->direction[1] /= length;
        light->direction[2] /= length;
    }
}

static bool32 muiMissionLightAdjust(sdword row, sdword direction)
{
    ModernMissionLightingAuthoringState state;
    ModernMissionAuthoredLight *light;
    real32 step;
    sdword total;
    if (!modernMissionBackdropLightingAuthoringGet(&state))
    {
        muiLightingSetStatus("Mission HDR sky is not ready yet");
        return FALSE;
    }
    total = muiMissionLightSelectableCount(&state);
    if (total <= 0) return FALSE;
    if (row == 0)
    {
        muiLighting.selectedLight += direction;
        while (muiLighting.selectedLight < 0) muiLighting.selectedLight += total;
        while (muiLighting.selectedLight >= total) muiLighting.selectedLight -= total;
        return TRUE;
    }
    if (muiLighting.selectedLight < 0) muiLighting.selectedLight = 0;
    if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;

    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY)
    {
        real32 azimuth, elevation;
        muiMissionLightingAngles(state.direction, &azimuth, &elevation);
        switch (row)
        {
            case 1: return TRUE; /* Directional type is fixed. */
            case 2: state.sourceValid = !state.sourceValid; break;
            case 3:
                state.sunIntensity += (real32)direction * 0.05f;
                if (state.sunIntensity < 0.0f) state.sunIntensity = 0.0f;
                if (state.sunIntensity > 4.0f) state.sunIntensity = 4.0f;
                state.sourceValid = TRUE;
                break;
            case 4: case 5: case 6:
            {
                sdword channel = row - 4;
                state.sourceColorScale[channel] += (real32)direction * 0.05f;
                if (state.sourceColorScale[channel] < 0.0f) state.sourceColorScale[channel] = 0.0f;
                if (state.sourceColorScale[channel] > 4.0f) state.sourceColorScale[channel] = 4.0f;
                state.sourceValid = TRUE;
                break;
            }
            case 10:
                azimuth += (real32)direction;
                if (azimuth > 180.0f) azimuth -= 360.0f;
                if (azimuth < -180.0f) azimuth += 360.0f;
                muiMissionLightingDirection(azimuth, elevation, state.direction);
                state.sourceValid = TRUE;
                break;
            default: return TRUE;
        }
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("PRIMARY KEY LIVE / UNSAVED");
        return TRUE;
    }

    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_AMBIENT)
    {
        switch (row)
        {
            case 1: return TRUE;
            case 2: state.ambientEnabled = !state.ambientEnabled; break;
            case 3:
                state.ambientEnabled = TRUE;
                state.ambientScale += (real32)direction * 0.05f;
                if (state.ambientScale < 0.0f) state.ambientScale = 0.0f;
                if (state.ambientScale > 4.0f) state.ambientScale = 4.0f;
                break;
            case 4: case 5: case 6:
            {
                sdword channel = row - 4;
                state.ambientEnabled = TRUE;
                state.ambientColorOverride = TRUE;
                state.ambientColor[channel] += (real32)direction * 0.05f;
                if (state.ambientColor[channel] < 0.0f) state.ambientColor[channel] = 0.0f;
                if (state.ambientColor[channel] > 4.0f) state.ambientColor[channel] = 4.0f;
                break;
            }
            default: return TRUE;
        }
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("SKY AMBIENT LIVE / UNSAVED");
        return TRUE;
    }

    light = muiMissionSelectedAuthoredLight(&state);
    if (light == NULL) return FALSE;
    switch (row)
    {
        case 1:
            light->type += direction;
            if (light->type > MODERN_MISSION_LIGHT_AMBIENT) light->type = MODERN_MISSION_LIGHT_POINT;
            if (light->type < MODERN_MISSION_LIGHT_POINT) light->type = MODERN_MISSION_LIGHT_AMBIENT;
            break;
        case 2: light->enabled = !light->enabled; break;
        case 3:
            light->intensity += (real32)direction * 0.05f;
            light->intensity = (real32)muiClamp((sdword)(light->intensity * 100.0f), 0, 800) / 100.0f;
            break;
        case 4: case 5: case 6:
        {
            sdword channel = row - 4;
            light->color[channel] += (real32)direction * 0.05f;
            if (light->color[channel] < 0.0f) light->color[channel] = 0.0f;
            if (light->color[channel] > 4.0f) light->color[channel] = 4.0f;
            break;
        }
        case 7:
            if (light->type == MODERN_MISSION_LIGHT_AMBIENT) return TRUE;
            step = max(100.0f, light->radius * 0.05f);
            light->radius += (real32)direction * step;
            if (light->radius < 100.0f) light->radius = 100.0f;
            if (light->radius > 1000000.0f) light->radius = 1000000.0f;
            break;
        case 8:
            if (light->type != MODERN_MISSION_LIGHT_SPOT) return TRUE;
            light->coneAngle += (real32)direction;
            if (light->coneAngle < 1.0f) light->coneAngle = 1.0f;
            if (light->coneAngle > 89.0f) light->coneAngle = 89.0f;
            break;
        case 9:
            if (light->type != MODERN_MISSION_LIGHT_SPOT) return TRUE;
            light->edgeAngle += (real32)direction;
            if (light->edgeAngle < 0.0f) light->edgeAngle = 0.0f;
            if (light->edgeAngle > 89.0f) light->edgeAngle = 89.0f;
            break;
        case 10:
            if (light->type != MODERN_MISSION_LIGHT_SPOT) return TRUE;
            muiMissionLightSetYawDegrees(light, muiMissionLightYawDegrees(light->direction) + (real32)direction);
            break;
        case 11: case 12: case 13:
            if (light->type == MODERN_MISSION_LIGHT_AMBIENT) return TRUE;
            step = mrCamera != NULL ? max(25.0f, mrCamera->distance * 0.01f) : 100.0f;
            light->position[row - 11] += (real32)direction * step;
            break;
        default: return FALSE;
    }
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("CUSTOM LIGHT LIVE / UNSAVED");
    return TRUE;
}

static bool32 muiMissionLightPlaceAtCursor(void)
{
    ModernMissionLightingAuthoringState state;
    ModernMissionAuthoredLight *light;
    real32 origin[3], ray[3], targetDistance, sourceDistance, target[3], length;
    sdword total;
    if (!modernMissionBackdropLightingAuthoringGet(&state)) return FALSE;
    total = muiMissionLightSelectableCount(&state);
    if (total <= 0) return FALSE;
    if (muiLighting.selectedLight < 0) muiLighting.selectedLight = 0;
    if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;
    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY)
        return muiMissionLightingPlaceKeyAtCursor();
    if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_AMBIENT)
    {
        state.ambientEnabled = TRUE;
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("SKY AMBIENT ENABLED / GLOBAL / UNSAVED");
        return TRUE;
    }
    light = muiMissionSelectedAuthoredLight(&state);
    if (light == NULL) return FALSE;
    if (light->type == MODERN_MISSION_LIGHT_AMBIENT)
    {
        light->enabled = TRUE;
        if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
        muiLightingSetStatus("AMBIENT LIGHT IS GLOBAL / ENABLED / UNSAVED");
        return TRUE;
    }
    if (!muiLightingCursorWorldRay(origin, ray))
    {
        muiLightingSetStatus("Cannot place light until the mission camera is ready");
        return FALSE;
    }
    targetDistance = mrCamera != NULL ? mrCamera->distance : 10000.0f;
    if (targetDistance < 500.0f) targetDistance = 500.0f;
    target[0] = origin[0] + ray[0] * targetDistance;
    target[1] = origin[1] + ray[1] * targetDistance;
    target[2] = origin[2] + ray[2] * targetDistance;
    sourceDistance = light->type == MODERN_MISSION_LIGHT_SPOT ? targetDistance * 0.65f : targetDistance;
    light->position[0] = origin[0] + ray[0] * sourceDistance;
    light->position[1] = origin[1] + ray[1] * sourceDistance;
    light->position[2] = origin[2] + ray[2] * sourceDistance;
    if (light->type == MODERN_MISSION_LIGHT_SPOT)
    {
        light->direction[0] = target[0] - light->position[0];
        light->direction[1] = target[1] - light->position[1];
        light->direction[2] = target[2] - light->position[2];
        length = (real32)sqrt(light->direction[0] * light->direction[0] +
                              light->direction[1] * light->direction[1] +
                              light->direction[2] * light->direction[2]);
        if (length > 0.000001f)
        {
            light->direction[0] /= length;
            light->direction[1] /= length;
            light->direction[2] /= length;
        }
    }
    light->enabled = TRUE;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus(light->type == MODERN_MISSION_LIGHT_SPOT ?
        "SPOT PLACED + AIMED AT CURSOR DEPTH / A re-aims / UNSAVED" :
        "POINT LIGHT PLACED AT CURSOR DEPTH / UNSAVED");
    return TRUE;
}

static bool32 muiMissionLightAimAtCursor(void)
{
    ModernMissionLightingAuthoringState state;
    ModernMissionAuthoredLight *light;
    real32 origin[3], ray[3], targetDistance, target[3], length;
    if (!modernMissionBackdropLightingAuthoringGet(&state)) return FALSE;
    if (muiLighting.selectedLight < MUI_LIGHTING_SYSTEM_COUNT)
    {
        muiLightingSetStatus("AIM is only used by custom/imported SPOT lights");
        return FALSE;
    }
    light = muiMissionSelectedAuthoredLight(&state);
    if (light == NULL || light->type != MODERN_MISSION_LIGHT_SPOT)
    {
        muiLightingSetStatus("AIM is only used by SPOT lights");
        return FALSE;
    }
    if (!muiLightingCursorWorldRay(origin, ray)) return FALSE;
    targetDistance = mrCamera != NULL ? mrCamera->distance : 10000.0f;
    if (targetDistance < 500.0f) targetDistance = 500.0f;
    target[0] = origin[0] + ray[0] * targetDistance;
    target[1] = origin[1] + ray[1] * targetDistance;
    target[2] = origin[2] + ray[2] * targetDistance;
    light->direction[0] = target[0] - light->position[0];
    light->direction[1] = target[1] - light->position[1];
    light->direction[2] = target[2] - light->position[2];
    length = (real32)sqrt(light->direction[0] * light->direction[0] +
                          light->direction[1] * light->direction[1] +
                          light->direction[2] * light->direction[2]);
    if (length <= 0.000001f) return FALSE;
    light->direction[0] /= length;
    light->direction[1] /= length;
    light->direction[2] /= length;
    if (!modernMissionBackdropLightingAuthoringSet(&state)) return FALSE;
    muiLightingSetStatus("SPOT AIMED AT CURSOR / UNSAVED");
    return TRUE;
}

static bool32 muiMissionLightingProject(const real32 direction[3],
                                        sdword *outX, sdword *outY,
                                        bool32 *outClamped)
{
    const real32 *view = (const real32 *)&rndCameraMatrix;
    real32 viewX;
    real32 viewY;
    real32 viewZ;
    real32 aspect;
    real32 tanHalfFov;
    real32 ndcX;
    real32 ndcY;
    real32 screenX;
    real32 screenY;
    bool32 clamped = FALSE;
    sdword margin = uiScaleSize(20);
    if (mrCamera == NULL || MAIN_WindowWidth <= 0 || MAIN_WindowHeight <= 0)
        return FALSE;
    viewX = view[0] * direction[0] + view[4] * direction[1] + view[8] * direction[2];
    viewY = view[1] * direction[0] + view[5] * direction[1] + view[9] * direction[2];
    viewZ = view[2] * direction[0] + view[6] * direction[1] + view[10] * direction[2];
    if (viewZ >= -0.0001f) return FALSE;
    aspect = rndAspectRatio > 0.01f ? rndAspectRatio :
        (real32)MAIN_WindowWidth / (real32)MAIN_WindowHeight;
    tanHalfFov = (real32)tan(muiClamp((sdword)mrCamera->fieldofview, 1, 179) *
                                (MUI_PI / 360.0f));
    ndcX = viewX / (-viewZ * tanHalfFov * aspect);
    ndcY = viewY / (-viewZ * tanHalfFov);
    screenX = (ndcX * 0.5f + 0.5f) * (real32)MAIN_WindowWidth;
    screenY = (0.5f - ndcY * 0.5f) * (real32)MAIN_WindowHeight;
    if (screenX < margin) { screenX = (real32)margin; clamped = TRUE; }
    if (screenX > MAIN_WindowWidth - margin)
    { screenX = (real32)(MAIN_WindowWidth - margin); clamped = TRUE; }
    if (screenY < margin) { screenY = (real32)margin; clamped = TRUE; }
    if (screenY > MAIN_WindowHeight - margin)
    { screenY = (real32)(MAIN_WindowHeight - margin); clamped = TRUE; }
    *outX = (sdword)screenX;
    *outY = (sdword)screenY;
    if (outClamped != NULL) *outClamped = clamped;
    return TRUE;
}

static bool32 muiMissionLightingProjectPoint(const real32 point[3],
                                             sdword *outX, sdword *outY,
                                             bool32 *outClamped)
{
    const real32 *view = (const real32 *)&rndCameraMatrix;
    real32 viewX;
    real32 viewY;
    real32 viewZ;
    real32 aspect;
    real32 tanHalfFov;
    real32 ndcX;
    real32 ndcY;
    real32 screenX;
    real32 screenY;
    bool32 clamped = FALSE;
    sdword margin = uiScaleSize(20);
    if (mrCamera == NULL || MAIN_WindowWidth <= 0 || MAIN_WindowHeight <= 0)
        return FALSE;
    viewX = view[0] * point[0] + view[4] * point[1] +
            view[8] * point[2] + view[12];
    viewY = view[1] * point[0] + view[5] * point[1] +
            view[9] * point[2] + view[13];
    viewZ = view[2] * point[0] + view[6] * point[1] +
            view[10] * point[2] + view[14];
    if (viewZ >= -0.0001f) return FALSE;
    aspect = rndAspectRatio > 0.01f ? rndAspectRatio :
        (real32)MAIN_WindowWidth / (real32)MAIN_WindowHeight;
    tanHalfFov = (real32)tan(muiClamp((sdword)mrCamera->fieldofview, 1, 179) *
                                (MUI_PI / 360.0f));
    ndcX = viewX / (-viewZ * tanHalfFov * aspect);
    ndcY = viewY / (-viewZ * tanHalfFov);
    screenX = (ndcX * 0.5f + 0.5f) * (real32)MAIN_WindowWidth;
    screenY = (0.5f - ndcY * 0.5f) * (real32)MAIN_WindowHeight;
    if (screenX < margin) { screenX = (real32)margin; clamped = TRUE; }
    if (screenX > MAIN_WindowWidth - margin)
    { screenX = (real32)(MAIN_WindowWidth - margin); clamped = TRUE; }
    if (screenY < margin) { screenY = (real32)margin; clamped = TRUE; }
    if (screenY > MAIN_WindowHeight - margin)
    { screenY = (real32)(MAIN_WindowHeight - margin); clamped = TRUE; }
    *outX = (sdword)screenX;
    *outY = (sdword)screenY;
    if (outClamped != NULL) *outClamped = clamped;
    return TRUE;
}

static bool32 muiMissionLightingNormalize3(const real32 inVector[3], real32 outVector[3])
{
    real32 length = (real32)sqrt(inVector[0] * inVector[0] +
                                inVector[1] * inVector[1] +
                                inVector[2] * inVector[2]);
    if (length <= 0.000001f) return FALSE;
    outVector[0] = inVector[0] / length;
    outVector[1] = inVector[1] / length;
    outVector[2] = inVector[2] / length;
    return TRUE;
}

static void muiMissionLightingDrawArrow2D(sdword x0, sdword y0,
                                          sdword x1, sdword y1,
                                          color lineColor)
{
    real32 dx = (real32)(x1 - x0);
    real32 dy = (real32)(y1 - y0);
    real32 length = (real32)sqrt(dx * dx + dy * dy);
    real32 nx;
    real32 ny;
    real32 head = (real32)uiScaleSize(10);
    if (length < 2.0f) return;
    nx = dx / length;
    ny = dy / length;
    primLine2(x0, y0, x1, y1, lineColor);
    primLine2(x1, y1,
              (sdword)(x1 - nx * head - ny * head * 0.55f),
              (sdword)(y1 - ny * head + nx * head * 0.55f),
              lineColor);
    primLine2(x1, y1,
              (sdword)(x1 - nx * head + ny * head * 0.55f),
              (sdword)(y1 - ny * head - nx * head * 0.55f),
              lineColor);
}

static real32 muiMissionLightingGizmoLength(const ModernMissionAuthoredLight *light)
{
    real32 length = mrCamera != NULL ? mrCamera->distance * 0.45f : 6000.0f;
    if (length < 750.0f) length = 750.0f;
    if (light != NULL && light->radius > 0.0f && length > light->radius)
        length = light->radius;
    if (length < 250.0f) length = 250.0f;
    return length;
}

static void muiMissionLightingDrawSpotCone(const ModernMissionAuthoredLight *light,
                                           color innerColor)
{
    enum { MUI_CONE_SEGMENTS = 12 };
    real32 direction[3];
    real32 reference[3] = {0.0f, 1.0f, 0.0f};
    real32 right[3];
    real32 up[3];
    real32 rightLength;
    real32 length;
    real32 center[3];
    real32 outerAngle;
    real32 innerAngle;
    real32 outerRadius;
    real32 innerRadius;
    real32 outerPoints[MUI_CONE_SEGMENTS][3];
    real32 innerPoints[MUI_CONE_SEGMENTS][3];
    sdword outerX[MUI_CONE_SEGMENTS], outerY[MUI_CONE_SEGMENTS];
    sdword innerX[MUI_CONE_SEGMENTS], innerY[MUI_CONE_SEGMENTS];
    bool32 outerVisible[MUI_CONE_SEGMENTS];
    bool32 innerVisible[MUI_CONE_SEGMENTS];
    sdword apexX, apexY, centerX, centerY;
    sdword index;
    if (light == NULL || light->type != MODERN_MISSION_LIGHT_SPOT) return;
    if (!muiMissionLightingNormalize3(light->direction, direction)) return;
    if (fabs(direction[1]) > 0.92f)
    {
        reference[0] = 1.0f;
        reference[1] = 0.0f;
        reference[2] = 0.0f;
    }
    right[0] = reference[1] * direction[2] - reference[2] * direction[1];
    right[1] = reference[2] * direction[0] - reference[0] * direction[2];
    right[2] = reference[0] * direction[1] - reference[1] * direction[0];
    rightLength = (real32)sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    if (rightLength <= 0.000001f) return;
    right[0] /= rightLength; right[1] /= rightLength; right[2] /= rightLength;
    up[0] = direction[1] * right[2] - direction[2] * right[1];
    up[1] = direction[2] * right[0] - direction[0] * right[2];
    up[2] = direction[0] * right[1] - direction[1] * right[0];

    length = muiMissionLightingGizmoLength(light);
    center[0] = light->position[0] + direction[0] * length;
    center[1] = light->position[1] + direction[1] * length;
    center[2] = light->position[2] + direction[2] * length;
    outerAngle = light->coneAngle + light->edgeAngle;
    if (outerAngle < 1.0f) outerAngle = 1.0f;
    if (outerAngle > 85.0f) outerAngle = 85.0f;
    innerAngle = light->coneAngle;
    if (innerAngle < 1.0f) innerAngle = 1.0f;
    if (innerAngle > outerAngle) innerAngle = outerAngle;
    outerRadius = length * (real32)tan(outerAngle * (MUI_PI / 180.0f));
    innerRadius = length * (real32)tan(innerAngle * (MUI_PI / 180.0f));

    if (!muiMissionLightingProjectPoint(light->position, &apexX, &apexY, NULL))
        return;
    if (muiMissionLightingProjectPoint(center, &centerX, &centerY, NULL))
        muiMissionLightingDrawArrow2D(apexX, apexY, centerX, centerY, innerColor);

    for (index = 0; index < MUI_CONE_SEGMENTS; ++index)
    {
        real32 angle = (real32)index * (2.0f * MUI_PI / (real32)MUI_CONE_SEGMENTS);
        real32 ca = (real32)cos(angle);
        real32 sa = (real32)sin(angle);
        sdword coord;
        for (coord = 0; coord < 3; ++coord)
        {
            real32 radial = right[coord] * ca + up[coord] * sa;
            outerPoints[index][coord] = center[coord] + radial * outerRadius;
            innerPoints[index][coord] = center[coord] + radial * innerRadius;
        }
        outerVisible[index] = muiMissionLightingProjectPoint(
            outerPoints[index], &outerX[index], &outerY[index], NULL);
        innerVisible[index] = muiMissionLightingProjectPoint(
            innerPoints[index], &innerX[index], &innerY[index], NULL);
    }
    for (index = 0; index < MUI_CONE_SEGMENTS; ++index)
    {
        sdword next = (index + 1) % MUI_CONE_SEGMENTS;
        if (outerVisible[index] && outerVisible[next])
            primLine2(outerX[index], outerY[index], outerX[next], outerY[next], muiMuted);
        if (innerVisible[index] && innerVisible[next])
            primLine2(innerX[index], innerY[index], innerX[next], innerY[next], innerColor);
        if ((index % 3) == 0 && outerVisible[index])
            primLine2(apexX, apexY, outerX[index], outerY[index], muiMuted);
    }
}

static void muiMissionLightingDrawPointCage(const ModernMissionAuthoredLight *light,
                                            color cageColor)
{
    enum { MUI_POINT_SEGMENTS = 16 };
    real32 radius;
    sdword axis;
    if (light == NULL || light->type != MODERN_MISSION_LIGHT_POINT) return;
    radius = light->radius;
    if (mrCamera != NULL && radius > mrCamera->distance * 0.55f)
        radius = mrCamera->distance * 0.55f;
    if (radius < 250.0f) radius = 250.0f;
    for (axis = 0; axis < 2; ++axis)
    {
        sdword previousX = 0, previousY = 0;
        bool32 previousVisible = FALSE;
        sdword index;
        for (index = 0; index <= MUI_POINT_SEGMENTS; ++index)
        {
            real32 angle = (real32)(index % MUI_POINT_SEGMENTS) *
                           (2.0f * MUI_PI / (real32)MUI_POINT_SEGMENTS);
            real32 point[3] = { light->position[0], light->position[1], light->position[2] };
            sdword x, y;
            bool32 visible;
            if (axis == 0)
            {
                point[0] += (real32)cos(angle) * radius;
                point[2] += (real32)sin(angle) * radius;
            }
            else
            {
                point[1] += (real32)cos(angle) * radius;
                point[2] += (real32)sin(angle) * radius;
            }
            visible = muiMissionLightingProjectPoint(point, &x, &y, NULL);
            if (visible && previousVisible)
                primLine2(previousX, previousY, x, y, cageColor);
            previousX = x;
            previousY = y;
            previousVisible = visible;
        }
    }
}

static void muiDrawLightingMarker(regionhandle region)
{
    ModernMissionLightingAuthoringState state;
    ModernMissionAuthoredLight *light = NULL;
    sdword x;
    sdword y;
    sdword r = uiScaleSize(11);
    bool32 clamped = FALSE;
    real32 effective[3];
    real32 maximum;
    color sourceColor;
    char label[96];
    sdword total;
    (void)region;
    if (!muiLighting.active ||
        !modernMissionBackdropLightingAuthoringGet(&state))
        return;

    if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
    {
        total = muiMissionLightSelectableCount(&state);
        if (total <= 0) return;
        if (muiLighting.selectedLight < 0) muiLighting.selectedLight = 0;
        if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;
        if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_AMBIENT)
        {
            snprintf(label, sizeof(label), "SKY AMBIENT / GLOBAL / %s",
                     state.ambientEnabled ? "ON" : "REMOVED");
            muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                    label, muiCompactFont);
            return;
        }
        if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY)
        {
            if (!state.sourceValid)
            {
                muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                        "PRIMARY KEY / REMOVED / RESET RESTORES", muiCompactFont);
                return;
            }
            if (!muiMissionLightingProject(state.direction, &x, &y, &clamped))
            {
                muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                        "PRIMARY KEY IS BEHIND CAMERA", muiCompactFont);
                return;
            }
            effective[0] = state.baseSourceColor[0] * state.sunIntensity * state.sourceColorScale[0];
            effective[1] = state.baseSourceColor[1] * state.sunIntensity * state.sourceColorScale[1];
            effective[2] = state.baseSourceColor[2] * state.sunIntensity * state.sourceColorScale[2];
        }
        else
        {
            light = muiMissionSelectedAuthoredLight(&state);
            if (light == NULL) return;
            if (light->type == MODERN_MISSION_LIGHT_AMBIENT)
            {
                snprintf(label, sizeof(label), "AMBIENT %02d / GLOBAL",
                         muiLighting.selectedLight - MUI_LIGHTING_SYSTEM_COUNT + 1);
                muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                        label, muiCompactFont);
                return;
            }
            if (!muiMissionLightingProjectPoint(light->position, &x, &y, &clamped))
            {
                muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                        "SELECTED LIGHT IS BEHIND CAMERA", muiCompactFont);
                return;
            }
            effective[0] = light->color[0] * light->intensity;
            effective[1] = light->color[1] * light->intensity;
            effective[2] = light->color[2] * light->intensity;
        }
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
    {
        if (!state.sourceValid) return;
        if (!muiMissionLightingProject(state.direction, &x, &y, &clamped))
        {
            muiText(uiScaleSize(18), uiScaleSize(18), muiSignal,
                    "KEY SOURCE IS BEHIND CAMERA", muiCompactFont);
            return;
        }
        effective[0] = state.baseSourceColor[0] * state.sunIntensity * state.sourceColorScale[0];
        effective[1] = state.baseSourceColor[1] * state.sunIntensity * state.sourceColorScale[1];
        effective[2] = state.baseSourceColor[2] * state.sunIntensity * state.sourceColorScale[2];
    }
    else
    {
        return;
    }

    maximum = max(effective[0], max(effective[1], effective[2]));
    if (maximum < 0.001f) maximum = 1.0f;
    sourceColor = colRGB(
        muiClamp((sdword)(effective[0] / maximum * 255.0f), 0, 255),
        muiClamp((sdword)(effective[1] / maximum * 255.0f), 0, 255),
        muiClamp((sdword)(effective[2] / maximum * 255.0f), 0, 255));
    if (!clamped && light != NULL)
    {
        if (light->type == MODERN_MISSION_LIGHT_SPOT)
            muiMissionLightingDrawSpotCone(light, sourceColor);
        else if (light->type == MODERN_MISSION_LIGHT_POINT)
            muiMissionLightingDrawPointCage(light, muiMuted);
    }
    primCircleSolid2(x, y, r, 24, colRGBA(0, 0, 0, 150));
    primCircleSolid2(x, y, uiScaleSize(5), 20, sourceColor);
    primLine2(x - r - uiScaleSize(7), y, x + r + uiScaleSize(7), y, muiSignal);
    primLine2(x, y - r - uiScaleSize(7), x, y + r + uiScaleSize(7), muiSignal);
    if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
    {
        if (muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY)
        {
            snprintf(label, sizeof(label), clamped ?
                     "PRIMARY KEY / YAW %+.0f / OFFSCREEN" :
                     "PRIMARY KEY / YAW %+.0f",
                     muiMissionLightYawDegrees(state.direction));
        }
        else if (light != NULL && light->type == MODERN_MISSION_LIGHT_SPOT)
        {
            snprintf(label, sizeof(label), clamped ?
                     "SPOT %02d / YAW %+.0f / OFFSCREEN" :
                     "SPOT %02d / YAW %+.0f",
                     muiLighting.selectedLight - MUI_LIGHTING_SYSTEM_COUNT + 1,
                     muiMissionLightYawDegrees(light->direction));
        }
        else
        {
            snprintf(label, sizeof(label), clamped ?
                     "POINT %02d / OMNI / OFFSCREEN" : "POINT %02d / OMNI",
                     muiLighting.selectedLight - MUI_LIGHTING_SYSTEM_COUNT + 1);
        }
    }
    else
    {
        snprintf(label, sizeof(label), clamped ? "KEY M%02d / OFFSCREEN" : "KEY M%02d",
                 state.mission);
    }
    muiText(x + uiScaleSize(16), y - uiScaleSize(8), muiPaper,
            label, muiCompactFont);
    if (!clamped && light != NULL && light->type == MODERN_MISSION_LIGHT_SPOT)
    {
        char directionText[96];
        snprintf(directionText, sizeof(directionText),
                 "DIR %+.2f  %+.2f  %+.2f / CONE %.0f+%.0f",
                 light->direction[0], light->direction[1], light->direction[2],
                 light->coneAngle, light->edgeAngle);
        muiText(x + uiScaleSize(16), y + uiScaleSize(8), muiMuted,
                directionText, muiTinyFont);
    }
}

static void muiDrawLightingRow(const rectangle *row, bool32 selected,
                               bool32 hovered, const char *label,
                               const char *description, const char *valueText)
{
    rectangle marker;
    rectangle labelRect = *row;
    rectangle descriptionRect = *row;
    rectangle valueRect;
    rectangle minus;
    rectangle plus;
    sdword controlWidth = uiScaleSize(168);
    if (selected || hovered) muiFill(row, muiSignalSoft);
    if (selected)
    {
        marker = *row;
        marker.x1 = marker.x0 + uiScaleSize(2);
        muiFill(&marker, muiSignal);
    }
    labelRect.x0 += uiScaleSize(12);
    labelRect.x1 -= controlWidth + uiScaleSize(8);
    labelRect.y1 = labelRect.y0 + uiScaleSize(23);
    descriptionRect.x0 = labelRect.x0;
    descriptionRect.x1 = labelRect.x1;
    descriptionRect.y0 = labelRect.y1 - uiScaleSize(2);
    descriptionRect.y1 -= uiScaleSize(3);
    muiTextLeftAdaptiveFull(&labelRect, selected ? muiPaper : muiMuted,
        label, muiBodyFont, muiCompactFont, muiTinyFont, 0);
    muiTextLeftAdaptiveFull(&descriptionRect, muiDisabled, description,
        muiCompactFont, muiTinyFont, muiTinyFont, 0);
    minus = muiRect(row->x1 - uiScaleSize(160), row->y0 + uiScaleSize(4),
                    row->x1 - uiScaleSize(126), row->y1 - uiScaleSize(4));
    valueRect = muiRect(row->x1 - uiScaleSize(120), row->y0 + uiScaleSize(4),
                        row->x1 - uiScaleSize(42), row->y1 - uiScaleSize(4));
    plus = muiRect(row->x1 - uiScaleSize(34), row->y0 + uiScaleSize(4),
                   row->x1 - uiScaleSize(2), row->y1 - uiScaleSize(4));
    muiTextCentered(&minus, muiMuted, "-", muiBodyFont);
    muiTextCenteredAdaptiveFull(&valueRect, muiPaper, valueText,
        muiBodyFont, muiCompactFont, muiTinyFont, 0);
    muiTextCentered(&plus, muiMuted, "+", muiBodyFont);
    primLine2(row->x0, row->y1, row->x1, row->y1, muiLineSoft);
}

static void muiDrawLightingEditor(regionhandle region)
{
    static const char *tuningLabels[MUI_LIGHTING_ROW_COUNT] = {
        "PRIMARY SUN / SHADOW", "SKY AMBIENT / COLOR WASH",
        "ENVIRONMENT REFLECTIONS", "MISSION-AUTHORED LIGHTS",
        "FX / EMISSIVE LIGHTS", "SURFACE REFLECTIVITY",
        "SURFACE ROUGHNESS", "GENERATED NORMALS", "PATH-LIGHT EXPOSURE",
        "COLOR BANDING FILTER"
    };
    static const char *tuningDescriptions[MUI_LIGHTING_ROW_COUNT] = {
        "Global gain after the mission-local key authoring values",
        "Mission ambient fill; hue may be overridden on the KEY page",
        "Sky sampling used for reflections and the small neutral path floor",
        "HSF plus live point/spot/ambient lights authored per mission",
        "Weapons, engines, beams, explosions, glow maps and nav lights",
        "Stable primary-surface highlights from generated detail",
        "Lower values sharpen reflections; higher values spread them",
        "LIF-derived panel and paint relief strength",
        "Final path-light transfer; classic color remains untouched",
        "Stable shadow-weighted output dithering before the 8-bit presenter"
    };
    static const char *shadowLabels[MUI_LIGHTING_SHADOW_ROW_COUNT] = {
        "SHADOW STRENGTH", "SUN ANGULAR RADIUS", "SUN SHADOW SAMPLES",
        "LOCAL SHADOW SAMPLES", "LOCAL LIGHT RADIUS", "RECEIVER BIAS",
        "NORMAL BIAS", "CONTACT SHADOW STRENGTH", "CONTACT DISTANCE",
        "MAX SHADOW DISTANCE"
    };
    static const char *shadowDescriptions[MUI_LIGHTING_SHADOW_ROW_COUNT] = {
        "Global opacity of ray-traced visibility shadows",
        "Physical sun-disc softness; larger values broaden distant penumbrae",
        "Directional-light visibility rays per shaded hit",
        "Point/spot/FX visibility rays per shaded hit",
        "Angular area-light spread for local point/spot/FX sources",
        "Ray start offset along the outgoing shadow direction; controls acne",
        "Surface-normal offset before shadow rays; controls self-intersection",
        "Near-field hard contact reinforcement under soft area shadows",
        "World-space range over which contact reinforcement remains active",
        "Maximum directional/local shadow-ray reach in world units"
    };
    static const char *missionLabels[MUI_LIGHTING_KEY_ROW_COUNT] = {
        "SUN AZIMUTH", "SUN ELEVATION", "SUN INTENSITY",
        "SUN RED", "SUN GREEN", "SUN BLUE", "AMBIENT STRENGTH",
        "AMBIENT COLOR MODE", "AMBIENT RED", "AMBIENT GREEN", "AMBIENT BLUE"
    };
    static const char *missionDescriptions[MUI_LIGHTING_KEY_ROW_COUNT] = {
        "Rotate the mission key around world Y; 0 degrees points along -Z",
        "Raise/lower the mission key above or below the mission plane",
        "Per-mission multiplier on detected HDR source energy",
        "Red multiplier relative to the detected HDR source color",
        "Green multiplier relative to the detected HDR source color",
        "Blue multiplier relative to the detected HDR source color",
        "Per-mission strength of the global HDR-derived ambient fill",
        "AUTO follows the HDR sky mean; MANUAL uses the RGB tint below",
        "Manual ambient hue red; changing this automatically enables MANUAL",
        "Manual ambient hue green; changing this automatically enables MANUAL",
        "Manual ambient hue blue; changing this automatically enables MANUAL"
    };
    static const char *lightLabels[MUI_LIGHTING_LIGHT_ROW_COUNT] = {
        "SELECT LIGHT", "LIGHT TYPE", "ENABLED", "INTENSITY",
        "RED", "GREEN", "BLUE", "RANGE / RADIUS", "SPOT CONE",
        "SPOT EDGE", "Y ROTATION / YAW", "POSITION X", "POSITION Y", "POSITION Z"
    };
    static const char *lightDescriptions[MUI_LIGHTING_LIGHT_ROW_COUNT] = {
        "Cycle primary key, sky ambient, imported HSF lights and added lights",
        "System key is Directional; editable local lights are Point, Spot or Ambient",
        "Temporarily enable/disable this light without deleting it",
        "Per-light energy; also follows the global Mission-Authored slider",
        "Linear red contribution", "Linear green contribution",
        "Linear blue contribution", "Point/Spot falloff distance in world units",
        "Spot inner half-angle in degrees; A aims the selected spot at the cursor",
        "Spot feather angle outside the inner cone",
        "Rotate a Spot around world Y without changing its vertical pitch",
        "Fine-tune placed Point/Spot world position",
        "Fine-tune placed Point/Spot world position",
        "Fine-tune placed Point/Spot world position"
    };
    rectangle panel = muiLightingPanelRect();
    rectangle rows[MUI_LIGHTING_MAX_ROW_COUNT];
    rectangle missionTab;
    rectangle lightsTab;
    rectangle tuningTab;
    rectangle shadowTab;
    rectangle footer[4];
    rectangle marker;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    sdword rowCount = muiLightingRowCountForPage(muiLighting.page);
    sdword index;
    char valueText[48];
    char subtitle[192];
    ModernMissionLightingAuthoringState missionState;
    bool32 missionAvailable = modernMissionBackdropLightingAuthoringGet(&missionState);
    (void)region;
    muiEnsureFonts();
    muiLightingLayout(rowCount, rows, &missionTab, &lightsTab, &tuningTab, &shadowTab, footer);
    muiFill(&panel, colRGBA(7, 14, 19, 246));
    primRectOutline2(&panel, 1, muiLine);
    marker = panel;
    marker.x1 = marker.x0 + uiScaleSize(4);
    muiFill(&marker, muiSignal);
    muiText(panel.x0 + uiScaleSize(18), panel.y0 + uiScaleSize(16),
            muiSignal, "LIVE MISSION LIGHTING / SHIFT+F12", muiBodyFont);
    muiText(panel.x0 + uiScaleSize(18), panel.y0 + uiScaleSize(42),
            muiPaper, "RTX LIGHTING AUTHORING", muiHeadingFont);
    if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
    {
        if (missionAvailable)
            snprintf(subtitle, sizeof(subtitle),
                     "MISSION %02d / P place key / S save+export / L reload / I solo / E export visuals",
                     missionState.mission);
        else
            snprintf(subtitle, sizeof(subtitle),
                     "Mission sky not ready / P place / S save+export / L reload / I solo / E export visuals");
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
    {
        const sdword total = missionAvailable ?
            muiMissionLightSelectableCount(&missionState) : 0;
        snprintf(subtitle, sizeof(subtitle),
                 "CUSTOM LIGHTS %d TOTAL / %d LOCAL / N add / DEL remove / P place / A aim / S save+export",
                 total, missionAvailable ? missionState.authoredLightCount : 0);
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_TUNING)
    {
        snprintf(subtitle, sizeof(subtitle),
                 "GLOBAL TUNING / E exports graphics + shader settings / TAB cycles pages");
    }
    else
    {
        snprintf(subtitle, sizeof(subtitle),
                 "AAA RAY-TRACED SHADOWS / LIVE GLOBAL QUALITY + BIAS + CONTACT TUNING / E export");
    }
    muiText(panel.x0 + uiScaleSize(18), panel.y0 + uiScaleSize(69),
            muiMuted, subtitle, muiCompactFont);
    muiDrawButtonRect(&missionTab, "MISSION KEY",
                      muiLighting.page == MUI_LIGHTING_PAGE_MISSION,
                      muiPointIn(&missionTab, mouseX, mouseY));
    muiDrawButtonRect(&lightsTab, "CUSTOM LIGHTS",
                      muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS,
                      muiPointIn(&lightsTab, mouseX, mouseY));
    muiDrawButtonRect(&tuningTab, "GLOBAL TUNING",
                      muiLighting.page == MUI_LIGHTING_PAGE_TUNING,
                      muiPointIn(&tuningTab, mouseX, mouseY));
    muiDrawButtonRect(&shadowTab, "SHADOWS",
                      muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS,
                      muiPointIn(&shadowTab, mouseX, mouseY));
    muiText(panel.x0 + uiScaleSize(18), panel.y0 + uiScaleSize(132),
            muiLighting.status[0] ? muiSignal : muiDisabled,
            muiLighting.status[0] ? muiLighting.status :
            "Live edit: S saves+exports mission lighting; E exports visual/shader settings",
            muiTinyFont);

    if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
    {
        real32 azimuth = 0.0f;
        real32 elevation = 0.0f;
        if (missionAvailable)
            muiMissionLightingAngles(missionState.direction, &azimuth, &elevation);
        for (index = 0; index < rowCount; ++index)
        {
            if (!missionAvailable)
            {
                snprintf(valueText, sizeof(valueText), "WAIT");
            }
            else if (index == 0)
                snprintf(valueText, sizeof(valueText), "%+.0f DEG", azimuth);
            else if (index == 1)
                snprintf(valueText, sizeof(valueText), "%+.0f DEG", elevation);
            else if (index == 2)
            {
                if (missionState.sourceValid)
                    snprintf(valueText, sizeof(valueText), "%.0f%%", missionState.sunIntensity * 100.0f);
                else
                    snprintf(valueText, sizeof(valueText), "REMOVED");
            }
            else if (index >= 3 && index <= 5)
                snprintf(valueText, sizeof(valueText), "%.0f%%",
                         missionState.sourceColorScale[index - 3] * 100.0f);
            else if (index == 6)
            {
                if (missionState.ambientEnabled)
                    snprintf(valueText, sizeof(valueText), "%.0f%%", missionState.ambientScale * 100.0f);
                else
                    snprintf(valueText, sizeof(valueText), "REMOVED");
            }
            else if (index == 7)
            {
                if (!missionState.ambientEnabled)
                    snprintf(valueText, sizeof(valueText), "REMOVED");
                else
                    snprintf(valueText, sizeof(valueText), "%s",
                             missionState.ambientColorOverride ? "MANUAL" : "AUTO");
            }
            else if (missionState.ambientColorOverride)
                snprintf(valueText, sizeof(valueText), "%.0f%%",
                         missionState.ambientColor[index - 8] * 100.0f);
            else
                snprintf(valueText, sizeof(valueText), "AUTO %.0f%%",
                         missionState.ambientColor[index - 8] * 100.0f);
            muiDrawLightingRow(&rows[index], index == muiLighting.selectedRow,
                muiPointIn(&rows[index], mouseX, mouseY), missionLabels[index],
                missionDescriptions[index], valueText);
        }
        muiDrawButtonRect(&footer[0], "RESET MISSION", FALSE,
                          muiPointIn(&footer[0], mouseX, mouseY));
        muiDrawButtonRect(&footer[1], "SAVE + EXPORT", TRUE,
                          muiPointIn(&footer[1], mouseX, mouseY));
        muiDrawButtonRect(&footer[2], muiLighting.soloKey ? "UNSOLO KEY" : "SOLO KEY",
                          muiLighting.soloKey, muiPointIn(&footer[2], mouseX, mouseY));
        muiDrawButtonRect(&footer[3], "DONE", FALSE,
                          muiPointIn(&footer[3], mouseX, mouseY));
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
    {
        ModernMissionAuthoredLight *light = NULL;
        sdword total = missionAvailable ? muiMissionLightSelectableCount(&missionState) : 0;
        bool32 systemKey = FALSE;
        bool32 systemAmbient = FALSE;
        if (missionAvailable && total > 0)
        {
            if (muiLighting.selectedLight < 0) muiLighting.selectedLight = 0;
            if (muiLighting.selectedLight >= total) muiLighting.selectedLight = total - 1;
            systemKey = muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_KEY;
            systemAmbient = muiLighting.selectedLight == MUI_LIGHTING_SYSTEM_AMBIENT;
            if (!systemKey && !systemAmbient)
                light = muiMissionSelectedAuthoredLight(&missionState);
        }
        for (index = 0; index < rowCount; ++index)
        {
            if (!missionAvailable)
                snprintf(valueText, sizeof(valueText), "WAIT");
            else if (index == 0)
            {
                if (systemKey) snprintf(valueText, sizeof(valueText), "KEY / %d", total);
                else if (systemAmbient) snprintf(valueText, sizeof(valueText), "AMBIENT / %d", total);
                else if (light != NULL) snprintf(valueText, sizeof(valueText), "%d / %d",
                    muiLighting.selectedLight + 1, total);
                else snprintf(valueText, sizeof(valueText), "0 / %d", total);
            }
            else if (systemKey)
            {
                if (index == 1) snprintf(valueText, sizeof(valueText), "DIRECTIONAL");
                else if (index == 2) snprintf(valueText, sizeof(valueText), "%s", missionState.sourceValid ? "ON" : "REMOVED");
                else if (index == 3) snprintf(valueText, sizeof(valueText), "%.2fx", missionState.sunIntensity);
                else if (index >= 4 && index <= 6) snprintf(valueText, sizeof(valueText), "%.0f%%", missionState.sourceColorScale[index - 4] * 100.0f);
                else if (index == 7) snprintf(valueText, sizeof(valueText), "INFINITE");
                else if (index == 10) snprintf(valueText, sizeof(valueText), "%+.0f DEG", muiMissionLightYawDegrees(missionState.direction));
                else snprintf(valueText, sizeof(valueText), "N/A");
            }
            else if (systemAmbient)
            {
                if (index == 1) snprintf(valueText, sizeof(valueText), "AMBIENT");
                else if (index == 2) snprintf(valueText, sizeof(valueText), "%s", missionState.ambientEnabled ? "ON" : "REMOVED");
                else if (index == 3) snprintf(valueText, sizeof(valueText), "%.2fx", missionState.ambientScale);
                else if (index >= 4 && index <= 6)
                {
                    if (missionState.ambientColorOverride)
                        snprintf(valueText, sizeof(valueText), "%.0f%%", missionState.ambientColor[index - 4] * 100.0f);
                    else
                        snprintf(valueText, sizeof(valueText), "AUTO %.0f%%", missionState.ambientColor[index - 4] * 100.0f);
                }
                else if (index == 7) snprintf(valueText, sizeof(valueText), "GLOBAL");
                else snprintf(valueText, sizeof(valueText), "N/A");
            }
            else if (light == NULL)
                snprintf(valueText, sizeof(valueText), "ADD");
            else if (index == 1)
                snprintf(valueText, sizeof(valueText), "%s",
                    light->type == MODERN_MISSION_LIGHT_SPOT ? "SPOT" :
                    (light->type == MODERN_MISSION_LIGHT_AMBIENT ? "AMBIENT" : "POINT"));
            else if (index == 2)
                snprintf(valueText, sizeof(valueText), "%s", light->enabled ? "ON" : "OFF");
            else if (index == 3)
                snprintf(valueText, sizeof(valueText), "%.2fx", light->intensity);
            else if (index >= 4 && index <= 6)
                snprintf(valueText, sizeof(valueText), "%.0f%%", light->color[index - 4] * 100.0f);
            else if (index == 7)
            {
                if (light->type == MODERN_MISSION_LIGHT_AMBIENT) snprintf(valueText, sizeof(valueText), "GLOBAL");
                else snprintf(valueText, sizeof(valueText), "%.0f", light->radius);
            }
            else if (index == 8)
            {
                if (light->type == MODERN_MISSION_LIGHT_SPOT) snprintf(valueText, sizeof(valueText), "%.0f DEG", light->coneAngle);
                else snprintf(valueText, sizeof(valueText), "N/A");
            }
            else if (index == 9)
            {
                if (light->type == MODERN_MISSION_LIGHT_SPOT) snprintf(valueText, sizeof(valueText), "%.0f DEG", light->edgeAngle);
                else snprintf(valueText, sizeof(valueText), "N/A");
            }
            else if (index == 10)
            {
                if (light->type == MODERN_MISSION_LIGHT_SPOT) snprintf(valueText, sizeof(valueText), "%+.0f DEG", muiMissionLightYawDegrees(light->direction));
                else if (light->type == MODERN_MISSION_LIGHT_POINT) snprintf(valueText, sizeof(valueText), "OMNI");
                else snprintf(valueText, sizeof(valueText), "GLOBAL");
            }
            else
            {
                if (light->type == MODERN_MISSION_LIGHT_AMBIENT) snprintf(valueText, sizeof(valueText), "GLOBAL");
                else snprintf(valueText, sizeof(valueText), "%.0f", light->position[index - 11]);
            }
            muiDrawLightingRow(&rows[index], index == muiLighting.selectedRow,
                muiPointIn(&rows[index], mouseX, mouseY), lightLabels[index],
                lightDescriptions[index], valueText);
        }
        muiDrawButtonRect(&footer[0], "ADD LIGHT", TRUE,
                          muiPointIn(&footer[0], mouseX, mouseY));
        muiDrawButtonRect(&footer[1], "DELETE", FALSE,
                          muiPointIn(&footer[1], mouseX, mouseY));
        muiDrawButtonRect(&footer[2], "SAVE + EXPORT", TRUE,
                          muiPointIn(&footer[2], mouseX, mouseY));
        muiDrawButtonRect(&footer[3], "DONE", FALSE,
                          muiPointIn(&footer[3], mouseX, mouseY));
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_TUNING)
    {
        for (index = 0; index < rowCount; ++index)
        {
            sdword low, high, step;
            sdword *value = muiLightingValue(index, &low, &high, &step);
            snprintf(valueText, sizeof(valueText), "%d%%", *value);
            muiDrawLightingRow(&rows[index], index == muiLighting.selectedRow,
                muiPointIn(&rows[index], mouseX, mouseY), tuningLabels[index],
                tuningDescriptions[index], valueText);
        }
        muiDrawButtonRect(&footer[0], "RESET TUNING", FALSE,
                          muiPointIn(&footer[0], mouseX, mouseY));
        muiDrawButtonRect(&footer[1], "EXPORT VISUALS", TRUE,
                          muiPointIn(&footer[1], mouseX, mouseY));
        muiDrawButtonRect(&footer[2], "SHADOWS PAGE", TRUE,
                          muiPointIn(&footer[2], mouseX, mouseY));
        muiDrawButtonRect(&footer[3], "DONE", FALSE,
                          muiPointIn(&footer[3], mouseX, mouseY));
    }
    else
    {
        for (index = 0; index < rowCount; ++index)
        {
            sdword low, high, step;
            sdword *value = muiShadowValue(index, &low, &high, &step);
            if (index == 0 || index == 7)
                snprintf(valueText, sizeof(valueText), "%d%%", *value);
            else if (index == 1 || index == 4)
                snprintf(valueText, sizeof(valueText), "%.2f DEG", (float)*value / 100.0f);
            else if (index == 2 || index == 3)
                snprintf(valueText, sizeof(valueText), "%d RAYS", *value);
            else if (index == 5 || index == 6)
                snprintf(valueText, sizeof(valueText), "%.2f", (float)*value / 100.0f);
            else
                snprintf(valueText, sizeof(valueText), "%d", *value);
            muiDrawLightingRow(&rows[index], index == muiLighting.selectedRow,
                muiPointIn(&rows[index], mouseX, mouseY), shadowLabels[index],
                shadowDescriptions[index], valueText);
        }
        muiDrawButtonRect(&footer[0], "RESET SHADOWS", FALSE,
                          muiPointIn(&footer[0], mouseX, mouseY));
        muiDrawButtonRect(&footer[1], "EXPORT VISUALS", TRUE,
                          muiPointIn(&footer[1], mouseX, mouseY));
        muiDrawButtonRect(&footer[2], "GLOBAL TUNING", TRUE,
                          muiPointIn(&footer[2], mouseX, mouseY));
        muiDrawButtonRect(&footer[3], "DONE", FALSE,
                          muiPointIn(&footer[3], mouseX, mouseY));
    }
}
static void muiLightingHandleFooter(sdword index)
{
    if (index == 3)
    {
        modernUICloseLightingEditor();
        return;
    }
    if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
    {
        if (index == 0)
        {
            modernMissionBackdropLightingAuthoringReset();
            modernMissionBackdropLightingAuthoringAdoptMapLights();
            muiLighting.selectedLight = 0;
            muiLightingSetStatus("RESET TO HDR SKY + RETAIL MAP LIGHTS REIMPORTED / UNSAVED");
        }
        else if (index == 1)
        {
            muiSaveAndExportMissionLighting();
        }
        else if (index == 2)
        {
            muiLightingSoloKeySet(!muiLighting.soloKey);
        }
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
    {
        if (index == 0)
        {
            muiMissionLightAdd();
        }
        else if (index == 1)
        {
            muiMissionLightDelete();
        }
        else if (index == 2)
        {
            muiSaveAndExportMissionLighting();
        }
    }
    else if (muiLighting.page == MUI_LIGHTING_PAGE_TUNING)
    {
        if (index == 0)
        {
            muiLightingReset();
            muiLightingSetStatus("GLOBAL LIGHTING TUNING RESET");
        }
        else if (index == 1)
        {
            opOptionsSaveSettings();
            if (muiExportVisualSettings())
                muiLightingSetStatus("GLOBAL VISUAL + SHADER SETTINGS EXPORTED TO GAME/RTXExports/Global");
            else
                muiLightingSetStatus("VISUAL EXPORT FAILED - CHECK LOG");
        }
        else if (index == 2)
        {
            muiLightingSetPage(MUI_LIGHTING_PAGE_SHADOWS);
        }
    }
    else
    {
        if (index == 0)
        {
            muiShadowReset();
            muiLightingSetStatus("GLOBAL RAY-TRACED SHADOW SETTINGS RESET");
        }
        else if (index == 1)
        {
            opOptionsSaveSettings();
            if (muiExportVisualSettings())
                muiLightingSetStatus("SHADOW + VISUAL SETTINGS EXPORTED TO GAME/RTXExports/Global");
            else
                muiLightingSetStatus("VISUAL EXPORT FAILED - CHECK LOG");
        }
        else if (index == 2)
        {
            muiLightingSetPage(MUI_LIGHTING_PAGE_TUNING);
        }
    }
}

static udword muiLightingProcess(regionhandle region, smemsize ID,
                                 udword event, udword data)
{
    rectangle rows[MUI_LIGHTING_MAX_ROW_COUNT];
    rectangle missionTab;
    rectangle lightsTab;
    rectangle tuningTab;
    rectangle shadowTab;
    rectangle footer[4];
    rectangle minus;
    rectangle plus;
    sdword rowCount = muiLightingRowCountForPage(muiLighting.page);
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)data;
    if (event == RPE_KeyDown)
    {
        if (ID == ESCKEY ||
            (ID == F12KEY &&
             (keyIsHit(LSHIFTKEY) || keyIsHit(RSHIFTKEY)) &&
             !(keyIsHit(LCONTROLKEY) || keyIsHit(RCONTROLKEY))))
        {
            modernUICloseLightingEditor();
            return RPR_Redraw;
        }
        if (ID == TABKEY)
        {
            muiLightingSetPage((muiLighting.page + 1) % 4);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (ID == ARRUP || ID == ARRDOWN)
        {
            rowCount = muiLightingRowCountForPage(muiLighting.page);
            muiLighting.selectedRow = (muiLighting.selectedRow +
                (ID == ARRUP ? rowCount - 1 : 1)) % rowCount;
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (ID == ARRLEFT || ID == ARRRIGHT)
        {
            const sdword adjust = ID == ARRLEFT ? -1 : 1;
            if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
                muiMissionLightingAdjust(muiLighting.selectedRow, adjust);
            else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
                muiMissionLightAdjust(muiLighting.selectedRow, adjust);
            else if (muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS)
                muiShadowAdjust(muiLighting.selectedRow, adjust);
            else
                muiLightingAdjust(muiLighting.selectedRow, adjust);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (ID == RKEY)
        {
            if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
            {
                modernMissionBackdropLightingAuthoringReset();
                modernMissionBackdropLightingAuthoringAdoptMapLights();
                muiLighting.selectedLight = 0;
                muiLightingSetStatus("RESET TO HDR SKY + RETAIL MAP LIGHTS REIMPORTED / UNSAVED");
            }
            else if (muiLighting.page == MUI_LIGHTING_PAGE_TUNING)
            {
                muiLightingReset();
                muiLightingSetStatus("GLOBAL LIGHTING TUNING RESET");
            }
            else if (muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS)
            {
                muiShadowReset();
                muiLightingSetStatus("GLOBAL RAY-TRACED SHADOW SETTINGS RESET");
            }
            else
            {
                muiLightingSetStatus("LIGHTS PAGE: use ADD / DELETE, or edit the selected light live");
            }
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (ID == PKEY)
        {
            if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
                muiMissionLightingPlaceKeyAtCursor();
            else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
                muiMissionLightPlaceAtCursor();
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS && ID == AKEY)
        {
            muiMissionLightAimAtCursor();
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS && ID == NKEY)
        {
            muiMissionLightAdd();
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS && ID == DELETEKEY)
        {
            muiMissionLightDelete();
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if ((muiLighting.page == MUI_LIGHTING_PAGE_MISSION ||
             muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS) && ID == SKEY)
        {
            muiSaveAndExportMissionLighting();
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (ID == EKEY)
        {
            opOptionsSaveSettings();
            if (muiExportVisualSettings())
                muiLightingSetStatus("GLOBAL VISUAL + SHADER SETTINGS EXPORTED TO GAME/RTXExports/Global");
            else
                muiLightingSetStatus("VISUAL EXPORT FAILED - CHECK LOG");
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if ((muiLighting.page == MUI_LIGHTING_PAGE_MISSION ||
             muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS) && ID == LKEY)
        {
            ModernMissionLightingAuthoringState state;
            if (modernMissionBackdropLightingAuthoringReload())
            {
                modernMissionBackdropLightingAuthoringAdoptMapLights();
                muiLightingSetStatus("SAVED MISSION LIGHTING RELOADED");
            }
            else
            {
                modernMissionBackdropLightingAuthoringAdoptMapLights();
                muiLightingSetStatus("NO SAVED OVERRIDE - HDR + RETAIL MAP LIGHTS ACTIVE");
            }
            if (modernMissionBackdropLightingAuthoringGet(&state))
            {
                sdword total = muiMissionLightSelectableCount(&state);
                if (muiLighting.selectedLight >= total)
                    muiLighting.selectedLight = total > 0 ? total - 1 : 0;
            }
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION && ID == IKEY)
        {
            muiLightingSoloKeySet(!muiLighting.soloKey);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        return 0;
    }
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft && event != RPE_WheelUp &&
        event != RPE_WheelDown)
        return 0;

    rowCount = muiLightingRowCountForPage(muiLighting.page);
    muiLightingLayout(rowCount, rows, &missionTab, &lightsTab, &tuningTab, &shadowTab, footer);
    if (event == RPE_ReleaseLeft && muiPointIn(&missionTab, mouseX, mouseY))
    {
        muiLightingSetPage(MUI_LIGHTING_PAGE_MISSION);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft && muiPointIn(&lightsTab, mouseX, mouseY))
    {
        muiLightingSetPage(MUI_LIGHTING_PAGE_LIGHTS);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft && muiPointIn(&tuningTab, mouseX, mouseY))
    {
        muiLightingSetPage(MUI_LIGHTING_PAGE_TUNING);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft && muiPointIn(&shadowTab, mouseX, mouseY))
    {
        muiLightingSetPage(MUI_LIGHTING_PAGE_SHADOWS);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft)
    {
        for (index = 0; index < 4; ++index)
        {
            if (muiPointIn(&footer[index], mouseX, mouseY))
            {
                muiLightingHandleFooter(index);
                if (muiLighting.active && muiLighting.region != NULL)
                    regRecursiveSetDirty(muiLighting.region);
                return RPR_Redraw;
            }
        }
    }
    for (index = 0; index < rowCount; ++index)
    {
        if (!muiPointIn(&rows[index], mouseX, mouseY)) continue;
        muiLighting.selectedRow = index;
        if (event == RPE_WheelUp || event == RPE_WheelDown)
        {
            const sdword adjust = event == RPE_WheelUp ? 1 : -1;
            if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
                muiMissionLightingAdjust(index, adjust);
            else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
                muiMissionLightAdjust(index, adjust);
            else if (muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS)
                muiShadowAdjust(index, adjust);
            else
                muiLightingAdjust(index, adjust);
        }
        else
        {
            minus = muiRect(rows[index].x1 - uiScaleSize(160),
                            rows[index].y0 + uiScaleSize(4),
                            rows[index].x1 - uiScaleSize(126),
                            rows[index].y1 - uiScaleSize(4));
            plus = muiRect(rows[index].x1 - uiScaleSize(34),
                           rows[index].y0 + uiScaleSize(4),
                           rows[index].x1 - uiScaleSize(2),
                           rows[index].y1 - uiScaleSize(4));
            if (muiPointIn(&minus, mouseX, mouseY))
            {
                if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
                    muiMissionLightingAdjust(index, -1);
                else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
                    muiMissionLightAdjust(index, -1);
                else if (muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS)
                    muiShadowAdjust(index, -1);
                else
                    muiLightingAdjust(index, -1);
            }
            else if (muiPointIn(&plus, mouseX, mouseY))
            {
                if (muiLighting.page == MUI_LIGHTING_PAGE_MISSION)
                    muiMissionLightingAdjust(index, 1);
                else if (muiLighting.page == MUI_LIGHTING_PAGE_LIGHTS)
                    muiMissionLightAdjust(index, 1);
                else if (muiLighting.page == MUI_LIGHTING_PAGE_SHADOWS)
                    muiShadowAdjust(index, 1);
                else
                    muiLightingAdjust(index, 1);
            }
        }
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    return 0;
}

bool32 modernUILightingEditorActive(void)
{
    return muiLighting.active;
}

void modernUICloseLightingEditor(void)
{
    regionhandle region = muiLighting.region;
    regionhandle markerRegion = muiLighting.markerRegion;
    if (!muiLighting.active && region == NULL && markerRegion == NULL) return;
    if (muiLighting.soloKey)
        muiLightingSoloKeySet(FALSE);
    muiLighting.active = FALSE;
    muiLighting.region = NULL;
    muiLighting.markerRegion = NULL;
    opOptionsSaveSettings();
    if (region != NULL) regRegionDelete(region);
    if (markerRegion != NULL) regRegionDelete(markerRegion);
    fprintf(stderr, "[ModernUI] Lighting authoring editor closed; global settings saved.\n");
}

void modernUIToggleLightingEditor(void)
{
    rectangle panel;
    ModernMissionLightingAuthoringState state;
    if (muiLighting.active)
    {
        modernUICloseLightingEditor();
        return;
    }
    if (ghMainRegion == NULL || !gameIsRunning) return;
    modernUICloseShipDossier();
    mdCloseAllManagers();
    muiEnsureFonts();
    panel = muiLightingPanelRect();
    muiLighting.active = TRUE;
    muiLighting.selectedRow = 0;
    muiLighting.selectedLight = 0;
    muiLighting.page = MUI_LIGHTING_PAGE_MISSION;
    muiLighting.soloKey = FALSE;
    muiLightingSetStatus("");
    {
        ModernMissionLightingAuthoringState beforeState;
        sdword beforeCount = -1;
        if (modernMissionBackdropLightingAuthoringGet(&beforeState))
            beforeCount = beforeState.authoredLightCount;
        if (!modernMissionBackdropLightingAuthoringAdoptMapLights())
            muiLightingSetStatus("WARNING: COULD NOT IMPORT ALL RETAIL MAP LIGHTS");
        else if (beforeCount >= 0 &&
                 modernMissionBackdropLightingAuthoringGet(&state) &&
                 state.authoredLightCount > beforeCount)
        {
            char message[128];
            snprintf(message, sizeof(message),
                     "IMPORTED %d RETAIL HSF LOCAL LIGHT(S) INTO CUSTOM LIGHTS / UNSAVED",
                     state.authoredLightCount - beforeCount);
            muiLightingSetStatus(message);
        }
    }
    muiLighting.markerRegion = regChildAlloc(
        ghMainRegion, (smemsize)&muiLighting, 0, 0,
        MAIN_WindowWidth, MAIN_WindowHeight, 0, RPE_DrawEveryFrame);
    regDrawFunctionSet(muiLighting.markerRegion, muiDrawLightingMarker);
    muiLighting.region = regChildAlloc(
        ghMainRegion, (smemsize)&muiLighting, panel.x0, panel.y0,
        panel.x1 - panel.x0, panel.y1 - panel.y0, 0,
        RPE_PressLeft | RPE_ReleaseLeft | RPE_WheelUp |
        RPE_WheelDown | RPE_DrawEveryFrame);
    regDrawFunctionSet(muiLighting.region, muiDrawLightingEditor);
    regFunctionSet(muiLighting.region, muiLightingProcess);
    regKeyChildAlloc(muiLighting.region, ESCKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, ESCKEY);
    regKeyChildAlloc(muiLighting.region, F12KEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, F12KEY);
    regKeyChildAlloc(muiLighting.region, TABKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, TABKEY);
    regKeyChildAlloc(muiLighting.region, ARRUP, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, ARRUP);
    regKeyChildAlloc(muiLighting.region, ARRDOWN, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, ARRDOWN);
    regKeyChildAlloc(muiLighting.region, ARRLEFT, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, ARRLEFT);
    regKeyChildAlloc(muiLighting.region, ARRRIGHT, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, ARRRIGHT);
    regKeyChildAlloc(muiLighting.region, RKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, RKEY);
    regKeyChildAlloc(muiLighting.region, PKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, PKEY);
    regKeyChildAlloc(muiLighting.region, SKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, SKEY);
    regKeyChildAlloc(muiLighting.region, EKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, EKEY);
    regKeyChildAlloc(muiLighting.region, LKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, LKEY);
    regKeyChildAlloc(muiLighting.region, IKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, IKEY);
    regKeyChildAlloc(muiLighting.region, AKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, AKEY);
    regKeyChildAlloc(muiLighting.region, NKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, NKEY);
    regKeyChildAlloc(muiLighting.region, DELETEKEY, RPE_KeyDown,
                     (regionfunction)muiLightingProcess, 1, DELETEKEY);
    regSiblingMoveToFront(muiLighting.markerRegion);
    regSiblingMoveToFront(muiLighting.region);
    regRecursiveSetDirty(muiLighting.markerRegion);
    regRecursiveSetDirty(muiLighting.region);
    if (modernMissionBackdropLightingAuthoringGet(&state))
    {
        fprintf(stderr,
                "[ModernUI] RTX lighting authoring opened for mission %02d (Shift+F12). Custom Lights includes system + adopted HSF lights. N add, P place, A aim, Delete remove, S save+export mission, E export visuals.\n",
                state.mission);
    }
    else
    {
        fprintf(stderr,
                "[ModernUI] RTX lighting authoring opened; waiting for mission HDR sky.\n");
    }
}

void modernUIPrepareScreen(fescreen *screen)
{
    ModernUIScreenLayout layout;
    ModernUIScreenKind kind;
    featom *base;
    real32 scale;
    real32 scaleX;
    real32 scaleY;
    real32 offsetX;
    real32 offsetY;
    sdword loadedX0;
    sdword loadedY0;
    sdword loadedX1;
    sdword loadedY1;
    sdword index;
    sdword buttonIndex;
    bool32 buildManager;
    if (screen == NULL || screen->nAtoms == 0 || modernUIOwnsScreen(screen->name))
        return;
    kind = muiScreenKind(screen);
    buildManager = muiNameIs(screen->name, "Construction_manager");

    /* ManagerDock has already laid out the docked Build Manager before
       feScreenStart() reaches this function.  Do not run the generic modern
       manager reflow over that geometry: it imposes a 450-pixel minimum
       width and reconstructs the four build actions as one horizontal row,
       which overwrites the compact factory-aware layout in ManagerDock.
       Research and Launch still use the generic modern manager reflow. */
    if (buildManager && mdManagerDockEnabled())
    {
        return;
    }

    if (kind == MUI_SCREEN_GAMEPLAY && !muiManagerScreen(screen->name)) return;
    layout = muiScreenLayout(kind);
    base = &screen->atoms[0];
    if (base->loadedWidth <= 0 || base->loadedHeight <= 0) return;

    /* Old FEM bases frequently describe only a 640x480 root while their
       multiplayer children extend far beyond it.  Scale the complete atom
       envelope so no lobby, player list or option control is clipped. */
    if (muiManagerScreen(screen->name))
    {
        /* The old root is a 640x480 canvas, not useful content.  Including
           it in the bounds was creating the huge blank half-window seen in
           the modern managers.  Bound managers by their actual child
           controls instead. */
        loadedX0 = SDWORD_Max;
        loadedY0 = SDWORD_Max;
        loadedX1 = SDWORD_Min;
        loadedY1 = SDWORD_Min;
    }
    else
    {
        loadedX0 = base->loadedX;
        loadedY0 = base->loadedY;
        loadedX1 = base->loadedX + base->loadedWidth;
        loadedY1 = base->loadedY + base->loadedHeight;
    }
    for (index = 1; index < screen->nAtoms; ++index)
    {
        featom *atom = &screen->atoms[index];
        if (atom->loadedWidth <= 0 || atom->loadedHeight <= 0) continue;
        if (muiManagerScreen(screen->name) &&
            (atom->type == FA_DecorativeRegion ||
             atom->type == FA_OpaqueDecorativeRegion))
            continue;
        if (buildManager &&
            (muiBuildActionAtom(atom) || muiBuildActionCaption(screen, atom)))
            continue;
        if (atom->loadedX < loadedX0) loadedX0 = atom->loadedX;
        if (atom->loadedY < loadedY0) loadedY0 = atom->loadedY;
        if (atom->loadedX + atom->loadedWidth > loadedX1)
            loadedX1 = atom->loadedX + atom->loadedWidth;
        if (atom->loadedY + atom->loadedHeight > loadedY1)
            loadedY1 = atom->loadedY + atom->loadedHeight;
    }

    if (loadedX0 == SDWORD_Max || loadedY0 == SDWORD_Max ||
        loadedX1 <= loadedX0 || loadedY1 <= loadedY0)
    {
        loadedX0 = base->loadedX;
        loadedY0 = base->loadedY;
        loadedX1 = base->loadedX + base->loadedWidth;
        loadedY1 = base->loadedY + base->loadedHeight;
    }

    if (muiManagerScreen(screen->name))
    {
        rectangle infoRect;
        sdword sideMargin = uiScaleSize(16);
        sdword headerHeight = uiScaleSize(56);
        sdword innerMargin = uiScaleSize(14);
        sdword bottomPadding = uiScaleSize(10);
        sdword actionAreaHeight = buildManager ? uiScaleSize(50) : 0;
        sdword bottomLimit = MAIN_WindowHeight - sideMargin;
        sdword maxContentWidth;
        sdword maxContentHeight;
        sdword contentWidth;
        sdword contentHeight;
        sdword panelWidth;
        sdword panelHeight;
        sdword panelX1;
        sdword panelY0;

        tbGetInfoBarRect(&infoRect);
        if (infoRect.y0 > sideMargin * 2 && infoRect.y0 < MAIN_WindowHeight)
            bottomLimit = infoRect.y0 - uiScaleSize(6);

        if (buildManager)
        {
            maxContentWidth = MAIN_WindowWidth * 30 / 100;
            if (maxContentWidth > uiScaleSize(430))
                maxContentWidth = uiScaleSize(430);
            if (maxContentWidth < uiScaleSize(330))
                maxContentWidth = uiScaleSize(330);
        }
        else
        {
            maxContentWidth = MAIN_WindowWidth * 36 / 100;
            if (maxContentWidth > uiScaleSize(520))
                maxContentWidth = uiScaleSize(520);
            if (maxContentWidth < uiScaleSize(280))
                maxContentWidth = uiScaleSize(280);
        }
        if (maxContentWidth > MAIN_WindowWidth - sideMargin * 2)
            maxContentWidth = MAIN_WindowWidth - sideMargin * 2;

        maxContentHeight = bottomLimit - sideMargin - headerHeight -
                           bottomPadding - actionAreaHeight;
        if (maxContentHeight < uiScaleSize(220))
            maxContentHeight = MAIN_WindowHeight - sideMargin * 2 -
                               headerHeight - bottomPadding - actionAreaHeight;

        /* Never enlarge the legacy manager beyond its authored pixel size.
           Modern screens have room for it; enlargement only makes the type
           enormous and creates dead space.  Shrink only when required. */
        scale = 0.92f;
        if ((real32)(loadedX1 - loadedX0) * scale > (real32)maxContentWidth)
            scale = (real32)maxContentWidth / (real32)(loadedX1 - loadedX0);
        if ((real32)(loadedY1 - loadedY0) * scale > (real32)maxContentHeight)
            scale = (real32)maxContentHeight / (real32)(loadedY1 - loadedY0);
        if (scale <= 0.0f) scale = 1.0f;

        contentWidth = (sdword)((real32)(loadedX1 - loadedX0) * scale + 0.5f);
        contentHeight = (sdword)((real32)(loadedY1 - loadedY0) * scale + 0.5f);
        panelWidth = contentWidth + innerMargin * 2;
        if (buildManager && panelWidth < uiScaleSize(450))
            panelWidth = uiScaleSize(450);
        panelHeight = headerHeight + contentHeight + bottomPadding +
                      actionAreaHeight;
        panelX1 = MAIN_WindowWidth - sideMargin;
        panelY0 = sideMargin +
                  (bottomLimit - sideMargin - panelHeight) / 2;
        if (panelY0 < sideMargin) panelY0 = sideMargin;

        layout.outer = muiRect(panelX1 - panelWidth, panelY0,
                               panelX1, panelY0 + panelHeight);
        layout.content = muiRect(
            layout.outer.x0 + (panelWidth - contentWidth) / 2,
            layout.outer.y0 + headerHeight,
            layout.outer.x0 + (panelWidth - contentWidth) / 2 + contentWidth,
            layout.outer.y0 + headerHeight + contentHeight);
        scaleX = scale;
        scaleY = scale;
        offsetX = (real32)layout.content.x0;
        offsetY = (real32)layout.content.y0;
    }
    else
    {
        scaleX = (real32)(layout.content.x1 - layout.content.x0) /
                 (real32)(loadedX1 - loadedX0);
        scaleY = (real32)(layout.content.y1 - layout.content.y0) /
                 (real32)(loadedY1 - loadedY0);
        scale = scaleX < scaleY ? scaleX : scaleY;
        scaleX = scale;
        scaleY = scale;
        offsetX = (real32)layout.content.x0 +
                  ((real32)(layout.content.x1 - layout.content.x0) -
                   (real32)(loadedX1 - loadedX0) * scale) * 0.5f;
        offsetY = (real32)layout.content.y0 +
                  ((real32)(layout.content.y1 - layout.content.y0) -
                   (real32)(loadedY1 - loadedY0) * scale) * 0.5f;
    }

    for (index = 1; index < screen->nAtoms; ++index)
    {
        featom *atom = &screen->atoms[index];
        atom->x = (sword)(offsetX +
            (real32)(atom->loadedX - loadedX0) * scaleX + 0.5f);
        atom->y = (sword)(offsetY +
            (real32)(atom->loadedY - loadedY0) * scaleY + 0.5f);
        atom->width = (sword)((real32)atom->loadedWidth * scaleX + 0.5f);
        atom->height = (sword)((real32)atom->loadedHeight * scaleY + 0.5f);
        if (atom->type == FA_DecorativeRegion ||
            atom->type == FA_OpaqueDecorativeRegion)
        {
            bitSet(atom->flags, FAF_Hidden);
        }
    }

    if (buildManager)
    {
        sdword actionCount = 0;
        sdword actionIndex = 0;
        sdword actionGap = uiScaleSize(6);
        sdword actionHeight = uiScaleSize(34);
        sdword actionY1 = layout.outer.y1 - uiScaleSize(10);
        sdword actionY0 = actionY1 - actionHeight;
        sdword rowX0 = layout.outer.x0 + uiScaleSize(14);
        sdword rowX1 = layout.outer.x1 - uiScaleSize(14);
        sdword actionWidth;

        for (index = 1; index < screen->nAtoms; ++index)
            if (muiBuildActionAtom(&screen->atoms[index])) ++actionCount;
        actionWidth = actionCount > 0
            ? (rowX1 - rowX0 - actionGap * (actionCount - 1)) / actionCount
            : 0;

        for (index = 1; index < screen->nAtoms; ++index)
        {
            featom *action = &screen->atoms[index];
            sdword newX;
            if (!muiBuildActionAtom(action)) continue;
            newX = rowX0 + actionIndex * (actionWidth + actionGap);
            action->x = (sword)newX;
            action->y = (sword)actionY0;
            action->width = (sword)actionWidth;
            action->height = (sword)actionHeight;

            /* Some FIBs store the caption as a separate static-text atom.
               Preserve that relationship when the button is reflowed. */
            for (buttonIndex = 1; buttonIndex < screen->nAtoms; ++buttonIndex)
            {
                featom *caption = &screen->atoms[buttonIndex];
                sdword cx;
                sdword cy;
                if (caption->type != FA_StaticText) continue;
                cx = caption->loadedX + caption->loadedWidth / 2;
                cy = caption->loadedY + caption->loadedHeight / 2;
                if (cx >= action->loadedX &&
                    cx < action->loadedX + action->loadedWidth &&
                    cy >= action->loadedY &&
                    cy < action->loadedY + action->loadedHeight)
                {
                    caption->x = (sword)(newX + uiScaleSize(4));
                    caption->y = (sword)(actionY0 + uiScaleSize(2));
                    caption->width = (sword)(actionWidth - uiScaleSize(8));
                    caption->height = (sword)(actionHeight - uiScaleSize(4));
                }
            }
            ++actionIndex;
        }
    }

    if (kind == MUI_SCREEN_POPUP)
    {
        /* Several original popup screens place button captions as separate
           static-text atoms inside very narrow legacy text boxes.  The modern
           popup scaling keeps the button bodies readable, but those tiny text
           boxes can collapse down to an ellipsis-only caption.  Re-anchor any
           static text whose centre lands inside a live button so the label uses
           the full button face. */
        for (index = 1; index < screen->nAtoms; ++index)
        {
            featom *atom = &screen->atoms[index];
            rectangle textRect;
            sdword textCentreX;
            sdword textCentreY;

            if (atom->type != FA_StaticText || atom->width <= 0 || atom->height <= 0)
                continue;

            textRect = muiRect(atom->x, atom->y,
                               atom->x + atom->width,
                               atom->y + atom->height);
            textCentreX = (textRect.x0 + textRect.x1) / 2;
            textCentreY = (textRect.y0 + textRect.y1) / 2;

            for (buttonIndex = 1; buttonIndex < screen->nAtoms; ++buttonIndex)
            {
                featom *buttonAtom = &screen->atoms[buttonIndex];
                rectangle buttonRect;
                sdword insetX;
                sdword insetY;
                sdword newWidth;
                sdword newHeight;

                if (!muiAtomIsButtonLike(buttonAtom) ||
                    buttonAtom->width <= 0 || buttonAtom->height <= 0)
                    continue;

                buttonRect = muiRect(buttonAtom->x, buttonAtom->y,
                                     buttonAtom->x + buttonAtom->width,
                                     buttonAtom->y + buttonAtom->height);
                if (!muiPointIn(&buttonRect, textCentreX, textCentreY))
                    continue;

                insetX = muiClamp(buttonAtom->width / 12,
                                  uiScaleSize(8), uiScaleSize(20));
                insetY = muiClamp(buttonAtom->height / 6,
                                  uiScaleSize(2), uiScaleSize(10));
                newWidth = buttonAtom->width - insetX * 2;
                newHeight = buttonAtom->height - insetY * 2;
                if (newWidth < uiScaleSize(24) || newHeight < uiScaleSize(12))
                    break;

                atom->x = (sword)(buttonAtom->x + insetX);
                atom->y = (sword)(buttonAtom->y + insetY);
                atom->width = (sword)newWidth;
                atom->height = (sword)newHeight;
                break;
            }
        }
    }

    base->x = (sword)layout.outer.x0;
    base->y = (sword)layout.outer.y0;
    base->width = (sword)(layout.outer.x1 - layout.outer.x0);
    base->height = (sword)(layout.outer.y1 - layout.outer.y0);
}

static void muiBuildLayout(ModernUILayout *layout)
{
    sdword margin = uiScaleSize(24);
    sdword top = uiScaleSize(28);
    sdword bottom = MAIN_WindowHeight - uiScaleSize(24);
    sdword railWidth = muiClamp(MAIN_WindowWidth / 5,
                                uiScaleSize(176), uiScaleSize(300));
    sdword previewWidth = muiClamp(MAIN_WindowWidth / 4,
                                   uiScaleSize(220), uiScaleSize(430));
    sdword gap = uiScaleSize(18);
    sdword contentLeft;
    sdword contentRight;
    sdword rowTop;
    sdword rowHeight;
    sdword index;

    layout->canvas = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
    layout->rail = muiRect(margin, top, margin + railWidth, bottom);
    layout->preview = muiRect(MAIN_WindowWidth - margin - previewWidth,
                              top, MAIN_WindowWidth - margin, bottom);
    contentLeft = layout->rail.x1 + gap;
    contentRight = layout->preview.x0 - gap;
    if (contentRight - contentLeft < uiScaleSize(330))
    {
        layout->preview.x0 = layout->preview.x1;
        contentRight = MAIN_WindowWidth - margin;
    }
    layout->content = muiRect(contentLeft, top, contentRight, bottom);

    for (index = 0; index < MUI_COLOR_TAB_COUNT; ++index)
    {
        layout->tabs[index] = muiRect(
            layout->rail.x0 + uiScaleSize(14),
            layout->rail.y0 + uiScaleSize(112 + index * 48),
            layout->rail.x1 - uiScaleSize(14),
            layout->rail.y0 + uiScaleSize(150 + index * 48));
    }

    rowTop = layout->content.y0 + uiScaleSize(132);
    rowHeight = muiClamp((layout->content.y1 - rowTop - uiScaleSize(116)) /
                         MUI_ROW_COUNT, uiScaleSize(48), uiScaleSize(76));
    for (index = 0; index < MUI_ROW_COUNT; ++index)
    {
        layout->rows[index] = muiRect(
            layout->content.x0 + uiScaleSize(22),
            rowTop + index * rowHeight,
            layout->content.x1 - uiScaleSize(22),
            rowTop + (index + 1) * rowHeight - uiScaleSize(4));
    }

    layout->apply = muiRect(layout->content.x1 - uiScaleSize(150),
                            layout->content.y1 - uiScaleSize(64),
                            layout->content.x1 - uiScaleSize(22),
                            layout->content.y1 - uiScaleSize(24));
    layout->cancel = muiRect(layout->apply.x0 - uiScaleSize(120),
                             layout->apply.y0,
                             layout->apply.x0 - uiScaleSize(12),
                             layout->apply.y1);
}

static rectangle muiMainItemRect(sdword index)
{
    sdword margin = uiScaleSize(38);
    sdword width = muiClamp(MAIN_WindowWidth / 4,
                            uiScaleSize(220), uiScaleSize(390));
    sdword top = muiClamp(MAIN_WindowHeight / 3,
                          uiScaleSize(144), MAIN_WindowHeight - uiScaleSize(330));
    sdword height = uiScaleSize(42);
    return muiRect(margin, top + index * uiScaleSize(48),
                   margin + width, top + index * uiScaleSize(48) + height);
}

static rectangle muiMainAnimatedItemRect(sdword index, real32 expansion)
{
    rectangle rect = muiMainItemRect(index);
    sdword shift = 0;
    if (index >= 4)
    {
        /* Story, Training, Multiplayer and Settings stay completely fixed.
           The settings drawer only makes room below Settings: Archive and
           Exit slide down while the four destinations fade into the gap. */
        shift = (sdword)((real32)uiScaleSize(156) * expansion + 0.5f);
    }
    rect.y0 += shift;
    rect.y1 += shift;
    return rect;
}

static rectangle muiMainSettingsItemRect(sdword index)
{
    rectangle settings = muiMainItemRect(3);
    sdword rowHeight = uiScaleSize(32);
    sdword rowStep = uiScaleSize(38);
    sdword top = settings.y1 + uiScaleSize(6) + index * rowStep;
    return muiRect(settings.x0 + uiScaleSize(24), top,
                   settings.x1, top + rowHeight);
}

static rectangle muiMainSettingsAnimatedItemRect(sdword index,
                                                 real32 expansion)
{
    rectangle settings = muiMainItemRect(3);
    sdword rowHeight = uiScaleSize(32);
    sdword rowStep = uiScaleSize(38);
    sdword gap = uiScaleSize(6);
    sdword top = settings.y1 +
        (sdword)((real32)(gap + index * rowStep) * expansion + 0.5f);
    sdword height = (sdword)((real32)rowHeight * expansion + 0.5f);
    if (height < 1) height = 1;
    return muiRect(settings.x0 + uiScaleSize(24), top,
                   settings.x1, top + height);
}

static rectangle muiMainSettingsHoverRect(void)
{
    rectangle settings = muiMainItemRect(3);
    rectangle last = muiMainSettingsItemRect(MUI_MAIN_SETTINGS_COUNT - 1);
    settings.x0 -= uiScaleSize(6);
    settings.x1 += uiScaleSize(6);
    settings.y0 -= uiScaleSize(4);
    settings.y1 = last.y1 + uiScaleSize(6);
    return settings;
}

static void muiMainSettingsUpdate(void)
{
    rectangle settings = muiMainItemRect(3);
    rectangle hoverArea = muiMainSettingsHoverRect();
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    udword now = SDL_GetTicks();
    udword delta;
    real32 step;
    bool32 wantsOpen;

    if (muiMainSettingsLastTick == 0)
        muiMainSettingsLastTick = now;
    delta = now - muiMainSettingsLastTick;
    muiMainSettingsLastTick = now;
    if (delta > 80) delta = 80;
    step = (real32)delta / 150.0f;

    wantsOpen = muiMainSettingsKeyboardOpen ||
                muiPointIn(&settings, mouseX, mouseY) ||
                (muiMainSettingsExpansion > 0.01f &&
                 muiPointIn(&hoverArea, mouseX, mouseY));
    if (wantsOpen)
    {
        muiMainSettingsExpansion += step;
        if (muiMainSettingsExpansion > 1.0f)
            muiMainSettingsExpansion = 1.0f;
    }
    else
    {
        muiMainSettingsExpansion -= step;
        if (muiMainSettingsExpansion < 0.0f)
            muiMainSettingsExpansion = 0.0f;
    }
}

static featom *muiMainActionAtom(fescreen *screen, sdword index)
{
    static const char *actions[] = {
        "SinglePlayerOptions",
        "Main_game_screen-TO-Select_tutorial",
        "FE_Multiplayer_Game",
        "OP_Options_Start",
        "FE_SHOWCREDITS",
        "FE_HideMenuOrGotoQuitGame"
    };
    if (index < 0 || index >= (sdword)(sizeof(actions) / sizeof(actions[0])))
        return NULL;
    return feAtomFindInScreen(screen, (char *)actions[index]);
}

static void muiActivateMain(fescreen *screen, sdword index)
{
    featom *atom = muiMainActionAtom(screen, index);
    if (atom == NULL) return;
    if (bitTest(atom->flags, FAF_Link))
    {
        felink *link = feLinkFindInScreen(screen, atom->name);
        if (link != NULL)
        {
            feScreenDisappear(NULL, NULL);
            feScreenStart(ghMainRegion, link->linkToName);
        }
    }
    else
    {
        feFunctionExecute(atom->name, atom, FALSE);
    }
}

static void muiActivateMainSettings(sdword index)
{
    static const char *screens[MUI_MAIN_SETTINGS_COUNT] = {
        "Audio_Options", "Video_Options", "Gameplay", "Keys"
    };
    if (index < 0 || index >= MUI_MAIN_SETTINGS_COUNT) return;
    muiMainSettingsKeyboardOpen = FALSE;
    muiMainSettingsExpansion = 0.0f;
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, (char *)screens[index]);
}

static void muiDrawMain(regionhandle region)
{
    static const char *items[] = {
        "STORY", "TRAINING", "MULTIPLAYER", "SETTINGS", "ARCHIVE", "EXIT"
    };
    static const char *indices[] = {
        "01", "02", "03", "04", "05", "06"
    };
    static const char *settingsItems[MUI_MAIN_SETTINGS_COUNT] = {
        "AUDIO", "VIDEO", "GAMEPLAY", "KEY BINDINGS"
    };
    rectangle whole = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
    rectangle rail;
    rectangle item;
    rectangle mark;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    real32 expansion;
    real32 textFade;
    ubyte fadeAlpha;
    color submenuPaper;
    color submenuMuted;
    color submenuLine;
    color submenuSignal;
    (void)region;

    muiEnsureFonts();
    muiMainSettingsUpdate();
    expansion = muiMainSettingsExpansion;
    textFade = (expansion - 0.22f) / 0.78f;
    if (textFade < 0.0f) textFade = 0.0f;
    if (textFade > 1.0f) textFade = 1.0f;
    fadeAlpha = (ubyte)(255.0f * textFade + 0.5f);
    submenuPaper = colRGBA(225, 222, 206, fadeAlpha);
    submenuMuted = colRGBA(143, 157, 157, fadeAlpha);
    submenuLine = colRGBA(72, 94, 102, (ubyte)(118.0f * textFade + 0.5f));
    submenuSignal = colRGBA(222, 162, 62, fadeAlpha);

    muiDrawBackdrop(MUI_ART_GALAXY);
    muiFill(&whole, colRGBA(4, 7, 10, 74));
    rail = muiRect(0, 0, muiClamp(MAIN_WindowWidth * 2 / 5,
                                  uiScaleSize(320), MAIN_WindowWidth / 2),
                   MAIN_WindowHeight);
    muiFill(&rail, colRGBA(13, 22, 29, 246));
    primLine2(rail.x1, 0, rail.x1, MAIN_WindowHeight, muiLineSoft);

    muiText(uiScaleSize(38), uiScaleSize(34), muiSignal,
            "RELIC / 1999", muiBodyFont);
    muiText(uiScaleSize(38), uiScaleSize(68), muiPaper,
            "HOMEWORLD", muiHeadingFont);
    muiText(uiScaleSize(38), uiScaleSize(98), muiMuted,
            "MODERN WINDOWS EDITION", muiBodyFont);

    for (index = 0; index < 6; ++index)
    {
        bool32 hovered;
        item = muiMainAnimatedItemRect(index, expansion);
        hovered = muiPointIn(&item, mouseX, mouseY);
        if (hovered)
        {
            muiMainSelection = index;
            if (index != 3)
                muiMainSettingsKeyboardOpen = FALSE;
        }
        if (index == muiMainSelection || (index == 3 && expansion > 0.01f))
        {
            muiFill(&item, muiSignalSoft);
            mark = item;
            mark.x1 = mark.x0 + uiScaleSize(3);
            muiFill(&mark, muiSignal);
        }
        muiText(item.x0 + uiScaleSize(14), item.y0 + uiScaleSize(12),
                index == muiMainSelection ? muiSignal : muiDisabled,
                indices[index], muiBodyFont);
        muiText(item.x0 + uiScaleSize(54), item.y0 + uiScaleSize(12),
                (index == muiMainSelection || (index == 3 && expansion > 0.01f)) ?
                    muiPaper : muiMuted,
                items[index], muiBodyFont);
        primLine2(item.x0, item.y1, item.x1, item.y1, muiLineSoft);
    }

    if (expansion > 0.001f)
    {
        for (index = 0; index < MUI_MAIN_SETTINGS_COUNT; ++index)
        {
            rectangle sub = muiMainSettingsAnimatedItemRect(index, expansion);
            bool32 hovered = muiPointIn(&sub, mouseX, mouseY);
            bool32 selected = hovered ||
                (muiMainSettingsKeyboardOpen && index == muiMainSettingsSelection);
            if (hovered)
            {
                muiMainSelection = 3;
                muiMainSettingsSelection = index;
            }
            if (selected)
            {
                muiFill(&sub, colRGBA(222, 162, 62,
                    (ubyte)(42.0f * expansion + 0.5f)));
                mark = sub;
                mark.x1 = mark.x0 + uiScaleSize(2);
                muiFill(&mark, submenuSignal);
            }
            muiText(sub.x0 + uiScaleSize(13), sub.y0 + uiScaleSize(8),
                    selected ? submenuPaper : submenuMuted,
                    settingsItems[index], muiBodyFont);
            primLine2(sub.x0, sub.y1, sub.x1, sub.y1, submenuLine);
        }
    }

    muiText(uiScaleSize(38), MAIN_WindowHeight - uiScaleSize(60), muiMuted,
            "A RECONSTRUCTION OF THE ORIGINAL JOURNEY", muiBodyFont);
    muiText(uiScaleSize(38), MAIN_WindowHeight - uiScaleSize(34), muiDisabled,
            "DIRECT3D 12 / DXR / NATIVE UI", muiBodyFont);

    muiTextRight(MAIN_WindowWidth - uiScaleSize(38), uiScaleSize(38),
                 muiSignal, "KARAK / DEPARTURE ARRAY", muiBodyFont);
    muiTextRight(MAIN_WindowWidth - uiScaleSize(38), uiScaleSize(64),
                 muiPaper, "THE JOURNEY BEGINS", muiHeadingFont);
    muiTextRight(MAIN_WindowWidth - uiScaleSize(38),
                 MAIN_WindowHeight - uiScaleSize(42), muiMuted,
                 "ARCHIVE PLATE 01 / FLEET COMMAND", muiBodyFont);
}

static udword muiMainProcess(regionhandle region, smemsize ID,
                             udword event, udword data)
{
    fescreen *screen = muiMainScreen;
    sdword index;
    (void)data;

    if (event == RPE_PressLeft)
    {
        if (muiMainSettingsExpansion > 0.55f)
        {
            for (index = 0; index < MUI_MAIN_SETTINGS_COUNT; ++index)
            {
                rectangle sub = muiMainSettingsAnimatedItemRect(
                    index, muiMainSettingsExpansion);
                if (muiPointIn(&sub, mouseCursorX(), mouseCursorY()))
                {
                    muiMainSelection = 3;
                    muiMainSettingsSelection = index;
                    regRecursiveSetDirty(region);
                    return RPR_Redraw;
                }
            }
        }
        for (index = 0; index < 6; ++index)
        {
            rectangle item = muiMainAnimatedItemRect(index,
                                                     muiMainSettingsExpansion);
            if (muiPointIn(&item, mouseCursorX(), mouseCursorY()))
            {
                muiMainSelection = index;
                break;
            }
        }
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_KeyDown)
    {
        if (ID == ESCKEY)
        {
            if (muiMainSettingsKeyboardOpen ||
                (muiMainSelection == 3 && muiMainSettingsExpansion > 0.05f))
            {
                muiMainSettingsKeyboardOpen = FALSE;
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
            muiActivateMain(screen, 5);
            return RPR_Redraw;
        }

        if (muiMainSettingsKeyboardOpen && muiMainSelection == 3)
        {
            if (ID == ARRUP)
                muiMainSettingsSelection =
                    (muiMainSettingsSelection + MUI_MAIN_SETTINGS_COUNT - 1) %
                    MUI_MAIN_SETTINGS_COUNT;
            else if (ID == ARRDOWN)
                muiMainSettingsSelection =
                    (muiMainSettingsSelection + 1) % MUI_MAIN_SETTINGS_COUNT;
            else if (ID == RETURNKEY)
            {
                muiActivateMainSettings(muiMainSettingsSelection);
                return RPR_Redraw;
            }
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }

        if (ID == ARRUP) muiMainSelection = (muiMainSelection + 5) % 6;
        else if (ID == ARRDOWN) muiMainSelection = (muiMainSelection + 1) % 6;
        else if (ID == RETURNKEY)
        {
            if (muiMainSelection == 3)
            {
                muiMainSettingsKeyboardOpen = TRUE;
                muiMainSettingsSelection = 0;
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
            muiActivateMain(screen, muiMainSelection);
            return RPR_Redraw;
        }
        muiMainSettingsKeyboardOpen = (muiMainSelection == 3);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft)
    {
        if (muiMainSettingsExpansion > 0.55f)
        {
            for (index = 0; index < MUI_MAIN_SETTINGS_COUNT; ++index)
            {
                rectangle sub = muiMainSettingsAnimatedItemRect(
                    index, muiMainSettingsExpansion);
                if (muiPointIn(&sub, mouseCursorX(), mouseCursorY()))
                {
                    muiMainSettingsSelection = index;
                    muiActivateMainSettings(index);
                    return RPR_Redraw;
                }
            }
        }
        for (index = 0; index < 6; ++index)
        {
            rectangle item = muiMainAnimatedItemRect(index,
                                                     muiMainSettingsExpansion);
            if (muiPointIn(&item, mouseCursorX(), mouseCursorY()))
            {
                muiMainSelection = index;
                if (index == 3)
                {
                    /* Settings is now a hover drawer, not a gateway to the
                       legacy Audio screen. */
                    regRecursiveSetDirty(region);
                    return RPR_Redraw;
                }
                muiMainSettingsKeyboardOpen = FALSE;
                muiActivateMain(screen, index);
                return RPR_Redraw;
            }
        }
    }
    return 0;
}

static rectangle muiConnectionItemRect(sdword index)
{
    sdword margin = uiScaleSize(34);
    sdword left = muiClamp(MAIN_WindowWidth * 35 / 100,
                           uiScaleSize(330), MAIN_WindowWidth / 2);
    sdword right = MAIN_WindowWidth - margin;
    sdword top = muiClamp(MAIN_WindowHeight * 25 / 100,
                          uiScaleSize(150), MAIN_WindowHeight - uiScaleSize(430));
    sdword height = muiClamp(MAIN_WindowHeight / 8,
                             uiScaleSize(78), uiScaleSize(126));
    sdword gap = uiScaleSize(14);
    return muiRect(left, top + index * (height + gap),
                   right, top + index * (height + gap) + height);
}

static featom *muiConnectionActionAtom(fescreen *screen, sdword index)
{
    static const char *actions[] = {
        "MG_Skirmish", "MG_LANIPX", "MG_InternetWON", "MG_ConnectionBack"
    };
    if (screen == NULL || index < 0 || index >= 4) return NULL;
    return feAtomFindInScreen(screen, (char *)actions[index]);
}

static void muiActivateConnection(sdword index)
{
    featom *atom = muiConnectionActionAtom(muiConnectionScreen, index);
    if (atom != NULL)
        feFunctionExecute(atom->name, atom, FALSE);
}

static void muiDrawConnection(regionhandle region)
{
    static const char *titles[] = {
        "SKIRMISH", "LOCAL NETWORK", "ONLINE SERVICES", "RETURN"
    };
    static const char *descriptions[] = {
        "Command a private battle against computer fleets",
        "Discover and join games on your local IP network",
        "Connect through the configured Internet service",
        "Return to fleet command"
    };
    rectangle whole = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
    rectangle rail;
    rectangle item;
    rectangle accent;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)region;

    muiEnsureFonts();
    muiDrawBackdrop(MUI_ART_SYSTEMS);
    muiFill(&whole, colRGBA(3, 6, 9, 92));
    rail = muiRect(0, 0, muiClamp(MAIN_WindowWidth * 30 / 100,
                                  uiScaleSize(290), MAIN_WindowWidth / 2),
                   MAIN_WindowHeight);
    muiFill(&rail, colRGBA(10, 18, 24, 246));
    primLine2(rail.x1, 0, rail.x1, MAIN_WindowHeight, muiLineSoft);

    muiText(uiScaleSize(38), uiScaleSize(38), muiSignal,
            "FLEET NETWORK", muiBodyFont);
    muiText(uiScaleSize(38), uiScaleSize(72), muiPaper,
            "MULTIPLAYER", muiHeadingFont);
    muiText(uiScaleSize(38), uiScaleSize(106), muiMuted,
            "CONNECTION METHOD", muiBodyFont);
    muiText(uiScaleSize(38), MAIN_WindowHeight - uiScaleSize(92), muiMuted,
            "SELECT A COMMAND CHANNEL", muiBodyFont);
    muiText(uiScaleSize(38), MAIN_WindowHeight - uiScaleSize(62), muiDisabled,
            "ALL LEGACY NETWORK CALLBACKS PRESERVED", muiBodyFont);

    for (index = 0; index < 4; ++index)
    {
        bool32 hovered;
        char number[8];
        item = muiConnectionItemRect(index);
        hovered = muiPointIn(&item, mouseX, mouseY);
        if (hovered) muiConnectionSelection = index;
        muiFill(&item, index == muiConnectionSelection ?
                muiSurfaceRaised : muiSurface);
        primRectOutline2(&item, 1,
                         index == muiConnectionSelection ? muiSignal : muiLineSoft);
        if (index == muiConnectionSelection)
        {
            accent = item;
            accent.x1 = accent.x0 + uiScaleSize(4);
            muiFill(&accent, muiSignal);
        }
        snprintf(number, sizeof(number), "%02d", index + 1);
        muiText(item.x0 + uiScaleSize(20), item.y0 + uiScaleSize(18),
                index == muiConnectionSelection ? muiSignal : muiDisabled,
                number, muiBodyFont);
        muiText(item.x0 + uiScaleSize(72), item.y0 + uiScaleSize(16),
                index == muiConnectionSelection ? muiPaper : muiMuted,
                titles[index], muiHeadingFont);
        muiText(item.x0 + uiScaleSize(72), item.y0 + uiScaleSize(48),
                muiMuted, descriptions[index], muiBodyFont);
    }
}

static udword muiConnectionProcess(regionhandle region, smemsize ID,
                                    udword event, udword data)
{
    sdword index;
    (void)data;
    if (event == RPE_KeyDown)
    {
        if (ID == ARRUP) muiConnectionSelection = (muiConnectionSelection + 3) % 4;
        else if (ID == ARRDOWN) muiConnectionSelection = (muiConnectionSelection + 1) % 4;
        else if (ID == RETURNKEY) muiActivateConnection(muiConnectionSelection);
        else if (ID == ESCKEY) muiActivateConnection(3);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_PressLeft || event == RPE_ReleaseLeft)
    {
        for (index = 0; index < 4; ++index)
        {
            rectangle item = muiConnectionItemRect(index);
            if (muiPointIn(&item, mouseCursorX(), mouseCursorY()))
            {
                muiConnectionSelection = index;
                if (event == RPE_ReleaseLeft) muiActivateConnection(index);
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
    }
    return 0;
}

static sdword muiDisplayMode(void)
{
    if (!fullScreen) return 0;
    return mainExclusiveFullscreen ? 2 : 1;
}

static void muiSetDisplayMode(sdword mode)
{
    mode = (mode + 3) % 3;
    fullScreen = mode != 0;
    mainExclusiveFullscreen = mode == 2;
}

static const sdword muiResolutions[][2] = {
    {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1080},
    {2560, 1440}, {3440, 1440}, {3840, 1600}, {3840, 2160},
    {5120, 1440}, {5120, 2160}, {7680, 4320}
};

static const sdword muiRefreshRates[] = {
    0, 60, 75, 90, 100, 120, 144, 165, 240, 360
};

static const sdword muiFrameCaps[] = {
    0, 30, 60, 90, 120, 144, 165, 240, 360
};

static const sdword muiUIScales[] = {75, 100, 125, 150, 175, 200};

static sdword muiNearestPairIndex(sdword width, sdword height)
{
    sdword index;
    sdword best = 0;
    udword bestDistance = 0xffffffffu;
    for (index = 0; index < (sdword)(sizeof(muiResolutions) /
                                    sizeof(muiResolutions[0])); ++index)
    {
        udword distance = (udword)(abs(muiResolutions[index][0] - width) +
                                   abs(muiResolutions[index][1] - height));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

static sdword muiNearestValueIndex(const sdword *values, sdword count,
                                   sdword value)
{
    sdword index;
    sdword best = 0;
    sdword bestDistance = 0x7fffffff;
    for (index = 0; index < count; ++index)
    {
        sdword distance = abs(values[index] - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = index;
        }
    }
    return best;
}

static void muiApplyRendererLive(void)
{
    opDetailThresholdVal = (udword)mainFxLightingStrengthPercent;
    opNoLODVal = mainRaytracing;
    opBrightnessVal = muiClamp(opBrightnessVal, 0, 100);
#ifdef HW_ENABLE_D3D12_BACKEND
    hwModernGraphicsSetRaytracingEnabled(mainRaytracing);
    hwModernGraphicsSetFxLightingStrength(
        (float)mainFxLightingStrengthPercent / 100.0f);
    hwModernGraphicsSetPathTracingSettings(
        (unsigned int)mainPathTracingSamples,
        (unsigned int)mainPathTracingBounces,
        (float)mainGeneratedNormalStrengthPercent / 100.0f);
    hwModernGraphicsSetLightingEditorSettings(
        (float)mainEnvironmentLightingPercent / 100.0f,
        (float)mainAuthoredLightingPercent / 100.0f,
        (float)mainSurfaceReflectivityPercent / 100.0f,
        (float)mainSurfaceRoughnessPercent / 100.0f,
        (float)mainLightingExposurePercent / 100.0f);
    hwModernGraphicsSetSkyLightingStrengths(
        (float)mainPrimarySunLightingPercent / 100.0f,
        (float)mainSkyAmbientLightingPercent / 100.0f);
    hwModernGraphicsSetShadowSettings(
        (float)mainShadowStrengthPercent / 100.0f,
        (float)mainSunShadowAngularRadiusCentidegrees / 100.0f,
        (unsigned int)mainSunShadowSamples,
        (unsigned int)mainLocalShadowSamples,
        (float)mainLocalShadowAngularRadiusCentidegrees / 100.0f,
        (float)mainShadowReceiverBiasHundredths / 100.0f,
        (float)mainShadowNormalBiasHundredths / 100.0f,
        (float)mainContactShadowStrengthPercent / 100.0f,
        (float)mainContactShadowDistance,
        (float)mainShadowMaximumDistance);
    hwModernGraphicsSetAntiAliasingMode(mainDlaa);
    /* The old Universal 2x interpolator was not DLSS-G and could corrupt
       motion/disocclusion frames. Keep frame generation disabled until the
       signed Streamline DLSS-G + Reflex/PCL runtime is integrated. */
    mainFrameGeneration = 0;
    hwModernGraphicsSetFrameGenerationMode(HW_MODERN_FRAME_GENERATION_OFF);
    hwModernGraphicsSetPostProcessingSettings(
        (float)mainChromaticAberrationPercent / 100.0f,
        (float)mainMotionBlurPercent / 100.0f,
        (float)mainFilmGrainPercent / 100.0f,
        (float)mainGodRaysPercent / 100.0f,
        (float)mainBloomPercent / 100.0f);
    hwModernGraphicsSetOutputDitherStrength(
        (float)mainOutputDitherPercent / 100.0f);
#endif
    opUpdateVideoSettings();
}

static void muiAdjustDisplay(sdword row, sdword direction)
{
    sdword index;
    switch (row)
    {
        case 0:
            muiSetDisplayMode(muiDisplayMode() + direction);
            break;
        case 1:
            index = muiNearestPairIndex(mainWindowWidth, mainWindowHeight);
            index = muiClamp(index + direction, 0,
                (sdword)(sizeof(muiResolutions) / sizeof(muiResolutions[0])) - 1);
            mainWindowWidth = muiResolutions[index][0];
            mainWindowHeight = muiResolutions[index][1];
            break;
        case 2:
            index = muiNearestValueIndex(muiRefreshRates,
                (sdword)(sizeof(muiRefreshRates) / sizeof(muiRefreshRates[0])),
                mainRefreshRate);
            index = muiClamp(index + direction, 0,
                (sdword)(sizeof(muiRefreshRates) / sizeof(muiRefreshRates[0])) - 1);
            mainRefreshRate = muiRefreshRates[index];
            break;
        case 3:
            index = muiNearestValueIndex(muiFrameCaps,
                (sdword)(sizeof(muiFrameCaps) / sizeof(muiFrameCaps[0])),
                mainFrameRateLimit);
            index = muiClamp(index + direction, 0,
                (sdword)(sizeof(muiFrameCaps) / sizeof(muiFrameCaps[0])) - 1);
            mainFrameRateLimit = muiFrameCaps[index];
            break;
        case 4:
            index = muiNearestValueIndex(muiUIScales,
                (sdword)(sizeof(muiUIScales) / sizeof(muiUIScales[0])),
                mainUIScalePercent);
            index = muiClamp(index + direction, 0,
                (sdword)(sizeof(muiUIScales) / sizeof(muiUIScales[0])) - 1);
            mainUIScalePercent = muiUIScales[index];
            break;
    }
}

static void muiAdjustPath(sdword row, sdword direction)
{
    switch (row)
    {
        case 0: mainRaytracing = !mainRaytracing; break;
        case 1:
            mainPathTracingBounces = muiClamp(
                mainPathTracingBounces + direction, 1, 3);
            break;
        case 2:
            mainPathTracingSamples = muiClamp(
                mainPathTracingSamples + direction, 1, 4);
            break;
        case 3:
            mainGeneratedNormalStrengthPercent = muiClamp(
                mainGeneratedNormalStrengthPercent + direction * 10, 0, 400);
            break;
        case 4:
            mainFxLightingStrengthPercent = muiClamp(
                mainFxLightingStrengthPercent + direction * 10, 0, 200);
            break;
    }
    muiApplyRendererLive();
}

static void muiAdjustImage(sdword row, sdword direction)
{
    switch (row)
    {
        case 0:
            mainDlaa = (mainDlaa + direction +
                (HW_MODERN_AA_XESS_ULTRA_PERFORMANCE + 1)) %
                (HW_MODERN_AA_XESS_ULTRA_PERFORMANCE + 1);
            break;
        case 1:
            opBrightnessVal = muiClamp(opBrightnessVal + direction * 5, 0, 100);
            break;
        case 2:
            opEffectsVal = muiClamp(opEffectsVal + direction, 0, 2);
            break;
        case 3:
            opEffectsVal = 2;
            opNumEffects = muiClamp(opNumEffects + direction * 16, 0, 256);
            break;
        default:
            mainFrameGeneration = 0;
            break;
    }
    muiApplyRendererLive();
}

static void muiAdjustPost(sdword row, sdword direction)
{
    switch (row)
    {
        case 0:
            mainChromaticAberrationPercent = muiClamp(
                mainChromaticAberrationPercent + direction * 5, 0, 100);
            break;
        case 1:
            mainMotionBlurPercent = muiClamp(
                mainMotionBlurPercent + direction * 5, 0, 100);
            break;
        case 2:
            mainFilmGrainPercent = muiClamp(
                mainFilmGrainPercent + direction * 5, 0, 100);
            break;
        case 3:
            mainGodRaysPercent = muiClamp(
                mainGodRaysPercent + direction * 5, 0, 100);
            break;
        default:
            mainBloomPercent = muiClamp(
                mainBloomPercent + direction * 10, 0, 400);
            break;
    }
    muiApplyRendererLive();
}

static void muiRestoreStagedDisplay(void)
{
    fullScreen = muiVideo.savedFullScreen;
    mainExclusiveFullscreen = muiVideo.savedExclusive;
    mainWindowWidth = muiVideo.savedWidth;
    mainWindowHeight = muiVideo.savedHeight;
    mainRefreshRate = muiVideo.savedRefresh;
    mainFrameRateLimit = muiVideo.savedFrameCap;
    mainUIScalePercent = muiVideo.savedUIScale;
}

static void muiExitVideo(bool32 apply)
{
    bool32 inGame = muiVideo.inGame;
    if (apply)
    {
        muiApplyRendererLive();
        opOptionsSaveSettings();
        /* RTX-0079: APPLY in either main-menu or in-game Graphics keeps the
           shareable game-directory export synchronized with the accepted
           visual settings. Resolution/refresh/AA/framegen remain excluded. */
        muiExportVisualSettings();
    }
    else
    {
        opRestoreSavedSettings();
        muiRestoreStagedDisplay();
        muiApplyRendererLive();
    }
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, inGame ? "Game_options" : "Main_game_screen");
}

static void muiDrawButtonRect(const rectangle *rect, const char *label,
                              bool32 primary, bool32 hovered)
{
    rectangle accent;
    color fill = hovered ? muiSurfaceRaised : muiSurface;
    muiFill(rect, fill);
    primRectOutline2((rectangle *)rect, 1, primary ? muiSignal : muiLine);
    if (primary)
    {
        accent = *rect;
        accent.x1 = accent.x0 + uiScaleSize(3);
        muiFill(&accent, muiSignal);
    }
    muiText(rect->x0 + uiScaleSize(14),
            rect->y0 + (rect->y1 - rect->y0 - fontHeight("A")) / 2,
            primary ? muiPaper : muiMuted, label, muiBodyFont);
}

static const char *muiDisplayValue(sdword row, char *buffer, size_t count)
{
    static const char *modes[] = {"WINDOWED", "BORDERLESS", "EXCLUSIVE"};
    switch (row)
    {
        case 0: return modes[muiDisplayMode()];
        case 1:
            snprintf(buffer, count, "%d x %d", mainWindowWidth, mainWindowHeight);
            return buffer;
        case 2:
            if (mainRefreshRate <= 0) return "AUTO";
            snprintf(buffer, count, "%d HZ", mainRefreshRate);
            return buffer;
        case 3:
            if (mainFrameRateLimit <= 0) return "UNLIMITED";
            snprintf(buffer, count, "%d FPS", mainFrameRateLimit);
            return buffer;
        default:
            snprintf(buffer, count, "%d%%", mainUIScalePercent);
            return buffer;
    }
}

static const char *muiPathValue(sdword row, char *buffer, size_t count)
{
    switch (row)
    {
        case 0: return mainRaytracing ? "ON" : "OFF";
        case 1:
            snprintf(buffer, count, "%d", mainPathTracingBounces);
            return buffer;
        case 2:
            snprintf(buffer, count, "%d / PIXEL", mainPathTracingSamples);
            return buffer;
        case 3:
            snprintf(buffer, count, "%d%%", mainGeneratedNormalStrengthPercent);
            return buffer;
        default:
            snprintf(buffer, count, "%d%%", mainFxLightingStrengthPercent);
            return buffer;
    }
}

static const char *muiImageValue(sdword row, char *buffer, size_t count)
{
    static const char *effects[] = {"HIGH", "LOW", "CUSTOM"};
    static const char *antiAliasing[] = {
        "OFF", "DLAA", "FXAA", "DLSS QUALITY", "DLSS BALANCED",
        "DLSS PERFORMANCE", "DLSS ULTRA PERFORMANCE", "AUTO / QUALITY",
        "AUTO / NATIVE AA", "UNIVERSAL TEMPORAL AA", "FSR NATIVE AA", "FSR QUALITY",
        "FSR BALANCED", "FSR PERFORMANCE", "FSR ULTRA PERFORMANCE",
        "XESS AA", "XESS QUALITY", "XESS BALANCED", "XESS PERFORMANCE",
        "XESS ULTRA PERFORMANCE"
    };
    switch (row)
    {
        case 0:
        {
            sdword mode = muiClamp(mainDlaa, HW_MODERN_AA_OFF,
                                   HW_MODERN_AA_XESS_ULTRA_PERFORMANCE);
#ifdef HW_ENABLE_D3D12_BACKEND
            if (mode != HW_MODERN_AA_OFF && mode != HW_MODERN_AA_FXAA &&
                !hwModernGraphicsIsDlaaActive())
            {
                snprintf(buffer, count, "%s / FXAA", antiAliasing[mode]);
                return buffer;
            }
#endif
            return antiAliasing[mode];
        }
        case 1:
            snprintf(buffer, count, "%d%%", opBrightnessVal);
            return buffer;
        case 2: return effects[muiClamp(opEffectsVal, 0, 2)];
        case 3:
            snprintf(buffer, count, "%d ACTIVE", opNumEffects);
            return buffer;
        default:
            return "DLSS-G / NOT INSTALLED";
    }
}

static const char *muiPostValue(sdword row, char *buffer, size_t count)
{
    sdword value = 0;
    switch (row)
    {
        case 0: value = mainChromaticAberrationPercent; break;
        case 1: value = mainMotionBlurPercent; break;
        case 2: value = mainFilmGrainPercent; break;
        case 3: value = mainGodRaysPercent; break;
        default: value = mainBloomPercent; break;
    }
    if (value == 0) return "OFF";
    snprintf(buffer, count, "%d%%", value);
    return buffer;
}

static void muiDrawVideoRow(const rectangle *rect, const char *label,
                            const char *description, const char *value,
                            bool32 hovered, bool32 adjustable)
{
    rectangle stripe = *rect;
    if (hovered)
    {
        muiFill(rect, muiSignalSoft);
    }
    stripe.x1 = stripe.x0 + uiScaleSize(2);
    muiFill(&stripe, hovered ? muiSignal : muiLineSoft);
    muiText(rect->x0 + uiScaleSize(16), rect->y0 + uiScaleSize(8),
            hovered ? muiPaper : muiMuted, label, muiBodyFont);
    muiText(rect->x0 + uiScaleSize(16), rect->y0 + uiScaleSize(27),
            muiDisabled, description, muiBodyFont);
    muiTextRight(rect->x1 - uiScaleSize(adjustable ? 34 : 12),
                 rect->y0 + uiScaleSize(15),
                 hovered ? muiSignal : muiPaper, value, muiBodyFont);
    if (adjustable)
    {
        muiTextRight(rect->x1 - uiScaleSize(12), rect->y0 + uiScaleSize(15),
                     muiMuted, ">", muiBodyFont);
        muiText(rect->x1 - uiScaleSize(28), rect->y0 + uiScaleSize(15),
                muiMuted, "<", muiBodyFont);
    }
    primLine2(rect->x0, rect->y1, rect->x1, rect->y1, muiLineSoft);
}

static void muiDrawVideo(regionhandle region)
{
    static const char *tabNames[MUI_VIDEO_TAB_COUNT] = {
        "DISPLAY", "PATH TRACING", "IMAGE QUALITY", "POST EFFECTS"
    };
    static const char *headings[MUI_VIDEO_TAB_COUNT] = {
        "DISPLAY SYSTEM", "PATH-TRACED LIGHT", "IMAGE PIPELINE", "LENS & ATMOSPHERE"
    };
    static const char *subheads[MUI_VIDEO_TAB_COUNT] = {
        "Window behavior, timing and interface density",
        "Multi-bounce DXR integration and material response",
        "Full-scene reconstruction, native UI and final presentation",
        "Optional scene-only finishing controls; interface remains untouched"
    };
    static const char *labels[MUI_VIDEO_TAB_COUNT][MUI_ROW_COUNT] = {
        {"DISPLAY MODE", "OUTPUT RESOLUTION", "REFRESH RATE", "FRAME LIMIT", "INTERFACE SCALE"},
        {"PATH TRACING", "LIGHT BOUNCES", "SAMPLES", "GENERATED NORMALS", "FX EMISSION"},
        {"ANTI-ALIASING / UPSCALING", "BRIGHTNESS", "EFFECT DENSITY", "EFFECT BUDGET", "FRAME GENERATION"},
        {"CHROMATIC ABERRATION", "MOTION BLUR", "LUMINANCE FILM GRAIN", "GOD RAYS", "BLOOM"}
    };
    static const char *descriptions[MUI_VIDEO_TAB_COUNT][MUI_ROW_COUNT] = {
        {"Default: desktop-matched borderless", "Staged for the next launch", "Auto follows the active display", "Zero removes the limiter", "Staged; scales from resolution automatically"},
        {"DXR scene traversal and accumulated lighting", "Diffuse radiance depth", "New stochastic paths each frame", "LIF-derived micro-surface response", "Weapons, engines, beams, blasts and nav lights"},
        {"Auto, native TAA, DLAA/DLSS, FSR or XeSS-SR full-scene reconstruction", "Final output transfer", "Backgrounds, trails and impact complexity", "Maximum simultaneous effects", "Disabled until real vendor frame generation is installed"},
        {"Radial RGB lens separation", "DXR motion-vector blur; default off", "Response-controlled midtone grain; protected blacks/highlights", "Driven by luminous mission-background regions", "Soft bright-scene glow; 0-400%, default 100%"}
    };
    ModernUILayout layout;
    rectangle marker;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    char value[64];
    const char *valueText;
    bool32 adjustable;

    (void)region;
    muiEnsureFonts();
    muiBuildLayout(&layout);
    muiDrawBackdrop(MUI_ART_SYSTEMS);
    muiFill(&layout.canvas, colRGBA(4, 7, 10, 122));
    muiFill(&layout.rail, muiSurface);
    muiFill(&layout.content, muiSurface);
    if (layout.preview.x1 > layout.preview.x0)
    {
        muiFill(&layout.preview, colRGBA(11, 20, 27, 226));
    }

    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(18), muiSignal,
            "HOMEWORLD / MODERN", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(52), muiPaper,
            "VIDEO", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(76), muiMuted,
            "RENDERING & DISPLAY", muiBodyFont);

    for (index = 0; index < MUI_VIDEO_TAB_COUNT; ++index)
    {
        bool32 selected = index == muiVideo.tab;
        bool32 hovered = muiPointIn(&layout.tabs[index], mouseX, mouseY);
        if (selected || hovered)
        {
            muiFill(&layout.tabs[index], selected ? muiSignalSoft : muiSurfaceRaised);
        }
        if (selected)
        {
            marker = layout.tabs[index];
            marker.x1 = marker.x0 + uiScaleSize(3);
            muiFill(&marker, muiSignal);
        }
        muiText(layout.tabs[index].x0 + uiScaleSize(13),
                layout.tabs[index].y0 + uiScaleSize(11),
                selected ? muiPaper : muiMuted, tabNames[index], muiBodyFont);
    }

    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(22), muiSignal,
            "CONFIGURATION / 01", muiBodyFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(52), muiPaper,
            headings[muiVideo.tab], muiHeadingFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(82), muiMuted,
            subheads[muiVideo.tab], muiBodyFont);

    for (index = 0; index < MUI_ROW_COUNT; ++index)
    {
        bool32 hovered = muiPointIn(&layout.rows[index], mouseX, mouseY);
        adjustable = TRUE;
        if (muiVideo.tab == 2 && index == 4)
            adjustable = FALSE;
        if (muiVideo.tab == 0)
            valueText = muiDisplayValue(index, value, sizeof(value));
        else if (muiVideo.tab == 1)
            valueText = muiPathValue(index, value, sizeof(value));
        else if (muiVideo.tab == 2)
            valueText = muiImageValue(index, value, sizeof(value));
        else
            valueText = muiPostValue(index, value, sizeof(value));
        muiDrawVideoRow(&layout.rows[index], labels[muiVideo.tab][index],
                        descriptions[muiVideo.tab][index], valueText,
                        hovered, adjustable);
    }

    muiDrawButtonRect(&layout.cancel, "CANCEL", FALSE,
                      muiPointIn(&layout.cancel, mouseX, mouseY));
    muiDrawButtonRect(&layout.apply, "APPLY", TRUE,
                      muiPointIn(&layout.apply, mouseX, mouseY));

    if (layout.preview.x1 > layout.preview.x0)
    {
        sdword x = layout.preview.x0 + uiScaleSize(18);
        sdword y = layout.preview.y0 + uiScaleSize(24);
        muiText(x, y, muiSignal, "PIPELINE", muiBodyFont);
        muiText(x, y + uiScaleSize(34), muiPaper, "WORLD", muiHeadingFont);
        muiText(x, y + uiScaleSize(62), muiMuted, "Raster color + alpha", muiBodyFont);
        muiText(x, y + uiScaleSize(92), muiCool, "DXR PATH INTEGRATION", muiBodyFont);
        muiText(x, y + uiScaleSize(120), muiMuted, "Depth / motion / emissive", muiBodyFont);
        muiText(x, y + uiScaleSize(150), muiCool, "SCENE AA / UPSCALING", muiBodyFont);
        muiText(x, y + uiScaleSize(178), muiMuted, "Auto, TAA, DLSS/DLAA, FSR or XeSS", muiBodyFont);
        muiText(x, y + uiScaleSize(208), muiSignal, "UI COMPOSITE", muiBodyFont);
        muiText(x, y + uiScaleSize(236), muiMuted, "After temporal processing", muiBodyFont);
        marker = muiRect(x, y + uiScaleSize(282),
                         layout.preview.x1 - uiScaleSize(18),
                         y + uiScaleSize(283));
        muiFill(&marker, muiLine);
        muiText(x, y + uiScaleSize(300), muiPaper,
                mainRaytracing ? "PATH TRACING REQUESTED" : "PATH TRACING DISABLED",
                muiBodyFont);
        muiText(x, y + uiScaleSize(326),
                mainDlaa ? muiCool : muiDisabled,
                muiImageValue(0, value, sizeof(value)), muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(68), muiMuted,
                "DISPLAY CHANGES", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(44), muiPaper,
                "TAKE EFFECT ON NEXT LAUNCH", muiBodyFont);
    }
}

static udword muiVideoProcess(regionhandle region, smemsize ID,
                              udword event, udword data)
{
    ModernUILayout layout;
    sdword index;
    sdword direction;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)region;
    (void)data;

    /* Dirty immediately so the pressed state is visible before release. */
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_KeyDown && ID == ESCKEY)
    {
        muiExitVideo(FALSE);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft && event != RPE_WheelUp &&
        event != RPE_WheelDown)
    {
        return 0;
    }

    muiBuildLayout(&layout);
    if (event == RPE_ReleaseLeft)
    {
        if (muiPointIn(&layout.apply, mouseX, mouseY))
        {
            muiExitVideo(TRUE);
            return RPR_Redraw;
        }
        if (muiPointIn(&layout.cancel, mouseX, mouseY))
        {
            muiExitVideo(FALSE);
            return RPR_Redraw;
        }
        for (index = 0; index < MUI_VIDEO_TAB_COUNT; ++index)
        {
            if (muiPointIn(&layout.tabs[index], mouseX, mouseY))
            {
                muiVideo.tab = index;
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
    }

    direction = (event == RPE_WheelUp) ? 1 : -1;
    for (index = 0; index < MUI_ROW_COUNT; ++index)
    {
        if (muiPointIn(&layout.rows[index], mouseX, mouseY))
        {
            if (event == RPE_ReleaseLeft)
            {
                direction = mouseX >= (layout.rows[index].x0 +
                    layout.rows[index].x1) / 2 ? 1 : -1;
            }
            if (muiVideo.tab == 0) muiAdjustDisplay(index, direction);
            else if (muiVideo.tab == 1) muiAdjustPath(index, direction);
            else if (muiVideo.tab == 2) muiAdjustImage(index, direction);
            else muiAdjustPost(index, direction);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    return 0;
}

static const char *muiGameplayValue(sdword row, char *buffer, size_t count)
{
    switch (row)
    {
        case 0:
            snprintf(buffer, count, "%d%%", opMouseSens);
            return buffer;
        case 1: return opUnlimitedFuel ? "ON" : "OFF";
        case 2: return opPauseOrders ? "ON" : "OFF";
        case 3: return opShipRecoil ? "ON" : "OFF";
        case 4:
            snprintf(buffer, count, "%d%%", mouseCursorScalePercent);
            return buffer;
        case 5:
            snprintf(buffer, count, "%ux",
                     (unsigned int)muiClamp((sdword)opResourceMultiplier, 1, 4));
            return buffer;
        default: return "";
    }
}

static void muiGameplayFitLayout(ModernUILayout *layout)
{
    sdword rowTop = layout->content.y0 + uiScaleSize(132);
    sdword bottom = layout->apply.y0 - uiScaleSize(8);
    sdword gap = uiScaleSize(4);
    sdword specialTarget = uiScaleSize(52);
    sdword available = bottom - rowTop;
    sdword rowStep = (available - specialTarget * 2 - gap) / MUI_GAMEPLAY_ROW_COUNT;
    sdword index;

    rowStep = muiClamp(rowStep, uiScaleSize(40), uiScaleSize(68));
    for (index = 0; index < MUI_GAMEPLAY_ROW_COUNT; ++index)
    {
        layout->rows[index] = muiRect(
            layout->content.x0 + uiScaleSize(22),
            rowTop + index * rowStep,
            layout->content.x1 - uiScaleSize(22),
            rowTop + (index + 1) * rowStep - gap);
    }
}

static rectangle muiGameplaySpecialRect(const ModernUILayout *layout, sdword index)
{
    sdword gap = uiScaleSize(4);
    sdword top = layout->rows[MUI_GAMEPLAY_ROW_COUNT - 1].y1 + gap;
    sdword limit = layout->apply.y0 - uiScaleSize(8);
    sdword available = limit - top;
    sdword height = (available - gap) / 2;

    if (height > uiScaleSize(58)) height = uiScaleSize(58);
    if (height < uiScaleSize(30)) height = uiScaleSize(30);

    return muiRect(layout->rows[4].x0,
                   top + index * (height + gap),
                   layout->rows[4].x1,
                   top + index * (height + gap) + height);
}

static rectangle muiGameplaySuperSalvagersRect(const ModernUILayout *layout)
{
    return muiGameplaySpecialRect(layout, 0);
}

static rectangle muiGameplayCaptureBuildAllRect(const ModernUILayout *layout)
{
    return muiGameplaySpecialRect(layout, 1);
}

static rectangle muiGameplayCursorSliderRect(const rectangle *row)
{
    sdword width = row->x1 - row->x0;
    sdword trackLeft = row->x0 + width * 58 / 100;
    sdword trackRight = row->x1 - uiScaleSize(74);
    sdword centreY = row->y0 + (row->y1 - row->y0) * 68 / 100;
    if (trackRight <= trackLeft + uiScaleSize(40))
        trackLeft = row->x0 + width * 46 / 100;
    return muiRect(trackLeft, centreY - uiScaleSize(5),
                   trackRight, centreY + uiScaleSize(5));
}

static void muiGameplayDrawCursorSlider(const rectangle *row, bool32 hovered)
{
    rectangle hit = muiGameplayCursorSliderRect(row);
    rectangle track = hit;
    rectangle fill;
    rectangle thumb;
    sdword range = hit.x1 - hit.x0;
    sdword x;
    real32 t = (real32)(mouseCursorScalePercent - 25) / 75.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    track.y0 = (hit.y0 + hit.y1) / 2 - uiScaleSize(1);
    track.y1 = track.y0 + uiScaleSize(2);
    muiFill(&track, muiLineSoft);
    fill = track;
    fill.x1 = fill.x0 + (sdword)((real32)range * t + 0.5f);
    if (fill.x1 > fill.x0) muiFill(&fill, hovered ? muiSignal : muiCool);
    x = hit.x0 + (sdword)((real32)range * t + 0.5f);
    thumb = muiRect(x - uiScaleSize(4), hit.y0,
                    x + uiScaleSize(4), hit.y1);
    muiFill(&thumb, hovered ? muiSignal : muiPaper);
    primRectOutline2(&thumb, 1, hovered ? muiPaper : muiLine);
}

static void muiGameplaySetCursorScaleFromX(const rectangle *row, sdword mouseX)
{
    rectangle slider = muiGameplayCursorSliderRect(row);
    sdword width = slider.x1 - slider.x0;
    sdword value;
    if (width <= 0) return;
    if (mouseX < slider.x0) mouseX = slider.x0;
    if (mouseX > slider.x1) mouseX = slider.x1;
    value = 25 + (mouseX - slider.x0) * 75 / width;
    /* Keep the UI predictable and readable: snap the continuous slider to
       five-percent increments while retaining direct pointer positioning. */
    value = ((value + 2) / 5) * 5;
    mouseCursorScalePercent = muiClamp(value, 25, 100);
}

static void muiGameplayApplyLive(void)
{
    cameraSensitivitySet(opMouseSens);
    /* The modern Info manager follows gameplay/cutscene context and is not a
       user-hidden legacy overlay. */
    opInfoOverlayVar = 1;
    ioEnable();
}

static void muiAdjustGameplay(sdword row, sdword direction)
{
    switch (row)
    {
        case 0:
            opMouseSens = muiClamp(opMouseSens + direction * 5, 0, 100);
            break;
        case 1: opUnlimitedFuel = !opUnlimitedFuel; break;
        case 2: opPauseOrders = !opPauseOrders; break;
        case 3: opShipRecoil = !opShipRecoil; break;
        case 4:
            mouseCursorScalePercent = muiClamp(
                mouseCursorScalePercent + direction * 5, 25, 100);
            break;
        case 5:
            opResourceMultiplier = (udword)muiClamp(
                (sdword)opResourceMultiplier + direction, 1, 4);
            break;
        default: return;
    }
    muiGameplayApplyLive();
}

static void muiExitGameplay(bool32 apply)
{
    bool32 inGame = muiGameplay.inGame;
    if (apply)
    {
        muiGameplayApplyLive();
        opOptionsSaveSettings();
    }
    else
    {
        opMouseSens = muiGameplay.savedMouseSensitivity;
        mouseCursorScalePercent = muiGameplay.savedCursorScalePercent;
        opUnlimitedFuel = muiGameplay.savedUnlimitedFuel;
        opPauseOrders = muiGameplay.savedPauseOrders;
        opShipRecoil = muiGameplay.savedShipRecoil;
        opSuperSalvagers = muiGameplay.savedSuperSalvagers;
        opCaptureBuildAll = muiGameplay.savedCaptureBuildAll;
        opResourceMultiplier = muiGameplay.savedResourceMultiplier;
        muiGameplayApplyLive();
    }
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, inGame ? "Game_options" : "Main_game_screen");
}

static void muiDrawGameplay(regionhandle region)
{
    static const char *labels[MUI_GAMEPLAY_ROW_COUNT] = {
        "CAMERA SENSITIVITY", "UNLIMITED STRIKE-CRAFT FUEL",
        "ISSUE ORDERS WHILE PAUSED", "SHIP WEAPON RECOIL",
        "CURSOR SCALE", "RESOURCE MULTIPLIER"
    };
    static const char *descriptions[MUI_GAMEPLAY_ROW_COUNT] = {
        "Continuous camera response; five-percent steps",
        "Single-player only; flight orders and docking behavior are unchanged",
        "Accept tactical commands while simulation time is paused",
        "Physical impulse from firing heavy weapons",
        "Pointer size; 25% through 100%, live preview",
        "1x-4x deposited RU yield and collector harvest rate; single-player only"
    };
    ModernUILayout layout;
    rectangle marker;
    rectangle superRow;
    rectangle captureRow;
    bool32 superHovered;
    bool32 captureHovered;
    bool32 resourceHovered;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    char value[64];
    (void)region;

    muiEnsureFonts();
    muiBuildLayout(&layout);
    muiGameplayFitLayout(&layout);
    muiDrawBackdrop(MUI_ART_SYSTEMS);
    muiFill(&layout.canvas, colRGBA(4, 7, 10, 122));
    muiFill(&layout.rail, muiSurface);
    muiFill(&layout.content, muiSurface);
    if (layout.preview.x1 > layout.preview.x0)
        muiFill(&layout.preview, colRGBA(11, 20, 27, 226));

    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(18), muiSignal,
            "HOMEWORLD / MODERN", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(56), muiPaper,
            "GAMEPLAY", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(84), muiMuted,
            "COMMAND BEHAVIOR", muiBodyFont);
    marker = muiRect(layout.rail.x0 + uiScaleSize(14),
                     layout.rail.y0 + uiScaleSize(122),
                     layout.rail.x1 - uiScaleSize(14),
                     layout.rail.y0 + uiScaleSize(166));
    muiFill(&marker, muiSignalSoft);
    marker.x1 = marker.x0 + uiScaleSize(3);
    muiFill(&marker, muiSignal);
    muiText(layout.rail.x0 + uiScaleSize(28),
            layout.rail.y0 + uiScaleSize(137), muiPaper,
            "ACCESS & CONTROL", muiBodyFont);

    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(22), muiSignal,
            "CONFIGURATION / 02", muiBodyFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(52), muiPaper,
            "FLEET CONTROL", muiHeadingFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(82), muiMuted,
            "Simulation preferences with multiplayer-safe boundaries",
            muiBodyFont);

    for (index = 0; index < MUI_GAMEPLAY_ROW_COUNT; ++index)
    {
        bool32 hovered = muiPointIn(&layout.rows[index], mouseX, mouseY);
        muiDrawVideoRow(&layout.rows[index], labels[index],
                        descriptions[index],
                        muiGameplayValue(index, value, sizeof(value)),
                        hovered, index != 4);
        if (index == 4)
            muiGameplayDrawCursorSlider(&layout.rows[index], hovered);
    }

    superRow = muiGameplaySuperSalvagersRect(&layout);
    superHovered = muiPointIn(&superRow, mouseX, mouseY);
    muiDrawVideoRow(&superRow, "SUPER SALVAGERS",
                    "2x mobility / agility / speed / braking; 3x effective health",
                    opSuperSalvagers ? "ON" : "OFF",
                    superHovered, TRUE);

    resourceHovered = muiPointIn(&layout.rows[5], mouseX, mouseY);
    captureRow = muiGameplayCaptureBuildAllRect(&layout);
    captureHovered = muiPointIn(&captureRow, mouseX, mouseY);
    muiDrawVideoRow(&captureRow, "CAPTURE / BUILD ALL",
                    "Capture exceptional production motherships; use their native build rosters",
                    opCaptureBuildAll ? "ON" : "OFF",
                    captureHovered, TRUE);

    muiDrawButtonRect(&layout.cancel, "CANCEL", FALSE,
                      muiPointIn(&layout.cancel, mouseX, mouseY));
    muiDrawButtonRect(&layout.apply, "APPLY", TRUE,
                      muiPointIn(&layout.apply, mouseX, mouseY));

    if (layout.preview.x1 > layout.preview.x0)
    {
        sdword x = layout.preview.x0 + uiScaleSize(18);
        sdword y = layout.preview.y0 + uiScaleSize(24);
        muiText(x, y, muiSignal, "SIMULATION CONTRACT", muiBodyFont);
        if (resourceHovered)
        {
            muiText(x, y + uiScaleSize(38), muiPaper,
                    "RESOURCE MULTIPLIER", muiHeadingFont);
            muiText(x, y + uiScaleSize(70), muiMuted,
                    "Deposited collector cargo pays out at 1x-4x RU.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(98), muiMuted,
                    "Collector harvest / fill rate rises by the same factor.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(142), muiCool,
                    "Authored asteroid / cloud resource values stay unchanged.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(168), muiCool,
                    "Single-player resource simulation only.", muiBodyFont);
        }
        else if (captureHovered)
        {
            muiText(x, y + uiScaleSize(38), muiPaper,
                    "CAPTURE / BUILD ALL", muiHeadingFont);
            muiText(x, y + uiScaleSize(70), muiMuted,
                    "Capture exceptional enemy production motherships.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(98), muiMuted,
                    "Turanic / Kadeshi factories keep their native rosters.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(142), muiCool,
                    "Captured hulls remain yours while this is OFF.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(168), muiCool,
                    "Re-enable to restore their Build Manager access.",
                    muiBodyFont);
        }
        else if (superHovered)
        {
            muiText(x, y + uiScaleSize(38), muiPaper,
                    "SUPER SALVAGERS", muiHeadingFont);
            muiText(x, y + uiScaleSize(70), muiMuted,
                    "2x thrust, reverse thrust, steering and top speed.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(98), muiMuted,
                    "3x effective durability; single-player only.",
                    muiBodyFont);
            muiText(x, y + uiScaleSize(142), muiCool,
                    "C'mon - we've all modded them", muiBodyFont);
            muiText(x, y + uiScaleSize(168), muiCool,
                    "to be like this", muiBodyFont);
        }
        else
        {
            muiText(x, y + uiScaleSize(38), muiPaper,
                    "UNLIMITED FUEL", muiHeadingFont);
            muiText(x, y + uiScaleSize(70), muiMuted,
                    "Refills fuel before the legacy burn stage.", muiBodyFont);
            muiText(x, y + uiScaleSize(98), muiMuted,
                    "No AI, pass-distance or docking edits.", muiBodyFont);
            muiText(x, y + uiScaleSize(142), muiCool,
                    "CURSOR SCALE", muiBodyFont);
            muiText(x, y + uiScaleSize(170), muiMuted,
                    "25%-100%; applied immediately and saved.", muiBodyFont);
        }
        muiText(x, layout.preview.y1 - uiScaleSize(54), muiSignal,
                "OFF BY DEFAULT", muiBodyFont);
    }
}

static udword muiGameplayProcess(regionhandle region, smemsize ID,
                                 udword event, udword data)
{
    ModernUILayout layout;
    rectangle superRow;
    rectangle captureRow;
    sdword index;
    sdword direction;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)ID;
    (void)data;
    muiBuildLayout(&layout);
    muiGameplayFitLayout(&layout);
    if ((event == RPE_PressLeft || event == RPE_HoldLeft ||
         event == RPE_ReleaseLeft) &&
        muiPointIn(&layout.rows[4], mouseX, mouseY))
    {
        rectangle slider = muiGameplayCursorSliderRect(&layout.rows[4]);
        rectangle generous = slider;
        generous.y0 = layout.rows[4].y0;
        generous.y1 = layout.rows[4].y1;
        if (muiPointIn(&generous, mouseX, mouseY))
        {
            muiGameplaySetCursorScaleFromX(&layout.rows[4], mouseX);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_KeyDown && ID == ESCKEY)
    {
        muiExitGameplay(FALSE);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft && event != RPE_WheelUp &&
        event != RPE_WheelDown)
        return 0;

    if (event == RPE_ReleaseLeft && muiPointIn(&layout.apply, mouseX, mouseY))
    {
        muiExitGameplay(TRUE);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft && muiPointIn(&layout.cancel, mouseX, mouseY))
    {
        muiExitGameplay(FALSE);
        return RPR_Redraw;
    }
    superRow = muiGameplaySuperSalvagersRect(&layout);
    if ((event == RPE_ReleaseLeft || event == RPE_WheelUp ||
         event == RPE_WheelDown) &&
        muiPointIn(&superRow, mouseX, mouseY))
    {
        opSuperSalvagers = !opSuperSalvagers;
        muiGameplayApplyLive();
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    captureRow = muiGameplayCaptureBuildAllRect(&layout);
    if ((event == RPE_ReleaseLeft || event == RPE_WheelUp ||
         event == RPE_WheelDown) &&
        muiPointIn(&captureRow, mouseX, mouseY))
    {
        opCaptureBuildAll = !opCaptureBuildAll;
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    direction = event == RPE_WheelUp ? 1 : -1;
    for (index = 0; index < MUI_GAMEPLAY_ROW_COUNT; ++index)
    {
        if (muiPointIn(&layout.rows[index], mouseX, mouseY))
        {
            if (event == RPE_ReleaseLeft)
                direction = mouseX >= (layout.rows[index].x0 +
                                        layout.rows[index].x1) / 2 ? 1 : -1;
            muiAdjustGameplay(index, direction);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    return 0;
}


static const char *muiAudioValue(sdword tab, sdword row,
                                 char *buffer, size_t count)
{
    sdword minVoices;
    sdword maxVoices;
    if (tab == 0)
    {
        switch (row)
        {
            case 0: snprintf(buffer, count, "%d%%", opMusicVol); return buffer;
            case 1: snprintf(buffer, count, "%d%%", opSpeechVol); return buffer;
            case 2: snprintf(buffer, count, "%d%%", opSFXVol); return buffer;
            case 3:
                soundGetVoiceLimits(&minVoices, &maxVoices);
                snprintf(buffer, count, "%d", opNumChannels + minVoices);
                return buffer;
            default: return opAutoChannel == SOUND_MODE_AUTO ? "ON" : "OFF";
        }
    }
    switch (row)
    {
        case 0:
            if (opSoundQuality == SOUND_MODE_LOW) return "LOW";
            if (opSoundQuality == SOUND_MODE_AUTO) return "AUTO";
            return "HIGH";
        case 1:
            if (opSpeakerSetting == 0) return "MULTIMEDIA";
            if (opSpeakerSetting == 1) return "STEREO / SURROUND";
            if (opSpeakerSetting == 2) return "HEADPHONES";
            return "CUSTOM";
        case 2: return opVoiceComm ? "ON" : "OFF";
        case 3: return opVoiceStat ? "ON" : "OFF";
        default: return opVoiceChat ? "ON" : "OFF";
    }
}

static void muiAdjustAudio(sdword row, sdword direction)
{
    sdword minVoices;
    sdword maxVoices;
    if (muiAudio.tab == 0)
    {
        switch (row)
        {
            case 0: opMusicVol = muiClamp(opMusicVol + direction * 5, 0, 100); break;
            case 1: opSpeechVol = muiClamp(opSpeechVol + direction * 5, 0, 100); break;
            case 2: opSFXVol = muiClamp(opSFXVol + direction * 5, 0, 100); break;
            case 3:
                soundGetVoiceLimits(&minVoices, &maxVoices);
                opNumChannels = muiClamp(opNumChannels + direction, 0,
                                         maxVoices - minVoices);
                break;
            case 4:
                opAutoChannel = opAutoChannel == SOUND_MODE_AUTO ?
                                SOUND_MODE_NORM : SOUND_MODE_AUTO;
                break;
            default: return;
        }
    }
    else
    {
        switch (row)
        {
            case 0:
                opSoundQuality += direction;
                if (opSoundQuality < SOUND_MODE_NORM) opSoundQuality = SOUND_MODE_LOW;
                if (opSoundQuality > SOUND_MODE_LOW) opSoundQuality = SOUND_MODE_NORM;
                break;
            case 1:
                opSpeakerSetting += direction;
                if (opSpeakerSetting < 0) opSpeakerSetting = 3;
                if (opSpeakerSetting > 3) opSpeakerSetting = 0;
                break;
            case 2: opVoiceComm = !opVoiceComm; break;
            case 3: opVoiceStat = !opVoiceStat; break;
            case 4: opVoiceChat = !opVoiceChat; break;
            default: return;
        }
    }
    opUpdateAudioSettings();
}

static void muiExitAudio(bool32 apply)
{
    bool32 inGame = muiAudio.inGame;
    if (apply)
    {
        opUpdateAudioSettings();
        opOptionsSaveSettings();
    }
    else
    {
        opRestoreSavedSettings();
    }
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, inGame ? "Game_options" : "Main_game_screen");
}

static void muiDrawAudio(regionhandle region)
{
    static const char *tabNames[MUI_AUDIO_TAB_COUNT] = {
        "MIXER", "OUTPUT / VOICE"
    };
    static const char *headings[MUI_AUDIO_TAB_COUNT] = {
        "AUDIO MIXER", "OUTPUT & VOICE"
    };
    static const char *subheads[MUI_AUDIO_TAB_COUNT] = {
        "Live volume, channel capacity and voice allocation",
        "Playback quality, speaker profile and fleet speech"
    };
    static const char *labels[MUI_AUDIO_TAB_COUNT][MUI_ROW_COUNT] = {
        {"MUSIC VOLUME", "SPEECH VOLUME", "SOUND EFFECTS", "VOICE CHANNELS", "AUTO CHANNEL MANAGEMENT"},
        {"SOUND QUALITY", "SPEAKER PROFILE", "VOICE COMMANDS", "VOICE STATUS", "VOICE CHATTER"}
    };
    static const char *descriptions[MUI_AUDIO_TAB_COUNT][MUI_ROW_COUNT] = {
        {"Music master level", "Fleet and narrative speech level", "Weapons, engines and interface effects", "Maximum simultaneous voices", "Let the mixer adapt voice pressure automatically"},
        {"High, automatic fallback or low processing", "EQ profile for the active playback system", "Command acknowledgements and order speech", "Unit status and operational callouts", "Ambient fleet chatter"}
    };
    ModernUILayout layout;
    rectangle marker;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    char value[64];
    (void)region;

    muiEnsureFonts();
    muiBuildLayout(&layout);
    muiDrawBackdrop(MUI_ART_SYSTEMS);
    muiFill(&layout.canvas, colRGBA(4, 7, 10, 122));
    muiFill(&layout.rail, muiSurface);
    muiFill(&layout.content, muiSurface);
    if (layout.preview.x1 > layout.preview.x0)
        muiFill(&layout.preview, colRGBA(11, 20, 27, 226));

    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(18), muiSignal,
            "HOMEWORLD / MODERN", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(56), muiPaper,
            "AUDIO", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(84), muiMuted,
            "MIXER & VOICE", muiBodyFont);

    for (index = 0; index < MUI_AUDIO_TAB_COUNT; ++index)
    {
        bool32 selected = index == muiAudio.tab;
        bool32 hovered = muiPointIn(&layout.tabs[index], mouseX, mouseY);
        if (selected || hovered)
            muiFill(&layout.tabs[index], selected ? muiSignalSoft : muiSurfaceRaised);
        if (selected)
        {
            marker = layout.tabs[index];
            marker.x1 = marker.x0 + uiScaleSize(3);
            muiFill(&marker, muiSignal);
        }
        muiText(layout.tabs[index].x0 + uiScaleSize(13),
                layout.tabs[index].y0 + uiScaleSize(11),
                selected ? muiPaper : muiMuted, tabNames[index], muiBodyFont);
    }

    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(22), muiSignal,
            "CONFIGURATION / 03", muiBodyFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(52), muiPaper,
            headings[muiAudio.tab], muiHeadingFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(82), muiMuted,
            subheads[muiAudio.tab], muiBodyFont);

    for (index = 0; index < MUI_ROW_COUNT; ++index)
    {
        bool32 hovered = muiPointIn(&layout.rows[index], mouseX, mouseY);
        muiDrawVideoRow(&layout.rows[index], labels[muiAudio.tab][index],
                        descriptions[muiAudio.tab][index],
                        muiAudioValue(muiAudio.tab, index, value, sizeof(value)),
                        hovered, TRUE);
    }

    muiDrawButtonRect(&layout.cancel, "CANCEL", FALSE,
                      muiPointIn(&layout.cancel, mouseX, mouseY));
    muiDrawButtonRect(&layout.apply, "APPLY", TRUE,
                      muiPointIn(&layout.apply, mouseX, mouseY));

    if (layout.preview.x1 > layout.preview.x0)
    {
        sdword x = layout.preview.x0 + uiScaleSize(18);
        sdword y = layout.preview.y0 + uiScaleSize(24);
        muiText(x, y, muiSignal, "AUDIO PIPELINE", muiBodyFont);
        muiText(x, y + uiScaleSize(38), muiPaper,
                muiAudio.tab == 0 ? "LIVE MIX" : "PLAYBACK", muiHeadingFont);
        muiText(x, y + uiScaleSize(72), muiCool,
                "NO LEGACY SUB-MENU", muiBodyFont);
        muiText(x, y + uiScaleSize(100), muiMuted,
                "All core audio controls live here.", muiBodyFont);
        muiText(x, y + uiScaleSize(136), muiMuted,
                "Changes preview immediately.", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(68), muiMuted,
                "CANCEL RESTORES", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(44), muiPaper,
                "THE ENTRY SNAPSHOT", muiBodyFont);
    }
}

static udword muiAudioProcess(regionhandle region, smemsize ID,
                              udword event, udword data)
{
    ModernUILayout layout;
    sdword index;
    sdword direction;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)ID;
    (void)data;

    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_KeyDown && ID == ESCKEY)
    {
        muiExitAudio(FALSE);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft && event != RPE_WheelUp &&
        event != RPE_WheelDown)
        return 0;

    muiBuildLayout(&layout);
    if (event == RPE_ReleaseLeft)
    {
        if (muiPointIn(&layout.apply, mouseX, mouseY))
        {
            muiExitAudio(TRUE);
            return RPR_Redraw;
        }
        if (muiPointIn(&layout.cancel, mouseX, mouseY))
        {
            muiExitAudio(FALSE);
            return RPR_Redraw;
        }
        for (index = 0; index < MUI_AUDIO_TAB_COUNT; ++index)
        {
            if (muiPointIn(&layout.tabs[index], mouseX, mouseY))
            {
                muiAudio.tab = index;
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
    }

    direction = event == RPE_WheelUp ? 1 : -1;
    for (index = 0; index < MUI_ROW_COUNT; ++index)
    {
        if (muiPointIn(&layout.rows[index], mouseX, mouseY))
        {
            if (event == RPE_ReleaseLeft)
                direction = mouseX >= (layout.rows[index].x0 +
                                        layout.rows[index].x1) / 2 ? 1 : -1;
            muiAdjustAudio(index, direction);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    return 0;
}

static rectangle muiKeysListRect(const ModernUILayout *layout)
{
    return muiRect(layout->content.x0 + uiScaleSize(22),
                   layout->content.y0 + uiScaleSize(132),
                   layout->content.x1 - uiScaleSize(22),
                   layout->cancel.y0 - uiScaleSize(72));
}

static void muiKeysActionRects(const ModernUILayout *layout,
                               rectangle *reset, rectangle *rebind)
{
    rectangle list = muiKeysListRect(layout);
    sdword width = muiClamp((layout->content.x1 - layout->content.x0) / 5,
                            uiScaleSize(116), uiScaleSize(180));
    sdword y0 = list.y1 + uiScaleSize(12);
    sdword y1 = layout->cancel.y0 - uiScaleSize(12);
    *reset = muiRect(list.x0, y0, list.x0 + width, y1);
    *rebind = muiRect(reset->x1 + uiScaleSize(12), y0,
                      reset->x1 + uiScaleSize(12) + width, y1);
}

static sdword muiKeysVisibleRows(const rectangle *list)
{
    sdword usable = list->y1 - list->y0 - uiScaleSize(26);
    sdword rows = usable / uiScaleSize(28);
    return muiClamp(rows, 5, MUI_KEYS_VISIBLE_ROWS);
}

static rectangle muiKeysRowRect(const rectangle *list, sdword visibleRow,
                                sdword visibleRows)
{
    sdword header = uiScaleSize(26);
    sdword rowHeight = (list->y1 - list->y0 - header) / visibleRows;
    return muiRect(list->x0, list->y0 + header + visibleRow * rowHeight,
                   list->x1, list->y0 + header + (visibleRow + 1) * rowHeight);
}

static void muiKeysKeepSelectionVisible(sdword visibleRows)
{
    sdword count = kbModernCommandCount();
    sdword maxScroll = count > visibleRows ? count - visibleRows : 0;
    if (muiKeys.selected < 0) muiKeys.selected = 0;
    if (muiKeys.selected >= count) muiKeys.selected = count - 1;
    if (muiKeys.selected < muiKeys.scroll)
        muiKeys.scroll = muiKeys.selected;
    if (muiKeys.selected >= muiKeys.scroll + visibleRows)
        muiKeys.scroll = muiKeys.selected - visibleRows + 1;
    muiKeys.scroll = muiClamp(muiKeys.scroll, 0, maxScroll);
}

static void muiKeysClampScroll(sdword visibleRows)
{
    sdword count = kbModernCommandCount();
    sdword maxScroll = count > visibleRows ? count - visibleRows : 0;
    if (count <= 0)
    {
        muiKeys.selected = 0;
        muiKeys.scroll = 0;
        return;
    }
    if (muiKeys.selected < 0) muiKeys.selected = 0;
    if (muiKeys.selected >= count) muiKeys.selected = count - 1;
    muiKeys.scroll = muiClamp(muiKeys.scroll, 0, maxScroll);
}

static void muiKeysSetCapture(bool32 active)
{
    muiKeys.capturing = active;
    if (muiKeys.inputRegion != NULL)
    {
        if (active)
            bitSet(muiKeys.inputRegion->status, RSF_KeyCapture);
        else
            bitClear(muiKeys.inputRegion->status, RSF_KeyCapture);
    }
}

static void muiExitKeys(bool32 apply)
{
    bool32 inGame = muiKeys.inGame;
    if (apply)
    {
        kbSaveSettings();
        opOptionsSaveSettings();
    }
    else
    {
        kbRestoreSavedSettings();
    }
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, inGame ? "Game_options" : "Main_game_screen");
}

static void muiDrawKeys(regionhandle region)
{
    ModernUILayout layout;
    rectangle list;
    rectangle reset;
    rectangle rebind;
    rectangle header;
    rectangle marker;
    sdword count;
    sdword visible;
    sdword visibleRows;
    sdword maxScroll;
    sdword row;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    udword now = SDL_GetTicks();
    (void)region;

    muiEnsureFonts();
    muiBuildLayout(&layout);
    list = muiKeysListRect(&layout);
    visibleRows = muiKeysVisibleRows(&list);
    /* Drawing must never drag the scroll position back to the selected row.
       The old implementation did exactly that every frame, which made mouse
       wheel scrolling appear broken whenever the selected command was above
       the viewport.  Keyboard navigation still calls KeepSelectionVisible;
       ordinary drawing only clamps the independently scrollable viewport. */
    muiKeysClampScroll(visibleRows);
    muiKeysActionRects(&layout, &reset, &rebind);
    count = kbModernCommandCount();
    maxScroll = count > visibleRows ? count - visibleRows : 0;

    muiDrawBackdrop(MUI_ART_SYSTEMS);
    muiFill(&layout.canvas, colRGBA(4, 7, 10, 122));
    muiFill(&layout.rail, muiSurface);
    muiFill(&layout.content, muiSurface);
    if (layout.preview.x1 > layout.preview.x0)
        muiFill(&layout.preview, colRGBA(11, 20, 27, 226));

    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(18), muiSignal,
            "HOMEWORLD / MODERN", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(56), muiPaper,
            "KEY BINDINGS", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(84), muiMuted,
            "COMMAND INPUT", muiBodyFont);
    marker = muiRect(layout.rail.x0 + uiScaleSize(14),
                     layout.rail.y0 + uiScaleSize(122),
                     layout.rail.x1 - uiScaleSize(14),
                     layout.rail.y0 + uiScaleSize(166));
    muiFill(&marker, muiSignalSoft);
    marker.x1 = marker.x0 + uiScaleSize(3);
    muiFill(&marker, muiSignal);
    muiText(layout.rail.x0 + uiScaleSize(28),
            layout.rail.y0 + uiScaleSize(137), muiPaper,
            "COMMAND MAP", muiBodyFont);

    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(22), muiSignal,
            "CONFIGURATION / 04", muiBodyFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(52), muiPaper,
            "KEYBOARD", muiHeadingFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(82), muiMuted,
            "Select a command, choose Rebind, then press the replacement key",
            muiBodyFont);

    muiFill(&list, colRGBA(11, 20, 27, 210));
    primRectOutline2(&list, 1, muiLineSoft);
    header = muiRect(list.x0, list.y0, list.x1, list.y0 + uiScaleSize(26));
    muiFill(&header, colRGBA(20, 42, 45, 220));
    muiText(header.x0 + uiScaleSize(10), header.y0 + uiScaleSize(6),
            muiPaper, "COMMAND", muiCompactFont);
    muiTextRight(header.x1 - uiScaleSize(maxScroll > 0 ? 24 : 10),
                 header.y0 + uiScaleSize(6),
                 muiPaper, "BOUND KEY", muiCompactFont);
    if (maxScroll > 0)
    {
        char rangeText[32];
        sdword endRow = muiKeys.scroll + visibleRows;
        if (endRow > count) endRow = count;
        snprintf(rangeText, sizeof(rangeText), "%d-%d / %d",
                 muiKeys.scroll + 1, endRow, count);
        muiTextCenteredAdaptiveFull(&header, muiCool, rangeText, muiCompactFont,
                                    muiTinyFont, muiTinyFont, uiScaleSize(120));
    }

    visible = count - muiKeys.scroll;
    if (visible > visibleRows) visible = visibleRows;
    for (row = 0; row < visible; ++row)
    {
        sdword command = muiKeys.scroll + row;
        rectangle rr = muiKeysRowRect(&list, row, visibleRows);
        rectangle commandCell;
        rectangle keyCell;
        bool32 selected = command == muiKeys.selected;
        bool32 hovered = muiPointIn(&rr, mouseX, mouseY);
        const char *commandName = kbModernCommandName(command);
        const char *keyName = kbModernKeyName(command);
        fonthandle commandFont;
        fonthandle keyFont;
        if (maxScroll > 0) rr.x1 -= uiScaleSize(16);
        if (selected || hovered)
            muiFill(&rr, selected ? muiSignalSoft : colRGBA(24, 36, 43, 160));
        if (selected)
        {
            rectangle stripe = rr;
            stripe.x1 = stripe.x0 + uiScaleSize(2);
            muiFill(&stripe, muiSignal);
        }
        commandCell = muiRect(rr.x0 + uiScaleSize(8), rr.y0,
                              rr.x0 + (rr.x1 - rr.x0) * 62 / 100 - uiScaleSize(6),
                              rr.y1);
        keyCell = muiRect(rr.x0 + (rr.x1 - rr.x0) * 62 / 100 + uiScaleSize(6),
                          rr.y0, rr.x1 - uiScaleSize(8), rr.y1);
        commandFont = muiBestFitFont(commandName,
                                     commandCell.x1 - commandCell.x0,
                                     muiBodyFont, muiCompactFont, muiTinyFont);
        keyFont = muiBestFitFont(
            muiKeys.capturing && selected ? "PRESS A KEY..." : keyName,
            keyCell.x1 - keyCell.x0,
            muiBodyFont, muiCompactFont, muiTinyFont);
        muiTextLeftFittedCentered(&commandCell,
                                  selected ? muiPaper : muiMuted,
                                  commandName, commandFont, 0);
        muiTextLeftFittedCentered(&keyCell,
                                  muiKeys.capturing && selected ? muiSignal : muiPaper,
                                  muiKeys.capturing && selected ? "PRESS A KEY..." : keyName,
                                  keyFont, 0);
        primLine2(commandCell.x1 + uiScaleSize(5), rr.y0 + uiScaleSize(4),
                  commandCell.x1 + uiScaleSize(5), rr.y1 - uiScaleSize(4),
                  muiLineSoft);
        primLine2(rr.x0, rr.y1, rr.x1, rr.y1, muiLineSoft);
    }

    if (maxScroll > 0)
    {
        rectangle track = muiRect(list.x1 - uiScaleSize(12),
                                  header.y1 + uiScaleSize(3),
                                  list.x1 - uiScaleSize(4),
                                  list.y1 - uiScaleSize(3));
        rectangle thumb = track;
        sdword trackHeight = track.y1 - track.y0;
        sdword thumbHeight = (trackHeight * visibleRows) / count;
        sdword travel;
        if (thumbHeight < uiScaleSize(18)) thumbHeight = uiScaleSize(18);
        if (thumbHeight > trackHeight) thumbHeight = trackHeight;
        travel = trackHeight - thumbHeight;
        thumb.y0 = track.y0 + (travel * muiKeys.scroll) / maxScroll;
        thumb.y1 = thumb.y0 + thumbHeight;
        muiFill(&track, colRGBA(17, 29, 36, 232));
        primRectOutline2(&track, 1, muiLine);
        muiFill(&thumb, muiSignalSoft);
        primRectOutline2(&thumb, 1, muiCool);
    }

    muiDrawButtonRect(&reset, "RESET DEFAULTS", FALSE,
                      muiPointIn(&reset, mouseX, mouseY));
    muiDrawButtonRect(&rebind,
                      muiKeys.capturing ? "PRESS A KEY..." : "REBIND SELECTED",
                      TRUE, muiPointIn(&rebind, mouseX, mouseY));
    muiDrawButtonRect(&layout.cancel, "CANCEL", FALSE,
                      muiPointIn(&layout.cancel, mouseX, mouseY));
    muiDrawButtonRect(&layout.apply, "APPLY", TRUE,
                      muiPointIn(&layout.apply, mouseX, mouseY));

    if (layout.preview.x1 > layout.preview.x0)
    {
        sdword x = layout.preview.x0 + uiScaleSize(18);
        sdword y = layout.preview.y0 + uiScaleSize(24);
        char countText[48];
        muiText(x, y, muiSignal, "INPUT MAP", muiBodyFont);
        muiText(x, y + uiScaleSize(38), muiPaper,
                "SELECTED COMMAND", muiHeadingFont);
        {
            fonthandle commandFont = muiBestFitFont(
                kbModernCommandName(muiKeys.selected),
                layout.preview.x1 - x - uiScaleSize(18),
                muiBodyFont, muiCompactFont, muiTinyFont);
            fonthandle keyFont = muiBestFitFont(
                kbModernKeyName(muiKeys.selected),
                layout.preview.x1 - x - uiScaleSize(18),
                muiHeadingFont, muiBodyFont, muiCompactFont);
            muiText(x, y + uiScaleSize(78), muiCool,
                    kbModernCommandName(muiKeys.selected), commandFont);
            muiText(x, y + uiScaleSize(108), muiPaper,
                    kbModernKeyName(muiKeys.selected), keyFont);
        }
        snprintf(countText, sizeof(countText), "%d COMMANDS", count);
        muiText(x, y + uiScaleSize(154), muiMuted, countText, muiBodyFont);
        muiText(x, y + uiScaleSize(184), muiMuted,
                "Mouse wheel scrolls the map.", muiBodyFont);
        muiText(x, y + uiScaleSize(212), muiMuted,
                "Delete clears the selected binding.", muiBodyFont);
        if (muiKeys.invalidUntil > now)
            muiText(x, y + uiScaleSize(254), muiDanger,
                    "KEY NOT AVAILABLE", muiBodyFont);
        else if (muiKeys.capturing)
            muiText(x, y + uiScaleSize(254), muiSignal,
                    "WAITING FOR INPUT", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(68), muiMuted,
                "ESC WHILE REBINDING", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(44), muiPaper,
                "CANCELS CAPTURE", muiBodyFont);
    }
}

static udword muiKeysProcess(regionhandle region, smemsize ID,
                             udword event, udword data)
{
    ModernUILayout layout;
    rectangle list;
    rectangle reset;
    rectangle rebind;
    sdword count = kbModernCommandCount();
    sdword visibleRows;
    sdword maxScroll;
    sdword row;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    udword keyPressed;

    muiBuildLayout(&layout);
    list = muiKeysListRect(&layout);
    visibleRows = muiKeysVisibleRows(&list);
    maxScroll = count > visibleRows ? count - visibleRows : 0;
    muiKeysActionRects(&layout, &reset, &rebind);

    if (event == RPE_KeyDown)
    {
        keyPressed = data != 0 ? (data & ~RF_ShiftBit) : (udword)ID;
        if (muiKeys.capturing)
        {
            if (keyPressed == ESCKEY)
            {
                muiKeysSetCapture(FALSE);
            }
            else if (kbModernAssignKey(muiKeys.selected, keyPressed))
            {
                muiKeysSetCapture(FALSE);
                muiKeys.invalidUntil = 0;
            }
            else
            {
                muiKeys.invalidUntil = SDL_GetTicks() + 1200;
            }
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (keyPressed == ESCKEY)
        {
            muiExitKeys(FALSE);
            return RPR_Redraw;
        }
        if (keyPressed == RETURNKEY)
        {
            muiKeysSetCapture(TRUE);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (keyPressed == DELETEKEY)
        {
            kbModernClearKey(muiKeys.selected);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (keyPressed == ARRUP)
        {
            if (muiKeys.selected > 0) --muiKeys.selected;
            muiKeysKeepSelectionVisible(visibleRows);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        if (keyPressed == ARRDOWN)
        {
            if (muiKeys.selected + 1 < count) ++muiKeys.selected;
            muiKeysKeepSelectionVisible(visibleRows);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        return 0;
    }

    if (event == RPE_WheelUp || event == RPE_WheelDown)
    {
        if (muiPointIn(&list, mouseX, mouseY))
        {
            muiKeys.scroll += event == RPE_WheelDown ? 3 : -3;
            muiKeys.scroll = muiClamp(muiKeys.scroll, 0, maxScroll);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
        return 0;
    }
    if (event == RPE_PressLeft)
    {
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event != RPE_ReleaseLeft) return 0;

    if (muiPointIn(&layout.apply, mouseX, mouseY))
    {
        muiExitKeys(TRUE);
        return RPR_Redraw;
    }
    if (muiPointIn(&layout.cancel, mouseX, mouseY))
    {
        muiExitKeys(FALSE);
        return RPR_Redraw;
    }
    if (muiPointIn(&reset, mouseX, mouseY))
    {
        kbModernResetDefaults();
        muiKeysSetCapture(FALSE);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (muiPointIn(&rebind, mouseX, mouseY))
    {
        muiKeysSetCapture(TRUE);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (maxScroll > 0 && mouseX >= list.x1 - uiScaleSize(16) &&
        mouseX < list.x1 && mouseY >= list.y0 + uiScaleSize(26) &&
        mouseY < list.y1)
    {
        rectangle track = muiRect(list.x1 - uiScaleSize(12),
                                  list.y0 + uiScaleSize(29),
                                  list.x1 - uiScaleSize(4),
                                  list.y1 - uiScaleSize(3));
        sdword trackHeight = track.y1 - track.y0;
        sdword thumbHeight = (trackHeight * visibleRows) / count;
        sdword travel;
        sdword thumbY;
        if (thumbHeight < uiScaleSize(18)) thumbHeight = uiScaleSize(18);
        if (thumbHeight > trackHeight) thumbHeight = trackHeight;
        travel = trackHeight - thumbHeight;
        thumbY = track.y0 + (travel * muiKeys.scroll) / maxScroll;
        if (mouseY < thumbY)
            muiKeys.scroll -= visibleRows;
        else if (mouseY >= thumbY + thumbHeight)
            muiKeys.scroll += visibleRows;
        muiKeys.scroll = muiClamp(muiKeys.scroll, 0, maxScroll);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    for (row = 0; row < visibleRows; ++row)
    {
        sdword command = muiKeys.scroll + row;
        rectangle rr;
        if (command >= count) break;
        rr = muiKeysRowRect(&list, row, visibleRows);
        if (muiPointIn(&rr, mouseX, mouseY))
        {
            muiKeys.selected = command;
            muiKeysSetCapture(FALSE);
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    return 0;
}

static color *muiActivePlayerColor(void)
{
    switch (muiColor.mode)
    {
        case 1: return &muiColor.stripe;
        case 2: return &muiColor.engine;
        case 3: return &muiColor.nav;
        case 4: return &muiColor.resourceBeam;
        case 5: return &muiColor.hyperspace;
        default: return &muiColor.base;
    }
}

static void muiColorHSV(color c, real32 *hue, real32 *sat, real32 *val)
{
    colRGBToHSV(hue, sat, val,
                colUbyteToReal(colRed(c)),
                colUbyteToReal(colGreen(c)),
                colUbyteToReal(colBlue(c)));
    if (*hue < 0.0f || *hue > 1.0f) *hue = 0.0f;
}

static color muiColorFromHSV(real32 hue, real32 sat, real32 val)
{
    real32 red, green, blue;
    if (hue < 0.0f) hue = 0.0f;
    if (hue > 1.0f) hue = 1.0f;
    if (sat < 0.0f) sat = 0.0f;
    if (sat > 1.0f) sat = 1.0f;
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    colHSVToRGB(&red, &green, &blue, hue, sat, val);
    return colRGB(colRealToUbyte(red), colRealToUbyte(green),
                  colRealToUbyte(blue));
}

static void muiColorRects(const ModernUILayout *layout,
                          rectangle *palette, rectangle *value,
                          rectangle *toggle)
{
    *palette = muiRect(layout->content.x0 + uiScaleSize(22),
                       layout->content.y0 + uiScaleSize(126),
                       layout->content.x1 - uiScaleSize(70),
                       layout->content.y1 - uiScaleSize(154));
    *value = muiRect(layout->content.x1 - uiScaleSize(54), palette->y0,
                     layout->content.x1 - uiScaleSize(22), palette->y1);
    *toggle = muiRect(palette->x0, palette->y1 + uiScaleSize(16),
                      value->x1, palette->y1 + uiScaleSize(56));
}

static void muiDrawHueSaturation(const rectangle *rect, real32 value)
{
    const sdword width = 256;
    const sdword height = 256;
    (void)value;
    if (muiHueSatTexture == 0)
    {
        color *pixels = (color *)memAlloc(width * height * sizeof(color),
                                          "modern HSV field", NonVolatile);
        sdword y;
        sdword x;
        for (y = 0; y < height; ++y)
        {
            real32 saturation = 1.0f - (real32)y / (real32)(height - 1);
            for (x = 0; x < width; ++x)
            {
                real32 hue = (real32)x / (real32)(width - 1);
                pixels[y * width + x] =
                    muiColorFromHSV(hue, saturation, 1.0f);
            }
        }
        muiHueSatTexture = trRGBTextureCreate(pixels, width, height, FALSE);
        memFree(pixels);
        trRGBTextureMakeCurrent(muiHueSatTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    trRGBTextureMakeCurrent(muiHueSatTexture);
    primRectSolidTextured2((rectangle *)rect);
}

static void muiDrawValueRamp(const rectangle *rect, real32 hue, real32 sat)
{
    const sdword width = 32;
    const sdword height = 256;
    sdword hueKey = (sdword)(hue * 1023.0f + 0.5f);
    sdword satKey = (sdword)(sat * 1023.0f + 0.5f);
    if (muiValueTexture == 0 || hueKey != muiValueHue ||
        satKey != muiValueSaturation)
    {
        color *pixels = (color *)memAlloc(width * height * sizeof(color),
                                          "modern HSV value ramp", NonVolatile);
        sdword y;
        sdword x;
        for (y = 0; y < height; ++y)
        {
            color row = muiColorFromHSV(
                hue, sat, 1.0f - (real32)y / (real32)(height - 1));
            for (x = 0; x < width; ++x)
                pixels[y * width + x] = row;
        }
        if (muiValueTexture == 0)
            muiValueTexture = trRGBTextureCreate(pixels, width, height, FALSE);
        else
            trRGBTextureUpdate(muiValueTexture, pixels, width, height);
        memFree(pixels);
        muiValueHue = hueKey;
        muiValueSaturation = satKey;
        trRGBTextureMakeCurrent(muiValueTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    trRGBTextureMakeCurrent(muiValueTexture);
    primRectSolidTextured2((rectangle *)rect);
}

static void muiDrawColorSwatch(const rectangle *rect, color c,
                               bool32 selected)
{
    rectangle inset = *rect;
    muiFill(rect, muiInk);
    inset.x0 += uiScaleSize(5);
    inset.y0 += uiScaleSize(5);
    inset.x1 -= uiScaleSize(5);
    inset.y1 -= uiScaleSize(5);
    muiFill(&inset, c);
    primRectOutline2((rectangle *)rect, selected ? 2 : 1,
                     selected ? muiSignal : muiLine);
}

static void muiDrawColorPicker(regionhandle region)
{
    static const char *tabs[MUI_COLOR_TAB_COUNT] = {
        "HULL BASE", "STRIPE / MARKING", "ENGINE EMISSION", "NAV LIGHTS",
        "HARVEST BEAM", "HYPERSPACE"
    };
    ModernUILayout layout;
    rectangle palette, value, toggle, cursor, swatch;
    color active = *muiActivePlayerColor();
    real32 hue, sat, val;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    char rgb[64];
    char hsv[64];
    char hex[32];
    (void)region;

    muiEnsureFonts();
    muiBuildLayout(&layout);
    muiColorRects(&layout, &palette, &value, &toggle);
    muiColorHSV(active, &hue, &sat, &val);

    muiDrawBackdrop(MUI_ART_FLEET);
    muiFill(&layout.canvas, colRGBA(4, 7, 10, 118));
    muiFill(&layout.rail, muiSurface);
    muiFill(&layout.content, muiSurface);
    if (layout.preview.x1 > layout.preview.x0)
        muiFill(&layout.preview, colRGBA(11, 20, 27, 232));

    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(18), muiSignal,
            "FLEET IDENTITY", muiHeadingFont);
    muiText(layout.rail.x0 + uiScaleSize(16),
            layout.rail.y0 + uiScaleSize(54), muiMuted,
            "PLAYER SHIPS", muiBodyFont);
    for (index = 0; index < MUI_COLOR_TAB_COUNT; ++index)
    {
        bool32 selected = index == muiColor.mode;
        bool32 hovered = muiPointIn(&layout.tabs[index], mouseX, mouseY);
        if (selected || hovered)
            muiFill(&layout.tabs[index], selected ? muiSignalSoft : muiSurfaceRaised);
        swatch = muiRect(layout.tabs[index].x0 + uiScaleSize(8),
                         layout.tabs[index].y0 + uiScaleSize(8),
                         layout.tabs[index].x0 + uiScaleSize(30),
                         layout.tabs[index].y1 - uiScaleSize(8));
        if (index == 0) active = muiColor.base;
        else if (index == 1) active = muiColor.stripe;
        else if (index == 2) active = muiColor.engine;
        else if (index == 3) active = muiColor.nav;
        else if (index == 4) active = muiColor.resourceBeam;
        else active = muiColor.hyperspace;
        muiDrawColorSwatch(&swatch, active, selected);
        muiText(layout.tabs[index].x0 + uiScaleSize(40),
                layout.tabs[index].y0 + uiScaleSize(11),
                selected ? muiPaper : muiMuted, tabs[index], muiBodyFont);
    }

    active = *muiActivePlayerColor();
    muiColorHSV(active, &hue, &sat, &val);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(22), muiSignal,
            "LIVERY / EMISSIVE PALETTE", muiBodyFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(54), muiPaper,
            tabs[muiColor.mode], muiHeadingFont);
    muiText(layout.content.x0 + uiScaleSize(22),
            layout.content.y0 + uiScaleSize(86), muiMuted,
            muiColor.mode < 2 ? "UNRESTRICTED PIGMENT" :
            "INDEPENDENT LIGHT-EMITTER COLOR", muiBodyFont);

    /* Keep the selectable chart visible even when the current color is black.
       Value is applied to the selected color and controlled by the adjacent
       ramp; it must not black out the hue/saturation reference plane. */
    muiDrawHueSaturation(&palette, 1.0f);
    muiDrawValueRamp(&value, hue, sat);
    primRectOutline2(&palette, 1, muiLine);
    primRectOutline2(&value, 1, muiLine);
    cursor = muiRect(
        palette.x0 + (sdword)(hue * (real32)(palette.x1 - palette.x0)) - uiScaleSize(5),
        palette.y1 - (sdword)(sat * (real32)(palette.y1 - palette.y0)) - uiScaleSize(5),
        palette.x0 + (sdword)(hue * (real32)(palette.x1 - palette.x0)) + uiScaleSize(5),
        palette.y1 - (sdword)(sat * (real32)(palette.y1 - palette.y0)) + uiScaleSize(5));
    primRectOutline2(&cursor, 2, muiPaper);
    cursor = muiRect(value.x0 - uiScaleSize(3),
                     value.y1 - (sdword)(val * (real32)(value.y1 - value.y0)) - uiScaleSize(2),
                     value.x1 + uiScaleSize(3),
                     value.y1 - (sdword)(val * (real32)(value.y1 - value.y0)) + uiScaleSize(2));
    muiFill(&cursor, muiPaper);

    if (muiColor.mode >= 2)
    {
        bool32 enabled = muiColor.mode == 2 ?
            muiColor.engineOverride : (muiColor.mode == 3 ?
            muiColor.navOverride : (muiColor.mode == 4 ?
            muiColor.resourceBeamOverride : muiColor.hyperspaceOverride));
        muiDrawButtonRect(&toggle,
            muiColor.mode == 2 ?
                (enabled ? "CUSTOM ENGINE COLOR" : "HULL-DERIVED ENGINE COLOR") :
            (muiColor.mode == 3 ?
                (enabled ? "CUSTOM PLAYER NAV LIGHTS" : "AUTHORED NAV LIGHTS") :
                (muiColor.mode == 4 ?
                    (enabled ? "CUSTOM PLAYER HARVEST BEAM" : "AUTHORED HARVEST BEAM") :
                    (enabled ? "CUSTOM PLAYER HYPERSPACE" : "AUTHORED HYPERSPACE"))),
            enabled, muiPointIn(&toggle, mouseX, mouseY));
    }
    else
    {
        muiText(toggle.x0, toggle.y0 + uiScaleSize(10), muiMuted,
                "Black and near-black hull values are permitted.", muiBodyFont);
    }

    muiDrawButtonRect(&layout.cancel, "CANCEL", FALSE,
                      muiPointIn(&layout.cancel, mouseX, mouseY));
    muiDrawButtonRect(&layout.apply, "APPLY", TRUE,
                      muiPointIn(&layout.apply, mouseX, mouseY));

    if (layout.preview.x1 > layout.preview.x0)
    {
        sdword x = layout.preview.x0 + uiScaleSize(18);
        sdword y = layout.preview.y0 + uiScaleSize(24);
        rectangle chip = muiRect(x, y + uiScaleSize(64),
                                 layout.preview.x1 - uiScaleSize(18),
                                 y + uiScaleSize(122));
        muiText(x, y, muiSignal, "LIVE MATERIAL KEY", muiBodyFont);
        muiText(x, y + uiScaleSize(30), muiPaper,
                "PLAYER FLEET", muiHeadingFont);
        muiDrawColorSwatch(&chip, active, TRUE);
        snprintf(rgb, sizeof(rgb), "RGB %03u / %03u / %03u",
                 (unsigned)colRed(active), (unsigned)colGreen(active),
                 (unsigned)colBlue(active));
        muiText(x, chip.y1 + uiScaleSize(18), muiMuted, rgb, muiBodyFont);
        snprintf(hsv, sizeof(hsv), "HSV %03d / %03d / %03d",
                 (sdword)(hue * 360.0f + 0.5f),
                 (sdword)(sat * 100.0f + 0.5f),
                 (sdword)(val * 100.0f + 0.5f));
        snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                 (unsigned)colRed(active), (unsigned)colGreen(active),
                 (unsigned)colBlue(active));
        muiText(x, chip.y1 + uiScaleSize(42), muiMuted, hsv, muiBodyFont);
        muiText(x, chip.y1 + uiScaleSize(66), muiSignal, hex, muiBodyFont);
        muiText(x, chip.y1 + uiScaleSize(104), muiCool,
                muiColor.mode == 5 ? "HYPERSPACE / DXR LINKED" :
                (muiColor.mode == 4 ? "HARVEST / DXR LINKED" :
                 (muiColor.mode == 3 ? "NAV / PLAYER ONLY" :
                  "ENGINE / DXR LINKED")), muiBodyFont);
        muiText(x, chip.y1 + uiScaleSize(130), muiMuted,
                muiColor.mode == 5 ? "Gate, slice and field emitter" :
                (muiColor.mode == 4 ? "Beam, nozzle glow and line emitter" :
                 (muiColor.mode == 3 ? "Lamp sprite and point emitter" :
                  "Ribbon, glow and light emitter")), muiBodyFont);
        muiText(x, chip.y1 + uiScaleSize(168), muiCool,
                "LOCAL PLAYER OVERRIDE", muiBodyFont);
        muiText(x, chip.y1 + uiScaleSize(194), muiMuted,
                "Other fleets stay authored", muiBodyFont);
        muiText(x, layout.preview.y1 - uiScaleSize(50), muiSignal,
                "NO BRIGHTNESS GATE", muiBodyFont);
    }
}

static void muiColorApplyPoint(const rectangle *palette,
                               const rectangle *value,
                               sdword mouseX, sdword mouseY)
{
    color *active = muiActivePlayerColor();
    real32 hue, sat, val;
    muiColorHSV(*active, &hue, &sat, &val);
    if (muiPointIn(palette, mouseX, mouseY))
    {
        hue = (real32)(mouseX - palette->x0) /
              (real32)(palette->x1 - palette->x0);
        sat = 1.0f - (real32)(mouseY - palette->y0) /
                     (real32)(palette->y1 - palette->y0);
    }
    else if (muiPointIn(value, mouseX, mouseY))
    {
        val = 1.0f - (real32)(mouseY - value->y0) /
                     (real32)(value->y1 - value->y0);
    }
    else
    {
        return;
    }
    *active = muiColorFromHSV(hue, sat, val);
    if (muiColor.mode == 2) muiColor.engineOverride = TRUE;
    if (muiColor.mode == 3) muiColor.navOverride = TRUE;
    if (muiColor.mode == 4) muiColor.resourceBeamOverride = TRUE;
    if (muiColor.mode == 5) muiColor.hyperspaceOverride = TRUE;
}

static void muiExitColorPicker(bool32 apply)
{
    if (apply)
    {
        cpModernColorsCommit(muiColor.base, muiColor.stripe);
        utyEngineTrailColor = muiColor.engine;
        utyNavLightColor = muiColor.nav;
        utyResourceBeamColor = muiColor.resourceBeam;
        utyHyperspaceColor = muiColor.hyperspace;
        utyEngineTrailColorOverride = muiColor.engineOverride;
        utyNavLightColorOverride = muiColor.navOverride;
        utyResourceBeamColorOverride = muiColor.resourceBeamOverride;
        utyHyperspaceColorOverride = muiColor.hyperspaceOverride;
        cpColorsAddToPreviousList(muiColor.base, muiColor.stripe);
        opOptionsSaveSettings();
    }
    feScreenDisappear(NULL, NULL);
    feScreenStart(ghMainRegion, "Main_game_screen");
}

static udword muiColorProcess(regionhandle region, smemsize ID,
                              udword event, udword data)
{
    ModernUILayout layout;
    rectangle palette, value, toggle;
    sdword index;
    sdword mouseX = mouseCursorX();
    sdword mouseY = mouseCursorY();
    (void)data;

    if (event == RPE_KeyDown && ID == ESCKEY)
    {
        muiExitColorPicker(FALSE);
        return RPR_Redraw;
    }
    muiBuildLayout(&layout);
    muiColorRects(&layout, &palette, &value, &toggle);
    if (event == RPE_PressLeft)
    {
        for (index = 0; index < MUI_COLOR_TAB_COUNT; ++index)
        {
            if (muiPointIn(&layout.tabs[index], mouseX, mouseY))
            {
                muiColor.mode = index;
                regRecursiveSetDirty(region);
                return RPR_Redraw;
            }
        }
        muiColorApplyPoint(&palette, &value, mouseX, mouseY);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_HoldLeft)
    {
        muiColorApplyPoint(&palette, &value, mouseX, mouseY);
        regRecursiveSetDirty(region);
        return RPR_Redraw;
    }
    if (event == RPE_ReleaseLeft)
    {
        if (muiPointIn(&layout.apply, mouseX, mouseY))
        {
            muiExitColorPicker(TRUE);
            return RPR_Redraw;
        }
        if (muiPointIn(&layout.cancel, mouseX, mouseY))
        {
            muiExitColorPicker(FALSE);
            return RPR_Redraw;
        }
        if (muiColor.mode >= 2 && muiPointIn(&toggle, mouseX, mouseY))
        {
            if (muiColor.mode == 2)
                muiColor.engineOverride = !muiColor.engineOverride;
            else if (muiColor.mode == 3)
                muiColor.navOverride = !muiColor.navOverride;
            else if (muiColor.mode == 4)
                muiColor.resourceBeamOverride =
                    !muiColor.resourceBeamOverride;
            else
                muiColor.hyperspaceOverride =
                    !muiColor.hyperspaceOverride;
            regRecursiveSetDirty(region);
            return RPR_Redraw;
        }
    }
    return 0;
}

bool32 modernUIOwnsScreen(const char *screenName)
{
    if (screenName == NULL) return FALSE;
    return strcmp(screenName, "Main_game_screen") == 0 ||
           strcmp(screenName, "Connection_Method") == 0 ||
           strcmp(screenName, "Audio_Options") == 0 ||
           strcmp(screenName, "Audio") == 0 ||
           strcmp(screenName, "Video_Options") == 0 ||
           strcmp(screenName, "Video") == 0 ||
           strcmp(screenName, "Select_colour") == 0 ||
           strcmp(screenName, "Gameplay_Options") == 0 ||
           strcmp(screenName, "Gameplay") == 0 ||
           strcmp(screenName, "Keys") == 0;
}

regionhandle modernUICreateOwnedScreen(regionhandle parent, fescreen *screen)
{
    regionhandle base;
    regionhandle input;
    bool32 inGame;
    if (screen == NULL || !modernUIOwnsScreen(screen->name)) return NULL;
    modernUIApplyPalette();
    if (strcmp(screen->name, "Main_game_screen") == 0)
    {
        sdword index;
        muiEnsureFonts();
        muiMainSelection = 0;
        muiMainSettingsSelection = 0;
        muiMainSettingsExpansion = 0.0f;
        muiMainSettingsLastTick = SDL_GetTicks();
        muiMainSettingsKeyboardOpen = FALSE;
        muiMainScreen = screen;
        /* Match FEFlow's modal contract: interactive children are processed
           before the modal base consumes events intended for lower screens. */
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawMain);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick);
        regFunctionSet(input, muiMainProcess);
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        for (index = 1; index < screen->nAtoms; ++index)
        {
            if (bitTest(screen->atoms[index].flags, FAF_CallOnCreate))
                feFunctionExecute(screen->atoms[index].name,
                                  &screen->atoms[index], TRUE);
        }
        regKeyChildAlloc(base, ARRUP, RPE_KeyDown,
                         (regionfunction)muiMainProcess, 1, ARRUP);
        regKeyChildAlloc(base, ARRDOWN, RPE_KeyDown,
                         (regionfunction)muiMainProcess, 1, ARRDOWN);
        regKeyChildAlloc(base, RETURNKEY, RPE_KeyDown,
                         (regionfunction)muiMainProcess, 1, RETURNKEY);
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiMainProcess, 1, ESCKEY);
        fprintf(stderr, "[ModernUI] Main menu replaced by native editorial UI.\n");
        return base;
    }
    if (strcmp(screen->name, "Connection_Method") == 0)
    {
        sdword index;
        muiEnsureFonts();
        muiConnectionSelection = 0;
        muiConnectionScreen = screen;
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawConnection);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick);
        regFunctionSet(input, muiConnectionProcess);
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        for (index = 1; index < screen->nAtoms; ++index)
        {
            if (bitTest(screen->atoms[index].flags, FAF_CallOnCreate))
                feFunctionExecute(screen->atoms[index].name,
                                  &screen->atoms[index], TRUE);
        }
        regKeyChildAlloc(base, ARRUP, RPE_KeyDown,
                         (regionfunction)muiConnectionProcess, 1, ARRUP);
        regKeyChildAlloc(base, ARRDOWN, RPE_KeyDown,
                         (regionfunction)muiConnectionProcess, 1, ARRDOWN);
        regKeyChildAlloc(base, RETURNKEY, RPE_KeyDown,
                         (regionfunction)muiConnectionProcess, 1, RETURNKEY);
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiConnectionProcess, 1, ESCKEY);
        fprintf(stderr, "[ModernUI] Multiplayer connection menu replaced by responsive UI.\n");
        return base;
    }
    if (strcmp(screen->name, "Select_colour") == 0)
    {
        muiEnsureFonts();
        memset(&muiColor, 0, sizeof(muiColor));
        muiColor.active = TRUE;
        cpModernColorsSnapshot(&muiColor.base, &muiColor.stripe);
        muiColor.engine = utyEngineTrailColor;
        muiColor.nav = utyNavLightColor;
        muiColor.resourceBeam = utyResourceBeamColor;
        muiColor.hyperspace = utyHyperspaceColor;
        muiColor.engineOverride = utyEngineTrailColorOverride;
        muiColor.navOverride = utyNavLightColorOverride;
        muiColor.resourceBeamOverride = utyResourceBeamColorOverride;
        muiColor.hyperspaceOverride = utyHyperspaceColorOverride;
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawColorPicker);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick | RPE_HoldLeft);
        regFunctionSet(input, muiColorProcess);
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiColorProcess, 1, ESCKEY);
        fprintf(stderr,
                "[ModernUI] Fleet livery, engine, nav, harvesting and hyperspace palette active.\n");
        return base;
    }
    if (strcmp(screen->name, "Audio_Options") == 0 ||
        strcmp(screen->name, "Audio") == 0)
    {
        inGame = strcmp(screen->name, "Audio") == 0 || gameIsRunning;
        muiEnsureFonts();
        memset(&muiAudio, 0, sizeof(muiAudio));
        muiAudio.active = TRUE;
        muiAudio.inGame = inGame;
        muiAudio.tab = 0;
        /* Match the original options contract: entering Audio establishes a
           complete rollback snapshot, but the modern screen owns all drawing
           and interaction from this point forward. */
        opOptionsSaveSettings();
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawAudio);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick | RPE_WheelUp | RPE_WheelDown);
        regFunctionSet(input, muiAudioProcess);
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiAudioProcess, 1, ESCKEY);
        fprintf(stderr,
                "[ModernUI] %s replaced by native responsive audio UI.\n",
                screen->name);
        return base;
    }
    if (strcmp(screen->name, "Keys") == 0)
    {
        muiEnsureFonts();
        memset(&muiKeys, 0, sizeof(muiKeys));
        muiKeys.active = TRUE;
        muiKeys.inGame = gameIsRunning;
        muiKeys.selected = 0;
        muiKeys.scroll = 0;
        kbSaveSettings();
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawKeys);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick | RPE_WheelUp | RPE_WheelDown |
                              RPE_KeyDown);
        regFunctionSet(input, muiKeysProcess);
        muiKeys.inputRegion = input;
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        /* Normal navigation uses a handful of keyed children.  Rebinding
           switches the full-screen input region into RSF_KeyCapture, which
           receives the next buffered key directly without allocating one
           region per SDL scancode. */
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiKeysProcess, 1, ESCKEY);
        regKeyChildAlloc(base, RETURNKEY, RPE_KeyDown,
                         (regionfunction)muiKeysProcess, 1, RETURNKEY);
        regKeyChildAlloc(base, DELETEKEY, RPE_KeyDown,
                         (regionfunction)muiKeysProcess, 1, DELETEKEY);
        regKeyChildAlloc(base, ARRUP, RPE_KeyDown,
                         (regionfunction)muiKeysProcess, 1, ARRUP);
        regKeyChildAlloc(base, ARRDOWN, RPE_KeyDown,
                         (regionfunction)muiKeysProcess, 1, ARRDOWN);
        fprintf(stderr,
                "[ModernUI] Key bindings replaced by native responsive command map.\n");
        return base;
    }
    if (strcmp(screen->name, "Gameplay_Options") == 0 ||
        strcmp(screen->name, "Gameplay") == 0)
    {
        inGame = gameIsRunning;
        muiEnsureFonts();
        memset(&muiGameplay, 0, sizeof(muiGameplay));
        muiGameplay.active = TRUE;
        muiGameplay.inGame = inGame;
        muiGameplay.savedMouseSensitivity = opMouseSens;
        muiGameplay.savedCursorScalePercent = mouseCursorScalePercent;
        muiGameplay.savedUnlimitedFuel = opUnlimitedFuel;
        muiGameplay.savedPauseOrders = opPauseOrders;
        muiGameplay.savedShipRecoil = opShipRecoil;
        muiGameplay.savedSuperSalvagers = opSuperSalvagers;
        muiGameplay.savedCaptureBuildAll = opCaptureBuildAll;
        muiGameplay.savedResourceMultiplier = opResourceMultiplier;
        base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                             MAIN_WindowWidth, MAIN_WindowHeight, 0,
                             RPE_ModalBreak | RPE_DrawEveryFrame);
        regSiblingMoveToFront(base);
        regDrawFunctionSet(base, muiDrawGameplay);
        input = regChildAlloc(base, (smemsize)screen, 0, 0,
                              MAIN_WindowWidth, MAIN_WindowHeight, 0,
                              RPE_LeftClick | RPE_HoldLeft |
                              RPE_WheelUp | RPE_WheelDown);
        regFunctionSet(input, muiGameplayProcess);
        base->atom = &screen->atoms[0];
        screen->atoms[0].region = base;
        regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                         (regionfunction)muiGameplayProcess, 1, ESCKEY);
        fprintf(stderr,
                "[ModernUI] %s replaced by native responsive gameplay UI.\n",
                screen->name);
        return base;
    }
    inGame = strcmp(screen->name, "Video") == 0;
    muiEnsureFonts();
    memset(&muiVideo, 0, sizeof(muiVideo));
    muiVideo.active = TRUE;
    muiVideo.inGame = inGame;
    muiVideo.savedFullScreen = fullScreen;
    muiVideo.savedExclusive = mainExclusiveFullscreen;
    muiVideo.savedWidth = mainWindowWidth;
    muiVideo.savedHeight = mainWindowHeight;
    muiVideo.savedRefresh = mainRefreshRate;
    muiVideo.savedFrameCap = mainFrameRateLimit;
    muiVideo.savedUIScale = mainUIScalePercent;

    base = regChildAlloc(parent, (smemsize)screen, 0, 0,
                         MAIN_WindowWidth, MAIN_WindowHeight, 0,
                         RPE_ModalBreak | RPE_DrawEveryFrame);
    regSiblingMoveToFront(base);
    regDrawFunctionSet(base, muiDrawVideo);
    input = regChildAlloc(base, (smemsize)screen, 0, 0,
                          MAIN_WindowWidth, MAIN_WindowHeight, 0,
                          RPE_LeftClick | RPE_WheelUp | RPE_WheelDown);
    regFunctionSet(input, muiVideoProcess);
    base->atom = &screen->atoms[0];
    screen->atoms[0].region = base;
    regKeyChildAlloc(base, ESCKEY, RPE_KeyDown,
                     (regionfunction)muiVideoProcess, 1, ESCKEY);
    fprintf(stderr, "[ModernUI] %s replaced by native responsive video UI.\n",
            screen->name);
    return base;
}

void modernUIOwnedScreenDeleted(const fescreen *screen)
{
    if (screen != NULL && modernUIOwnsScreen(screen->name))
    {
        muiVideo.active = FALSE;
        muiGameplay.active = FALSE;
        muiAudio.active = FALSE;
        muiKeys.active = FALSE;
        muiKeys.inputRegion = NULL;
        muiKeys.capturing = FALSE;
        muiColor.active = FALSE;
        if (strcmp(screen->name, "Main_game_screen") == 0)
            muiMainScreen = NULL;
        if (strcmp(screen->name, "Connection_Method") == 0)
            muiConnectionScreen = NULL;
    }
}

color modernUITextColor(const regionhandle region)
{
    if (region != NULL && (bitTest(region->status, RSF_MouseInside) ||
                           bitTest(region->status, RSF_CurrentSelected)))
        return muiPaper;
    return muiMuted;
}

void modernUIDrawBase(regionhandle region)
{
    fescreen *screen;
    ModernUIScreenKind kind;
    ModernUIScreenLayout layout;
    rectangle canvas;
    rectangle accent;
    rectangle infoRect;
    char title[96];
    if (region == NULL) return;
    screen = muiScreenForRegion(region);
    kind = muiScreenKind(screen);
    muiEnsureFonts();

    /* The tactical command deck is one visual instrument.  The taskbar is
       created outside the ordinary FE screen stack, so identify its base by
       its live geometry rather than by screen lookup.  Paint the same archive
       artwork through that original center base so the image continues
       mathematically from left deck -> vanilla center -> right roster. */
    tbGetInfoBarRect(&infoRect);
    if (tbInfoBarVisible() &&
        region->rect.x0 == infoRect.x0 && region->rect.x1 == infoRect.x1 &&
        region->rect.y0 == infoRect.y0 && region->rect.y1 == infoRect.y1)
    {
        muiCommandDrawBackdropSegment(&region->rect);
        primRectOutline2(&region->rect, 1, muiLineSoft);
        return;
    }

    if (kind == MUI_SCREEN_FRONTEND || kind == MUI_SCREEN_MULTIPLAYER)
    {
        layout = muiScreenLayout(kind);
        canvas = muiRect(0, 0, MAIN_WindowWidth, MAIN_WindowHeight);
        muiDrawBackdrop(muiArtworkForScreen(
            screen != NULL ? screen->name : NULL));
        muiFill(&canvas, colRGBA(3, 6, 9, 104));
        muiFill(&layout.content, colRGBA(12, 20, 26, 226));
        primRectOutline2(&layout.content, 1, muiLineSoft);
        accent = layout.content;
        accent.x1 = accent.x0 + uiScaleSize(3);
        muiFill(&accent, muiSignal);
        muiHumanizeTitle(screen != NULL ? screen->name : NULL,
                         title, sizeof(title));
        muiText(uiScaleSize(34), uiScaleSize(34), muiSignal,
                "HOMEWORLD / MODERN", muiHeadingFont);
        muiText(uiScaleSize(34), uiScaleSize(66), muiMuted,
                "FLEET COMMAND INTERFACE", muiBodyFont);
        muiText(layout.content.x0, layout.content.y0 - uiScaleSize(48),
                muiSignal, "COMMAND / ACTIVE SCREEN", muiBodyFont);
        muiText(layout.content.x0, layout.content.y0 - uiScaleSize(24),
                muiPaper, title, muiHeadingFont);
        return;
    }

    if (kind == MUI_SCREEN_INGAME)
    {
        muiDrawArtworkInRect(MUI_ART_SYSTEMS, &region->rect);
        muiFill(&region->rect, colRGBA(15, 25, 32, 226));
    }
    else if (kind == MUI_SCREEN_GAMEPLAY && screen != NULL &&
             muiManagerScreen(screen->name))
    {
        rectangle divider = region->rect;
        const char *division = "FLEET OPERATIONS";
        const char *managerTitle = "MANAGER";
        if (muiNameIs(screen->name, "Construction_manager"))
            muiDrawArtworkInRect(MUI_ART_PRODUCTION, &region->rect);
        else
            muiDrawArtworkInRect(MUI_ART_FLEET, &region->rect);
        muiFill(&region->rect, colRGBA(10, 18, 24, 232));
        if (muiNameIs(screen->name, "Construction_manager"))
        {
            division = "FLEET PRODUCTION";
            managerTitle = "BUILD MANAGER";
        }
        else if (muiNameIs(screen->name, "Research_Manager"))
        {
            division = "SCIENCE DIVISION";
            managerTitle = "RESEARCH MANAGER";
        }
        else if (muiNameIs(screen->name, "Launch_Manager"))
        {
            division = "FLIGHT OPERATIONS";
            managerTitle = "LAUNCH MANAGER";
        }
        muiText(region->rect.x0 + uiScaleSize(12),
                region->rect.y0 + uiScaleSize(7), muiSignal,
                division, muiTinyFont);
        muiText(region->rect.x0 + uiScaleSize(12),
                region->rect.y0 + uiScaleSize(21), muiPaper,
                managerTitle, muiBodyFont);
        divider.y0 = region->rect.y0 + uiScaleSize(48);
        divider.y1 = divider.y0 + 1;
        divider.x0 += uiScaleSize(12);
        divider.x1 -= uiScaleSize(12);
        muiFill(&divider, muiLineSoft);
    }
    else
    {
        muiFill(&region->rect, muiSurface);
    }
    primRectOutline2(&region->rect, 1, muiLineSoft);
    accent = region->rect;
    accent.x1 = accent.x0 + uiScaleSize(2);
    muiFill(&accent, muiSignal);
    if (kind == MUI_SCREEN_INGAME || kind == MUI_SCREEN_POPUP)
    {
        muiHumanizeTitle(screen != NULL ? screen->name : NULL,
                         title, sizeof(title));
        muiText(region->rect.x0 + uiScaleSize(18),
                region->rect.y0 + uiScaleSize(18), muiSignal,
                kind == MUI_SCREEN_POPUP ? "COMMAND NOTICE" : "FLEET COMMAND",
                muiBodyFont);
        muiText(region->rect.x0 + uiScaleSize(18),
                region->rect.y0 + uiScaleSize(40), muiPaper,
                title, muiHeadingFont);
    }
}

void modernUIDrawStaticRectangle(regionhandle region)
{
    rectangle infoRect;
    if (region == NULL) return;
    tbGetInfoBarRect(&infoRect);
    if (tbInfoBarVisible() &&
        region->rect.x0 >= infoRect.x0 && region->rect.x1 <= infoRect.x1 &&
        region->rect.y0 >= infoRect.y0 && region->rect.y1 <= infoRect.y1)
    {
        /* Matched smoked glass over the continuous command-bar artwork. */
        muiFill(&region->rect, colRGBA(15, 25, 32, 214));
    }
    else
    {
        muiFill(&region->rect, muiSurface);
    }
    primRectOutline2(&region->rect, 1, muiLineSoft);
}

void modernUIDrawDecorative(regionhandle region)
{
    if (region == NULL) return;
    primLine2(region->rect.x0, region->rect.y1 - 1,
              region->rect.x1, region->rect.y1 - 1, muiLineSoft);
}

void modernUIDrawButton(regionhandle region, bool32 checked, bool32 roundControl)
{
    rectangle accent;
    color fill;
    color outline;
    bool32 disabled;
    bool32 active;
    if (region == NULL) return;
    disabled = bitTest(region->status, RSF_RegionDisabled);
    active = checked || bitTest(region->status, RSF_LeftPressed) ||
             bitTest(region->status, RSF_CurrentSelected);
    fill = active ? muiSignalSoft :
           (bitTest(region->status, RSF_MouseInside) ? muiSurfaceRaised : muiSurface);
    outline = disabled ? muiDisabled : (active ? muiSignal : muiLine);
    muiFill(&region->rect, fill);
    primRectOutline2(&region->rect, 1, outline);
    if (!roundControl)
    {
        accent = region->rect;
        accent.x1 = accent.x0 + uiScaleSize(2);
        muiFill(&accent, active ? muiSignal : muiLineSoft);
    }
    else if (checked)
    {
        sdword radius = (region->rect.y1 - region->rect.y0) / 5;
        primCircleSolid2((region->rect.x0 + region->rect.x1) / 2,
                         (region->rect.y0 + region->rect.y1) / 2,
                         radius > 2 ? radius : 2, 12, muiSignal);
    }
    if (region->atom != NULL)
    {
        featom *buttonAtom = (featom *)region->atom;
        const char *label = NULL;
        const char *name = buttonAtom->name;
        if (name == NULL) return;
        if (strcmp(name, "CM_BuildShips") == 0) label = "BUILD";
        else if (strcmp(name, "CM_PauseJobs") == 0) label = "PAUSE BATCH";
        else if (strcmp(name, "CM_CancelJobs") == 0) label = "CANCEL BATCH";
        else if (strcmp(name, "CM_Close") == 0) label = "CLOSE";
        else if (strcmp(name, "RM_ResearchItem") == 0) label = "RESEARCH";
        else if (strcmp(name, "RM_ClearLab") == 0) label = "CLEAR LAB";
        else if (strcmp(name, "RM_ExtendedInfo") == 0) label = "DETAILS";
        else if (strcmp(name, "RM_ExitMenu") == 0) label = "CLOSE";
        else if (strcmp(name, "LM_Launch") == 0) label = "LAUNCH";
        else if (strcmp(name, "LM_LaunchAll") == 0) label = "LAUNCH ALL";
        else if (strcmp(name, "LM_Close") == 0) label = "CLOSE";
        else if (strcmp(name, "TB_SensorsManager") == 0 ||
                 strcmp(name, "TB_Sensors") == 0) label = "SENSORS";
        else if (strcmp(name, "CSM_Build") == 0) label = "BUILD";
        else if (strcmp(name, "CSM_Research") == 0) label = "RESEARCH";
        else if (strcmp(name, "CSM_Launch") == 0) label = "LAUNCH";
        if (label != NULL)
        {
            muiEnsureFonts();
            muiTextCenteredAdaptiveFull(&region->rect,
                                        disabled ? muiDisabled :
                                        (active ? muiPaper : muiMuted),
                                        label, muiTinyFont, muiTinyFont,
                                        muiTinyFont, uiScaleSize(2));
        }
    }
}

void modernUIDrawSlider(regionhandle region, bool32 vertical)
{
    sliderhandle slider = (sliderhandle)region;
    rectangle track = region->rect;
    rectangle value = region->rect;
    sdword range = slider->maxvalue > 1 ? slider->maxvalue - 1 : 1;
    real32 t = (real32)slider->value / (real32)range;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (vertical)
    {
        track.x0 = (track.x0 + track.x1) / 2 - 1;
        track.x1 = track.x0 + 2;
        value.y0 = value.y1 - (sdword)((real32)(value.y1 - value.y0) * t);
    }
    else
    {
        track.y0 = (track.y0 + track.y1) / 2 - 1;
        track.y1 = track.y0 + 2;
        value.x1 = value.x0 + (sdword)((real32)(value.x1 - value.x0) * t);
    }
    muiFill(&track, muiLineSoft);
    muiFill(&value, bitTest(region->status, RSF_RegionDisabled) ?
                       colRGBA(74, 82, 84, 90) : colRGBA(222, 162, 62, 80));
    primRectOutline2(&region->rect, 1,
        bitTest(region->status, RSF_MouseInside) ? muiSignal : muiLine);
}

void modernUIDrawScrollBar(regionhandle region)
{
    scrollbarhandle bar = (scrollbarhandle)region;
    uicScrollBarSynchronizeGeometry(bar);
    muiFill(&region->rect, muiSurface);
    muiFill(&bar->thumb, muiSignalSoft);
    primRectOutline2(&bar->thumb, 1, muiSignal);
}

void modernUIDrawScrollBarButton(regionhandle region)
{
    modernUIDrawButton(region, FALSE, FALSE);
}

void modernUIDrawListFrame(regionhandle region)
{
    muiFill(&region->rect, muiSurface);
    primRectOutline2(&region->rect, 1,
        bitTest(region->status, RSF_CurrentSelected) ? muiSignal : muiLine);
}

void modernUIDrawTextEntryFrame(regionhandle region)
{
    muiFill(&region->rect, muiSurfaceRaised);
    primRectOutline2(&region->rect, 1,
        bitTest(region->status, RSF_CurrentSelected) ? muiSignal : muiLine);
}

void modernUIDrawListSelection(rectangle *rect)
{
    muiFill(rect, muiSignalSoft);
    primRectOutline2(rect, 1, muiSignal);
}

void modernUIDrawMenuItem(regionhandle region, bool32 checked, bool32 linked)
{
    rectangle marker;
    featom *atom;
    if (bitTest(region->status, RSF_MouseInside) || checked)
    {
        muiFill(&region->rect, muiSignalSoft);
        marker = region->rect;
        marker.x1 = marker.x0 + uiScaleSize(2);
        muiFill(&marker, muiSignal);
    }
    if (linked)
    {
        primLine2(region->rect.x1 - uiScaleSize(7),
                  (region->rect.y0 + region->rect.y1) / 2,
                  region->rect.x1 - uiScaleSize(3),
                  (region->rect.y0 + region->rect.y1) / 2, muiMuted);
    }
    atom = (featom *)region->userID;
    if (atom != NULL && atom->name != NULL &&
        strcmp(atom->name, "CSM_Info") == 0)
    {
        muiEnsureFonts();
        muiTextCentered(&region->rect,
                        bitTest(region->status, RSF_MouseInside) ?
                            muiPaper : muiMuted,
                        "SHIP DOSSIER", muiBodyFont);
    }
}

void modernUIDrawDivider(regionhandle region)
{
    sdword y = (region->rect.y0 + region->rect.y1) / 2;
    primLine2(region->rect.x0, y, region->rect.x1, y, muiLineSoft);
}
