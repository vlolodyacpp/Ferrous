#!/usr/bin/env python3

import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXAMPLES = ROOT / "examples"

# цвета (отключаются, если вывод не в терминал)
if sys.stdout.isatty():
    GREEN, RED, GREY, RESET = "\033[32m", "\033[31m", "\033[90m", "\033[0m"
else:
    GREEN = RED = GREY = RESET = ""

passed = 0
failed = 0


def ok(name: str) -> None:
    global passed
    passed += 1
    print(f"  {GREEN}ok{RESET}   {name}")


def bad(name: str, reason: str) -> None:
    global failed
    failed += 1
    print(f"  {RED}FAIL{RESET} {name} — {reason}")


def show_error(text: str) -> None:
    """Печать текста ошибки err-файла с отступом."""
    text = text.rstrip("\n")
    if not text:
        text = "(пустой вывод)"
    for line in text.splitlines():
        print(f"       {GREY}{line}{RESET}")


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def error_marker(path: Path) -> str | None:
    """Маркер `// @error: <подстрока>` из первой строки файла."""
    with path.open(encoding="utf-8", errors="replace") as fh:
        for line in fh:
            stripped = line.strip()
            if stripped.startswith("// @error:"):
                return stripped[len("// @error:"):].strip()
            break
    return None


def main() -> int:
    fer = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build" / "Ferrous"
    if not (fer.is_file() and os.access(fer, os.X_OK)):
        print(f"компилятор не найден: {fer} "
              f"(собери проект или укажи путь аргументом)", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp:
        prog = str(Path(tmp) / "prog")

        # === позитивные ===
        print("== позитивные (компиляция + запуск, ожидаем код 0) ==")
        for f in sorted(EXAMPLES.glob("test_*.fer")):
            name = f.name
            comp = run([str(fer), str(f), "-o", prog])
            if comp.returncode != 0:
                head = (comp.stdout + comp.stderr).splitlines()
                bad(name, f"ошибка компиляции: {head[0] if head else ''}")
                continue
            execed = run([prog])
            if execed.returncode != 0:
                bad(name, f"ненулевой код выполнения ({execed.returncode})")
                continue
            expected = f.with_suffix(".expected")
            if expected.is_file():
                want = expected.read_text(encoding="utf-8", errors="replace")
                if execed.stdout != want:
                    bad(name, "вывод не совпал с эталоном")
                    continue
            ok(name)

        # === ошибки компиляции ===
        print("== ошибки компиляции (ожидаем код != 0 + совпадение маркера @error) ==")
        compile_err = sorted(EXAMPLES.glob("err_sema_*.fer")) \
            + sorted(EXAMPLES.glob("err_lex_*.fer"))
        for f in compile_err:
            name = f.name
            want = error_marker(f)
            comp = run([str(fer), str(f), "-o", prog])
            out = comp.stdout + comp.stderr
            if comp.returncode == 0:
                bad(name, "компилятор НЕ отверг программу (код 0)")
                continue
            if want and want not in out:
                head = out.splitlines()
                bad(name, f"нет ожидаемой диагностики «{want}»; "
                          f"получено: {head[0] if head else ''}")
                show_error(out)
                continue
            ok(name)
            show_error(out)

        # === рантайм-ошибки ===
        print("== рантайм-ошибки (компиляция ок, запуск даёт код != 0) ==")
        runtime_err = [f for f in sorted(EXAMPLES.glob("err_*.fer"))
                       if not f.name.startswith(("err_sema_", "err_lex_"))]
        for f in runtime_err:
            name = f.name
            comp = run([str(fer), str(f), "-o", prog])
            if comp.returncode != 0:
                head = (comp.stdout + comp.stderr).splitlines()
                bad(name, f"не должна падать на компиляции: {head[0] if head else ''}")
                continue
            execed = run([prog])
            if execed.returncode != 0:
                ok(name)
                show_error(execed.stdout + execed.stderr)
            else:
                bad(name, "ожидался ненулевой код выполнения")

    print()
    print(f"итого: пройдено {passed}, провалено {failed}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
