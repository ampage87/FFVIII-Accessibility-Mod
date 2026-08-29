// ff8_text_decode.h - FF8 custom character encoding to UTF-8 decoder
//
// FF8 uses a proprietary single-byte encoding for all in-game text.
// This module converts raw FF8 text bytes into readable UTF-8 strings.
//
// Encoding rebuilt from canonical Ifrit textformat.ifr:
//   0x00        = end of string
//   0x02        = newline (line separator in dialog)
//   0x03 + byte = character name (0x30=Squall, 0x31=Zell, etc.)
//   0x05 + byte = icon (controller buttons)
//   0x06 + byte = color code
//   0x0B + byte = choice cursor location marker
//   0x20        = space
//   0x21-0x2A   = digits '0'-'9'
//   0x2E        = '!'
//   0x2F        = '?'
//   0x30        = ellipsis (...)
//   0x3B        = '.'
//   0x3C        = ','
//   0x3A, 0x40, 0x43 = apostrophe variants
//   0x45-0x5E   = 'A'-'Z'
//   0x5F-0x78   = 'a'-'z'
//   0xE8-0xFF   = two-char compression sequences
//
// v04.00: Created for field dialog TTS.
// v04.01: Fixed table from Ifrit, fixed name IDs.
// v04.03: Fixed 0x43 backtick -> apostrophe. Rewrote DecodeChoices to use
//         line-index splitting with firstQ/lastQ. Added DecodeLines.
// v0.15.11.0: Added 0xFA "EC" and 0xFD "FE" to the compression sequence
//         switch (previously a gap relative to the v0.13.46 sysfnt.bin token
//         table verified by the now-retired DecodeFF8TextPreview). Changed
//         0x0E (icon code) from return-0 to return-1 so the icon ID byte is
//         consumed silently instead of leaking into the next decoded char.
//         These changes unblocked the v0.15.11.0 retirement of
//         DecodeFF8TextPreview from battle_tts_victory.inl, making this the
//         single canonical FF8 text decoder in the codebase.

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace FF8TextDecode {

// ============================================================================
// Choice dialog result (for ASK/AASK opcodes)
// ============================================================================

struct ChoiceDialog {
    std::string prompt;                 // Text before choice options
    std::vector<std::string> choices;   // Individual choice option texts
};

// Decode a raw FF8-encoded byte string into a single UTF-8 string.
// Newlines become spaces. Control codes are stripped.
// Character name substitutions (0x03+id) replaced with default names.
// `droppedOut`, when non-null, receives the number of bytes that were consumed
// but produced no text -- an unresolved substituted word, an unmapped glyph, a
// name insert this decoder cannot expand. Zero means "what you got back is the
// whole string"; anything else means a caller that reads the result aloud is
// reading a fragment and should say so.
std::string Decode(const uint8_t* data, size_t maxBytes = 1024,
                   int* droppedOut = nullptr);

// Resolve a 0x0E / 0x0F substituted word (namedic.bin) into its FF8-encoded
// bytes. Returns the number of bytes written; 0 means the engine would have
// emitted nothing too. See the disassembly block in ff8_text_decode.cpp.
size_t ResolveWord(uint8_t code, uint8_t param, uint8_t* out, size_t outSize);

// Point the word-table lookup at a namedic.bin image instead of the engine's
// own pointer at 0x01D2B80C. Host probes use this; the game never calls it.
void SetWordTableBase(const void* base);

// Point the nameable-name lookup (Squall, Rinoa, Angelo, Griever, Boko) at a
// savemap image instead of 0x01CFDC5C. Host probes use this; the game never
// calls it.
void SetNameTableBase(const void* base);

// Decode raw FF8-encoded bytes into separate lines (split on 0x02 newlines).
// Each element is one line of dialog text, trimmed of whitespace.
std::vector<std::string> DecodeLines(const uint8_t* data, size_t maxBytes = 1024);

// Decode a choice dialog, splitting into prompt and choices using the
// win_obj's firstQ/lastQ line indices. Lines before firstQ form the prompt;
// lines firstQ through lastQ (inclusive) are the choice options.
// Parentheses around choice text are automatically stripped.
ChoiceDialog DecodeChoices(const uint8_t* data, size_t maxBytes,
                           uint8_t firstQ, uint8_t lastQ);

// Decode raw FF8-encoded bytes and return a hex dump alongside.
std::string DecodeWithHex(const uint8_t* data, size_t maxBytes, std::string& hexDump);

// Dump raw bytes as hex string (for logging).
std::string HexDump(const uint8_t* data, size_t count);

// ============================================================================
// Menu font decoder (v07.11)
// ============================================================================
// The menu/save screen text rendering uses a DIFFERENT encoding from field
// dialog. Glyph indices come from the sysfnt.tex texture grid layout, where
// each cell is a character. The mapping was extracted from the authoritative
// Deling editor (myst6re/deling: src/qt/fonts/sysfnt.txt).
//
// Key differences from field dialog encoding:
//   Field: A=0x45, a=0x5F, space=0x20, digits=0x21-0x2A
//   Menu:  A=0x25, a=0x3F, space=0x00, digits=0x01-0x0A
//
// Used by: get_character_width hook (GCW buffer) in menu_tts.cpp

// Decode menu font glyph indices into a UTF-8 string.
// Input is an array of raw glyph index bytes as captured by the GCW hook.
// Repeated spaces are collapsed. Unknown glyphs are skipped.
std::string DecodeMenuText(const uint8_t* data, size_t len);

// v0.67.3: TEST SEAM for the button-icon resolution.
//
// Text code 0x05 draws the key the player has bound to a game action, and the
// mod now speaks it (see the 0x05 branch in ff8_text_decode.cpp). Resolving it
// means reading the live keymap at 0x01CD0208 and two config bytes -- fixed
// engine addresses, which is to say code that would otherwise only ever be
// checked by playing the game. Installing a reader lets a probe put a keymap in
// front of it and assert the sentence that comes out. Null (the default) is the
// real thing.
typedef unsigned char (*ButtonPeekFn)(uintptr_t addr);
void SetButtonPeekHook(ButtonPeekFn fn);

// v0.120.0 (#centra): the key the player has bound to pad BUTTON `b`
// (0..15, the order in button_mask_model.inl), written into `out` as a word a
// screen reader can say -- "Enter", "Space", "button 2". False when the engine
// has nothing bound, in which case the caller should fall back to the pad's own
// name rather than inventing one.
//
// The 0x05 icon codes reach the same resolver through an action index; a
// BTNTEST mask names buttons directly, so it comes in here.
bool ButtonKeyName(int b, char* out, size_t n);

}  // namespace FF8TextDecode
