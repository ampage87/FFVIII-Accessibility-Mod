#!/usr/bin/env python3
"""
Generate entity_classifications.h from field_entity_survey.json
================================================================
Reads the comprehensive survey data and produces a C++ header file with:
1. Controller skip list (entities to hide from catalog)
2. SYM name -> display name mappings (for TTS)
3. SYM name -> entity type classifications (draw point, save point, shop, etc.)
"""
import json, os, sys
from collections import defaultdict

# ============================================================================
# Classification rules
# ============================================================================

# Exact SYM names that are controllers/effects (never show in catalog)
SKIP_EXACT = {
    # Field directors
    'director0', 'director1', 'director2', 'director3',
    'Director', 'KANTOKU', 'kantoku',
    'Musickantoku', 'sounddir0', 'sounddir1', 'timerdir0',
    'seigyo', 'Seigyo',
    # Lighting
    'light', 'light1', 'light2', 'light3', 'light4', 'light5',
    'stairslight', 'cornerlight', 'sidelight', 'displight',
    'elelight', 'hotellight', 'wpnlight', 'petlight',
    'stoplight', 'stoplight1', 'stoplight2', 'shoplight', 'publight',
    'neonlight', 'tunnellight', 'doorlight', 'bluelight',
    'redlight', 'greenlight',
    'leftred', 'rightred',
    # Camera/view
    'camera', 'camera1', 'camera2', 'view',
    # Environment effects
    'water', 'plane', 'plane1', 'fog', 'fog1', 'fog2',
    'steam', 'wind', 'rain', 'snow', 'shadow', 'kage',
    'noise', 'noise1', 'noise2',
    'hamon1', 'hamon2', 'hamon3',  # ripples
    'laser', 'redlaser', 'yellowlaser', 'bluelaser',
    'hansha',  # reflection
    'elefog',
    'Partikuru',  # particle
    'black',  # screen blackout
    'lens', 'tvlens',
    # Scene controllers
    'Urakata',  # "behind the scenes"
    'battleyarou',  # random battle trigger
    'saveline0', 'saveline1',  # save activation line (invisible)
    'eventline0', 'eventline1',  # event activation line (invisible)
    'BGanimekun',  # BG animation controller
    'Trainscroll',  # scrolling background
    'witchin', 'witchout',  # transition effects
    'seiferout',  # exit animation
    # Door/curtain controllers
    'doorcont', 'door01',
    'curtain', 'glass',
    'cut',  # cutscene controller
    # Animation/movement
    'stl0', 'stl1',  # unknown controller
    'Shake',  # camera shake
    'hantei',  # judgment/decision controller
    'trap',  # trap controller
    'screen',  # screen effect
    'bokeie',  # blur effect
    'enzetu',  # speech/ceremony controller
    'Mawaruu', 'Mawaruu2',  # spinning animation
    'Henka',  # transformation
    'cork',  # stopper
    'zo',  # elephant? effect?
    'mugi',  # wheat? environment
}

# Substrings that indicate a controller (skip if name contains these)
SKIP_SUBSTRINGS = [
    'light',   # any lighting entity
    'scroll',  # scrolling effects
    'kantoku', # director (Japanese)
    'timer',   # timer controllers
    'laser',   # laser effects
    'spell',   # spell effects (redspell1-5)
]

# But these should NOT be skipped despite containing skip substrings
SKIP_EXCEPTIONS = {
    'Moonlight',  # could be an NPC name
    'Starlight',
    'Spotlight',
}

# Draw point SYM names (complete from survey)
DRAW_POINT_NAMES = {'drpoint', 'dp01', 'DrawPoint', 'DrawPointSampleCode'}

# Save point SYM names
SAVE_POINT_NAMES = {'savePoint', 'save', 'svpt'}

# Shop SYM names
SHOP_NAMES = {'shopkun', 'shop'}

# Card game SYM names
CARD_GAME_NAMES = {'cardgamemaster', 'cardgamemaster2', 'Cardtanto'}

