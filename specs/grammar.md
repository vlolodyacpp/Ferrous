# Грамматика языка Ferrous (EBNF)

Документ описывает лексическую и синтаксическую грамматику языка Ferrous в форме EBNF. Является нормативной частью спецификации: парсер обязан принимать в точности то множество программ, которое порождает эта грамматика.

---

## Нотация

| Запись     | Значение                                |
|------------|-----------------------------------------|
| `=`        | определение правила                     |
| `\|`        | альтернатива                            |
| `[ X ]`    | необязательно (0 или 1)                 |
| `{ X }`    | повторение (0 или больше)               |
| `( ... )`  | группировка                             |
| `"x"`      | терминал (литеральный токен)            |
| `IDENT`    | токен из лексера (UPPER_CASE)           |
| `(* ... *)`| комментарий внутри грамматики           |
| `? ... ?`  | специальная последовательность (прозой) |

Терминалы в кавычках — это последовательности символов, распознаваемые лексером и попадающие в поток токенов как соответствующие `TokenKind`. Имена в UPPER_CASE (`IDENT`, `INT_LIT`, `FLOAT_LIT`, `STRING_LIT`, `BOOL_LIT`, `EOF`) — токены, чья форма определена в §1 «Лексика».

---

## Принятые решения

Перечисленные ниже развилки явно зафиксированы в грамматике. Изменение любого пункта потребует правки соответствующей продукции.

| #  | Развилка                                         | Принято                                                  |
|----|--------------------------------------------------|----------------------------------------------------------|
| D1 | `;` в конце инструкции                           | Обязателен для `let`, `return`, `break`, `continue`, `ExprStmt` |
| D2 | Trailing comma в списках                         | Разрешена (params, fields, args, array/struct literals)  |
| D3 | Struct literal в условии `if`/`while`            | Запрещён (грамматически — через `ExprNoStruct`)          |
| D4 | `else if`                                        | Запись через `else IfStmt`, отдельной конструкции нет    |
| D5 | Скобки вокруг условия `if`/`while`               | Отсутствуют (Rust-style)                                 |

---

## §1. Лексика

```ebnf
(* --- Базовые символы --- *)
letter        = "A" | "B" | "C" | "D" | "E" | "F" | "G" | "H" | "I" | "J"
              | "K" | "L" | "M" | "N" | "O" | "P" | "Q" | "R" | "S" | "T"
              | "U" | "V" | "W" | "X" | "Y" | "Z"
              | "a" | "b" | "c" | "d" | "e" | "f" | "g" | "h" | "i" | "j"
              | "k" | "l" | "m" | "n" | "o" | "p" | "q" | "r" | "s" | "t"
              | "u" | "v" | "w" | "x" | "y" | "z"
              | "_" ;

digit         = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;

(* --- Идентификаторы --- *)
IDENT         = letter { letter | digit } ;

(* --- Числовые литералы --- *)
int_suffix    = "i8" | "i16" | "i32" | "i64"
              | "u8" | "u16" | "u32" | "u64" ;
float_suffix  = "f32" | "f64" ;

hex_digit     = digit | "A" | "B" | "C" | "D" | "E" | "F"
                      | "a" | "b" | "c" | "d" | "e" | "f" ;
bin_digit     = "0" | "1" ;

INT_LIT       = dec_int | hex_int | bin_int ;
dec_int       = digit { digit } [ int_suffix ] ;
hex_int       = "0" ( "x" | "X" ) hex_digit { hex_digit } [ int_suffix ] ;
bin_int       = "0" ( "b" | "B" ) bin_digit { bin_digit } [ int_suffix ] ;

exp_part      = ( "e" | "E" ) [ "+" | "-" ] digit { digit } ;
FLOAT_LIT     = digit { digit } "." digit { digit } [ exp_part ] [ float_suffix ]
              | digit { digit } exp_part [ float_suffix ]
              | "nan" [ float_suffix ]
              | "inf" [ float_suffix ] ;

(* --- Булевы литералы --- *)
BOOL_LIT      = "true" | "false" ;

(* --- Строковые литералы --- *)
escape_seq    = "\n" | "\t" | "\r" | "\\" | "\'" | "\"" | "\0" ;
str_char      = ? любой символ, кроме '"' и '\' ? | escape_seq ;
STRING_LIT    = '"' { str_char } '"' ;

char_inner    = ? любой символ, кроме ''' и '\' ? | escape_seq ;
CHAR_LIT      = "'" char_inner "'" ;

(* --- Пробельные символы и комментарии (выбрасываются лексером,
       в поток токенов не попадают) --- *)
whitespace    = " " | "\t" | "\n" | "\r" ;
line_comment  = "//" { ? любой символ до конца строки ? } ;
block_comment = "/*" { ? любой символ ? } "*/" ;
```

