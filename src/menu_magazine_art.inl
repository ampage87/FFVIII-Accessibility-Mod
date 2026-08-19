// menu_magazine_art.inl -- what the Weapons Monthly pictures show.
//
// PART OF menu_tts.cpp (textual include). v0.31.0 (#90).
//
// ---------------------------------------------------------------------------
// WHERE THIS CAME FROM, SO IT CAN BE CHECKED
//
// Aaron: *"each page of the weapons monthly has a picture of the weapon. Would
// it be possible for you to pull all of the weapons monthly pages / images,
// describe the appearance of each weapon..."* -- and then, crucially:
// *"Make sure to use the text on the page to help you create accurate
// descriptions of the images. e.g. Quistis' Slaying Tail might look like a tail,
// but it is a whip, and that is clear if you read the text on the page."*
//
// Every line below was written while looking at the actual sprite next to that
// page's own prose. The chain:
//
//   menu.fs/fi/fl        the .fi triple is {size, offset, compression} -- NOT
//                        {offset, size, ...}; reading it the other way produces
//                        plausible multi-megabyte garbage.
//   mmag.bin             69 records x 68 bytes. Records 0..27 are Weapons
//                        Monthly, four to an issue, in page order.
//                        +0x17 = texture sheet, +0x18 = weapon id,
//                        +0x34 = the 4 text-block descriptors.
//   magNN.TEX            256x256, 8bpp, one 256-entry RGBA palette at 0xEC,
//                        pixels at 0x4EC. **The palette is RGBA, not BGRA** --
//                        read as BGRA every weapon comes out cyan.
//                        Sheet = record/4, cell = record%4, laid out
//                        top-left, top-right, bottom-left, bottom-right.
//   mngrp section 0x57   111 strings, the page prose, decoded with the mod's
//                        own glyph table.
//
// **The mapping is verified, not assumed.** Record 9 decodes to "With the
// Maverick... The gloves are made of black leather and have metal plates on the
// knuckles", and sheet 2 cell 1 is a pair of near-black gloves with pale studs
// across the knuckles -- which is also the page in Aaron's own v0.30.1
// screenshot. Every other page was checked the same way: the whip pages show
// whips, the gunblade pages show gunblades, the saw-toothed ring is on the page
// that says "similar to a circular saw".
//
// Colour is described only where the sprite and the prose agree (the Flame
// Saber's red edge, the Maverick's black leather). The art is heavily dithered
// 8-bit, so precise shades are not claimed.
// ---------------------------------------------------------------------------

struct MagArtEntry { const char* name; const char* look; };