# Japanese NPC names -> English display names
NPC_DISPLAY_NAMES = {
    # Main party characters
    'squall': 'Squall', 'squall_u': 'Squall', 'squalls': 'Squall',
    'squallsd': 'Squall', 'squallo': 'Squall', 'squallsp': 'Squall',
    'squall2': 'Squall', 'Squall': 'Squall', 'Squall2': 'Squall',
    'Squall_u': 'Squall', 'Squall_O': 'Squall', 'scoaul': 'Squall',
    'zell': 'Zell', 'zell_u': 'Zell', 'zells': 'Zell',
    'Zell': 'Zell', 'Zell2': 'Zell', 'Zell_u': 'Zell', 'Zell1': 'Zell',
    'GalZell': 'Zell',
    'rinoa': 'Rinoa', 'Rinoa': 'Rinoa', 'rinoau': 'Rinoa',
    'Rinoau': 'Rinoa', 'Rinoa1': 'Rinoa', 'Rinoa2': 'Rinoa',
    'GalRinoa': 'Rinoa',
    'selphie': 'Selphie', 'Selphie': 'Selphie', 'selphies': 'Selphie',
    'Selphie_u': 'Selphie', 'SelphieDummy': 'Selphie', 'sel_arms': 'Selphie',
    'quistis': 'Quistis', 'Quistis': 'Quistis', 'Quistis2': 'Quistis',
    'quistis_n': 'Quistis', 'GalQuistis': 'Quistis',
    'irvine': 'Irvine', 'Irvine': 'Irvine',
    'seifer': 'Seifer', 'Seifer': 'Seifer', 'seifer_n': 'Seifer',
    'laguna': 'Laguna', 'Laguna': 'Laguna', 'laguna99': 'Laguna',
    'laguna02': 'Laguna',
    'kiros': 'Kiros', 'Kiros': 'Kiros',
    'ward': 'Ward', 'Ward': 'Ward',
    'edea': 'Edea', 'Edea': 'Edea', 'edea0': 'Edea',
    'edea1': 'Edea', 'edea2': 'Edea', 'edea3': 'Edea', 'edea4': 'Edea',
    'elone': 'Ellone', 'celone': 'Ellone',
    # Named NPCs
    'kadowaki': 'Dr. Kadowaki',
    'cid': 'Headmaster Cid',
    'dic': 'Directory',
    'nida': 'Nida',
    'xu': 'Xu',
    'fujin': 'Fujin',
    'raijin': 'Raijin',
    # Japanese NPC types
    'seito1': 'Student', 'seito2': 'Student', 'seito3': 'Student',
    'seito4': 'Student', 'seito5': 'Student', 'seito6': 'Student',
    'meskun': 'Student', 'betunikun': 'Student', 'naidarokun': 'Student',
    'student1': 'Student', 'student2': 'Student', 'student4': 'Student',
    'cameraman': 'Cameraman',
    # Trepies (Quistis fan club)
    'trepiegroupie': 'Trepie', 'trepiegroupie1': 'Trepie',
    'trepiegroupie2': 'Trepie', 'trepiegroupie3': 'Trepie',
    # Military/guards
    'Soldier1': 'Soldier', 'Soldier2': 'Soldier',
    'G_Army': 'Galbadian Soldier', 'G_Army01': 'Galbadian Soldier',
    'G_Army02': 'Galbadian Soldier',
    'GalbadiaSS': 'Galbadian Soldier', 'GalbadiaArmy01': 'Galbadian Soldier',
    'GalHei3': 'Galbadian Soldier', 'GalHei4': 'Galbadian Soldier',
    # Civilians
    'Boy1': 'Boy', 'Boy2': 'Boy',
    'Girl': 'Girl', 'Girl2': 'Girl',
    'Woman': 'Woman', 'Women1': 'Woman',
    'Man': 'Man',
    'Obaasan': 'Old Woman',
    'SPObasan': 'Old Woman',
    'uketsuke': 'Receptionist',
    'Anaun': 'Announcer',
    'Daitouryo': 'President',
    'ZonJichan': 'Zone',
    # Interactive objects
    'cliant': 'Terminal',
    'britinboard': 'Bulletin Board',
    'moni': 'Study Panel', 'monitor': 'Study Panel',
    'mess': 'Desk',
    'evl1': 'Elevator',
    'Lift': 'Elevator', 'Lifter': 'Elevator',
    'info': 'Information',
    'memo': 'Memo',
    'Newspaper': 'Newspaper',
    'adplate': 'Sign',
    'book': 'Book',
    'knob': 'Knob',
    # Card game
    'cardgamemaster': 'Card Player', 'cardgamemaster2': 'Card Player',
    'Cardtanto': 'Card Player',
    # Save/draw/shop
    'savePoint': 'Save Point', 'save': 'Save Point', 'svpt': 'Save Point',
    'shopkun': 'Shop', 'shop': 'Shop',
    'drpoint': 'Draw Point', 'dp01': 'Draw Point',
    'DrawPoint': 'Draw Point', 'DrawPointSampleCode': 'Draw Point',
    # Animals
    'Cat1': 'Cat', 'Cat2': 'Cat',
    'Fish': 'Fish', 'Fish2': 'Fish',
    'Kani': 'Crab',
    'kero': 'Frog',
    # Vehicles/machines
    'Train': 'Train', 'Train1': 'Train', 'Train2': 'Train', 'Train3': 'Train',
    'Monorail': 'Monorail', 'monorail': 'Monorail', 'BGMonorail': 'Monorail',
    'Agittrain': 'Train',
    # FF8-specific
    'majo': 'Sorceress',
    'dragon': 'Dragon',
    'Munba2': 'Moomba', 'Munba3': 'Moomba', 'Munbamini': 'Moomba',
    # Door entities (when visible/interactive)
    'door': 'Door', 'door1': 'Door', 'door2': 'Door',
    'hoteldoor': 'Door', 'wpndoor': 'Door', 'petdoor': 'Door',
    'maniadoor': 'Door', 'mindoor': 'Door', 'manhole': 'Manhole',
    # Lines and ladders
    'ladder': 'Ladder', 'hasigomodel': 'Ladder',
    'gate0': 'Gate', 'gate1': 'Gate',
    'Window1': 'Window',
}

