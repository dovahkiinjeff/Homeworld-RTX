#include "WindowsInstall.h"

#if defined(_WIN32)

#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>

#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

extern "C" void fileHomeworldDataPathSet(char *path);
extern "C" int fileOverrideBigPathSet(char *path);
extern "C" int fileUserSettingsPathSet(char *path);

namespace
{
std::wstring moduleDirectory()
{
    wchar_t path[32768] = {};
    DWORD count = GetModuleFileNameW(nullptr, path,
        static_cast<DWORD>(_countof(path)));
    if (count == 0 || count >= _countof(path)) return {};
    std::wstring result(path, count);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() :
        result.substr(0, slash);
}

std::wstring parentDirectory(const std::wstring &path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() :
        path.substr(0, slash);
}

std::wstring fullPath(const std::wstring &path)
{
    wchar_t resolved[32768] = {};
    DWORD count = GetFullPathNameW(path.c_str(),
        static_cast<DWORD>(_countof(resolved)), resolved, nullptr);
    return count == 0 || count >= _countof(resolved) ? path :
        std::wstring(resolved, count);
}

bool regularFile(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool validDataDirectory(const std::wstring &path)
{
    /* Only the BIG archive is required to bind the original game data.
       Speech/music stream archives are optional: installations that do not
       provide them still run with SFX and the rest of the game data. */
    return regularFile(path + L"\\homeworld.big");
}

std::wstring resolveDataDirectory(const std::wstring &path)
{
    if (path.empty()) return {};

    const std::wstring resolved = fullPath(path);
    const std::wstring candidates[] = {
        resolved,
        resolved + L"\\Data",
        resolved + L"\\data"
    };
    for (const std::wstring &candidate : candidates)
    {
        const std::wstring normalized = fullPath(candidate);
        if (validDataDirectory(normalized)) return normalized;
    }
    return {};
}

std::wstring environmentPath(const wchar_t *name)
{
    const DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) return {};
    std::vector<wchar_t> value(static_cast<size_t>(needed));
    const DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed) return {};
    return fullPath(value.data());
}

std::wstring locateDataNearModern()
{
    // RTX deployments live below the legally installed Homeworld tree, e.g.
    //   Homeworld1Classic\\RTX\\RTX Deployed\\HomeworldModern.exe
    // Walk several ancestors and accept either files directly in that folder
    // or the conventional Data subdirectory.  This also keeps standalone
    // deployments working when the data is deliberately placed beside the EXE.
    std::wstring current = moduleDirectory();
    for (int depth = 0; depth < 6 && !current.empty(); ++depth)
    {
        const std::wstring found = resolveDataDirectory(current);
        if (!found.empty()) return found;
        const std::wstring parent = parentDirectory(current);
        if (parent.empty() || parent == current) break;
        current = parent;
    }
    return {};
}

std::wstring perUserGameRoot()
{
    PWSTR documents = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(
            FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &documents)) &&
        documents != nullptr)
    {
        result.assign(documents);
        CoTaskMemFree(documents);
    }
    else
    {
        wchar_t path[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL |
            CSIDL_FLAG_CREATE, nullptr, SHGFP_TYPE_CURRENT, path)))
        {
            return moduleDirectory() + L"\\UserData";
        }
        result.assign(path);
    }
    return result + L"\\My Games\\Homeworld Modern";
}

std::wstring readSetting(const std::wstring &config, const wchar_t *name)
{
    wchar_t value[32768] = {};
    GetPrivateProfileStringW(L"Installation", name, L"", value,
        static_cast<DWORD>(_countof(value)), config.c_str());
    return value;
}

void saveInstallation(const std::wstring &config,
                      const std::wstring &dataDirectory)
{
    WritePrivateProfileStringW(L"Installation", L"DataDirectory",
        dataDirectory.c_str(), config.c_str());
}

std::wstring selectDataDirectory()
{
    HWND splash = FindWindowW(L"HomeworldModernStartupSplash", nullptr);
    if (splash != nullptr) ShowWindow(splash, SW_HIDE);

    wchar_t path[32768] = L"Homeworld.big";
    const wchar_t filter[] =
        L"Homeworld data archive (Homeworld.big)\0Homeworld.big\0"
        L"BIG archives (*.big)\0*.big\0\0";
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = nullptr;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(_countof(path));
    dialog.lpstrTitle =
        L"Locate Homeworld.big from your installed Homeworld 1 Classic data";
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_DONTADDTORECENT;
    const bool selected = GetOpenFileNameW(&dialog) != FALSE;

    if (splash != nullptr)
    {
        ShowWindow(splash, SW_SHOWNORMAL);
        UpdateWindow(splash);
    }
    if (!selected) return {};

    const std::wstring directory = parentDirectory(fullPath(path));
    return validDataDirectory(directory) ? directory : std::wstring();
}