---

## §2. Программа

```ebnf
Program = { Decl } EOF ;
```

На верхнем уровне модуля допускаются **только** декларации (см. `semantics.md` §Общее описание). Любая инструкция или выражение вне тела функции — синтаксическая ошибка.

---

## §3. Декларации

```ebnf
Decl = FnDecl
     | StructDecl
     | TypeAliasDecl
     | NamespaceDecl ;

(* --- Функции --- *)
FnDecl        = "fn" IDENT "(" [ ParamList ] ")" [ "->" Type ] Block ;
ParamList     = Param { "," Param } [ "," ] ;
Param         = IDENT ":" Type [ "=" Expr ] ;   (* "= Expr" — значение по умолчанию, A.2.9 *)

(* --- Структуры --- *)
StructDecl    = "struct" IDENT "{" [ FieldList ] "}" ;
FieldList     = Field { "," Field } [ "," ] ;
Field         = IDENT ":" Type ;

(* --- Синонимы типов --- *)
TypeAliasDecl = "type" IDENT "=" Type ";" ;

(* --- Пространства имён --- *)
NamespaceDecl = "namespace" IDENT "{" { Decl } "}" ;
```

**Замечания.**
- Если у функции возвращаемый тип опущен (`-> Type` отсутствует), он считается `void` (см. `semantics.md` §Функции).
- В `NamespaceDecl` тело — снова `{ Decl }`, что даёт произвольную вложенность пространств имён.

---

## §4. Типы

```ebnf
Type        = BuiltinType
            | ArrayType
            | NamedType ;

BuiltinType = "int8"  | "int16"  | "int32"  | "int64"
            | "uint8" | "uint16" | "uint32" | "uint64"
            | "float32" | "float64"
            | "bool"  | "string" | "char"  | "void" ;

ArrayType   = "[" Type ";" INT_LIT "]" ;
NamedType   = IDENT ;
```

---

## §5. Инструкции

```ebnf
Stmt = LetStmt
     | IfStmt
     | WhileStmt
     | ReturnStmt
     | BreakStmt
     | ContinueStmt
     | Block
     | ExprStmt
     | NullStmt ;

LetStmt      = "let" [ "mut" ] IDENT [ ":" Type ] "=" Expr ";" ;
IfStmt       = "if"    ExprNoStruct Block [ "else" ( IfStmt | Block ) ] ;
WhileStmt    = "while" ExprNoStruct Block ;
ReturnStmt   = "return" [ Expr ] ";" ;
BreakStmt    = "break" ";" ;
ContinueStmt = "continue" ";" ;
Block        = "{" { Stmt } "}" ;
ExprStmt     = Expr ";" ;
NullStmt     = ";" ;
```


---

## §6. Выражения

Выражения описаны **слоями приоритета** снизу вверх: каждый следующий слой связывает крепче предыдущего. Левая ассоциативность выражается повторением `{ ... }`, правая — рекурсией в правом операнде. Эта структура эквивалентна Pratt-разбору; разница только в способе записи.