def classify_name(name, category):
    """Classify a SYM name. Returns (type, display_name) or None to skip."""
    # Check skip list first
    if name in SKIP_EXACT:
        return None
    if name not in SKIP_EXCEPTIONS:
        nl = name.lower()
        for sub in SKIP_SUBSTRINGS:
            if sub in nl:
                return None
    
    # Empty names
    if not name:
        return None
    
    # Special entity types
    if name in DRAW_POINT_NAMES:
        return ('draw_point', 'Draw Point')
    if name in SAVE_POINT_NAMES:
        return ('save_point', 'Save Point')
    if name in SHOP_NAMES:
        return ('shop', 'Shop')
    if name in CARD_GAME_NAMES:
        return ('card_game', 'Card Player')
    
    # Known display name
    if name in NPC_DISPLAY_NAMES:
        return ('npc', NPC_DISPLAY_NAMES[name])
    
    # Pattern-based draw point detection
    nl = name.lower()
    if nl.startswith('dp') and len(nl) > 2 and nl[2].isdigit():
        return ('draw_point', 'Draw Point')
    if nl.startswith('drpoint') or nl.startswith('drawpoint') or nl.startswith('draw_point'):
        return ('draw_point', 'Draw Point')
    if nl.startswith('save') and 'point' in nl:
        return ('save_point', 'Save Point')
    if nl.startswith('svpt'):
        return ('save_point', 'Save Point')
    
    # Default: show as NPC with cleaned-up name
    return ('npc', None)  # None = use default name cleanup

