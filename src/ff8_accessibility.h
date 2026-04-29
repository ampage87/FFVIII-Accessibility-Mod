// ff8_accessibility.h - Core header for FF8 Original PC Accessibility Mod
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// ================================================================
// FF8 Original PC Accessibility Mod version
// Increment on every build change
// ================================================================
#define FF8OPC_VERSION "0.14.48"  // v0.14.48: Audio ducking tuning pass after v0.14.47 BAT. Three changes per Aaron's listening: (1) hold time 200ms -> 800ms on both buses to bridge gaps between menu-nav announcements (the v0.14.47 200ms hold was just short enough that volume bounced up between successive menu cursor moves). (2) BGM depth -15 dB -> -10 dB; v0.14.47 BGM duck was deeper than necessary now that ducking is working at all. (3) SFX depth -6 dB -> -15 dB; attack animation SFX still masked TTS at -6 dB, so escalated to match the depth that was previously on BGM. Pure constant changes in GameAudio::Initialize — no architecture changes.

// v0.14.47: Audio ducking Phase 2 — reusable AudioDucker module with per-bus configuration (BGM and SFX), reference-counted BeginDuck/EndDuck trigger, smoothed one-pole envelope (separate attack/release time constants), hold timer to bridge SAPI chunk gaps, and dB-based config converted to linear gain internally. New files: src/audio_ducker.h + audio_ducker.cpp. Wired into game_audio.cpp via four function-pointer accessors (GetUserBgmVolume / ApplyBgmVolume / GetUserSfxVolume / ApplySfxVolume) so the ducker stays domain-agnostic. GameAudio::Update polls ScreenReader::IsSpeaking() once per tick on the rising/falling edge to drive BeginDuck / EndDuck; existing 500ms periodic Reapply gates on !AudioDucker::IsActive(); F2 toggle now actually switches ducking on/off via AudioDucker::SetEnabled. Default depths: BGM -15 dB (deep — GF summon music masks AD heavily), SFX -6 dB (light — most SFX short). Default times: 100 ms attack, 600 ms release, 200 ms hold. dB depths hardcoded for now because Config::GetInt uses GetPrivateProfileInt which doesn't handle negative-value INI keys cleanly — tunable in source. F5/F6/F7/F8 volume changes skip the immediate Reapply call when ducker is active so they don't briefly flash to user-level mid-duck (ducker picks up the new user vol on next Tick).

// v0.14.45: SFX volume control + audio-ducking toggle + full keyboard layout reshuffle. Resolved game-side sfx_set_master_volume via opcode_effectplay2 chain (pExecuteOpcodeTable[0x21] -> +0x5F sfx_play_to_current_playing_channel -> +0x35 play_sfx_on_channel -> +0xA1 sfx_set_volume; sfx_get_master_volume = sfx_set_volume - 0x10; sfx_set_master_volume = sfx_get_master_volume - 0xE0). FFNx replaces sfx_set_master_volume with a JMP to its own setSFXMasterVolume bridge in the same way it replaces set_midi_volume. Mirrored the v0.09.24 BGM hook architecture: scan the FFNx replacement bytes for nxAudioEngine ptr + setSFXMasterVolume method address, MinHook the FFNx function, periodically reapply via direct __fastcall thiscall shim. New persistent INI keys: sfx_volume (default 100), tts_duck_enabled (default 1), sfx_duck_ratio (default 30, percent for hand-editability). Auto-duck logic itself is deferred to v0.14.46 — F2 toggle persists the bool and announces state but is otherwise inert until Phase 2 wires ScreenReader::IsSpeaking() polling. Keyboard layout reshuffled per Aaron: F1 cycle voice, F2 ducking toggle, F3/F4 speech rate, Shift+F3/F4 speech volume, F5/F6 SFX volume, F7/F8 BGM volume. Removed v0.12.21 stale F2 'Director Varblock' diagnostic block from field_nav_handlekeys.inl (DEVNOTES called it unused but it was firing). Generalized 'Function key repurposing rule' added to DEVNOTES.

// v0.14.44: GF summon audio descriptions. New module gf_audio_desc.cpp/h mirrors fmv_audio_desc architecture. Per-GF VTT files embedded as RCDATA resources (IDR_VTT_GF_BASE = 6000). Trigger wired into PollBattleMagicId() in battle_tts_ewm.inl: when battle_magic_id changes to a GF effect ID, GfAudioDesc::OnGFAnimationStart fires and begins cue playback on Channel 2 (event voice). Stops playback when battle_magic_id reverts (covers natural end + R1+L1 skip). VTTs cover all 16 junctioned GFs (Quezacotl, Shiva, Ifrit, Siren, Brothers, Diablos, Carbuncle, Leviathan, Pandemona, Cerberus, Alexander, Doomtrain, Bahamut, Cactuar, Tonberry, Eden) plus Phoenix (item) and Odin (auto-summon). Each opens with GF name + attack name (replaces visual popup), then cues describe animation phases. Timing draws on community references; will need BAT tuning per GF since animation durations are approximate.

