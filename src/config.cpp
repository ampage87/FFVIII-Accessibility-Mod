// config.cpp - Persistent accessibility settings via Windows INI file.
// See config.h for interface documentation.
// v0.13.51: Introduced.
// v0.14.106: Adds an auto-generated commented INI template so users who
//            open ff8_accessibility.ini in a text editor can see what each
//            option does and what values are accepted.

#include "config.h"
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"
#include <cstring>
#include <cstdio>
#include <cerrno>

namespace Config {

static char s_iniPath[MAX_PATH] = {};
static const char* SECTION = "Accessibility";
static bool s_loaded = false;

// v0.14.106: Marker we look for to decide whether the existing INI already
// has our commented header. Anything containing this substring counts.
static const char* INI_MARKER = "FF8 Accessibility Mod settings";

// v0.14.106: The commented INI template. printf-style format string with
// placeholders for the current values, applied via fprintf so the file we
// write reflects whatever the user already had set.
//
// Field order in the format string:
//   1. speech_rate          (int)
//   2. speech_volume        (int)
//   3. speech_voice_id      (string)
//   4. game_volume          (int)
//   5. sfx_volume           (int)
//   6. tts_duck_enabled     (int 0/1)
//   7. sfx_duck_ratio       (int)
//   8. ewm_enabled          (int 0/1)
static const char* INI_TEMPLATE =
"; ============================================================================\n"
"; FF8 Accessibility Mod settings\n"
"; ============================================================================\n"
"; This file stores your accessibility preferences across game sessions.\n"
"; The mod creates and updates it automatically as you adjust settings\n"
"; in-game with the function-key shortcuts. You can also hand-edit values\n"
"; here while the game is closed; the mod picks up your edits on next launch.\n"
";\n"
"; Lines starting with `;` are comments and are ignored by the mod. Each\n"
"; setting below shows its in-game keyboard shortcut (when applicable),\n"
"; the accepted value range, and what the value means.\n"
"; ============================================================================\n"
"\n"
"[Accessibility]\n"
"\n"
"; --- Speech (SAPI) ---------------------------------------------------------\n"
"\n"
"; Speech rate. Range: -10 (slowest) to 10 (fastest). Default 3.\n"
"; In-game: F4 = faster, F3 = slower.\n"
"speech_rate=%d\n"
"\n"
"; Speech volume. Range: 0 (silent) to 100 (loudest). Default 100.\n"
"; In-game: Shift+F4 = louder, Shift+F3 = quieter.\n"
"speech_volume=%d\n"
"\n"
"; SAPI voice token ID. The full registry path of the voice to use.\n"
"; In-game: F1 cycles through installed voices (saved automatically).\n"
"; Leave blank to use the system default voice.\n"
"speech_voice_id=%s\n"
"\n"
"; --- Game audio ------------------------------------------------------------\n"
"\n"
"; Background music volume. Range: 0 (silent) to 100 (loudest). Default 10.\n"
"; In-game: F8 = louder, F7 = quieter.\n"
"game_volume=%d\n"
"\n"
"; Sound-effects volume. Range: 0 (silent) to 100 (loudest). Default 100.\n"
"; In-game: F6 = louder, F5 = quieter.\n"
"sfx_volume=%d\n"
"\n"
"; Audio ducking. When enabled, BGM and SFX are temporarily quieted while\n"
"; the mod is speaking, so speech remains intelligible. Values: 0 = off,\n"
"; 1 = on. Default 1. In-game: F2 toggles.\n"
"tts_duck_enabled=%d\n"
"\n"
"; Ducking depth as a percentage. Lower = more ducking (game audio drops\n"
"; further while speech is active). Range: 0 to 100. Default 30.\n"
"; No in-game shortcut; hand-edit this file to tune.\n"
"sfx_duck_ratio=%d\n"
"\n"
"; --- Battle ----------------------------------------------------------------\n"
"\n"
"; Enhanced Wait Mode (EWM). Freezes ATB during menu navigation and damage\n"
"; announcements so blind players have time to read and decide without time\n"
"; pressure. Values: 0 = off, 1 = on. Default 1. In-game: O toggles.\n"
"ewm_enabled=%d\n";

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

// v0.14.106: Returns true if the INI on disk already contains our marker
// comment (i.e. has the commented template). Used to decide whether a legacy
// (un-commented) INI needs to be upgraded.
static bool HasTemplateMarker()
{
    FILE* f = fopen(s_iniPath, "r");
    if (!f) return false;
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, INI_MARKER)) {
            found = true;
            break;
        }
    }
    fclose(f);
    return found;
}

// v0.14.106: Write (or rewrite) the INI with the commented template, preserving
// any existing values that may already be on disk. Safe to call when the file
// doesn't exist yet (all reads will fall back to defaults).
//
// We open with "w" (truncate) and write the full template via fprintf. After
// this returns, the file always has the commented header. Subsequent
// SetInt/SetString calls go through WritePrivateProfileStringA which preserves
// existing comments and whitespace when updating in-place.
static void EnsureTemplate()
{
    // Read current values (or defaults if file is missing). These match the
    // defaults documented in the template comments so behavior is consistent.
    int  speechRate    = (int)GetPrivateProfileIntA(SECTION, "speech_rate",      3,   s_iniPath);
    int  speechVolume  = (int)GetPrivateProfileIntA(SECTION, "speech_volume",    100, s_iniPath);
    int  gameVolume    = (int)GetPrivateProfileIntA(SECTION, "game_volume",      10,  s_iniPath);
    int  sfxVolume     = (int)GetPrivateProfileIntA(SECTION, "sfx_volume",       100, s_iniPath);
    int  duckEnabled   = (int)GetPrivateProfileIntA(SECTION, "tts_duck_enabled", 1,   s_iniPath);
    int  duckRatio     = (int)GetPrivateProfileIntA(SECTION, "sfx_duck_ratio",   30,  s_iniPath);
    int  ewmEnabled    = (int)GetPrivateProfileIntA(SECTION, "ewm_enabled",      1,   s_iniPath);

    char voiceId[512] = {};
    GetPrivateProfileStringA(SECTION, "speech_voice_id", "",
                              voiceId, sizeof(voiceId), s_iniPath);

    FILE* f = fopen(s_iniPath, "w");
    if (!f) {
        Log::Mod("Config: EnsureTemplate failed to open %s for writing (err=%d)",
                   s_iniPath, errno);
        return;
    }
    fprintf(f, INI_TEMPLATE,
            speechRate, speechVolume, voiceId,
            gameVolume, sfxVolume, duckEnabled, duckRatio,
            ewmEnabled);
    fclose(f);
    Log::Mod("Config: Wrote commented INI template to %s "
             "(rate=%d vol=%d game=%d sfx=%d duck=%d ratio=%d ewm=%d voice='%s')",
             s_iniPath,
             speechRate, speechVolume, gameVolume, sfxVolume,
             duckEnabled, duckRatio, ewmEnabled, voiceId);
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
        Log::Mod("Config: INI not found at %s (creating commented template)", s_iniPath);
        ImportLegacyEWMConfig();  // import old ewm_config.txt if it's around
        EnsureTemplate();         // write fresh template, picking up the imported value
    } else {
        Log::Mod("Config: Using %s", s_iniPath);
        // v0.14.106: Upgrade legacy un-commented INIs to the commented template,
        // preserving the user's existing values. One-time per install: after
        // the rewrite, the marker is present and we skip this branch.
        if (!HasTemplateMarker()) {
            Log::Mod("Config: Existing INI lacks comment header; upgrading...");
            EnsureTemplate();
        }
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
