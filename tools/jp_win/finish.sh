#!/bin/sh
# JP100 v3 finish: generate + hook the HLE additions, rebuild changed runtime objects, link, APK, source zip.
# Needs /data/work (v2 sources + restored build/android objects), the NDK, /data/setup_tools.sh and /data/v3/*.
set -e
R=/data/work/me1androd
cd $R
mkdir -p tools/jp_win logs
cp /data/v3/hle_jp_win_a.inc /data/v3/hle_jp_win_b.inc /data/v3/hle_jp_win_c.inc /data/v3/gen_jp_win.py /data/v3/finish.sh tools/jp_win/
python3 tools/jp_win/gen_jp_win.py $R
[ -d /data/tools/android-sdk/build-tools/30.0.3 ] || (sh /data/setup_tools.sh > /data/setup.log 2>&1 || echo SETUP_FAIL >> /data/setup.log) &
if ! make -f jp100.mk -j2 jp-lib > logs/build_lib.log 2>&1; then grep -n 'error' logs/build_lib.log | head -20; tail -5 logs/build_lib.log; exit 1; fi
ls -la build/android/*.so
wait
[ -f /data/setup.log ] && tail -2 /data/setup.log
if ! sh jp_launcher/build_apk_jp.sh > logs/build_apk.log 2>&1; then tail -20 logs/build_apk.log; exit 1; fi
tail -3 logs/build_apk.log
cp android_jp/out/JP100-port.apk /data/JP100-port-v3.apk
if ! grep -q '^## v3' $R/../README_JP100_RU.md; then cat >> $R/../README_JP100_RU.md <<'EOF'

## v3 (9 ноября 2026)

- v2 падал при старте CRT на отсутствующем KERNEL32!HeapSetInformation. В v3 разрешаются все оставшиеся Win32-импорты JurassicPark100.exe и fmodex.dll (кроме WLDAP32): около 330 записей в `runtime/hle_jp_win.inc`. Файл генерируется скриптом `tools/jp_win/gen_jp_win.py` из `hle_jp_win_{a,b,c}.inc` и подключается в `hle_jp.c`.
- Настоящие реализации: GlobalMemoryStatusEx, lstrcmp/lstrcpyn, MulDiv, LoadLibraryEx, CompareStringW, GetDate/TimeFormat, SList, BSTR/VARIANT, CoTaskMem*, CLSIDFromString, Path*, wsprintfA, ToUnicode/ToAscii, GetKeyboardState, Get/SetProp, CallWindowProc, MsgWaitForMultipleObjectsEx, htonl/ntohs/inet_addr/gethostname и др.
- Всё остальное закрыто заглушками: верное число stdcall-аргументов, код ошибки, одна строка `[hle] stub DLL!Name` в логе на функцию. GDI, печать, буфер обмена, IME, сеть и запись звука сообщают игре об отказе.
- rt_fault завершает процесс через `_Exit(3)` вместо `exit(3)`: atexit-обработчики и статические деструкторы больше не запускаются, пока работают другие гостевые потоки.
EOF
fi
cd /data/work && rm -f /data/jp100-port-v3-src.zip && zip -q -r -X /data/jp100-port-v3-src.zip . -x 'me1androd/build/*' 'me1androd/android_jp/*' 'me1androd/logs/*'
ls -la /data/jp100-port-v3-src.zip /data/JP100-port-v3.apk
echo FINISH_DONE
