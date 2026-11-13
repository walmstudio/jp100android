#!/bin/sh
# make with a memory watchdog: pause the newest compiler if RAM runs low, resume later
cd /data/work/me1androd || exit 1
T="$*"
date +%s
make -k -f jp100.mk -j${JOBS:-2} $T &
MP=$!
STOPPED=""
while kill -0 $MP 2>/dev/null; do
  av=$(awk '/MemAvailable/{print $2}' /proc/meminfo)
  n=$(pgrep -c -f -- ' -cc1 ')
  if [ -z "$STOPPED" ] && [ "$av" -lt 900000 ] && [ "$n" -ge 2 ]; then
    STOPPED=$(pgrep -n -f -- ' -cc1 '); kill -STOP $STOPPED; echo "[watch] low memory ($av kB): paused $STOPPED"
  elif [ -n "$STOPPED" ] && { [ "$av" -gt 1600000 ] || [ "$n" -le 1 ]; }; then
    kill -CONT $STOPPED 2>/dev/null; echo "[watch] resumed $STOPPED"; STOPPED=""
  fi
  sleep 1
done
wait $MP
echo "exit=$?"
date +%s
