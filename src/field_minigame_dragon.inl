// field_minigame_dragon.inl -- Laguna vs. the dragon (`tvglen3`, the movie shoot).
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included after field_minigame_bgbtl.inl, inside namespace FieldNavigation, so
// it can ring that module's block tone rather than owning a second one. The
// derivation of every constant is in dragon_fight_model.inl.
//
// WHAT THIS MODULE DOES, AND WHAT IT DELIBERATELY DOES NOT
//
// It rings a tone the instant the dragon's attack animation starts, reports
// each side's health in HITS, and reads the board back on "/".
//
// v0.44.0 adds the two things the first BAT asked for -- a GAME CONTROLS screen
// that names the player's own keys, and an F9 SKIP -- and both need to stop the
// dragon hurting Laguna while they are up. They do it with the ONE variable the
// fight already uses for exactly that: `var[1028]`, the guard flag, which
// `laguna::bougyo` sets while the block key is down and `dragon::default` reads
// after its animation. Pinning it to 1 is the same state the game itself
// produces from a held key. Nothing else is written -- in particular no health,
// which is the mistake the Garden battle's skip made in v0.20.120 (zero HP is
// lethal however briefly it is held).
//
// **The window is now measured, not guessed:** the 2026-08-21 BAT logged 828 ms
// to 1610 ms from cue to hit, in two clusters. That is a long warning, so the
// block still needs no input assist -- only the cue.
//
// HOW THE CUE IS CAUGHT
//
// The attack is not a REQ -- `dragon::default` runs `ANIME(47, 1)` inline -- so
// the REQ hook the Garden battle uses is blind to it. This module hooks
// **opcode 0x30** instead, installed only while `tvglen3` is loaded and removed
// on the way out.
//
// The hook has to fire ONCE per attack, and the opcode runs every frame for the
// whole animation. It cannot dedupe on the instruction pointer, because
// `dragon::default` loops straight back to the SAME instruction for the next
// attack -- two attacks in a row share a pointer. So it uses the engine's own
// answer: the dispatcher keeps a "this instruction is starting" bit, `1 <<
// [ctx+0x174]` in `[ctx+0x175]`, and clears it whenever a handler returns
// without bit 1 set. `0x00526810` tests exactly that bit to decide whether to
// start the animation or to poll it, so testing it before chaining is asking
// the same question the handler is about to ask.

