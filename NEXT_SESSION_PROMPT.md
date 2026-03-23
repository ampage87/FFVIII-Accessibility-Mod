# NEXT SESSION PROMPT
## FF8 Accessibility Mod — Immediate Context
## Updated: 2026-03-23 (session 9 end)

---

## Current Build: v0.09.40 (source + deployed)

## What Works
- **All features from session 8** — GameAudio, naming bypass, GF acquisition TTS, menu TTS, field dialog, field navigation, save screen, etc.
- **Infirmary glitch FIXED (v0.09.40)**: SET3 opcode hook permanently disabled. FieldNavigation re-enabled with all other hooks working.
- **GitHub Actions CI** guard added: `.github/workflows/safety-checks.yml` flags if SET3 hook is accidentally re-enabled.

## What Changed This Session (Session 9)
- **Binary search** isolated the infirmary hang to HookedSet3 (SET3 opcode 0x1E hook).
- Tried MinHook → dispatch table → SEH removal → minimal wrapper. ALL cause the hang.
- The FF8 script interpreter is incompatible with any SET3 handler replacement.
- SET3 hook was only used for PSHM_W investigation (already exhausted). Shift-pattern passthrough is the working fallback.
- Builds v0.09.32–v0.09.40 were the binary search + fix sequence.

## Open Items for Next Session

### Item 1 (READY): GitHub Push
- All features working, infirmary glitch resolved.
- Safe to push source + docs.

### Item 2 (PENDING): Continue menu TTS priorities
1. Top-level menu navigation TTS
2. Save Game flow TTS
3. Save Point entity catalog integration
4. Title Screen Continue TTS

### Item 3 (PENDING): Junction Menu TTS (v0.08.88+)
- Phase plan in DEVNOTES.md. GF assignment before sub-options.

### Item 4 (FUTURE): Separate SFX volume control (GitHub Issue #8)
- F3/F4 controls BGM+FMV. Separate keys for SFX volume.

---

## Key Learnings from This Session

### SET3 Hook is PERMANENTLY FORBIDDEN
- **DO NOT** re-enable HookedSet3 under any circumstances.
- ANY interception of opcode 0x1E (MinHook, dispatch table, even a pure passthrough wrapper) hangs the infirmary scene.
- GitHub Actions CI check in `.github/workflows/safety-checks.yml` will flag violations.
- If SET3 capture is ever needed, investigate naked/asm thunk or alternative approaches.
- Code and comment in field_navigation.cpp Initialize() documents this.

---

## Recovery Instructions
1. Read DEVNOTES.md for full architecture
2. `deploy.bat` is the ONLY build script
3. Bump `FF8OPC_VERSION` in 3 locations every build
4. "BAT" → read tail of `Logs/ff8_accessibility.log`
5. Current: v0.09.40 (source+deployed)
6. GitHub push is next priority