def generate_header(survey_data):
    """Generate C++ header content from survey data."""
    lines = []
    lines.append('// entity_classifications.h - Auto-generated entity classification tables')
    lines.append('// Generated from field_entity_survey.json by generate_classifications.py')
    lines.append('// DO NOT EDIT MANUALLY - regenerate from survey data instead.')
    lines.append('//')
    lines.append('// Coverage: %d fields, %d total entities, %d unique SYM names' % (
        survey_data['fields_scanned'],
        survey_data['total_entities'],
        sum(len(v) for v in survey_data['sym_names_by_category'].values())
    ))
    lines.append('')
    lines.append('#pragma once')
    lines.append('')
    
    # Collect all unique names from all categories
    all_names = set()
    for cat, names in survey_data['sym_names_by_category'].items():
        all_names.update(names.keys())
    
    # Remove empty string
    all_names.discard('')
    
    # Classify everything
    skip_names = set()
    display_names = {}  # name -> display
    type_names = {}     # name -> type
    
    for name in sorted(all_names):
        result = classify_name(name, None)
        if result is None:
            skip_names.add(name)
        else:
            etype, display = result
            type_names[name] = etype
            if display:
                display_names[name] = display
    
    # Generate skip list
    lines.append('// ============================================================================')
    lines.append('// Controller/effect entities to hide from the navigation catalog.')
    lines.append('// These are invisible script controllers, lighting, camera, effects, etc.')
    lines.append('// %d entries.' % len(skip_names))
    lines.append('// ============================================================================')
    lines.append('static const char* ENTITY_SKIP_NAMES[] = {')
    for name in sorted(skip_names, key=str.lower):
        lines.append('    "%s",' % name)
    lines.append('    nullptr')
    lines.append('};')
    lines.append('')
    
    # Generate display name table
    lines.append('// ============================================================================')
    lines.append('// SYM name -> friendly display name for TTS.')
    lines.append('// %d entries.' % len(display_names))
    lines.append('// ============================================================================')
    lines.append('struct EntityDisplayName {')
    lines.append('    const char* sym;')
    lines.append('    const char* display;')
    lines.append('};')
    lines.append('')
    lines.append('static const EntityDisplayName ENTITY_DISPLAY_NAMES[] = {')
    for name in sorted(display_names.keys(), key=str.lower):
        lines.append('    { "%s", "%s" },' % (name, display_names[name]))
    lines.append('    { nullptr, nullptr }')
    lines.append('};')
    lines.append('')
    
    # Generate type classification
    lines.append('// ============================================================================')
    lines.append('// SYM name -> entity type for special entities (draw/save/shop/card).')
    lines.append('// ============================================================================')
    lines.append('enum EntityClassificationType {')
    lines.append('    EC_NONE = 0,')
    lines.append('    EC_DRAW_POINT,')
    lines.append('    EC_SAVE_POINT,')
    lines.append('    EC_SHOP,')
    lines.append('    EC_CARD_GAME,')
    lines.append('    EC_NPC,')
    lines.append('    EC_INTERACTIVE_OBJECT,')
    lines.append('};')
    lines.append('')
    lines.append('struct EntityTypeEntry {')
    lines.append('    const char* sym;')
    lines.append('    EntityClassificationType type;')
    lines.append('};')
    lines.append('')
    
    # Only output special types (draw/save/shop/card)
    special = {n: t for n, t in type_names.items() if t != 'npc'}
    type_map = {
        'draw_point': 'EC_DRAW_POINT',
        'save_point': 'EC_SAVE_POINT',
        'shop': 'EC_SHOP',
        'card_game': 'EC_CARD_GAME',
    }
    lines.append('static const EntityTypeEntry ENTITY_TYPE_TABLE[] = {')
    for name in sorted(special.keys(), key=str.lower):
        ctype = type_map.get(special[name], 'EC_NONE')
        lines.append('    { "%s", %s },' % (name, ctype))
    lines.append('    { nullptr, EC_NONE }')
    lines.append('};')
    lines.append('')
    
    # Stats comment
    lines.append('// Classification statistics:')
    type_counts = defaultdict(int)
    for t in type_names.values():
        type_counts[t] += 1
    lines.append('//   Skip (controllers): %d names' % len(skip_names))
    for t in ['draw_point', 'save_point', 'shop', 'card_game', 'npc']:
        lines.append('//   %s: %d names' % (t, type_counts.get(t, 0)))
    lines.append('//   Total classified: %d names' % len(type_names))
    lines.append('')
    
    return '\n'.join(lines)

def main():
    sd = os.path.dirname(os.path.abspath(__file__))
    json_path = os.path.join(sd, 'field_entity_survey.json')
    
    if not os.path.exists(json_path):
        print("ERROR: field_entity_survey.json not found. Run the survey first.")
        sys.exit(1)
    
    with open(json_path) as f:
        survey = json.load(f)
    
    header = generate_header(survey)
    
    out_path = os.path.join(sd, 'src', 'entity_classifications.h')
    with open(out_path, 'w') as f:
        f.write(header)
    
    print("Generated: %s" % out_path)
    
    # Print summary
    all_names = set()
    for cat, names in survey['sym_names_by_category'].items():
        all_names.update(names.keys())
    all_names.discard('')
    
    skip_count = 0
    type_counts = defaultdict(int)
    for name in all_names:
        result = classify_name(name, None)
        if result is None:
            skip_count += 1
        else:
            type_counts[result[0]] += 1
    
    print("\nClassification summary:")
    print("  Total unique SYM names: %d" % len(all_names))
    print("  Skip (controllers): %d" % skip_count)
    for t in ['draw_point', 'save_point', 'shop', 'card_game', 'npc']:
        print("  %s: %d" % (t, type_counts.get(t, 0)))
    print("  Unclassified: %d" % (len(all_names) - skip_count - sum(type_counts.values())))

if __name__ == '__main__':
    main()