namespace DragonFight {

using namespace DragonFightModel;

// Opcode handlers all share this signature. field_nav_mapjump_diag.inl and
// field_minigame_bgbtl.inl each declare their own inside their own namespace,
// so there is nothing to reach for from here -- this is the third copy of one
// line, not a dependency on either of them.
typedef int (__cdecl *OpcodeFunc_t)(void* ctx, int param);

static const uint16_t DF_FIELD_ID = 948;    // tvglen3

// ---------------------------------------------------------------- hook state
//
// v0.45.0: the hook records EVERY animation this field starts, not only the one
// it cues on. The 2026-08-21 BAT had a hit with no cue in front of it -- "one
// attack, the one we flag now, is a longer rearing-up attack, while the one we
// are missing is a quick attack" -- and the only way to settle whether that is a
// missed hook or a second signature is to have the log list what the dragon
// actually started. Four words a frame into a ring, drained in Update().
struct AnimeEvent { uint32_t ctx; uint16_t ip; int16_t chan, a, b; uint16_t speed; };
static const int DF_RING = 32;
static AnimeEvent   s_ring[DF_RING];
static volatile LONG s_ringWrite = 0;       // written by the hook only
static LONG         s_ringRead  = 0;

static uint32_t     s_origAnime = 0;
static bool         s_installed = false;

// Deliberately tiny: a handful of reads, one ring slot, chain. Anything more
// belongs in Update().
static int __cdecl HookedAnime(void* ctx, int param)
{
    if (s_installed && ctx != nullptr) {
        __try {
            const uint8_t bit   = *(const uint8_t*)((const char*)ctx + DF_VMCTX_FLAG_BIT);
            const uint8_t flags = *(const uint8_t*)((const char*)ctx + DF_VMCTX_FLAGS);
            if (bit < 8 && (flags & (uint8_t)(1u << bit))) {         // first frame only
                const signed char sp = *(const signed char*)((const char*)ctx + DF_VMCTX_SP);
                const LONG w = s_ringWrite;
                AnimeEvent& e = s_ring[w & (DF_RING - 1)];
                e.ctx   = (uint32_t)(uintptr_t)ctx;
                e.ip    = *(const uint16_t*)((const char*)ctx + DF_VMCTX_IP);
                e.chan  = (int16_t)param;
                e.b     = *(const int16_t*)((const char*)ctx + (int)sp * 4);
                e.a     = *(const int16_t*)((const char*)ctx + (int)sp * 4 - 4);
                e.speed = *(const uint16_t*)((const char*)ctx + DF_VMCTX_SPEED);
                s_ringWrite = w + 1;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A bad read here must never take the game down. Drop the event.
        }
    }
    return ((OpcodeFunc_t)s_origAnime)(ctx, param);
}

static bool Install()
{
    if (s_installed) return true;
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        Log::Field("FieldNavigation: [DRAGON] cannot install -- opcode table not resolved");
        return false;
    }
    uint32_t* entry = &FF8Addresses::pExecuteOpcodeTable[DF_OPCODE_ANIME];
    s_origAnime = *entry;

    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
        Log::Field("FieldNavigation: [DRAGON] VirtualProtect failed (err=%lu)", GetLastError());
        return false;
    }
    *entry = (uint32_t)(uintptr_t)&HookedAnime;
    DWORD restore = 0;
    VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);

    s_installed = true;
    s_ringRead = s_ringWrite;
    Log::Field("FieldNavigation: [DRAGON] ANIME hook installed: [0x030] 0x%08X -> 0x%08X",
               s_origAnime, (uint32_t)(uintptr_t)&HookedAnime);
    return true;
}

static void Uninstall()
{
    if (!s_installed) return;
    uint32_t* entry = &FF8Addresses::pExecuteOpcodeTable[DF_OPCODE_ANIME];
    DWORD oldProtect = 0;
    if (VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
        *entry = s_origAnime;
        DWORD restore = 0;
        VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);
    }
    s_installed = false;
    Log::Field("FieldNavigation: [DRAGON] ANIME hook removed");
}

// ---------------------------------------------------------------- fight state
static bool  s_onField     = false;
static bool  s_briefed     = false;
static bool  s_slashWas    = false;
static int   s_lagunaHp    = -1;
static int   s_dragonHp    = -1;
static int   s_active      = -1;
// v0.47.0: the outcome is decided by the attack's own recovery animation, not
// by a deadline -- DF_OUTCOME_MS_V now sizes only the post-briefing grace.

// The cue queue lives in dragon_fight_model.inl: it is pure state, it is what
// has been wrong three times running, and the probe exercises it there.

// The Game Controls screen and the skip. Both pin the guard flag; the skip also
// leaves it pinned, which is the whole of "the dragon cannot hurt Laguna".
static bool  s_briefing    = false;
static bool  s_briefedOnce = false;   // per VISIT, not per attempt
static DWORD s_briefAt     = 0;       // when the box opened -- it times itself out
static DWORD s_graceUntil  = 0;       // guard held past the close for one wind-up
static int   s_round       = 0;
static bool  s_needKeyUp   = false;   // Enter must be seen UP before it counts
static bool  s_skip        = false;
static bool  s_f9Was       = false;
static uint32_t s_taughtMask = 0;     // masks already named this briefing