// v0.14.43: Diagnostic cleanup post-Bug-A resolution. Stripped DumpItemMenuState (the [ITEM-DUMP] block) from src/battle_tts_menu.inl — v0.14.42 BAT verified the disassembly model so the dump is no longer needed. Stripped [BATTLESPEAK-DIAG] log preamble/postamble from BattleSpeak in src/battle_tts.cpp — added in v0.14.37 to investigate items audio purge, no longer needed. Stripped [SPEAK-DIAG] log statements from ScreenReader::Speak in src/screen_reader.cpp — same provenance. Net: ~150 lines of diagnostic code removed; F12 stays free for next investigation. The [ITEM-LIST] block is retained (low overhead, one shot per submenu open, useful for future regression checks). No functional changes; behavior identical to v0.14.42 with cleaner logs.

// v0.14.40: Items submenu — visual page/slot fix + comprehensive diagnostic dump. v0.14.39 BAT showed (a) FindItemControllerNode() returns NULL on every cursor read (40/40) — the v0.10.104 handler signature 0x4F81F0 isn't where expected in this build, OR FFNx replaced it, OR the pool layout shifted; (b) Aaron's saved arrangement has gaps between items ("Elixir was page 1 slot 4 when really it was on page 3 following a bunch of empty slots"). Fix part 1 (visual position): added boIdx field to BattleItemEntry tracking the original battle_order position. BuildItemList now records boIdx for each item. New GetItemVisualPos() helper computes page/slot from boIdx (so saved-arrangement gaps are reflected). Both items announce paths (main + SUBMENU-DELAYED) use it. Theory: BATTLE_SUBMENU_CURSOR is a compacted item-index cursor; engine renders items at their battle_order POSITIONS with empties preserved. Fix part 2 (diagnostic): added DumpItemMenuState() that runs once per items submenu open. Logs raw 32 bytes of battle_order, inv lookup for each non-FF entry, hex dump of all 10 pool slots first 0x40 bytes, every code-pointer DWORD found in pool memory (0x004XXXXX or 0x6E70XXXX range), explicit search for ITEM_HANDLER (0x4F81F0) at every offset, and 32 bytes around BATTLE_SUBMENU_CURSOR for cursor-neighborhood inspection.

// v0.14.39: Bug A items-ordering attempted fix via pool-node helper. BAT proved helper returns NULL across the entire battle — handler offset/address don't match this build.

// v0.14.38: Bug B fully FIXED (every damage event fired via sub_5068B0 / Flag #2). Bug A items-not-heard FIXED (false-exit suppression extended to Item submenu). Bug A new variant discovered: items announce in wrong order. Three flags codified: Flag #1 (sub_48EF80) = action-launch, NOT a trigger. Flag #2 (sub_5068B0) = damage sprite, PRIMARY. Flag #3 (anim flag 1->0) = catch-all fallback.

// v0.14.37: Bug B revert + Bug A diagnostic. v0.14.36 BAT FAILED — popup-create primary fired 1-5s before render-hook. Reverted to render-hook primary, popup-create as fallback. Added [BATTLESPEAK-DIAG] in BattleSpeak and [SPEAK-DIAG] in ScreenReader::Speak for items submenu investigation. Diagnostic logs remain in v0.14.38 and will be removed in v0.14.39.

// v0.14.36: Bug B FAILED FIX — inverted primary/fallback ordering. Made sub_48EF80 primary, sub_5068B0 fallback, both with 500ms freshness gate. BAT 22:09-22:11 showed popup-create fires 1-5s before dmg-render, every event ANIM-UP-timed. Reverted in v0.14.37.

// v0.14.35: Two-bug fix from v0.14.34 long-battle BAT. Bug A item-submenu fix retained.

// v0.14.34: Restore sprite/spell event hooks (bug 2 of 4 from v0.14.31 BAT — the actual fix).
//
// v0.14.33: Restore status-spell no-effect ALLY-already-status fallback (sub_48E830 watchdog hook — still required for cases where engine short-circuits before sub_4877F0)
//
// v0.14.32: REGRESSION FIX (bug 1 — damage announcement timing)
//
// v0.14.31: BUILD RECOVERY (linker errors)
//
// v0.14.30: BUILD RECOVERY (architectural fix — mod_forward_decls.h)
//
// v0.14.28: BUILD RECOVERY (name_bypass Mod, field_dialog/archive/navigation namespace decls)
//
// v0.14.27: BUILD RECOVERY (name_bypass forward decls — wrong function name)
//
// v0.14.26: BUILD RECOVERY (s_gdiplusToken, PollStatusChanges, menu_tts ff8_addresses include, game_audio namespace decls)
//
// v0.14.25: BUILD RECOVERY attempt 2 — restored battle_tts.cpp from GitHub HEAD + .inl includes (got most of the way; 4 errors left)
//
// v0.14.24: BUILD RECOVERY attempt 1 (failed — helpers + s_ewmEnabled fixes weren't enough)
//
// v0.14.22: Fixed spell ordering attempt (still backwards)
// v0.14.21: Auto-building scanner (worked but backwards)  
// v0.14.20: Manual spell collection proof-of-concept
#define FF8OPC_VERSION_DATE "2026-04-28"

// ============================================================================
// FF8 Runtime Address Resolution
// See ff8_addresses.h / ff8_addresses.cpp for the resolver that computes
// addresses at runtime using the same offset-chain technique as FFNx.
// ============================================================================
