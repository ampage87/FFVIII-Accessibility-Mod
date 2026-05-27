// field_charaone_parse.h - REMOVED IN v0.17.8.15
//
// The chara.one cross-reference chain (v0.17.8.11 through v0.17.8.14) was
// reverted in v0.17.8.15 after the BAT screenshot of bghall_3 disproved its
// core assumption: kanban2 IS Xu (a character standing in the world), not a
// signpost. The chara.one classifier was misclassifying p048 as a prop based
// on an unreliable texture-count heuristic, and a correctly-classifying
// chara.one parser would still be the wrong mechanism -- what matters for
// the player is the entity's behavior (Other-cat + SETMODEL = stands in
// world = NPC), not its model-file flag. v0.17.8.15 ships the simpler
// JSM-behavior signal: `jsmCategory == 3 && hasSetmodelInit` -> "NPC N".
//
// This file is no longer included anywhere and is no longer in deploy.bat.
// It can be deleted from the source tree at the next housekeeping pass.
#pragma once
