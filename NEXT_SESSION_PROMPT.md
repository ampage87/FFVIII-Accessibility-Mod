# Next Session Prompt: v0.17.8.18.1 ready to push (Chapter 3 closing)

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.18.1 BAT-validated, awaiting Aaron's push.** Chapter 3 (Scan-on-allies fix) is a one-patch ship: the local tree has all the changes; CHANGELOG, DEVNOTES, and the source file are aligned at v0.17.8.18.1; the BAT log confirmed both the success case (ally Squall scan) and the zero-regression case (Bite Bug enemy scan). No follow-on cleanup needed.

**BAT evidence (already captured 2026-05-28 17:05-17:07):**
```
[SCAN-CACHE] Captured slot=1 name='Squall' monsterId=0x00 hasDesc=1
[SCAN-TTS] Auto-announce slot=1 msg='Squall. Uses a sword called a gunblade.
           Special skill is Renzokuken, using the gunblade. Silent, and a
           bit cold. Press numbers 0 through 9 for details.'
[SCAN-TTS] SpeakField slot=1 fieldId=2 msg='Uses a sword called a gunblade...'
[SCAN-CACHE] Captured slot=3 name='Bite Bug' monsterId=0x2C hasDesc=1
```
Squall's `+0xB3` byte was `0x00` (a real scan-table index, not stale junk -- the original architectural assumption was wrong on every count). Both the auto-announce path and the key-`2` re-query path worked.

## If Aaron has pushed v0.17.8.18.1

Call `github:list_commits` first. Confirm new HEAD differs from `b7067354` (the prior v0.17.8.17.8 Chapter 2 close). Update DEVNOTES top section with the new HEAD sha. Then open Chapter 4 on whatever Aaron raises next.

If Aaron has NOT yet pushed, GitHub HEAD will still read `b7067354` -- the push utility is `Utilities/push_to_github.ps1`. It refuses unless CHANGELOG top heading matches `FF8OPC_VERSION` in `src/ff8_accessibility.h`; both are at `0.17.8.18.1` (already verified).

## Other open work (NOT this session's focus)

- **Chapter 2 carry-over:** Bug #8 NAMES FIELD entity catalog -- documented follow-up, needs dream-field model-ID observation before fixing.
- **Optional Chapter 3 follow-up (low priority):** complete playable-cast monster_id mapping. We confirmed Squall = 0x00 and the universal lookup works; the other 10 characters' IDs aren't needed for correctness (the engine resolves them at runtime) but would be useful documentation if we ever want a per-character override layer. Collect during a future battle BAT by scanning each party member and reading the `monsterId=` from the SCAN-CACHE line. NOT urgent.
- Chase-chapter carry-over (v0.15.9.8.3 bridge catch + v0.15.3.1 chase-agent summary log).
- Source-file refactor queue (only if approaching 80 KB).
- `DEVNOTES_HISTORY.md` trim (v0.17.8.7 cardgamemaster narrative + the now-pushed bug #10 chapter overdue for migration).
- Plan & Research Documents update (Dollet countdown doc).
- GitHub issue #27 (R key "No SeeD rank yet" -- `FIELD_H_OFFSET=0xF94` hypothesis).
