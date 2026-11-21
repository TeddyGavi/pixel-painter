#!/usr/bin/env bash
# Run every sim/scripts/*.txt scenario headless and check the results.
#
#   sim/check.sh                  # run all scenarios
#   sim/check.sh draw place       # run selected scenarios
#   sim/check.sh --update-golden  # (re)write sim/golden/<name>.png from this run
#
# A scenario fails if the console shows a crash signature, or if
# sim/golden/<name>.png exists and the final screen differs from it.
# Outputs go to sim/out/<name>.{png,log}.
set -uo pipefail

SIM="$(cd "$(dirname "$0")" && pwd)"
OUT="$SIM/out"; GOLD="$SIM/golden"
mkdir -p "$OUT"

update=0; names=()
for a in "$@"; do
    if [[ "$a" == "--update-golden" ]]; then update=1; else names+=("$a"); fi
done
if [[ ${#names[@]} -eq 0 ]]; then
    for f in "$SIM"/scripts/*.txt; do names+=("$(basename "$f" .txt)"); done
fi

CRASH='Guru Meditation|abort\(\) was called|ESP_ERROR_CHECK failed|assert failed|Stack canary|stack overflow|Backtrace:|rst:0x[0-9a-f]+ \((TG|RTCWDT|INT_WDT)'
fail=0
for n in "${names[@]}"; do
    script="$SIM/scripts/$n.txt"
    [[ -f "$script" ]] || { echo "FAIL $n: no such script $script"; fail=1; continue; }
    # Scenarios end with a `stop` line; --max-seconds is only a safety net.
    "$SIM/run.sh" --script "$script" --tft-png "$OUT/$n.png" --max-seconds 60 --no-dump \
        >"$OUT/$n.log" 2>&1
    rc=$?
    status=PASS; why=""
    if [[ $rc -ne 0 ]]; then status=FAIL; why="esp32sim exit $rc"; fi
    if m=$(grep -Em1 "$CRASH" "$OUT/$n.log"); then status=FAIL; why="crash: $m"; fi
    if [[ ! -f "$OUT/$n.png" ]]; then status=FAIL; why="${why:-no screen captured}"; fi
    if [[ $status == PASS && -f "$OUT/$n.png" ]]; then
        if [[ $update -eq 1 ]]; then
            mkdir -p "$GOLD"; cp "$OUT/$n.png" "$GOLD/$n.png"; why="golden updated"
        elif [[ -f "$GOLD/$n.png" ]] && ! cmp -s "$OUT/$n.png" "$GOLD/$n.png"; then
            status=FAIL; why="screen differs from golden/$n.png (see out/$n.png)"
        fi
    fi
    echo "$status $n${why:+: $why}"
    [[ $status == PASS ]] || fail=1
done
exit $fail
