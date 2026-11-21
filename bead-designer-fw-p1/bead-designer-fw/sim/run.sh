#!/usr/bin/env bash
# Run the bead-designer firmware on esp32sim as the Freenove FNK0114A board.
#
#   sim/run.sh --web 8080                       # live board at http://127.0.0.1:8080/
#   sim/run.sh --script sim/scripts/draw.txt --tft-png out.png --max-seconds 6
#   sim/run.sh --build [...]                    # idf.py build (esp32) first; alone = build only
#
# Any arguments after the optional --build go straight to esp32sim.
# Env overrides: ESP32SIM (emulator binary), IDF_EXPORT (export.sh to source).
set -euo pipefail

FW="$(cd "$(dirname "$0")/.." && pwd)"
B="$FW/build"
ESP32SIM="${ESP32SIM:-$HOME/esp32sim/target/release/esp32sim}"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf/export.sh}"

if [[ "${1:-}" == "--build" ]]; then
    shift
    # shellcheck disable=SC1090
    . "$IDF_EXPORT" >/dev/null
    if ! grep -q '^CONFIG_IDF_TARGET="esp32"$' "$FW/sdkconfig" 2>/dev/null; then
        idf.py -C "$FW" set-target esp32
    fi
    idf.py -C "$FW" build
    # `--build` alone just builds; without --max-seconds/--web the emulator would run forever.
    [[ $# -gt 0 ]] || exit 0
fi

[[ -x "$ESP32SIM" ]] || { echo "esp32sim not found at $ESP32SIM (cargo build --release -p esp32sim in ~/esp32sim)" >&2; exit 1; }
[[ -f "$B/bead_designer.bin" ]] || { echo "no firmware build; run: sim/run.sh --build" >&2; exit 1; }
grep -q '^CONFIG_IDF_TARGET="esp32"$' "$FW/sdkconfig" || { echo "build/ is not an esp32 build; run: sim/run.sh --build" >&2; exit 1; }

# The classic-ESP32 ECO3 ROM (esp32_rev300_rom.elf) is found in ~/.espressif automatically.
exec "$ESP32SIM" --chip esp32 --board freenove-fnk0114 --boot rom \
    --bootloader "$B/bootloader/bootloader.bin" \
    --ptable "$B/partition_table/partition-table.bin" \
    --app "$B/bead_designer.bin" \
    --elf "$B/bead_designer.elf" \
    --console uart0 \
    "$@"
