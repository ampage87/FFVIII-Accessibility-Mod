// field_nav_caraway_diag.inl - v0.18.3.260 (#83 diagnostic, LOCAL)
//
// Per-frame, CHANGE-ONLY entity-struct watcher gated to Caraway's Mansion
// (glfurin4). Purpose: locate the byte the scene director sets to make the
// standing party-character actors (Quistis/Zell/Selphie) talkable.
//
// Decisive either way:
//   * If a byte in the interaction-flag window flips at the moment the game
//     lets the player talk to an actor, THAT is the talk-enable signal the
//     catalog must monitor (#83) -- we then read it alongside talkonoff@0x24B.
//   * If NOTHING flips when the actor becomes talkable, the engine allows
//     confirm-talk unconditionally for these scene actors and the fix is
//     structural: keep positioned non-active-party playable characters as
//     talkable NPCs (labelled by proper name), and lean on the runtime
//     dialog-confirmation layer to prune.
//
// Behaviour-neutral: logging only, no info.* or catalog mutation. Set
// CARAWAY_SCENE_DIAG 0 to compile it out entirely.
//
// Included from field_navigation.cpp inside the FieldNavigation namespace,
// AFTER field_nav_battlepause.inl and BEFORE Update(), so file-scope statics
// (s_playerEntityIdx, s_symOthersOffset, s_symNames, s_symNameCount,
// MAX_ENTITIES, ENTITY_STRIDE) are already visible. Reads the live "others"
// array; guarded by its own __try in addition to Update()'s.

// v0.18.3.265: disabled. This per-frame glfurin4 watcher did its job for #83 --
// it established that NO byte in the 0x244..0x257 window flips when a scene actor
// becomes talkable, which redirected the investigation to the engine itself (the
// talk-selection routine at 0x004796E0 and the inverted 0x24B polarity). #83 is
// fixed and closed; set back to 1 only if a similar flag hunt is needed again.
#define CARAWAY_SCENE_DIAG 0

#if CARAWAY_SCENE_DIAG

// Interaction-flag window. Brackets the known flags with margin and avoids the
// volatile position/anim/model regions (0x190 pos, 0x1FA tri, 0x218 model):
//   push @0x249  talk @0x24B  thru @0x24C  setpc @0x255
static const int   CARAWAY_WIN_START = 0x244;
static const int   CARAWAY_WIN_LEN   = 0x14;   // 20 bytes -> 0x244..0x257 inclusive
static uint8_t     s_carawayPrev[MAX_ENTITIES][CARAWAY_WIN_LEN] = {};
static bool        s_carawayHave[MAX_ENTITIES] = {};
static char        s_carawayField[24] = {0};
static bool        s_carawayActive = false;

static inline bool CarawayFieldGated(const char* f)
{
    // Extend this list to instrument other scene fields with the same symptom.
    return f && strcmp(f, "glfurin4") == 0;
}

static void CarawaySceneDiagTick()
{
    const char* fld = FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";

    // Field-change bookkeeping: reset baselines whenever the field name changes.
    if (strncmp(fld, s_carawayField, sizeof(s_carawayField) - 1) != 0) {
        strncpy(s_carawayField, fld, sizeof(s_carawayField) - 1);
        s_carawayField[sizeof(s_carawayField) - 1] = '\0';
        for (int i = 0; i < MAX_ENTITIES; i++) s_carawayHave[i] = false;
        s_carawayActive = CarawayFieldGated(fld);
        if (s_carawayActive)
            Log::Field("FieldNavigation: [CARAWAY-DIAG] armed on '%s' -- watching entity window "
                       "0x%03X..0x%03X (push@0x249 talk@0x24B thru@0x24C setpc@0x255), on-change only",
                       fld, CARAWAY_WIN_START, CARAWAY_WIN_START + CARAWAY_WIN_LEN - 1);
    }
    if (!s_carawayActive) return;
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;

    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        uint16_t playerTri = (s_playerEntityIdx >= 0)
            ? *(uint16_t*)(base + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA) : 0xFFFF;

        for (int i = 0; i < (int)lim; i++) {
            uint8_t*       block = base + ENTITY_STRIDE * i;
            const uint8_t* win   = block + CARAWAY_WIN_START;

            int symIdx = s_symOthersOffset + i;
            const char* sym = (symIdx >= 0 && symIdx < s_symNameCount) ? s_symNames[symIdx] : "(none)";
            uint8_t  setpc = *(block + 0x255);
            uint16_t tri   = *(uint16_t*)(block + 0x1FA);

            if (!s_carawayHave[i]) {
                memcpy(s_carawayPrev[i], win, CARAWAY_WIN_LEN);
                s_carawayHave[i] = true;
                char hex[CARAWAY_WIN_LEN * 3 + 1]; int n = 0;
                for (int b = 0; b < CARAWAY_WIN_LEN; b++)
                    n += snprintf(hex + n, (size_t)(sizeof(hex) - n), "%02X ", win[b]);
                Log::Field("FieldNavigation: [CARAWAY-DIAG] baseline slot%-2d sym='%s' setpc=%d tri=%u "
                           "win[0x%03X]: %s", i, sym, (int)setpc, (unsigned)tri, CARAWAY_WIN_START, hex);
                continue;
            }

            bool changed = false;
            char delta[CARAWAY_WIN_LEN * 16 + 1]; int dn = 0;
            for (int b = 0; b < CARAWAY_WIN_LEN; b++) {
                if (win[b] != s_carawayPrev[i][b]) {
                    changed = true;
                    dn += snprintf(delta + dn, (size_t)(sizeof(delta) - dn),
                                   "0x%03X:%02X->%02X ", CARAWAY_WIN_START + b,
                                   s_carawayPrev[i][b], win[b]);
                }
            }
            if (changed) {
                Log::Field("FieldNavigation: [CARAWAY-DIAG] CHANGE slot%-2d sym='%s' setpc=%d tri=%u "
                           "playerTri=%u | %s", i, sym, (int)setpc, (unsigned)tri,
                           (unsigned)playerTri, delta);
                memcpy(s_carawayPrev[i], win, CARAWAY_WIN_LEN);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // A diagnostic must never destabilise the field loop.
    }
}

#else
static inline void CarawaySceneDiagTick() {}
#endif
