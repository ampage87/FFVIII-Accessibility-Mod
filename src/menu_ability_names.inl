// menu_ability_names.inl -- the ability ID -> name table.
//
// PART OF menu_tts.cpp (textual include). Split out of it in v0.29.1 (#88) so
// tests/menu_gf_compile.cpp can exercise the GF Learn-list parse against the
// REAL table instead of a copy that would drift away from it.
//
// Names here are the SPOKEN forms. The on-screen forms differ and are derived
// by NormalizeAbilityToGcw() in menu_tts_gf.inl -- see its comment for the two
// substitutions the game makes and for the one that was missing.

// v0.09.43: Unified Ability ID → Name table (IDs 0–115)
// Source: kernel.bin sections 11–17, confirmed via deep research
static const char* ABILITY_NAMES[] = {
    "None",              //   0
    "HP-J",              //   1
    "Str-J",             //   2
    "Vit-J",             //   3
    "Mag-J",             //   4
    "Spr-J",             //   5
    "Spd-J",             //   6
    "Eva-J",             //   7
    "Hit-J",             //   8
    "Luck-J",            //   9
    "Elem-Atk-J",        //  10
    "ST-Atk-J",          //  11
    "Elem-Def-J",        //  12
    "ST-Def-J",          //  13
    "Elem-Def-J x2",     //  14
    "Elem-Def-J x4",     //  15
    "ST-Def-J x2",       //  16
    "ST-Def-J x4",       //  17
    "Ability x3",        //  18
    "Ability x4",        //  19
    "Magic",             //  20
    "GF",                //  21
    "Draw",              //  22
    "Item",              //  23
    "Empty",             //  24
    "Card",              //  25
    "Doom",              //  26
    "Mad Rush",          //  27
    "Treatment",         //  28
    "Defend",            //  29
    "Darkside",          //  30
    "Recover",           //  31
    "Absorb",            //  32
    "Revive",            //  33
    "LV Down",           //  34
    "LV Up",             //  35
    "Kamikaze",          //  36
    "Devour",            //  37
    "MiniMog",           //  38
    "HP plus 20%",       //  39
    "HP plus 40%",       //  40
    "HP plus 80%",       //  41
    "Str plus 20%",      //  42
    "Str plus 40%",      //  43
    "Str plus 60%",      //  44
    "Vit plus 20%",      //  45
    "Vit plus 40%",      //  46
    "Vit plus 60%",      //  47
    "Mag plus 20%",      //  48
    "Mag plus 40%",      //  49
    "Mag plus 60%",      //  50
    "Spr plus 20%",      //  51
    "Spr plus 40%",      //  52
    "Spr plus 60%",      //  53
    "Spd plus 20%",      //  54
    "Spd plus 40%",      //  55
    "Eva plus 30%",      //  56
    "Luck plus 50%",     //  57
    "Mug",               //  58
    "Med Data",          //  59
    "Counter",           //  60
    "Return Damage",     //  61
    "Cover",             //  62
    "Initiative",        //  63
    "Move-HP Up",        //  64
    "HP Bonus",          //  65
    "Str Bonus",         //  66
    "Vit Bonus",         //  67
    "Mag Bonus",         //  68
    "Spr Bonus",         //  69
    "Auto-Protect",      //  70
    "Auto-Shell",        //  71
    "Auto-Reflect",      //  72
    "Auto-Haste",        //  73
    "Auto-Potion",       //  74
    "Expend x2-1",       //  75
    "Expend x3-1",       //  76
    "Ribbon",            //  77
    "Alert",             //  78
    "Move-Find",         //  79
    "Enc-Half",          //  80
    "Enc-None",          //  81
    "Rare Item",         //  82
    "SumMag plus 10%",   //  83
    "SumMag plus 20%",   //  84
    "SumMag plus 30%",   //  85
    "SumMag plus 40%",   //  86
    "GFHP plus 10%",     //  87
    "GFHP plus 20%",     //  88
    "GFHP plus 30%",     //  89
    "GFHP plus 40%",     //  90
    "Boost",             //  91
    "Haggle",            //  92
    "Sell-High",         //  93
    "Familiar",          //  94
    "Call Shop",         //  95
    "Junk Shop",         //  96
    "T Mag-RF",          //  97
    "I Mag-RF",          //  98
    "F Mag-RF",          //  99
    "L Mag-RF",          // 100
    "Time Mag-RF",       // 101
    "ST Mag-RF",         // 102
    "Supt Mag-RF",       // 103
    "Forbid Mag-RF",     // 104
    "Recov Med-RF",      // 105
    "ST Med-RF",         // 106
    "Ammo-RF",           // 107
    "Tool-RF",           // 108
    "Forbid Med-RF",     // 109
    "GFRecov Med-RF",    // 110
    "GFAbl Med-RF",      // 111
    "Mid Mag-RF",        // 112
    "High Mag-RF",       // 113
    "Med LV Up",         // 114
    "Card Mod",          // 115
};
static const int ABILITY_NAME_COUNT = 116;