```ebnf
Expr        = AssignExpr ;

(* --- Слой 1: присваивание (правая ассоциативность) --- *)
AssignExpr  = OrExpr [ AssignOp AssignExpr ] ;

(* простое и составные присваивания (A.1.9) — один приоритетный слой *)
AssignOp    = "=" | "+=" | "-=" | "*=" | "/=" | "%="
            | "&=" | "|=" | "^=" | "<<=" | ">>=" ;

(* --- Слой 2: логическое ИЛИ --- *)
OrExpr      = AndExpr { "||" AndExpr } ;

(* --- Слой 3: логическое И --- *)
AndExpr     = BitOrExpr { "&&" BitOrExpr } ;

(* --- Слой 4: побитовое ИЛИ --- *)
BitOrExpr   = BitXorExpr { "|" BitXorExpr } ;

(* --- Слой 5: побитовое исключающее ИЛИ --- *)
BitXorExpr  = BitAndExpr { "^" BitAndExpr } ;

(* --- Слой 6: побитовое И --- *)
BitAndExpr  = EqExpr { "&" EqExpr } ;

(* --- Слой 7: равенство --- *)
EqExpr      = RelExpr { ( "==" | "!=" ) RelExpr } ;

(* --- Слой 8: сравнения --- *)
RelExpr     = ShiftExpr { ( "<" | "<=" | ">" | ">=" ) ShiftExpr } ;

(* --- Слой 9: сдвиги --- *)
ShiftExpr   = AddExpr { ( "<<" | ">>" ) AddExpr } ;

(* --- Слой 10: аддитивные --- *)
AddExpr     = MulExpr { ( "+" | "-" ) MulExpr } ;

(* --- Слой 11: мультипликативные --- *)
MulExpr     = UnaryExpr { ( "*" | "/" | "%" ) UnaryExpr } ;

(* --- Слой 12: префиксные унарные --- *)
UnaryExpr   = ( "-" | "!" | "~" ) UnaryExpr
            | PostfixExpr ;

(* --- Слой 13: постфиксные (call, index, field, path, as) --- *)
PostfixExpr = PrimaryExpr { PostfixOp } ;
PostfixOp   = "(" [ ArgList ] ")"        (* вызов функции    *)
            | "[" Expr "]"               (* индексирование   *)
            | "." IDENT                  (* доступ к полю    *)
            | "::" IDENT                 (* сегмент пути     *)
            | "as" Type ;                (* приведение типа  *)

ArgList     = Arg { "," Arg } [ "," ] ;
Arg         = [ IDENT "=" ] Expr ;   (* "IDENT =" — именованный аргумент, A.2.9 *)

(* --- Слой 11: первичные выражения --- *)
PrimaryExpr = INT_LIT
            | FLOAT_LIT
            | BOOL_LIT
            | STRING_LIT
            | CHAR_LIT
            | ArrayLit
            | StructLit
            | IDENT
            | "(" Expr ")" ;

(* --- Литералы составных типов --- *)
ArrayLit        = "[" [ Expr { "," Expr } [ "," ] ] "]" ;
StructLit       = IDENT "{" [ StructFieldInit { "," StructFieldInit } [ "," ] ] "}" ;
StructFieldInit = IDENT ":" Expr ;
```

### 6.1. Вариант без struct literal

В позиции условия `if`/`while` `PrimaryExpr` не должен начинаться с `IDENT "{" ...`, иначе синтаксис двусмыслен (struct literal конфликтует начало блока). Грамматически вводится параллельное правило `ExprNoStruct`, отличающееся ровно одной альтернативой в `PrimaryExpr`:

```ebnf
ExprNoStruct        = AssignExprNoStruct ;

(* Все слои AssignExprNoStruct, OrExprNoStruct, …, PostfixExprNoStruct
   получаются механической заменой PrimaryExpr на PrimaryExprNoStruct. *)

PrimaryExprNoStruct = INT_LIT
                    | FLOAT_LIT
                    | BOOL_LIT
                    | STRING_LIT
                    | CHAR_LIT
                    | ArrayLit
                    | IDENT
                    | "(" Expr ")" ;
```



### 6.2. Приоритет и ассоциативность (сводка)

| Приоритет | Конструкция                       | Ассоциативность |
|-----------|-----------------------------------|-----------------|
| 13 (выше) | литералы, `IDENT`, `(...)`        | —               |
| 12        | `()`, `[]`, `.`, `::`, `as`       | левая           |
| 11        | унарные `-`, `!`, `~`             | правая          |
| 10        | `*`, `/`, `%`                     | левая           |
| 9         | `+`, `-`                          | левая           |
| 8         | `<<`, `>>`                        | левая           |
| 7         | `<`, `<=`, `>`, `>=`              | левая           |
| 6         | `==`, `!=`                        | левая           |
| 5         | `&`                               | левая           |
| 4         | `^`                               | левая           |
| 3         | `\|`                               | левая           |
| 2         | `&&`                              | левая           |
| 1 (ниже)  | `\|\|`, затем `=` и составные `+= -= *= /= %= &= \|= ^= <<= >>=` | левая / правая |


### 6.3. Порядок вычисления (информативно)

- операнды бинарных операторов вычисляются слева направо;
- аргументы функций — слева направо;
- `&&` и `||` — short-circuit;
- `arr[i]` — сначала `arr`, затем `i`.

---