// POD + SEH (MSVC C2712 -- tests/lint_seh.py): no std::string in this frame.
static bool DfReadVars(int* lagunaHp, int* dragonHp, int* blocking, int* active)
{
    bool ok = false;
    __try {
        *lagunaHp = *(volatile const uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_LAGUNA_HP);
        *dragonHp = *(volatile const uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_DRAGON_HP);
        *blocking = *(volatile const uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_BLOCKING);
        *active   = *(volatile const uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_ACTIVE);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// The only write this module makes, and only to the byte `laguna::bougyo` writes
// every frame the block key is held. POD + SEH (MSVC C2712).
static void DfPinGuard(int on)
{
    __try { *(volatile uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_BLOCKING) = (uint8_t)(on ? 1 : 0); }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void DfSetDragonHp(int hp)
{
    __try { *(volatile uint8_t*)(uintptr_t)DfVarAddr(DF_VAR_DRAGON_HP) = (uint8_t)hp; }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void DfSay(const char* text, bool interrupt)
{
    if (!text || !*text) return;
    ScreenReader::Speak(text, interrupt);
    Log::Field("FieldNavigation: [DRAGON] \"%s\"", text);
}

static void Reset()
{
    // A box left open when the scene ends is a box drawn over whatever comes
    // next, and this one is closed from three places for the same reason the
    // space rescue's freeze is released from four: no arrangement of Enter, F9,
    // a timeout, a death or walking out may leave it up.
    GardenBattle::CloseBriefDialog();
    s_onField  = false;
    s_briefed  = false;
    s_briefing = false;
    s_briefedOnce = false;
    s_briefAt  = 0;
    s_graceUntil = 0;
    s_round    = 0;
    s_needKeyUp = false;
    s_skip     = false;
    s_taughtMask = 0;
    s_lagunaHp = s_dragonHp = s_active = -1;
    DfCueClear();
}

// The player's own key for a mask, learned by the Garden battle's learner from
// the same button word this fight reads. Seeded with its four defaults, so the
// first sentence has a name in it even before anything has been pressed.
static void DfKeyName(char* dst, size_t n, unsigned mask)
{
    GardenBattle::CopyKeyName(dst, n, (uint32_t)mask);
}

// The id comes from the community mapId table the mod's display names are built
// from; the NAME comes from the engine. Either is enough, and accepting both
// means a wrong id in that table cannot make this module silent.
static bool OnDragonField(uint16_t fid)
{
    if (fid == DF_FIELD_ID) return true;
    const char* n = FF8Addresses::pCurrentFieldName;
    if (!n) return false;
    for (int i = 0; i < 8; i++) {
        char a = n[i], b = DF_FIELD[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return false;
        if (b == '\0') return true;
    }
    return false;   // longer than the name, so not it
}

static void Update()
{
    const uint16_t fid = FF8Addresses::pCurrentFieldId
                       ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    const bool here = OnDragonField(fid);

    if (!here) {
        if (s_onField) {
            Log::Field("FieldNavigation: [DRAGON] left tvglen3");
            Uninstall();
            Reset();
        }
        return;
    }

    if (!s_onField) {
        s_onField = true;
        Install();
        Log::Field("FieldNavigation: [DRAGON] entered tvglen3 (field %u)", (unsigned)fid);
    }

    int lagunaHp = 0, dragonHp = 0, blocking = 0, active = 0;
    if (!DfReadVars(&lagunaHp, &dragonHp, &blocking, &active)) return;

    const DWORD now = GetTickCount();
    const bool ready = DfFightReady(lagunaHp, dragonHp, active);

    // "/" -- bound only while this field is loaded, so it cannot collide. Read
    // before every early return below, so the key works during the briefing and
    // a held slash cannot fire once one of them stops returning.
    const bool slashDown = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    if (slashDown && !s_slashWas) {
        char line[192];
        DfStatusLine(lagunaHp, dragonHp, blocking != 0, line, sizeof(line));
        DfSay(line, true);
    }
    s_slashWas = slashDown;

    // ---- the skip ----------------------------------------------------------
    //
    // F9 does the two things a skip can do here without forging anything: it
    // pins the guard so the dragon's attacks all land on a block, and it sets
    // the dragon to ONE HIT from defeat so the game's own win path -- the
    // `var[1032] == 0` branch inside `dragon::damage` -- runs on the next swing.
    // The scene ends the way it always does; it just gets there in one hit.
    const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !s_f9Was && ready) {
        s_skip = true;
        DfSetDragonHp(DF_DRAGON_HIT);
        s_dragonHp = DF_DRAGON_HIT;
        char atkKey[24], msg[256];
        DfKeyName(atkKey, sizeof(atkKey), DF_MASK_ATTACK);
        snprintf(msg, sizeof(msg),
                 "Skip on. The dragon cannot hurt Laguna. One hit left -- tap %s to finish.",
                 atkKey);
        DfSay(msg, true);
        Log::Field("FieldNavigation: [DRAGON] skip armed");
    }
    s_f9Was = f9;
    if (s_skip && ready) DfPinGuard(1);

    // ---- the Game Controls screen -----------------------------------------
    //
    // Opened when the scene hands control over -- BOTH bars at full, which is
    // what `dic::defalut` writes to start the fight and `laguna::damage` writes
    // again on a retry. v0.43.0 opened on `var[1030] == 1` alone and caught the
    // scene twelve seconds early, with `laguna=216 dragon=0` still in the bytes.
    if (DfFightStarting(lagunaHp, dragonHp, active) && !s_briefed) {
        s_briefed    = true;
        s_lagunaHp   = lagunaHp;
        s_dragonHp   = dragonHp;
        s_round++;
        DfCueClear();

        // A RESTART DOES NOT RE-OPEN THE BOX. See dragon_fight_model.inl: the
        // 2026-08-21 log has the retry box swallowing every cue for seventy
        // seconds because it was waiting for a key the player had no reason to
        // press again.
        if (s_briefedOnce) {
            char blk[24], atk[24], line[192];
            DfKeyName(blk, sizeof(blk), DF_MASK_BLOCK);
            DfKeyName(atk, sizeof(atk), DF_MASK_ATTACK);
            snprintf(line, sizeof(line),
                     "Round %d. Both back to full. Hold %s to block, tap %s to attack.",
                     s_round, blk, atk);
            DfSay(line, true);
            s_graceUntil = now + DF_GRACE_MS;   // nothing in flight may land yet
            if (!s_graceUntil) s_graceUntil = 1;             // GetTickCount wrap
            s_active = active;
        } else {
            s_briefedOnce = true;
            s_briefing    = true;
            s_briefAt     = now ? now : 1;
            s_needKeyUp   = true;   // Enter may still be down from the last screen
            s_taughtMask  = 0;
            char blockKey[24], atkKey[24], msg[512];
            DfKeyName(blockKey, sizeof(blockKey), DF_MASK_BLOCK);
            DfKeyName(atkKey,   sizeof(atkKey),   DF_MASK_ATTACK);

            // ---- AND IT GETS A BOX, like the other two ---------------------
            //
            // Aaron: *"We want to be consistent whenever we implement a Game
            // Controls dialog prior to the start of a mini-game."* The Garden
            // battle and the space rescue both put their controls in one of
            // FF8's own dialog windows; this fight was the odd one out, spoken
            // and nothing more, so a sighted player beside him had no idea what
            // the mod had just said.
            //
            // SEPARATE TEXT FROM THE SPOKEN BRIEF, deliberately, and for the
            // same reason the other two have one: the spoken version can afford
            // to be a paragraph, the box cannot. FF8's window is 320 px wide and
            // its own measurer sizes the box to the text, so anything past ~34
            // columns wraps and anything past ten lines SCROLLS -- and a box
            // that scrolls loses its first lines behind a frozen frame. Six
            // lines, none over 34 columns, checked by the probe.
            //
            // Nothing here freezes the field: this fight pauses by pinning the
            // guard flag, so the window draws and finishes normally.
            char screen[256];
            snprintf(screen, sizeof(screen), DF_SCREEN_FMT, blockKey, atkKey);
            const bool box = GardenBattle::OpenBriefDialog(screen);
            Log::Field("FieldNavigation: [DRAGON] controls box %s",
                       box ? "open in the game's own window"
                           : "NOT open -- spoken only (see the BGBTL-DLG line above)");

            snprintf(msg, sizeof(msg),
                     "Game controls. Dragon fight. Hold %s to block, tap %s to attack. "
                     "A rising two note tone means the dragon is winding up -- hold block "
                     "until it lands. Three hits and Laguna is down; the dragon takes ten. "
                     "Press any key to hear what it does. Slash for health. "
                     "F9 for the skip. Press Enter to start.", blockKey, atkKey);
            DfSay(msg, true);
            Log::Field("FieldNavigation: [DRAGON] briefing open (block=%s attack=%s)",
                       blockKey, atkKey);
        }
    }
    if (active == 0 && s_active == 1) {
        // The fight ended -- a death, and the game's own retry prompt is up.
        // `s_briefed` re-arms the START detection; `s_briefedOnce` does NOT, so
        // the next round announces itself instead of re-opening the box.
        s_briefed  = false;
        s_briefing = false;
        GardenBattle::CloseBriefDialog();   // never leave a box behind a dead fight
        DfCueClear();
    }

    if (s_briefing) {
        // Nothing can hurt Laguna while the box is up. This is the guard flag
        // the fight already uses, held the way a held key would hold it.
        DfPinGuard(1);
        s_lagunaHp = lagunaHp;
        s_dragonHp = dragonHp;

        // Name each key as it is pressed, once each, from the same learner and
        // the same button word the fight itself reads.
        const uint32_t rose = GardenBattle::LearnButtons();
        for (int i = 0; i < 2; i++) {
            const unsigned mask = i ? DF_MASK_ATTACK : DF_MASK_BLOCK;
            if (!(rose & mask) || (s_taughtMask & mask)) continue;
            s_taughtMask |= mask;
            char key[24], line[96];
            DfKeyName(key, sizeof(key), mask);
            snprintf(line, sizeof(line), "%s. %s.", key, i ? "Attack" : "Block");
            DfSay(line, true);
        }

        // Enter starts it -- and only once it has been seen UP, because the same
        // key may still be down from whatever opened this scene. (The Garden
        // battle learned that one the hard way: its retry briefing was dismissed
        // by the keypress that caused it.)
        const bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        if (s_needKeyUp) { if (!enter) s_needKeyUp = false; }

        // **AND IT CLOSES ITSELF.** A box that waits forever for one key holds
        // the guard pinned and swallows every cue, which is what the 2026-08-21
        // log caught: seventy seconds of an unloseable, unannounced fight. A
        // timeout cannot make the scene worse; not having one already did.
        const bool timedOut = !s_needKeyUp && s_briefAt &&
                              (DWORD)(now - s_briefAt) > (DWORD)DF_BRIEF_TIMEOUT_MS;

        // F9 also starts it. A player who has decided to skip the fight has by
        // definition finished with the controls screen, and a second key out of
        // the box is worth having after one that could hold it open forever.
        if ((enter && !s_needKeyUp) || timedOut || s_skip) {
            s_briefing   = false;
            s_briefAt    = 0;
            GardenBattle::CloseBriefDialog();
            s_ringRead   = s_ringWrite;       // drop anything that rang behind the box
            DfCueClear();
            // The guard stays pinned for one wind-up: an attack that started
            // behind the box must not land on a player who heard no cue for it.
            s_graceUntil = now + DF_GRACE_MS;
            if (!s_graceUntil) s_graceUntil = 1;             // GetTickCount wrap
            DfSay(timedOut ? "Starting." : "Game start.", true);
            Log::Field("FieldNavigation: [DRAGON] briefing closed%s",
                       timedOut ? " (timed out)" : (s_skip ? " (skip)" : ""));
        }
        s_active = active;
        return;                               // the fight is on hold until it closes
    }

    // The grace window after the box closes or a round restarts.
    if (s_graceUntil) {
        if ((LONG)(now - s_graceUntil) < 0) {
            DfPinGuard(1);
        } else {
            s_graceUntil = 0;
            if (!s_skip) DfPinGuard(0);
            Log::Field("FieldNavigation: [DRAGON] grace over -- the fight is live");
        }
    }


    // ---- drain the ring ----------------------------------------------------
    //
    // Every animation the field started since the last tick, logged whether it
    // is cued or not. That is the point: a hit with no cue in front of it is
    // either a signature this does not know or a hook that did not fire, and
    // one line per animation tells them apart without another guess.
    while (s_ringRead != s_ringWrite) {
        const AnimeEvent e = s_ring[s_ringRead & (DF_RING - 1)];
        s_ringRead++;
        const bool isAttack   = DfIsAttackAnime(e.chan, e.a, e.b);
        const bool isRecovery = DfIsRecoveryAnime(e.chan, e.a, e.b);
        const bool fast       = (e.speed == DF_SPEED_FAST);
        // v0.47.0: CHANNEL 2 ONLY. The all-channels trace is what identified the
        // attack's two call sites and its recovery, and it cost two hundred
        // lines a fight to do it. Both questions are answered, so the noise
        // goes and the gate stays -- flip this on if another channel ever needs
        // watching.
#define DRAGON_ANIME_TRACE_ALL 0
#if !DRAGON_ANIME_TRACE_ALL
        if (e.chan == DF_ATTACK_CHAN)
#endif
        Log::Field("FieldNavigation: [DRAGON] anime ctx=0x%08X ip=%u ch=%d (%d,%d) speed=%u%s%s",
                   e.ctx, (unsigned)e.ip, (int)e.chan, (int)e.a, (int)e.b,
                   (unsigned)e.speed, isAttack ? "  ATTACK" : (isRecovery ? "  recovery" : ""),
                   (isAttack && fast) ? " (quick)" : "");

        // The recovery is the attack's own full stop: the block check has been
        // made. Give the damage script its beat to run, then decide.
        if (isRecovery) {
            bool named = false;
            const int c = DfCueForRecovery(e.ip, &named);
            if (c >= 0) {
                s_cues[c].resolveAt = now + DF_RESOLVE_MS;
                if (!s_cues[c].resolveAt) s_cues[c].resolveAt = 1;   // tick wrap
                if (!named)
                    Log::Field("FieldNavigation: [DRAGON] recovery ip=%u matched "
                               "no cue by gap -- fell back to oldest (ip=%u)",
                               (unsigned)e.ip, (unsigned)s_cues[c].ip);
            }
            continue;
        }
        if (!isAttack) continue;

        // The tone is NOT gated on the health bytes being sane. v0.44.0 gated it
        // on `ready` and dropped the cue silently when that was false; a cue the
        // player does not get is worse than one that arrives a beat early.
        if (!s_briefed || s_briefing) continue;

        // v0.50.0: **NOT WHILE THE SKIP IS ARMED.** F9 pins the guard, so the
        // dragon cannot land anything -- and the 2026-08-21 log has the cue
        // ringing anyway and then "Blocked." a second later, which is a tone
        // telling the player to do something that does not matter followed by
        // a report contradicting the skip's own promise that the dragon cannot
        // hurt Laguna. The only thing left to do after F9 is tap the attack
        // key, and the skip line already says so. Logged, not spoken.
        if (s_skip) {
            Log::Field("FieldNavigation: [DRAGON] attack%s during skip -- no cue",
                       fast ? " QUICK" : "");
            continue;
        }

        GardenBattle::PlayTone();
        DfCuePush(now, fast, e.ip);
        Log::Field("FieldNavigation: [DRAGON] attack cue%s (laguna=%d dragon=%d block=%d)",
                   fast ? " QUICK" : "", lagunaHp, dragonHp, blocking);
    }

    // ---- what the cue came to ---------------------------------------------
    //
    // The block flag is read at the END of the animation, so the outcome is not
    // knowable when the cue rings. A drop in Laguna's health is the hit; the
    // absence of one by the deadline is the block. The delta is logged so the
    // next BAT can tighten the window on a measurement rather than on this
    // guess.
    // Attacks overlap -- `dragon::default` and the REQ'd `dragon::kougeki` both
    // swing -- so v0.44.0's single pending slot lost the outcome of the first
    // one every time a second cue arrived. Each cue now carries its own
    // deadline.
    if (ready && lagunaHp < s_lagunaHp) {
        const DWORD at = DfCueTakeForHit();
        if (at) Log::Field("FieldNavigation: [DRAGON] hit %lu ms after the cue",
                           (unsigned long)(now - at));
        else    Log::Field("FieldNavigation: [DRAGON] **hit with no cue outstanding**");
    }
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        if (!s_cues[i].at) continue;

        // Resolved by the recovery, and the damage script has had its beat: no
        // drop in health means the guard was up. This is the ONLY path that
        // says "Blocked." -- v0.46.0 said it on a fixed deadline and a wind-up
        // the player interrupted came out as a block that never happened.
        if (s_cues[i].resolveAt && (LONG)(now - s_cues[i].resolveAt) >= 0) {
            // v0.48.0: NOT while a newer cue is still in the air. "Blocked." is
            // heard as `you can let go now`, and on 2026-08-21 it arrived one
            // second after the next wind-up had already rung -- Laguna released
            // the guard into it and went down. The block still happened; there
            // is just nothing safe to say about it yet, and the cue that is
            // still live will speak for both.
            const bool superseded = DfCueNewerLive(i);
            s_cues[i].at = 0; s_cues[i].resolveAt = 0; s_cues[i].ip = 0;
            if (superseded) {
                Log::Field("FieldNavigation: [DRAGON] blocked, held silent -- "
                           "a newer cue is still live");
                continue;
            }
            // Only a fight that is still on can have blocked anything. With
            // Laguna already down, the absence of a hit means nothing.
            if (ready && lagunaHp > 0) DfSay("Blocked.", false);
            continue;
        }

        // A cue whose recovery never arrived. Nothing true can be said about
        // it, so it goes quietly and says so in the log.
        if ((DWORD)(now - s_cues[i].at) > (DWORD)DF_CUE_MAX_MS) {
            s_cues[i].at = 0; s_cues[i].resolveAt = 0; s_cues[i].ip = 0;
            Log::Field("FieldNavigation: [DRAGON] cue expired with no recovery");
        }
    }

    // ---- health ------------------------------------------------------------
    if (!ready) { s_active = active; return; }
    if (s_lagunaHp >= 0 && lagunaHp != s_lagunaHp && active == 1) {
        char line[96];
        DfHealthLine(true, lagunaHp, line, sizeof(line));
        if (lagunaHp < s_lagunaHp) {
            char hit[128];
            snprintf(hit, sizeof(hit), "Hit. %s", line);
            DfSay(hit, false);
        } else {
            DfSay(line, false);
        }
    }
    if (s_dragonHp >= 0 && dragonHp < s_dragonHp && active == 1) {
        char line[96];
        DfHealthLine(false, dragonHp, line, sizeof(line));
        DfSay(line, false);
    }

    s_lagunaHp = lagunaHp;
    s_dragonHp = dragonHp;
    s_active   = active;

}

}  // namespace DragonFight
