// config.cpp - Persistent accessibility settings via Windows INI file.
// See config.h for interface documentation.
// v0.13.51: Introduced.

#include "config.h"
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"
#include <cstring>
#include <cstdio>

namespace Config {

static char s_iniPath[MAX_PATH] = {};
static const char* SECTION = "Accessibility";
static bool s_loaded = false;

// Resolve the INI path to sit next to this DLL.
static void BuildPath()
{
    char dllPath[MAX_PATH] = {};
    HMODULE hMod = NULL;
    // Find our DLL's HMODULE by passing the address of a function inside us.
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&BuildPath, &hMod);
    GetModuleFileNameA(hMod, dllPath, sizeof(dllPath));
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    else dllPath[0] = '\0';
    snprintf(s_iniPath, sizeof(s_iniPath), "%sff8_accessibility.ini", dllPath);
}

// If ewm_config.txt exists alongside the DLL, import its value into the INI
// as ewm_enabled, then delete the legacy file so we don't re-import it.
// Called from Load() only when the INI didn't already exist.
static void ImportLegacyEWMConfig()
{
    // Derive the DLL directory from s_iniPath (strip the INI filename).
    char dllDir[MAX_PATH];
    strncpy(dllDir, s_iniPath, sizeof(dllDir));
    dllDir[sizeof(dllDir) - 1] = '\0';
    char* lastSlash = strrchr(dllDir, '\\');
    if (!lastSlash) return;
    *(lastSlash + 1) = '\0';

    char legacyPath[MAX_PATH];
    snprintf(legacyPath, sizeof(legacyPath), "%sewm_config.txt", dllDir);

    FILE* f = fopen(legacyPath, "r");
    if (!f) return;

    char buf[16] = {};
    fgets(buf, sizeof(buf), f);
    fclose(f);

    int val = (buf[0] == '0') ? 0 : 1;
    WritePrivateProfileStringA(SECTION, "ewm_enabled", val ? "1" : "0", s_iniPath);

    if (DeleteFileA(legacyPath)) {
        Log::Mod("Config: Imported legacy ewm_config.txt (value=%d), deleted source file", val);
    } else {
        Log::Mod("Config: Imported legacy ewm_config.txt (value=%d), delete failed (err=%u)",
                   val, GetLastError());
    }
}

void Load()
{
    if (s_loaded) return;
    s_loaded = true;
    BuildPath();

    DWORD attrs = GetFileAttributesA(s_iniPath);
    bool iniExists = (attrs != INVALID_FILE_ATTRIBUTES) &&
                     !(attrs & FILE_ATTRIBUTE_DIRECTORY);

    if (!iniExists) {
        Log::Mod("Config: INI not found at %s (will create on first write)", s_iniPath);
        ImportLegacyEWMConfig();
    } else {
        Log::Mod("Config: Using %s", s_iniPath);
    }
}

int GetInt(const char* key, int defaultValue)
{
    if (!s_loaded) Load();
    return (int)GetPrivateProfileIntA(SECTION, key, defaultValue, s_iniPath);
}

void SetInt(const char* key, int value)
{
    if (!s_loaded) Load();
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    WritePrivateProfileStringA(SECTION, key, buf, s_iniPath);
}

bool GetString(const char* key, char* buf, size_t bufSize, const char* defaultValue)
{
    if (!s_loaded) Load();
    if (!buf || bufSize == 0) return false;
    DWORD ret = GetPrivateProfileStringA(
        SECTION, key,
        defaultValue ? defaultValue : "",
        buf, (DWORD)bufSize, s_iniPath);
    return ret > 0 && buf[0] != '\0';
}

void SetString(const char* key, const char* value)
{
    if (!s_loaded) Load();
    WritePrivateProfileStringA(SECTION, key, value ? value : "", s_iniPath);
}

const char* GetPath()
{
    if (!s_loaded) Load();
    return s_iniPath;
}

}  // namespace Config