bool narrowPath(const std::wstring &source, std::vector<char> &destination)
{
    const int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
        source.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 0) return false;
    destination.resize(static_cast<size_t>(length));
    BOOL usedDefault = FALSE;
    if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, source.c_str(), -1,
        destination.data(), length, nullptr, &usedDefault) <= 0 || usedDefault)
    {
        return false;
    }
    return true;
}
}

extern "C" int hwWindowsInstallPrepare(void)
{
    const std::wstring root = perUserGameRoot();
    const std::wstring settings = root;
    const std::wstring config = root + L"\\installation.ini";
    const std::wstring logs = root + L"\\Logs";
    SHCreateDirectoryExW(nullptr, settings.c_str(), nullptr);
    SHCreateDirectoryExW(nullptr, logs.c_str(), nullptr);

    // The original executable is not a runtime dependency.  Bind directly to
    // the legally installed data instead.  Prefer an explicit HW_Data value,
    // then a previously remembered directory, then auto-discover around the
    // deployed executable.  Only show a picker when all automatic routes fail.
    std::wstring dataDirectory =
        resolveDataDirectory(environmentPath(L"HW_Data"));
    if (dataDirectory.empty())
    {
        dataDirectory = resolveDataDirectory(
            readSetting(config, L"DataDirectory"));
    }
    if (dataDirectory.empty())
    {
        dataDirectory = locateDataNearModern();
    }
    if (dataDirectory.empty())
    {
        dataDirectory = selectDataDirectory();
        if (dataDirectory.empty())
        {
            MessageBoxW(nullptr,
                L"Homeworld Modern needs the installed Homeworld 1 Classic "
                L"Homeworld.big archive. The original Homeworld.exe, "
                L"HW_Comp.vce and HW_Music.wxd are not required to launch. "
                L"Place the RTX deployment beneath your Homeworld1Classic "
                L"installation or select Homeworld.big when prompted.",
                L"Homeworld data not found",
                MB_OK | MB_ICONERROR | MB_TASKMODAL);
            return 0;
        }
    }
    saveInstallation(config, dataDirectory);

    std::vector<char> dataNarrow;
    std::vector<char> overrideNarrow;
    std::vector<char> settingsNarrow;
    if (!narrowPath(dataDirectory, dataNarrow) ||
        !narrowPath(moduleDirectory(), overrideNarrow) ||
        !narrowPath(settings, settingsNarrow))
    {
        MessageBoxW(nullptr,
            L"Homeworld Modern cannot currently open an installation "
            L"whose path contains characters outside the active Windows "
            L"code page. Move the mod or game to a simpler path and retry.",
            L"Unsupported installation path", MB_OK | MB_ICONERROR |
            MB_TASKMODAL);
        return 0;
    }

    fileHomeworldDataPathSet(dataNarrow.data());
    fileOverrideBigPathSet(overrideNarrow.data());
    fileUserSettingsPathSet(settingsNarrow.data());
    SetEnvironmentVariableW(L"HW_Data", dataDirectory.c_str());
    _wputenv_s(L"HW_Data", dataDirectory.c_str());
    /* Diagnostic launches provide their own run-specific crash directory.
       Preserve it so run-with-log.ps1 can package the dump with its exact
       executable and PDB; normal launches use the My Games Logs folder. */
    wchar_t diagnosticCrashDirectory[2] = {};
    if (GetEnvironmentVariableW(L"HW_CRASH_DIR", diagnosticCrashDirectory,
                                _countof(diagnosticCrashDirectory)) == 0)
    {
        SetEnvironmentVariableW(L"HW_CRASH_DIR", logs.c_str());
    }
    std::fprintf(stderr,
        "[Install] Verified external Homeworld data.\n"
        "[Install] Content root: %ls\n"
        "[Install] Loose-file override root: %ls\n"
        "[Install] Player data root: %ls\n",
        dataDirectory.c_str(), moduleDirectory().c_str(), settings.c_str());
    return 1;
}

#else

extern "C" int hwWindowsInstallPrepare(void) { return 1; }

#endif
