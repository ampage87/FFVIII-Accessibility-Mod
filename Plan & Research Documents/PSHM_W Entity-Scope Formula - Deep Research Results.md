# PSHM_W Entity-Scope Formula — Deep Research Results
## Received: 2026-03-19

# PSHM_W entity-scope path: undocumented but reconstructible

**The dual code path in the PSHM_W handler at 0x0051C5C0 is genuine but has zero public documentation.** No community wiki, open-source reimplementation, or published RE database describes this behavior — the user has discovered original ground. All public sources (ffrtt.ru, Qhimm wiki, FFNx, Deling, OpenVIII) describe PSHM_W as a single-path varblock read: `*(int16_t*)(0x1CFE9B8 + param)`. However, by cross-referencing the user's observed data patterns with the documented JSM architecture, the entity-scope path's logic can be substantially reconstructed.

## Key Findings Summary

### Three Parameter Modes (per-axis independent)

1. **Passthrough** — Negative params ARE literal coordinates (cannot be valid unsigned varblock offsets)
2. **Entity-scope** — Small positive below threshold + execution flag at entity+0x160 → subroutine at 0x00532890
3. **Standard varblock** — Positive above threshold → `*(int16_t*)(0x1CFE9B8 + param)`

### Descriptor Struct
- ~144 bytes (9 × 16 = 0x90), indexed by flat entity index
- Key fields: +0x0C/+0x0E = computed X/Y, +0x68 = curve data ptr, +0x7E = cache key

### bghall_1 Entity Data

| Entity | PSHM Addr (X,Y,Z) | Actual Pos (X,Y) | Pattern |
|--------|-------------------|-------------------|---------|
| elelight | (654,-10304,567) | (654,-10304) | All passthrough |
| stairlight | (1,-700,-8593) | (-700,-8593) | First=mode selector, Y,Z=passthrough |
| l1 | (1032,-2865,-5421) | (-2865,-5421) | X=varblock, Y,Z=passthrough |
| l4 | (1032,1040,4) | (-290,-11298) | All varblock reads |

### Z=567
Floor-level Z coordinate for bghall_1 main hall. Default when shift pattern consumes first param as mode selector.

### Static Extraction Feasibility
- **PSHN_L literals**: Trivially extractable from bytecode
- **Negative PSHM_W**: Passthrough — param IS the coordinate
- **Varblock reads**: Tractable if init scripts traced to constant stores
- **Parametric curves**: Runtime-only (minority of entities)

### Recommended Implementation
Hybrid strategy: static JSM extraction for literals + negative PSHM_W passthrough, runtime hook for entity-scope/curve entities.

---

*Full research text preserved below for reference.*

---

(See uploaded file `compass_artifact_wf-e5623574-49a0-413c-91dd-a2d67bc916c5_text_markdown.md` for the complete ~6000-word analysis including pseudocode, data tables, and implementation recommendations.)
