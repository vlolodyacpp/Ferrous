# Ferrous

Компилятор собственного языка программирования → LLVM IR → нативный исполняемый файл (x86-64).

## Сборка

```bash
cmake --preset clang
cmake --build build
```

## Зависимости

- clang++ ≥ 17, CMake ≥ 3.30, Ninja
- **LLVM 17+** (`pacman -S llvm` / `apt install llvm-dev`)


## Запуск

```bash
./build/Ferrous examples/hello.fer                    # компиляция
./a.out                                               # запуск
./build/Ferrous examples/hello.fer -o hi             # свой выходной файл
./build/Ferrous examples/hello.fer --dump-sema  # AST с типами
```

## Pipeline

```
.fer → Lexer → Parser → Semantic → Codegen → LLVM IR → clang++ → executable
```


