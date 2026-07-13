// resources.h - Resource IDs for embedded data files
#pragma once

// Audio description VTT files — special (embedded as RCDATA)
#define IDR_VTT_INTRO           101
#define IDR_VTT_OPENING_CREDITS 102

// Audio description VTT files — per-disc systematic IDs
// Resource ID = base + FMV number (e.g., disc02_15h = 3000 + 15 = 3015)
#define IDR_VTT_DISC00_BASE 1000   // disc00_00h .. disc00_29h (30h = intro, handled by IDR_VTT_INTRO)
#define IDR_VTT_DISC01_BASE 2000   // disc01_00h .. disc01_33h
#define IDR_VTT_DISC02_BASE 3000   // disc02_00h .. disc02_31h
#define IDR_VTT_DISC03_BASE 4000   // disc03_00h .. disc03_04h, disc03_06h (no 05h)
#define IDR_VTT_DISC04_BASE 5000   // disc04_00h

// GF summon audio description VTT files (v0.14.44)
// Resource ID = base + per-GF index (mapped in gf_audio_desc.cpp::g_gfTable)
#define IDR_VTT_GF_BASE     6000   // gf_quezacotl..gf_eden..gf_phoenix..gf_odin

// AVI-to-VTT mapping file (embedded as RCDATA) — kept for special-case overrides
#define IDR_AD_MAPPING          201

// NVDA controller client DLL (embedded for single-file distribution)
#define IDR_NVDA_CLIENT_DLL     301

// v0.18.3.172: world-map auto-drive teleport SFX (rising shimmer), embedded as RCDATA
#define IDR_WAV_TELEPORT        7000
