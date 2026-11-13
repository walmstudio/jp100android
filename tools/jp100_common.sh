# tools/jp100_common.sh — общие функции build-android-jp100.sh и build-linux-jp100.sh (подключается через ".").
# Вызывающий скрипт задаёт ROOT, GAME, EXE, FMOD, BINK, FORCE, NODATA, VERBOSE; PY находит find_python.
# Вывод и поведение — как у build-android.sh / build-linux.sh из me1port.

if [ -t 1 ]; then BO=$(printf '\033[1m'); GR=$(printf '\033[32m'); YE=$(printf '\033[33m'); RE=$(printf '\033[31m'); NO=$(printf '\033[0m')
else BO= GR= YE= RE= NO=; fi
say()  { printf '\n%s==> %s%s\n' "$BO" "$*" "$NO"; }
ok()   { printf '  %s✓%s %s\n' "$GR" "$NO" "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '  %s!%s %s\n' "$YE" "$NO" "$*" >&2; }
die()  { printf '\n%sОшибка:%s %s\n' "$RE" "$NO" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
uniq_lines() { awk 'NF && !seen[$0]++'; }
kill_tree() { for _c in $(pgrep -P "$1" 2>/dev/null); do kill_tree "$_c"; done; kill -TERM "$1" 2>/dev/null; }
on_int() { trap - INT TERM; for _j in $(jobs -p); do kill_tree "$_j"; done; rm -rf "$ROOT"/out/.new-* 2>/dev/null; printf '\nпрервано\n' >&2; exit 130; }
trap on_int INT TERM

ncpu() { nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2; }
mem_gb() { local k; k=$(awk '/^MemTotal:/ { print $2 }' /proc/meminfo 2>/dev/null); if [ -n "$k" ]; then echo $(( k / 1048576 )); else echo 4; fi; }
default_jobs() { local c j; c=$(ncpu); j=$(( $(mem_gb) * 2 / 3 )); [ "$j" -gt "$c" ] && j=$c; [ "$j" -lt 1 ] && j=1; echo "$j"; }
sha() { cat "$@" | { if have sha256sum; then sha256sum; else shasum -a 256; fi; } | cut -c1-16; }
abspath() {
    if [ -d "$1" ]; then (cd "$1" && pwd -P)
    elif [ -e "$1" ]; then printf '%s/%s\n' "$(cd "$(dirname "$1")" && pwd -P)" "$(basename "$1")"
    else case $1 in /*) printf '%s\n' "$1" ;; *) printf '%s/%s\n' "$(pwd -P)" "$1" ;; esac
    fi
}

# исходные файлы из архива (если есть): ../inputs
JP_INPUTS=$(cd "$ROOT/.." && pwd -P)/inputs

wait_job() {   # PID ПОДПИСЬ — дождаться фоновой задачи, показывая время
    local p=$1 lab=$2 t0 e rc
    t0=$(date +%s)
    if [ -t 1 ] && [ "$VERBOSE" = 0 ]; then
        while kill -0 "$p" 2>/dev/null; do
            e=$(( $(date +%s) - t0 )); printf '\r    %s… %d:%02d ' "$lab" $((e / 60)) $((e % 60)); sleep 1
        done
        printf '\r\033[K'
    fi
    wait "$p"; rc=$?
    e=$(( $(date +%s) - t0 ))
    [ "$rc" = 0 ] && ok "$lab ($((e / 60)) мин $((e % 60)) с)"
    return "$rc"
}

run_make() {   # ЛОГ ПОДПИСЬ ВСЕГО аргументы make… — make в фоне со счётчиком строк "  CC файл"
    local log=$1 lab=$2 tot=$3 mp t0 n e rc; shift 3
    if [ "$VERBOSE" = 1 ]; then make "$@" 2>&1 | tee "$log"; return "${PIPESTATUS[0]}"; fi
    : > "$log"; t0=$(date +%s)
    make "$@" > "$log" 2>&1 & mp=$!
    while kill -0 "$mp" 2>/dev/null; do
        if [ -t 1 ]; then
            n=$(grep -c -E '^  (CC|AS) ' "$log" 2>/dev/null); e=$(( $(date +%s) - t0 ))
            printf '\r    %s: %s из ~%s, %d:%02d ' "$lab" "${n:-0}" "$tot" $((e / 60)) $((e % 60))
        fi
        sleep 2
    done
    if [ -t 1 ]; then printf '\r\033[K'; fi
    wait "$mp"; rc=$?
    e=$(( $(date +%s) - t0 ))
    [ "$rc" = 0 ] && ok "$lab ($((e / 60)) мин $((e % 60)) с)"
    return "$rc"
}

make_fail() {   # ЛОГ СООБЩЕНИЕ
    printf '    --- ошибки (%s) ---\n' "$1" >&2
    grep -n -E 'error|undefined reference|No rule|\*\*\*' "$1" | head -n 20 >&2
    printf '    --- последние строки ---\n' >&2
    tail -n 15 "$1" >&2
    die "$2 (полный лог: $1)"
}

find_python() {   # PY = python3 >= 3.8 с capstone; при необходимости — своё окружение с pip install capstone
    local venv=${XDG_CACHE_HOME:-$HOME/.cache}/me1port/pyenv p sys
    for p in ${ME1_PYTHON:-} python3 /usr/bin/python3 /usr/bin/python3.[0-9]* /usr/local/bin/python3 "$venv/bin/python3"; do
        [ -n "$p" ] || continue
        have "$p" || [ -x "$p" ] || continue
        if "$p" -c 'import sys, capstone; sys.exit(sys.version_info < (3, 8))' 2>/dev/null; then PY=$p; return 0; fi
    done
    sys=$(command -v python3 2>/dev/null) || return 1
    info "capstone нет — ставлю в отдельное окружение $venv (pip install capstone)"
    if [ ! -x "$venv/bin/python3" ]; then
        rm -rf "$venv"; mkdir -p "$(dirname "$venv")"
        "$sys" -m venv "$venv" >/dev/null 2>&1 || { rm -rf "$venv"; return 1; }
    fi
    "$venv/bin/python3" -m pip install -q capstone >/dev/null 2>&1 || { rm -rf "$venv"; return 1; }
    "$venv/bin/python3" -c 'import capstone' 2>/dev/null || return 1
    PY=$venv/bin/python3
}

ci_find() {   # ПАПКА ИМЯ [ГЛУБИНА=3] — первый файл с таким именем без учёта регистра (или ничего)
    [ -n "$1" ] && [ -d "$1" ] || return 0
    find "$1" -maxdepth "${3:-3}" -type f -iname "$2" 2>/dev/null | head -n 1
}

pick_dll() {   # ПЕРЕМЕННАЯ ИМЯ.dll — явный путь или поиск рядом с exe / в папке игры / в ../inputs, с проверкой экспортов
    local var=$1 name=$2 cur c out
    eval "cur=\${$var:-}"
    if [ -n "$cur" ]; then
        [ -f "$cur" ] || die "нет файла $cur"
        out=$("$PY" tools/jp100_check_dll.py "$EXE" "$cur" "$name" 2>&1) || die "$cur не подходит к игре: $out"
        info "$name: $cur"
        return 0
    fi
    while IFS= read -r c; do
        [ -n "$c" ] || continue
        if out=$("$PY" tools/jp100_check_dll.py "$EXE" "$c" "$name" 2>&1); then
            eval "$var=\$c"; info "$name: $c"; return 0
        fi
        warn "$c не подходит ($out) — ищу дальше"
    done <<EOF
$( { ci_find "$(dirname "$EXE")" "$name" 1; ci_find "$GAME" "$name" 4; ci_find "$JP_INPUTS" "$name" 4; } | uniq_lines)
EOF
    return 1
}

find_inputs() {   # EXE, GAME, FMOD, BINK
    local d s
    if [ -z "$EXE" ]; then
        for d in "$GAME" "${JP_GAME:-}" "$HOME/JPTelltale" "$JP_INPUTS"; do
            EXE=$(ci_find "$d" JurassicPark100.exe 4); [ -n "$EXE" ] && break
        done
    fi
    [ -n "$EXE" ] && [ -f "$EXE" ] || die "не найден JurassicPark100.exe — укажите папку игры: --game ПАПКА (или --exe ФАЙЛ)"
    EXE=$(abspath "$EXE")
    [ -n "$GAME" ] || GAME=$(dirname "$EXE")
    s=$(sha "$EXE")
    info "exe: $EXE (sha256 $s…)"
    [ "$s" = e686a26571e093d2 ] || warn "это другая сборка JurassicPark100.exe (рантайм писался под sha256 e686a26571e093d2…) — возможны падения, пришлите логи"
    pick_dll FMOD fmodex.dll || die "не найден подходящий fmodex.dll — укажите --fmodex ФАЙЛ"
    pick_dll BINK binkw32.dll || die "не найден binkw32.dll — укажите --bink ФАЙЛ"
}

make_seeds() {   # SEEDS = build/seeds/jp100-<sha>.txt: RTTI/vtable + таблицы CRT + указатели из данных + seeds/
    local s rt
    mkdir -p build/seeds
    s=$(sha "$EXE"); rt=build/seeds/jp100-$s-rtti.txt
    if [ ! -s "$rt" ] || [ "$FORCE" = 1 ]; then
        info "RTTI/vtable (tools/jp100_seeds.py)…"
        "$PY" tools/jp100_seeds.py "$EXE" "$rt" > "build/seeds/jp100-$s-rtti.log" 2>&1 \
            || { tail -n 15 "build/seeds/jp100-$s-rtti.log" >&2; die "tools/jp100_seeds.py не отработал (build/seeds/jp100-$s-rtti.log)"; }
    fi
    SEEDS=build/seeds/jp100-$s.txt
    set -- --merge "$rt" --merge seeds --expect 00efcf7c
    [ "$NODATA" = 1 ] && set -- "$@" --no-data
    "$PY" tools/jp100_crt_seeds.py "$EXE" "$SEEDS" "$@" > "build/seeds/jp100-$s.log" 2>&1 \
        || { tail -n 15 "build/seeds/jp100-$s.log" >&2; die "tools/jp100_crt_seeds.py не отработал (build/seeds/jp100-$s.log)"; }
    grep -v '^\[seeds\]   ' "build/seeds/jp100-$s.log" | sed 's/^\[seeds\] /    /'
}

recomp_job() {   # МОДУЛЬ ИСТОЧНИК КЛЮЧ аргументы recomp.py… (лог: build/recomp/МОДУЛЬ.log)
    local m=$1 src=$2 key=$3; shift 3
    rm -rf "out/.new-$m"; mkdir -p build/recomp out
    "$PY" tools/recomp.py "$src" "out/.new-$m" "$@" > "build/recomp/$m.log" 2>&1 || return 1
    [ -f "out/.new-$m/image.bin" ] || { echo "нет out/.new-$m/image.bin" >> "build/recomp/$m.log"; return 1; }
    rm -rf "out/$m" && mv "out/.new-$m" "out/$m" && printf '%s\n' "$key" > "out/$m/.build-key"
}

plan_mod() {   # МОДУЛЬ ИСТОЧНИК аргументы… — в TODO, если out/МОДУЛЬ устарел
    local m=$1 src=$2 key; shift 2
    if [ -z "$src" ]; then [ -f "out/$m/image.bin" ] && return 0; die "нет исходника для $m и нет готового out/$m"; fi
    key="$(sha "$src");$*;$TH"
    [ "$m" = jp100 ] && key="$key;$(sha "$SEEDS")"
    if [ "$FORCE" = 0 ] && [ -f "out/$m/image.bin" ] && [ "$(cat "out/$m/.build-key" 2>/dev/null)" = "$key" ]; then
        ok "$m: перекомпиляция не нужна"; return 0
    fi
    TODO="$TODO$m|$src|$key|$*
"
}

recompile_all() {   # out/fmodex, out/bink, out/jp100 (jp100 — в фоне, если хватает памяти и ядер)
    local jp rest m src key args jpid= p
    TH=$(sha tools/recomp.py tools/fpsse.py tools/ehscan.py tools/pefile_min.py)
    TODO=
    plan_mod fmodex "$FMOD" --prefix fmod
    plan_mod bink "$BINK" --prefix bk --base 10400000
    plan_mod jp100 "$EXE" --prefix jp --seeds "$SEEDS" --unrelocated-immediates
    [ -n "$TODO" ] || return 0
    jp=$(printf '%s' "$TODO" | grep '^jp100|')
    rest=$(printf '%s' "$TODO" | grep -v '^jp100|')
    if [ -n "$jp" ] && [ "$(mem_gb)" -ge 6 ] && [ "$(ncpu)" -ge 2 ]; then
        IFS='|' read -r m src key args <<EOF
$jp
EOF
        info "jp100: рекомпиляция exe в фоне — в первый раз это долго, лог build/recomp/jp100.log"
        recomp_job jp100 "$src" "$key" $args & jpid=$!
        jp=
    fi
    while IFS='|' read -r m src key args; do
        [ -n "$m" ] || continue
        recomp_job "$m" "$src" "$key" $args & p=$!
        wait_job "$p" "рекомпиляция $m" || {
            [ -n "$jpid" ] && kill_tree "$jpid"
            tail -n 15 "build/recomp/$m.log" >&2; die "рекомпиляция $m не удалась (build/recomp/$m.log)"; }
    done <<EOF
$rest
EOF
    if [ -n "$jp" ]; then
        IFS='|' read -r m src key args <<EOF
$jp
EOF
        info "jp100: рекомпиляция exe — в первый раз это долго, лог build/recomp/jp100.log"
        recomp_job jp100 "$src" "$key" $args & jpid=$!
    fi
    if [ -n "$jpid" ]; then
        wait_job "$jpid" "рекомпиляция jp100" || { tail -n 15 build/recomp/jp100.log >&2; die "рекомпиляция exe не удалась (build/recomp/jp100.log)"; }
    fi
    rm -rf out/.new-*
    for m in fmodex bink jp100; do
        [ -f "build/recomp/$m.log" ] || continue
        local sl; sl=$(grep -E 'functions:' "build/recomp/$m.log" | tail -n 1)   # v5: summary line, not the log tail
        [ -n "$sl" ] || sl=$(tail -n 2 "build/recomp/$m.log" | tr '\n' ' ')
        info "$m: $sl"
    done
}