// Indexed by mmag.bin record. Records 28+ are the other magazines, which have
// no weapon plate; the reader says so rather than inventing one.
static const int MAG_ART_COUNT = 28;
static const MagArtEntry MAG_ART[MAG_ART_COUNT] = {
    /*  0 */ { "Lionheart", "A long, slender, straight blade in bright polished steel, held out at a shallow angle. Near the base a small pistol grip and trigger guard sit under the blade, with a short pale barrel and a small swept wing-shaped guard where blade meets gun." },
    /*  1 */ { "Shooting Star", "A ring of long feathered blades radiating from a small dark hub, like a pinwheel made of angel wings. Eight or so pale cream feather shapes fan out all the way round, each one notched at the tip." },
    /*  2 */ { "Exeter", "A long rifle lying at a shallow angle, pale cream and gold along a deep straight body, with a squared-off stock at the left end and a raised sight and bolt housing toward the muzzle." },
    /*  3 */ { "Strange Vision", "A whip, not a tail: a short handle with a knobbed pommel at the upper right, and a long thin dark lash falling away from it and bending sharply at the bottom, ending in a small flared tip at the left." },
    /*  4 */ { "Revolver", "A broad single-edged blade in bright polished steel angled up to the right, with a black revolver grip, trigger and small cylinder tucked beneath the base of the blade." },
    /*  5 */ { "Metal Knuckle", "A pair of dark gloves. A small blunt knuckle-piece sits at the left; the larger back-of-hand plate at the right is near-black with a bright polished steel plate across the knuckles carrying three round studs." },
    /*  6 */ { "Flail", "Two dark metal bars linked at one end, one long and straight and one bent into a narrow loop, crossing each other in an X. Plain, unpolished dark steel throughout." },
    /*  7 */ { "Chain Whip", "A long dark segmented chain lying in a shallow open curve, with a short straight handle rising at the top left. Every link is visible; there is no blade or tip." },
    /*  8 */ { "Shear Trigger", "A long slim single-edged blade in pale polished steel angled up to the left, with a dark grey gunblade housing, trigger loop and stub barrel at the lower end." },
    /*  9 */ { "Maverick", "Two near-black gloves with dark red-brown leather panels. The smaller knuckle-piece sits at the left, and the larger back-of-hand plate at the right carries small pale metal studs across the knuckles." },
    /* 10 */ { "Pinwheel", "A broad flat throwing ring in bright polished silver, its outer edge cut into an octagon, with an angular triangular cut-out through the middle and two curved horn-like prongs rising from the top." },
    /* 11 */ { "Valiant", "A long dark firearm, close to a sawn-off shotgun: a heavy cylindrical barrel running down to the left, a raised loading port on top, and a short grip at the right end." },
    /* 12 */ { "Cutting Trigger", "A broad heavy single-edged blade in dark blue-grey steel flecked with brass, angled up to the right, with a rounded trigger loop hanging beneath the base." },
    /* 13 */ { "Valkyrie", "A pale silver dart with a long central spike and swept-back angular fins at the tail, each fin pierced by a diamond-shaped cut-out. It reads as an arrowhead crossed with an aircraft tail." },
    /* 14 */ { "Ulysses", "A long dark rifle, plain and heavy, with a straight cylindrical barrel running down to the left and a thick angled stock and grip at the right." },
    /* 15 */ { "Slaying Tail", "A whip, despite the name: a long dark segmented lash bent into a squared-off Z, running down from the top right, across the bottom, and ending in a small bulbous tip at the right." },
    /* 16 */ { "Flame Saber", "A gunblade with the blade angled up to the left in dark steel, warm red running along its cutting edge, and a large oval trigger loop with a pale barrel at the base." },
    /* 17 */ { "Gauntlet", "Two dark navy armoured hand-pieces. The larger one is built from stacked bands of steel across the back of the hand, catching pale highlights along each ridge; the smaller knuckle-piece sits beside it." },
    /* 18 */ { "Morning Star", "A dark shaft bent at a right angle, with a spiked head at the left end and a plain rod running up to the right. It reads as a two-piece flail rather than a single club." },
    /* 19 */ { "Red Scorpion", "A whip: a long dark lash laid out in a squared C, down the left side and along the bottom, ending in a pale bulbous tip at the right." },
    /* 20 */ { "Twin Lance", "A gunblade whose broad dark blue blade splits along its length into two parallel points, with a barrel and grip beneath the base at the lower left." },
    /* 21 */ { "Rising Sun", "A thick ring with a saw-toothed outer edge, every tooth curled like a flame, wrapped around an open octagonal centre. It reads unmistakably as a circular saw blade." },
    /* 22 */ { "Bismarck", "A sleek rifle: one smooth cylindrical barrel running down to the left with no clutter along it, and a compact grip and short stock at the right." },
    /* 23 */ { "Crescent Wish", "A nunchaku: two straight rods joined at an angle, with a small starburst ornament at the lower left end and a forked crescent tip at the upper right." },
    /* 24 */ { "Punishment", "A gunblade almost identical to the Twin Lance, with a long twin-edged blade slotted down the middle, a heavy gun housing at the base and a pale barrel projecting below it." },
    /* 25 */ { "Ehrgeiz", "Two chunky armoured glove pieces, dark and thickly built, the larger one studded across the back of the hand with small bright points." },
    /* 26 */ { "Cardinal", "A projectile with a long central shaft and wide swept-back fins, broader than the Valkyrie's, spreading well out to either side of the body." },
    /* 27 */ { "Save the Queen", "A whip: a long lash laid out in a squared C, down the left and along the bottom, ending in a flat diamond-shaped tip at the right." },
};
