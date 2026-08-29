// field_archive_jsm.inl - JSM script scanner for entity classification
// (slim shell; included from field_archive.cpp inside namespace FieldArchive)
//
// v0.16.3 split. This file used to be the 91 KB monolithic JSM scanner
// (over the 80 KB CI hard-fail). The implementation is now divided into
// seven sub-`.inl` files, included textually below in dependency order.
//
// Module roles:
//   state.inl     — Hoisted struct definitions, size constants, and
//                   `static` cross-pass arrays shared between
//                   ScanJSMScripts() and RunDirectorDetection().
//                   Also holds the RunDirectorDetection forward decl.
//   constants.inl — JSM_OP_* opcode IDs and the JSMEntityTypeName()
//                   public-API lookup.
//   helpers.inl   — Low-level utilities: GetFieldIdByInternalName,
//                   SwapBE32, DecodeJSMInstruction.
//   opnames.inl   — GetOpcodeName() lookup used by the script-dump
//                   diagnostic.
//   director.inl  — RunDirectorDetection() post-pass that flags
//                   invisible Other entities as Directors and promotes
//                   their dispatch targets to Interactive Objects.
//                   Extracted from inside ScanJSMScripts() — was the
//                   tipping point that pushed the parent file past
//                   the size limit.
//   scan.inl      — ScanJSMScripts() main body. Per-entity opcode
//                   scan, position extraction (SET3/SET, PSHM_W marker
//                   handling), classification cascade, REQ-following,
//                   variable-dispatch detection, and final logging.
//   dump.inl      — DumpEntityScript() diagnostic that decodes a
//                   single entity's bytecode to the field log.
//
// Public-API surface unchanged: JSMEntityTypeName, ScanJSMScripts,
// and DumpEntityScript remain visible to field_archive.cpp callers
// exactly as before the split.

#include "field_archive_jsm_decode.inl"  // v0.59.0: the VM instruction encoding, read from the exe
#include "field_archive_jsm_order.inl"   // v0.58.0: engine-verified group/slot/SYM ordering
#include "field_archive_jsm_state.inl"
#include "field_archive_jsm_constants.inl"
// v0.111.0 (#dsrc): when a trigger line is a door. Pure, and tested.
#include "jsm_line_exit_model.inl"
#include "field_archive_jsm_helpers.inl"
#include "field_archive_jsm_opnames.inl"
#include "field_archive_jsm_director.inl"
#include "field_archive_jsm_mapjump_resolver.inl"  // v0.17.7.5: static destField resolver, included before scan.inl (used inside ScanJSMScripts)
#include "field_archive_jsm_scan.inl"
#include "field_archive_jsm_dump.inl"
#include "field_archive_jsm_initvars.inl"  // v0.17.7.2: public API for init-var lookups
