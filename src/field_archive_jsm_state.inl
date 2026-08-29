// field_archive_jsm_state.inl — hoisted state for the JSM scanner pipeline.
// Included from field_archive_jsm.inl. Do not compile independently.
//
// v0.16.3 split: the struct definitions and `static` arrays below were
// previously function-local statics inside ScanJSMScripts(). They have been
// hoisted to namespace scope so RunDirectorDetection() (in
// field_archive_jsm_director.inl) can reference the same cross-pass state.
//
// Behavioral semantics are unchanged:
//   - Function-local `static` arrays already had program lifetime;
//     namespace-scope statics have the same lifetime.
//   - ScanJSMScripts() still calls memset() on each array at the top of
//     every scan invocation, so the zero-on-entry contract is preserved.
//   - Visibility was previously ScanJSMScripts-only; now they are visible
//     to anything included after this file inside the FieldArchive namespace.

// ---------- Size constants ----------

// v0.07.84: Per-method MAPJUMP records. Indexed by global method index.
// 4096 is generous: largest fields have ~700 entry-point methods.
static const int MAX_METHOD_MAPJUMPS    = 4096;

// v0.07.87: PSHM_W reads per method (for variable-dispatch matching).
static const int MAX_PSHM_PER_METHOD    = 16;

// v0.07.84: REQ call targets recorded per entity.
static const int MAX_REQ_PER_ENTITY     = 8;

// v0.07.87: POPM_W writes recorded per entity.
static const int MAX_POPM_PER_ENTITY    = 16;

// v0.12.20: Literal PUSH + POPM_W pairs recorded from each entity's init method
// (method 0 only). Used by Director position extraction.
static const int MAX_VAR_WRITES_PER_ENT = 64;

// ---------- Struct definitions ----------

// v0.07.84/v0.07.87: Per-method MAPJUMP record. Captures destination field
// (when literal) and the PSHM_W memory addresses read inside the method
// (for variable-dispatch exit detection in the post-scan passes).
struct MethodMapjump {
    bool found;
    int destFieldId;
    int32_t pshmAddrs[MAX_PSHM_PER_METHOD];  // memory addresses read by PSHM_W
    int pshmCount;
};

// v0.07.84: Per-entity REQ call targets. Recorded when REQ/REQSW/REQEW fires
// with at least 3 stack values (entity_id, method_id, priority); used by the
// indirect-MAPJUMP post-pass and the Line REQ-following pass.
struct ReqCallInfo { int targetEntity; int targetMethod; int srcMethod; int srcRel; };  // v0.62.2/.3
struct EntityReqs  { ReqCallInfo calls[MAX_REQ_PER_ENTITY]; int count; };

// v0.07.87: Per-entity POPM_W writes (memory addresses written to). Used by
// the variable-dispatch exit detection post-pass.
struct EntityPopms { int32_t addrs[MAX_POPM_PER_ENTITY]; int count; };

// v0.12.20: Per-entity init-method variable map. Tracks literal-PUSH +
// POPM_W pairs found in method 0 only. Director position extraction reads
// this to resolve PSHM_W loads in interaction methods.
struct VarWrite { int32_t addr; int32_t value; };
struct EntityVarMap { VarWrite writes[64]; int count; };

// ---------- Static arrays (cross-pass state shared with director.inl) ----------

static MethodMapjump s_methodMapjumps[MAX_METHOD_MAPJUMPS];
static EntityReqs    s_entityReqs[128];
static EntityPopms   s_entityPopms[128];
static EntityVarMap  s_initVarMaps[128];

// v0.12.20: Per-entity REQ opcode count (independent of stack state).
// The stack-based REQ tracking in s_entityReqs often fails in complex
// Director methods (pushCount<3 when REQ fires). This counter simply
// counts REQ/REQSW/REQEW opcodes seen per entity, which is reliable.
static int  s_reqOpcodeCount[128];

// v0.12.20: Per-entity persistent flags for Director detection.
static bool s_hasSetmodelInit[128];   // SETMODEL found in init method (method 0)
static bool s_hasDialogAny[128];      // MES/ASK/AMES/AASK in any method
static bool s_hasExtDispatchArr[128]; // 0x1C extended dispatch in any method

// v0.19.7: this JSM entity is the TARGET of some REQ/REQSW/REQEW opcode, resolved
// STATICALLY from the opcode's inline param (opcParam) -- exe RE proved the target is
// arg2/inline, not a stack value (which is why the old stack-based s_entityReqs read
// reqResolved=0). The director-gate keeps a promoted "Object" only if it is a real REQ
// target; a director-promoted entity that no one REQs has no interaction path at all.
static bool s_isReqTarget[128];
// v0.62.0 (#123): this Line's OWN init calls LINEOFF (0x3B, handler 0x0051DD00,
// which writes 0 to the line object's enable flag at +0x194). A line the field
// deliberately switches off at load is gated content, not a passive tripwire.
static bool s_lineInitOff[128];

// ---------- Forward declarations ----------

// Director detection post-pass. Called from ScanJSMScripts() after the
// per-entity scan loop and the draw-point trigger cross-reference complete,
// and before the disabled-diagnostics block / Line REQ-following pass /
// final results logging. Defined in field_archive_jsm_director.inl.
static void RunDirectorDetection(const char* fieldName,
                                 JSMEntityInfo* outEntities,
                                 int outCount,
                                 int countDoors,
                                 int countLines,
                                 int countBg,
                                 char symNames[][32],
                                 int symCount);

// v0.17.8.7: Debug-leftover detector. Returns true for the 'cardgamemaster*'
// debug card-game entities (and, secondarily, entities whose init-var writes
// reference a "testbl*" field). Used to skip the INTERACTIVE_OBJECT promotion
// in BOTH the main scan and the Director post-pass. Defined in
// field_archive_jsm_scan.inl; forward-declared here so director.inl (included
// earlier) can call it.
static bool EntityIsDebugLeftover(int e, const char* sym);
