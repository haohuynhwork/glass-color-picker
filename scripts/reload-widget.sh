#!/usr/bin/env bash
# Install/update the Plasma widget and load the new version.
#   scripts/reload-widget.sh          update, then restart plasmashell (force-kill if it hangs)
#   scripts/reload-widget.sh --window update, then preview in a standalone window (no shell restart)
set -euo pipefail
cd "$(dirname "$0")/.."

kpackagetool6 -t Plasma/Applet -u plasmoid 2>/dev/null || kpackagetool6 -t Plasma/Applet -i plasmoid

if [[ "${1:-}" == "--window" ]]; then
    exec plasmawindowed dev.owen.glasscolorpicker
fi

# Plasma caches a widget's QML until the shell restarts, so a full restart is required.
# plasmashell often deadlocks on shutdown: give it ~1.5s, then kill it.
state() { systemctl --user is-active plasma-plasmashell || true; }
stopped() { [[ "$(state)" =~ ^(inactive|failed)$ ]]; }

systemctl --user stop --no-block plasma-plasmashell
for _ in $(seq 15); do stopped && break; sleep 0.1; done
if ! stopped; then
    echo "plasmashell hung on exit, killing it" >&2
    systemctl --user kill -s KILL plasma-plasmashell
    for _ in $(seq 20); do stopped && break; sleep 0.1; done
fi
systemctl --user reset-failed plasma-plasmashell 2>/dev/null || true
systemctl --user start plasma-plasmashell
echo "plasmashell: $(state)"
