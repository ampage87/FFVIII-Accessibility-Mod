#!/usr/bin/env bash
# run_gates.sh -- the pre-push gate. Must print "overall fail=0".
#   linters -> size gate -> every tests/*.cpp compiled and run
# Usage: bash tests/run_gates.sh   (from the repo root)
set -u
cd "$(dirname "$0")/.."
fail=0
OUT=tests_out
mkdir -p "$OUT"

echo "=== linters ==="
for l in lint_braces lint_freeze lint_seh lint_stub lint_xtu lint_harness_includes lint_msvc_macros lint_dialog_page lint_rag_guard; do
  if python3 "tests/$l.py" >"$OUT/$l.txt" 2>&1; then echo "$l  OK"
  else echo "$l  FAIL"; sed -n '1,40p' "$OUT/$l.txt"; fail=$((fail+1)); fi
done

echo "=== size gate (80 KB per .cpp/.inl, field_catalog.inl exempt) ==="
big=$(find src -name '*.cpp' -o -name '*.inl' | while read -r f; do
        case "$f" in */field_catalog.inl) continue;; esac
        s=$(stat -c%s "$f"); [ "$s" -gt 81920 ] && echo "$s $f"
      done)
if [ -n "$big" ]; then echo "OVER 80KB:"; echo "$big"; fail=$((fail+1)); else
  echo "size gate: OK  (largest non-exempt: $(find src -name '*.cpp' -o -name '*.inl' | grep -v field_catalog.inl | xargs stat -c'%s %n' | sort -rn | head -1))"
fi

echo "=== win32 syntax check (winshim) ==="
for f in src/field_archive.cpp src/field_overlay.cpp src/bahamut_light_overlay.cpp src/centra_code_overlay.cpp; do
  if g++ -std=c++17 -fsyntax-only -w -Isrc -Itests/winshim -Itests "$f" 2>"$OUT/syntax.txt"; then
    echo "$(basename "$f")  SYNTAX OK"
  else
    echo "$(basename "$f")  SYNTAX FAIL"; sed -n '1,25p' "$OUT/syntax.txt"; fail=$((fail+1))
  fi
done

echo "=== compile + run ==="
for t in tests/*.cpp; do
  b=$(basename "$t" .cpp)
  # menu_sim is a library for other tests; jsm_scan_harness needs the winshim
  # include path and the extracted archive, so it has its own stage below.
  case "$b" in menu_sim|jsm_scan_harness) continue;; esac
  extra=""
  grep -q 'ff8_text_decode.h' "$t" && extra="src/ff8_text_decode.cpp"
  # v0.63.1: a probe that RUNS a Win32 translation unit (countdown_hold_test
  # compiles src/countdown_timer.cpp itself) needs the winshim on the path.
  inc=""
  grep -q '<windows.h>' "$t" && inc="-Itests/winshim"
  if ! g++ -std=c++17 -O0 -w -Isrc -Itests $inc -o "$OUT/$b" "$t" $extra >"$OUT/$b.build" 2>&1; then
    echo "$b  BUILD FAIL"; sed -n '1,25p' "$OUT/$b.build"; fail=$((fail+1)); continue
  fi
  if "$OUT/$b" >"$OUT/$b.run" 2>&1; then echo "$b  OK"
  else echo "$b  RUN FAIL"; sed -n '1,25p' "$OUT/$b.run"; fail=$((fail+1)); fi
done

echo "=== field-script scanner over the real archive ==="
FIELDDATA="${FIELDDATA:-$PWD/../fielddata/out}"
if [ ! -d "$FIELDDATA" ]; then FIELDDATA="$PWD/fielddata/out"; fi
if [ -d "$FIELDDATA" ]; then
  if g++ -std=c++17 -O0 -w -Isrc -Itests/winshim -Itests -o "$OUT/jsm_scan_harness" tests/jsm_scan_harness.cpp 2>"$OUT/scanbuild.txt"; then
    "$OUT/jsm_scan_harness" "$FIELDDATA" --all > "$OUT/jsm_scan.txt" 2>/dev/null
    if diff -q tests/jsm_scan_golden.txt "$OUT/jsm_scan.txt" >/dev/null; then
      echo "jsm_scan_harness  OK  ($(grep -c '^### ' "$OUT/jsm_scan.txt") fields match the golden)"
    else
      echo "jsm_scan_harness  FAIL -- scanner output differs from tests/jsm_scan_golden.txt"
      diff tests/jsm_scan_golden.txt "$OUT/jsm_scan.txt" | head -40
      fail=$((fail+1))
    fi
  else
    echo "jsm_scan_harness  BUILD FAIL"; sed -n '1,25p' "$OUT/scanbuild.txt"; fail=$((fail+1))
  fi
else
  echo "jsm_scan_harness  SKIPPED (no extracted field data at $FIELDDATA --"
  echo "                  regenerate with fieldsim/extract_fields.py)"
fi

echo "overall fail=$fail"
exit $fail
