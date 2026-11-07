#!/usr/bin/env bash
# Auto-reload the widget whenever a file in plasmoid/ is saved.
#   scripts/watch-widget.sh          preview in a standalone window, relaunched on save (~1s, panel untouched)
#   scripts/watch-widget.sh --panel  restart plasmashell on save so the real panel updates
# Ctrl+C to stop.
set -uo pipefail
cd "$(dirname "$0")/.."

mode=${1:-window}
viewer=""

stamp() { find plasmoid -type f -printf '%T@ %p\n' | sort | md5sum; }

reload() {
    kpackagetool6 -t Plasma/Applet -u plasmoid >/dev/null 2>&1 || kpackagetool6 -t Plasma/Applet -i plasmoid >/dev/null
    if [[ "$mode" == "--panel" ]]; then
        scripts/reload-widget.sh >/dev/null 2>&1 &
    else
        [[ -n "$viewer" ]] && kill "$viewer" 2>/dev/null && wait "$viewer" 2>/dev/null
        # Show only our QML warnings/errors from the preview.
        # (process substitution so $! is plasmawindowed itself, not grep)
        plasmawindowed dev.owen.glasscolorpicker > >(grep --line-buffered -E 'dev\.owen\.glass|formats\.js') 2>&1 &
        viewer=$!
    fi
    echo "[$(date +%T)] reloaded"
}

trap '[[ -n "$viewer" ]] && kill "$viewer" 2>/dev/null; exit' INT TERM
last=$(stamp)
reload
while sleep 0.4; do
    now=$(stamp)
    [[ "$now" != "$last" ]] || continue
    last=$now
    sleep 0.2   # let the editor finish writing
    reload
done
