#!/usr/bin/env bash
# Run hw/hwtest.py inside the ESP-IDF Python env and the uucp group (for /dev/ttyACM0).
#   hw/run.sh boot | shot [file.png] | run [--update-golden] [names...] | list
#   hw/run.sh flash         # build (build-s3, sdkconfig.s3) and flash first
set -euo pipefail
FW="$(cd "$(dirname "$0")/.." && pwd)"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf/export.sh}"
PORT="${PORT:-/dev/ttyACM0}"
q() { printf '%q ' "$@"; }
if [[ "${1:-}" == "flash" ]]; then
    shift
    printf '. %q >/dev/null 2>&1; cd %q && idf.py -B build-s3 -D SDKCONFIG=sdkconfig.s3 -p %q flash\n' \
        "$IDF_EXPORT" "$FW" "$PORT" | newgrp uucp
    [[ $# -gt 0 ]] || exit 0
fi
printf '. %q >/dev/null 2>&1; python %q %s --port %q\n' "$IDF_EXPORT" "$FW/hw/hwtest.py" "$(q "$@")" "$PORT" | newgrp uucp
