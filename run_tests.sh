#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
FER="${1:-$ROOT/build/Ferrous}"
EX="$ROOT/examples"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$FER" ]; then
    echo "компилятор не найден: $FER (собери проект или укажи путь аргументом)" >&2
    exit 2
fi

pass=0; fail=0
ok()   { pass=$((pass+1)); printf '  \033[32mok\033[0m   %s\n' "$1"; }
bad()  { fail=$((fail+1)); printf '  \033[31mFAIL\033[0m %s — %s\n' "$1" "$2"; }

echo "== позитивные (компиляция + запуск, ожидаем код 0) =="
for f in "$EX"/test_*.fer; do
    name=$(basename "$f")
    if ! "$FER" "$f" -o "$TMP/prog" >"$TMP/out" 2>&1; then
        bad "$name" "ошибка компиляции: $(head -1 "$TMP/out")"; continue
    fi
    if "$TMP/prog" >/dev/null 2>&1; then ok "$name"; else
        bad "$name" "ненулевой код выполнения ($?)"; fi
done

echo "== ошибки компиляции (ожидаем код 1 + совпадение маркера @error) =="
for f in "$EX"/err_sema_*.fer; do
    name=$(basename "$f")
    want=$(sed -n 's|^// @error:[[:space:]]*||p' "$f" | head -1)
    "$FER" "$f" -o "$TMP/prog" >"$TMP/out" 2>&1
    rc=$?
    if [ $rc -eq 0 ]; then
        bad "$name" "компилятор НЕ отверг программу (код 0)"; continue
    fi
    if [ -n "$want" ] && ! grep -qF -- "$want" "$TMP/out"; then
        bad "$name" "нет ожидаемой диагностики «$want»; получено: $(head -1 "$TMP/out")"; continue
    fi
    ok "$name"
done

echo "== рантайм-ошибки (компиляция ок, запуск даёт код 1) =="
for f in "$EX"/err_*.fer; do
    case "$(basename "$f")" in err_sema_*) continue;; esac
    name=$(basename "$f")
    if ! "$FER" "$f" -o "$TMP/prog" >"$TMP/out" 2>&1; then
        bad "$name" "не должна падать на компиляции: $(head -1 "$TMP/out")"; continue
    fi
    "$TMP/prog" >/dev/null 2>&1
    if [ $? -ne 0 ]; then ok "$name"; else
        bad "$name" "ожидался ненулевой код выполнения"; fi
done

echo
echo "итого: пройдено $pass, провалено $fail"
[ $fail -eq 0 ]
