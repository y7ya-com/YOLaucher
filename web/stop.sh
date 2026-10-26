#!/bin/bash
# stop.sh: stop the servers web/play.sh started (a run.py it found already running is left alone).
DATA=${YO2WEB:-$HOME/.yo2web}
for p in web gateway server; do
  f=$DATA/play/pids/$p
  [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null && echo "stopped $p"
  rm -f "$f"
done
