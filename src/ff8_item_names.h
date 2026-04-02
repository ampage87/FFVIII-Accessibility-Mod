// ff8_item_names.h - FF8 item name lookup table (shared between menu_tts and battle_tts)
// 198 items, 1-based IDs (0=empty, 1=Potion, ..., 198=Occult Fan IV)
// Source: menu_tts.cpp v0.08.29, confirmed working in field Item menu.
// Battle display struct at 0x1D8DFF4: {item_id, quantity} byte pairs, qty=0 = empty.
#pragma once

static const char* const FF8_ITEM_NAMES[] = {
    nullptr,              //   0 = empty slot
    "Potion",             //   1
    "Potion+",            //   2
    "Hi-Potion",          //   3
    "Hi-Potion+",         //   4
    "X-Potion",           //   5
    "Mega-Potion",        //   6
    "Phoenix Down",       //   7
    "Mega Phoenix",       //   8
    "Elixir",             //   9
    "Megalixir",          //  10
    "Antidote",           //  11
    "Soft",               //  12
    "Eye Drops",          //  13
    "Echo Screen",        //  14
    "Holy Water",         //  15
    "Remedy",             //  16
    "Remedy+",            //  17
    "Hero-Trial",         //  18
    "Hero",               //  19
    "Holy War-Trial",     //  20
    "Holy War",           //  21
    "Shell Stone",        //  22
    "Protect Stone",      //  23
    "Aura Stone",         //  24
    "Death Stone",        //  25
    "Holy Stone",         //  26
    "Flare Stone",        //  27
    "Meteor Stone",       //  28
    "Ultima Stone",       //  29
    "Gysahl Greens",      //  30
    "Phoenix Pinion",     //  31
    "Friendship",         //  32
    "Tent",               //  33
    "Pet House",          //  34
    "Cottage",            //  35
    "G-Potion",           //  36
    "G-Hi-Potion",        //  37
    "G-Mega-Potion",      //  38
    "G-Returner",         //  39
    "Rename Card",        //  40
    "Amnesia Greens",     //  41
    "HP-J Scroll",        //  42
    "Str-J Scroll",       //  43
    "Vit-J Scroll",       //  44
    "Mag-J Scroll",       //  45
    "Spr-J Scroll",       //  46
    "Spd-J Scroll",       //  47
    "Luck-J Scroll",      //  48
    "Aegis Amulet",       //  49
    "Elem Atk",           //  50
    "Elem Guard",         //  51
    "Status Atk",         //  52
    "Status Guard",       //  53
    "Rosetta Stone",      //  54
    "Magic Scroll",       //  55
    "GF Scroll",          //  56
    "Draw Scroll",        //  57
    "Item Scroll",        //  58
    "Gambler's Spirit",   //  59
    "Healing Ring",       //  60
    "Phoenix Spirit",     //  61
    "Med Kit",            //  62
    "Bomb Spirit",        //  63
    "Hungry Cookpot",     //  64
    "Mog's Amulet",       //  65
    "Steel Pipe",         //  66
    "Star Fragment",      //  67
    "Energy Crystal",     //  68
    "Samantha Soul",      //  69
    "Healing Mail",       //  70
    "Silver Mail",        //  71
    "Gold Armor",         //  72
    "Diamond Armor",      //  73
    "Regen Ring",         //  74
    "Giant's Ring",       //  75
    "Gaea's Ring",        //  76
    "Strength Love",      //  77
    "Power Wrist",        //  78
    "Hyper Wrist",        //  79
    "Turtle Shell",       //  80
    "Orihalcon",          //  81
    "Adamantine",         //  82
    "Rune Armlet",        //  83
    "Force Armlet",       //  84
    "Magic Armlet",       //  85
    "Circlet",            //  86
    "Hypno Crown",        //  87
    "Royal Crown",        //  88
    "Jet Engine",         //  89
    "Rocket Engine",      //  90
    "Moon Curtain",       //  91
    "Steel Curtain",      //  92
    "Glow Curtain",       //  93
    "Accelerator",        //  94
    "Monk's Code",        //  95
    "Knight's Code",      //  96
    "Doc's Code",         //  97
    "Hundred Needles",    //  98
    "Three Stars",        //  99
    "Ribbon",             // 100
    "Normal Ammo",        // 101
    "Shotgun Ammo",       // 102
    "Dark Ammo",          // 103
    "Fire Ammo",          // 104
    "Demolition Ammo",    // 105
    "Fast Ammo",          // 106
    "AP Ammo",            // 107
    "Pulse Ammo",         // 108
    "M-Stone Piece",      // 109
    "Magic Stone",        // 110
    "Wizard Stone",       // 111
    "Ochu Tentacle",      // 112
    "Healing Water",      // 113
    "Cockatrice Pinion",  // 114
    "Zombie Powder",      // 115
    "Lightweight",        // 116
    "Sharp Spike",        // 117
    "Screw",              // 118
    "Saw Blade",          // 119
    "Mesmerize Blade",    // 120
    "Vampire Fang",       // 121
    "Fury Fragment",      // 122
    "Betrayal Sword",     // 123
    "Sleep Powder",       // 124
    "Life Ring",          // 125
    "Dragon Fang",        // 126
    "Spider Web",         // 127
    "Coral Fragment",     // 128
    "Curse Spike",        // 129
    "Black Hole",         // 130
    "Water Crystal",      // 131
    "Missile",            // 132
    "Mystery Fluid",      // 133
    "Running Fire",       // 134
    "Inferno Fang",       // 135
    "Malboro Tentacle",   // 136
    "Whisper",            // 137
    "Laser Cannon",       // 138
    "Barrier",            // 139
    "Power Generator",    // 140
    "Dark Matter",        // 141
    "Bomb Fragment",      // 142
    "Red Fang",           // 143
    "Arctic Wind",        // 144
    "North Wind",         // 145
    "Dynamo Stone",       // 146
    "Shear Feather",      // 147
    "Venom Fang",         // 148
    "Steel Orb",          // 149
    "Moon Stone",         // 150
    "Dino Bone",          // 151
    "Windmill",           // 152
    "Dragon Skin",        // 153
    "Fish Fin",           // 154
    "Dragon Fin",         // 155
    "Silence Powder",     // 156
    "Poison Powder",      // 157
    "Dead Spirit",        // 158
    "Chef's Knife",       // 159
    "Cactus Thorn",       // 160
    "Shaman Stone",       // 161
    "Fuel",               // 162
    "Girl Next Door",     // 163
    "Sorceress' Letter",  // 164
    "Chocobo's Tag",      // 165
    "Pet Nametag",        // 166
    "Solomon Ring",       // 167
    "Magical Lamp",       // 168
    "HP Up",              // 169
    "Str Up",             // 170
    "Vit Up",             // 171
    "Mag Up",             // 172
    "Spr Up",             // 173
    "Spd Up",             // 174
    "Luck Up",            // 175
    "LuvLuv G",           // 176
    "Weapons Mon 1st",    // 177
    "Weapons Mon Mar",    // 178
    "Weapons Mon Apr",    // 179
    "Weapons Mon May",    // 180
    "Weapons Mon Jun",    // 181
    "Weapons Mon Jul",    // 182
    "Weapons Mon Aug",    // 183
    "Combat King 001",    // 184
    "Combat King 002",    // 185
    "Combat King 003",    // 186
    "Combat King 004",    // 187
    "Combat King 005",    // 188
    "Pet Pals Vol. 1",    // 189
    "Pet Pals Vol. 2",    // 190
    "Pet Pals Vol. 3",    // 191
    "Pet Pals Vol. 4",    // 192
    "Pet Pals Vol. 5",    // 193
    "Pet Pals Vol. 6",    // 194
    "Occult Fan I",       // 195
    "Occult Fan II",      // 196
    "Occult Fan III",     // 197
    "Occult Fan IV",      // 198
};
static const int FF8_ITEM_COUNT = 199;  // 0-198, index 0 = empty

static const char* GetBattleItemName(uint8_t itemId)
{
    if (itemId > 0 && itemId < FF8_ITEM_COUNT && FF8_ITEM_NAMES[itemId])
        return FF8_ITEM_NAMES[itemId];
    return "Unknown Item";
}
