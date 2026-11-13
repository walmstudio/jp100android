#!/usr/bin/env bash
# Сборка APK Jurassic Park: The Game (эпизод 100) для Android — по образцу builder.sh (me1port).
#
#   ./build-android-jp100.sh --setup --game ~/JPTelltale   первый раз: JDK 17, Android SDK и NDK скачаются в ~/me1port-tools
#   ./build-android-jp100.sh --game ~/JPTelltale            дальше: пересобирается только изменившееся
#   ./build-android-jp100.sh --game ~/JPTelltale --install  + поставить APK на телефон по adb
#
# Нужны: bash, make, python3 (capstone поставится сам), zip; для --setup ещё curl или wget и unzip.
set -u
ROOT=$(cd "$(dirname "$0")" && pwd -P)
. "$ROOT/tools/jp100_common.sh"

usage() {
    cat <<'EOF'
Использование: ./build-android-jp100.sh [параметры]

  --game ПАПКА      папка установленной игры (там JurassicPark100.exe, fmodex.dll, binkw32.dll);
                    без неё ищется в $JP_GAME, ~/JPTelltale и ../inputs
  --exe ФАЙЛ        JurassicPark100.exe явно
  --fmodex ФАЙЛ     fmodex.dll явно
  --bink ФАЙЛ       binkw32.dll явно
  --setup           скачать недостающее: JDK 17, Android SDK (build-tools 30.0.3, android-29), NDK r27c
  --tools ПАПКА     куда ставить JDK/SDK/NDK (по умолчанию ~/me1port-tools, как у me1port)
  --recomp          перекомпилировать exe и DLL заново, даже если ничего не менялось
  --no-data-seeds   не искать указатели на код в секциях данных exe (если с ними что-то сломается)
  --opt "ФЛАГИ"     флаги компилятора для кода игры (по умолчанию -O1)
  -j N              потоков компиляции (по умолчанию — по ядрам и памяти)
  --clean           удалить объектные файлы и собрать заново (рекомпиляцию не трогает)
  --keystore ФАЙЛ   свой ключ подписи (пароль android, alias me1port); по умолчанию — android/keystore
  --no-spv          не собирать libme1spv.so (GLSL → SPIR-V): без него Vulkan-рендерер D3D9→Vulkan недоступен,
                    останется OpenGL ES (glslang 15 для Android соберётся сам при --setup)
  --gpu             собрать Mesa Zink + libadrenotools (android/gpu/build_gpu.sh, как у me1port: meson, ninja,
                    cmake, bison, flex, git, сеть; долго) — для Zink и своих драйверов AdrenoTools
  --install         поставить APK по adb (телефон по USB, отладка по USB включена)
  -v, --verbose     показывать полный вывод компиляции
  -h, --help        эта справка

Логи: build/logs/*.log, build/recomp/*.log, build/seeds/*.log.
Если игра падает с "[rt] MISSING-FUNC xxxxxxxx", добавьте адрес в seeds/jp100-extra.txt
(или положите туда лог запуска) и запустите сборку ещё раз — см. seeds/README.md.
EOF
}

GAME= EXE= FMOD= BINK= SETUP=0 FORCE=0 CLEAN=0 ADBINST=0 NODATA=0 VERBOSE=0 NOSPV=0 GPU=0 JOBS= KEYSTORE_ARG= OPT=
TOOLS=${ME1_TOOLS:-$HOME/me1port-tools}
while [ $# -gt 0 ]; do
    case $1 in
        --game) GAME=${2:?--game ПАПКА}; shift ;;
        --game=*) GAME=${1#*=} ;;
        --exe) EXE=${2:?--exe ФАЙЛ}; shift ;;
        --fmodex) FMOD=${2:?--fmodex ФАЙЛ}; shift ;;
        --bink) BINK=${2:?--bink ФАЙЛ}; shift ;;
        --setup) SETUP=1 ;;
        --tools) TOOLS=${2:?--tools ПАПКА}; shift ;;
        --recomp) FORCE=1 ;;
        --no-data-seeds) NODATA=1 ;;
        --opt) OPT=${2:?--opt ФЛАГИ}; shift ;;
        -j) JOBS=${2:?-j N}; shift ;;
        -j[0-9]*) JOBS=${1#-j} ;;
        --clean) CLEAN=1 ;;
        --keystore) KEYSTORE_ARG=${2:?--keystore ФАЙЛ}; shift ;;
        --install) ADBINST=1 ;;
        --no-spv) NOSPV=1 ;;
        --gpu) GPU=1 ;;
        -v|--verbose) VERBOSE=1 ;;
        -h|--help) usage; exit 0 ;;
        *) die "неизвестный параметр: $1 (см. --help)" ;;
    esac
    shift
done
for _v in GAME EXE FMOD BINK KEYSTORE_ARG TOOLS; do
    eval "_x=\$$_v"
    case $_x in "~/"*) _x=$HOME/${_x#"~/"} ;; esac
    [ -n "$_x" ] && eval "$_v=\$(abspath \"\$_x\")"
done
[ -z "$GAME" ] || [ -d "$GAME" ] || die "нет папки $GAME"
[ -z "$KEYSTORE_ARG" ] || [ -f "$KEYSTORE_ARG" ] || die "нет файла $KEYSTORE_ARG"
cd "$ROOT" || exit 1

say "Проверка окружения"
for _t in make find awk sed zip; do have "$_t" || die "нет $_t: sudo apt install $_t"; done
if [ "$SETUP" = 1 ]; then
    have unzip || die "для --setup нужен unzip: sudo apt install unzip"
    have curl || have wget || die "для --setup нужен curl или wget: sudo apt install curl"
fi
find_python || die "нужен python3 с модулем capstone: sudo apt install python3-capstone (или python3-venv для автоустановки)"
[ -n "$JOBS" ] || JOBS=$(default_jobs)
ok "python: $PY; потоков компиляции: $JOBS (ядер $(ncpu), памяти $(mem_gb) ГБ)"

# поиск и установка JDK/SDK/NDK — те же функции, что в builder.sh для Mass Effect
grep -q '^dl() {' builder.sh && grep -q '^# --- glslang' builder.sh \
    || die "в builder.sh (me1port) нет блока функций dl() … glslang — файл изменён?"
eval "$(sed -n '/^dl() {/,/^# --- glslang/p' builder.sh)"
# v15: glslang (libme1spv.so для Vulkan-рендерера) — тоже из builder.sh
grep -q '^gl_ok() {' builder.sh || die "в builder.sh нет функций glslang (gl_ok … setup_glslang)"
eval "$(sed -n '/^gl_ok() {/,/^if \[ \$SETUP = 1 \]; then/p' builder.sh | sed '$d')"
for _f in find_jdk setup_jdk find_sdk find_android_parts setup_sdk find_glslang find_cmake setup_glslang; do
    type "$_f" >/dev/null 2>&1 || die "в builder.sh нет функции $_f"
done

say "Android: JDK 17, SDK, NDK r27c"
JH= JV= SDK= SDKM= BT= AJAR= NDK_DIR=
find_jdk || { [ "$SETUP" = 1 ] && setup_jdk && find_jdk; } \
    || die "нужен JDK 17+: запустите с --setup (скачается в $TOOLS) или поставьте openjdk-17-jdk"
find_sdk; find_android_parts
_need=
[ -n "$BT" ] || _need="$_need build-tools;30.0.3"
[ -n "$AJAR" ] || _need="$_need platforms;android-29"
[ -n "$NDK_DIR" ] || _need="$_need ndk;27.2.12479018"
if [ "$ADBINST" = 1 ] && ! have adb && [ ! -x "${SDK:-/nonexistent}/platform-tools/adb" ]; then _need="$_need platform-tools"; fi
if [ -n "$_need" ]; then
    [ "$SETUP" = 1 ] || die "не хватает:$_need — запустите с --setup (поставится в $TOOLS) или задайте ANDROID_HOME / ANDROID_NDK_HOME"
    setup_sdk $_need || die "не удалось поставить:$_need"
    find_android_parts
    [ -n "$BT" ] && [ -n "$AJAR" ] && [ -n "$NDK_DIR" ] || die "после установки всё ещё нет build-tools 30.0.3 / android-29 / NDK — см. вывод выше"
fi
case $NDK_DIR in *" "*) die "путь к NDK с пробелами не поддерживается: $NDK_DIR" ;; esac
ok "JDK: $JH"; ok "build-tools: $BT"; ok "android.jar: $AJAR"; ok "NDK: $NDK_DIR"

# v15: Vulkan-рендерер (hle_d3d9_vk.h) компилирует шейдеры через libme1spv.so (glslang 15, dlopen)
GLS=
if [ "$NOSPV" = 0 ]; then
    find_glslang
    if [ -z "$GLS" ] && [ "$SETUP" = 1 ]; then
        find_cmake || { setup_sdk "cmake;3.22.1" && find_cmake; } || die "нет cmake 3.17+ для glslang (sdkmanager \"cmake;3.22.1\") — или --no-spv"
        setup_glslang; find_glslang
    fi
    case ${GLS:-} in *" "*) warn "путь к glslang с пробелами — Makefile его не примет; libme1spv.so пропускается"; GLS= ;; esac
    if [ -n "$GLS" ]; then ok "glslang: $GLS"
    else warn "нет glslang под Android — libme1spv.so (Vulkan-рендерер) не соберётся, останется OpenGL ES. --setup соберёт glslang"; fi
fi

if [ "$CLEAN" = 1 ]; then
    say "Очистка"
    rm -rf build/android/jp build/android/libme1game.so build/android/libme1run.so android_jp
fi
mkdir -p build/logs

say "Файлы игры"
find_inputs
say "Seeds (адреса функций для рекомпиляции)"
make_seeds
say "Рекомпиляция x86 → C"
recompile_all

say "Сборка libme1game.so (NDK, arm64)"
_n=$(( $(ls out/jp100/*.c out/fmodex/*.c out/bink/*.c 2>/dev/null | wc -l) + 25 ))
set -- -f jp100.mk -j"$JOBS" NDK="$NDK_DIR"
[ -n "$OPT" ] && set -- "$@" JPOPT="$OPT"
run_make build/logs/jp-make-android.log "компиляция" "$_n" "$@" jp-lib \
    || make_fail build/logs/jp-make-android.log "сборка libme1game.so не удалась"

if [ "$NOSPV" = 0 ] && [ -n "$GLS" ]; then
    say "libme1spv.so (GLSL → SPIR-V для Vulkan-рендерера)"
    make -f jp100.mk -j"$JOBS" NDK="$NDK_DIR" GLSLANG_SRC="$GLS" build/android/libme1spv.so > build/logs/jp-make-spv.log 2>&1 &
    wait_job $! "libme1spv.so" || { tail -n 20 build/logs/jp-make-spv.log >&2; die "libme1spv.so не собралась (build/logs/jp-make-spv.log); --no-spv — собрать без Vulkan-рендерера"; }
    ok "build/android/libme1spv.so"
fi
if [ "$GPU" = 1 ]; then
    say "GPU-драйверы: Mesa Zink + libadrenotools (android/gpu/build_gpu.sh, долго)"
    _m=; for t in meson ninja cmake bison flex git pkg-config; do have $t || _m="$_m $t"; done
    "${PY:-python3}" -c 'import mako, yaml, packaging' 2>/dev/null || python3 -c 'import mako, yaml, packaging' 2>/dev/null || _m="$_m python3-mako/pyyaml/packaging"
    [ -z "$_m" ] || die "для --gpu не хватает:$_m (например: sudo apt install meson ninja-build cmake bison flex git pkg-config python3-mako python3-yaml python3-packaging)"
    NDK=$NDK_DIR W=$TOOLS/build3p sh android/gpu/build_gpu.sh > build/logs/gpu.log 2>&1 &
    wait_job $! "Mesa + adrenotools" || { tail -n 20 build/logs/gpu.log >&2; die "build_gpu.sh не удался (лог build/logs/gpu.log)"; }
    ok "build/android/gpu/"
fi

say "Сборка APK"
JP_ROOT=$ROOT NDK=$NDK_DIR ANDROID_SDK=${SDK:-} ANDROID_BT=$BT ANDROID_JAR=$AJAR JAVA_HOME=$JH KEYSTORE=$KEYSTORE_ARG \
    PATH="$JH/bin:$PATH" bash jp_launcher/build_apk_jp.sh > build/logs/jp-apk.log 2>&1 &
wait_job $! "javac, d8, aapt2, подпись" && [ -f android_jp/out/JP100-port.apk ] \
    || { tail -n 20 build/logs/jp-apk.log >&2; die "сборка APK не удалась (build/logs/jp-apk.log)"; }
APK=$ROOT/android_jp/out/JP100-port.apk

if [ "$ADBINST" = 1 ]; then
    say "Установка на телефон"
    ADB=$(command -v adb || echo "${SDK:-}/platform-tools/adb")
    "$ADB" install -r "$APK" || die "adb install не удался (телефон подключён? отладка по USB включена?)"
    ok "установлено"
fi

say "Готово"
info "APK: $APK ($(du -h "$APK" | cut -f1))"
info "На телефоне: папка игры целиком в /sdcard/JPTelltale (JurassicPark100.exe, *.ttarch и остальное)"
info "exe на телефоне должен быть тем же файлом (sha256 $(sha "$EXE")…)"
info "Упало с [rt] MISSING-FUNC xxxxxxxx — адрес в seeds/jp100-extra.txt и снова этот скрипт (seeds/README.md)"
info "Логи сборки: build/logs, build/recomp, build/seeds"