## §7. Особенности

1. **`IDENT "{"` — struct literal vs начало блока.** Решено через `ExprNoStruct` в условиях `if`/`while` (D3, §6.1). В реализации — флаг `in_condition`, проверяемый при попытке начать `StructLit` в `parse_prefix`.

2. **`as` — постфикс.** Принято: `as` входит в постфиксный слой (наряду с `()`/`[]`/`.`/`::`), поэтому связывает крепче унарного минуса: `-x as int32` = `-(x as int32)`, а `1 + 2 as int64` = `1 + (2 as int64)`. В реализации обрабатывается в `parse_postfix`.

3. **Унарный `-` vs бинарный `-`.** Грамматически развязаны: бинарный встречается только внутри `AddExpr`, унарный — только в `UnaryExpr`. Контекст (позиция после оператора vs позиция выражения) определяет, какое правило применяется.

4. **`else if` vs `else { ... }`.** Обе формы покрываются `[ "else" ( IfStmt | Block ) ]`. Парсер рекурсивно вызывает `parse_if`, образуя цепочку.

5. **Висячий `else`.** Невозможен: ветка `then` обязана быть `Block`, поэтому `else` всегда привязывается к ближайшему `if`.

6. **`PathExpr` (имя в namespace).** В грамматике `::` — постфиксная операция, что даёт линейную раскрутку `a::b::c`. На уровне AST полученную цепочку можно сворачивать в `PathExpr` с вектором сегментов либо хранить как левую раскрутку — решение принадлежит реализации парсера, а не грамматике.

7. **Пустой массив `[]`.** Грамматически разрешён. Семантика отсекает массивы нулевого размера (см. `types.md` §Массивы).

8. **Trailing comma.** Допустима во всех списках через `[ "," ]` после последнего элемента (D2). Не обязательна.

9. **Именованный аргумент vs присваивание-выражение.** 
Внутри списка аргументов вызова конструкция `IDENT "=" Expr` всегда трактуется как именованный аргумент, а не как присваивание-выражение. Парсер распознаёт её по lookahead `IDENT "="` на верхнем уровне аргумента. Если в этой позиции действительно нужно присваивание-выражение, его заключают в скобки: `f((x = 5))`.

---

## §8. Соответствие AST

Каждая EBNF-продукция отображается на узел AST (`mod/ast.cppm`):

| Продукция        | Узел AST                                  |
|------------------|-------------------------------------------|
| `FnDecl`         | `FnDecl`                                  |
| `StructDecl`     | `StructDecl`                              |
| `TypeAliasDecl`  | `TypeAliasDecl`                           |
| `NamespaceDecl`  | `NameSpaceDecl`                           |
| `BuiltinType`    | `BuiltinTypeRef`                          |
| `NamedType`      | `NamedTypeRef`                            |
| `ArrayType`      | `ArrayTypeRef`                            |
| `LetStmt`        | `LetStmt`                                 |
| `IfStmt`         | `IfStmt`                                  |
| `WhileStmt`      | `WhileStmt`                               |
| `ReturnStmt`     | `ReturnStmt`                              |
| `BreakStmt`      | `BreakStmt`                               |
| `ContinueStmt`   | `ContinueStmt`                            |
| `Block`          | `BlockStmt`                               |
| `ExprStmt`       | `ExprStmt`                                |
| `NullStmt`       | `NullStmt`                                |
| `AssignExpr`/бинарки | `BinaryExpr`                          |
| `CastExpr`       | `CastExpr`                                |
| `UnaryExpr`      | `UnaryExpr`                               |
| `PostfixOp` call | `CallExpr`                                |
| `PostfixOp` `[]` | `IndexExpr`                               |
| `PostfixOp` `.`  | `FieldExpr`                               |
| `PostfixOp` `::` | `PathExpr` (после свёртки цепочки)        |
| `ArrayLit`       | `ArrayLitExpr`                            |
| `StructLit`      | `StructLitExpr`                           |
| `INT_LIT`        | `LitIntExpr`                              |
| `FLOAT_LIT`      | `LitFloatExpr`                            |
| `BOOL_LIT`       | `LitBoolExpr`                             |
| `STRING_LIT`     | `LitStringExpr`                           |
| `CHAR_LIT`       | `LitCharExpr`                             |
| `IDENT` (выражение) | `IdentExpr`                            |
| `"(" Expr ")"`   | `GroupExpr`                               |

---
