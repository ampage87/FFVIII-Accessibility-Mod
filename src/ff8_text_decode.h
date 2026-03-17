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
std::string Decode(const uint8_t* data, size_t maxBytes = 1024);

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

}  // namespace FF8TextDecode
