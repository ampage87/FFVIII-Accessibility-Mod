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

}  // namespace FF8TextDecode
