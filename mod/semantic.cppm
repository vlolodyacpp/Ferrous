module;
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

export module Ferrous.Semantic;
export import Ferrous.AST;

export namespace Semantic {


    // Семантический анализатор: третья фаза pipeline. Выполняет разрешение
    // имён, проверку типов и контроля потока, строит реестр типов и
    // аннотированный AST (aast); диагностики возвращает в DiagBag.

    // идентификатор типа
    struct TypeID {
        std::uint32_t id;
        bool operator==(const TypeID&) const = default;
    };

    // ошибки и варнинги
    enum class DiagSeverity {
        Error, Warning, Note,
    };


    struct Diagnostic {
        DiagSeverity severity;
        std::string message;
        std::size_t line;
        std::size_t column;
    };

    // контейнер для накопления диагностик в процессе проверки
    class DiagBag {
    public:
        void error(std::size_t, std::size_t, std::string);
        void warning(std::size_t, std::size_t, std::string);
        bool has_errors() const;
        const std::vector<Diagnostic>& all() const;
    private:
        std::vector<Diagnostic> items;
    };

    // реестр типов: хранит все типы программы и выдаёт по ним TypeID
    class TypeRegistry {
    public:
        // структура: имя + упорядоченные поля (имя, тип)
        struct StructType {
            std::string name;
            std::vector<std::pair<std::string, TypeID>> fields;
        };
        // массив фиксированного размера: тип элемента + размер (часть типа)
        struct ArrayType {
            TypeID elem;
            std::uint64_t size;
        };

        TypeRegistry();                          // регистрирует все встроенные типы
        TypeID builtin(Lexer::TokenKind) const;  // TypeID встроенного типа по его ключевому слову

        // служебные синглтон-типы
        TypeID untyped_int() const;              // общий untyped-int (42 без суффикса/контекста)
        TypeID untyped_float() const;            // общий untyped-float (3.14)
        TypeID error_type() const;               // тип-ошибка (подавляет каскад ошибок)
        TypeID void_type() const;                // void

        // предикаты над типами
        bool equal(TypeID, TypeID) const;        // равны (с учётом разворота алиасов)
        bool is_untyped(TypeID) const;           // untyped-int или untyped-float
        bool is_untyped_int(TypeID) const;
        bool is_untyped_float(TypeID) const;
        bool is_fixed_int(TypeID) const;         // конкретный целый (int8..uint64)
        bool is_fixed_float(TypeID) const;       // конкретный float (float32/64)
        bool is_float(TypeID) const;             // любой вещественный (вкл. untyped-float)

        TypeID array(TypeID elem, std::uint64_t size);   // получить/создать тип массива

        // поиск уже созданного типа массива без мутации (для codegen на const-реестре)
        std::optional<TypeID> find_array(TypeID elem, std::uint64_t size) const;

        // структуры (двухэтапно для forward-ссылок и рекурсии)
        TypeID struct_placeholder(std::string_view name);          // pass1_declare: зарегистрировать имя
        void finalize_struct(std::string_view name,                // pass1_declare: заполнить поля
            std::vector<std::pair<std::string_view, TypeID>> fields);

        // синонимы типов
        TypeID declare_alias(std::string_view name);               // зарегистрировать имя алиаса
        void register_alias(std::string_view name, TypeID target); // задать его цель
        std::optional<TypeID> by_name(std::string_view name) const;// TypeID структуры/алиаса по имени

        const StructType* get_struct(TypeID) const;   // данные структуры (или nullptr)
        const ArrayType* get_array(TypeID) const;     // данные массива (или nullptr)
        bool is_struct(TypeID) const;
        bool is_array(TypeID) const;
        const std::vector<TypeID>& all_structs() const;  // все структуры (для проверки рекурсии)
        const std::vector<TypeID>& all_aliases() const;  // все алиасы (для проверки циклов)

        bool is_alias(TypeID) const;                       // запись — синоним типа?
        std::optional<TypeID> alias_target(TypeID) const;  // сырая цель алиаса (nullopt, если не resolved)
        void mark_alias_error(TypeID);                     // пометить алиас сломанным (для разрыва цикла)

        TypeID resolve_alias(TypeID) const;   // развернуть цепочку алиасов в конечный тип
        std::string name(TypeID) const;       // человекочитаемое имя типа
    private:
        // встроенный тип
        struct Builtin {
            Lexer::TokenKind kind;
        };
        // нетипизированный литерал
        struct Untyped {
            enum class Kind { Int, Float };
            Kind kind;
        };
        // синоним типа: имя + цель (resolved — развёрнут ли target)
        struct AliasType {
            std::string name;
            TypeID target;
            bool resolved = false;
        };
        // ключ кеша массивов: (тип элемента, размер) — часть типа массива
        struct ArrayKey {
            TypeID elem;
            std::uint64_t size;
            bool operator==(const ArrayKey&) const = default;
        };
        // хеш для ArrayKey (чтобы класть в unordered_map)
        struct ArrayKeyHash {
            std::size_t operator()(const ArrayKey& key) const;
        };

        // одна запись реестра — любой из видов типа
        using TypeEntry = std::variant<Builtin, StructType, ArrayType, AliasType, Untyped>;

        std::vector<TypeEntry> entries;   // все типы; индекс в векторе = TypeID
        std::unordered_map<Lexer::TokenKind, TypeID> by_kind;     // builtin → TypeID
        std::unordered_map<std::string, TypeID> by_name_map;      // имя (struct/alias) → TypeID
        std::unordered_map<ArrayKey, TypeID, ArrayKeyHash> array_cache; // хэш типов массивов
        std::vector<TypeID> struct_ids;   // id всех структур (для проверки рекурсии)
        std::vector<TypeID> alias_ids;    // id всех алиасов (для проверки циклов)
        TypeID error_id{};                // тип-ошибка (после плохой типизации)
        TypeID void_id{};                 // void
        TypeID untyped_int_id{};          // общий untyped-int
        TypeID untyped_float_id{};        // общий untyped-float
    };

    struct AnnotatedAST {
        std::unordered_map<const Parser::Expr*, TypeID> expr_type;
        std::unordered_map<const Parser::LetStmt*, TypeID> let_type;
        std::unordered_map<const Parser::CallExpr*, std::size_t> resolved_overload;
        // полный список выражений-аргументов в порядке параметров
        std::unordered_map<const Parser::CallExpr*, std::vector<const Parser::Expr*>> call_slots;
        // типы параметров выбранной перегрузки в порядке слотов (для манглинга)
        std::unordered_map<const Parser::CallExpr*, std::vector<TypeID>> call_slot_types;
        // единая карта неявных приведений
        std::unordered_map<const Parser::Expr*, std::pair<TypeID, TypeID>> coerce;
        const TypeRegistry* types = nullptr;
    };

    // символы
    enum class SymbolKind {
        Variable, Function, Struct, TypeAllias, Namespace,
    };

    // cимвол-переменная
    struct VarSymbol {
        TypeID type;
        bool is_mut;
        std::size_t line, col;
    };

    // cигнатура функции (для перегрузки)
    struct FuncSig {
       std::vector<TypeID> params;
       TypeID return_type;
       const Parser::FnDecl *decl;  // nullptr для встроенных функций
       std::size_t line = 0, col = 0;  // позиция объявления (для диагностик)
    };

    // cимвол-функция
    struct FuncSymbol {
        std::vector<FuncSig> overloads;
    };

    // структура или псевдоним
    struct TypeSymbol {
        TypeID id;
    };

    // namespace
    struct NamespaceSymbol {
        class Scope* scope;
    };

    // универсальный символ
    struct Symbol {
        SymbolKind kind;
        std::string_view name;
        std::variant<VarSymbol, FuncSymbol, TypeSymbol, NamespaceSymbol> data;
    };

    // область видимости
    class Scope {
    public:
        Scope *parent = nullptr;
        std::unordered_map<std::string_view, Symbol> table;

        Symbol *lookup_local(std::string_view);
        Symbol *lookup_chain(std::string_view);
        bool insert(Symbol);
    };

    class Semantic{
    public:

        DiagBag check(const std::vector<Parser::Decl>&);
        AnnotatedAST aast;

    private:
        TypeRegistry registry;
        DiagBag diag;

        Scope root_scope;                   // корневая область видимости
        std::deque<Scope> scope_storage;    // хранилище вложенных scope
        Scope* current_scope = nullptr;     // текущая область при обходе
        TypeID current_return_type{};       // ожидаемый тип return
        bool in_loop = false;
        std::size_t cur_line = 0, cur_col = 0;  // позиция для диагностик без явного узла

        // TypeRef → TypeID
        std::optional<TypeID> resolve_type(const Parser::TypeRef&);

        // фазы анализа
        void install_builtins();                                    // print/input/len/exit/panic/assert
        void pass1_declare(const std::vector<Parser::Decl>&);       // регистрация имен верхнего уровня
        void pass1_declare_types(const std::vector<Parser::Decl>&); // регистрация типов верхнего уровня

        void pass1_declare(const std::vector<Parser::Decl>&, Scope&);       // перегрузка для текущей области видимости
        void pass1_declare_types(const std::vector<Parser::Decl>&, Scope&);
        void pass1_declare_fns(const std::vector<Parser::Decl>&, Scope&);   // регистрация только функций (+ рекурсия в namespace)

        void pass2_check_bodies(const std::vector<Parser::Decl>&, Scope&);  // проверка тел функций
        void check_recursive_structs();                        // запрет бесконечно-рекурсивных структур
        void verify_alias_cycles();                            // запрет циклов синонимов типов

        // позиции объявлений типов верхнего уровня (struct/alias) — для диагностик
        std::unordered_map<std::uint32_t, std::pair<std::size_t, std::size_t>> decl_pos;
        void pass1_declare_fn(Scope&, const Parser::FnDecl&, std::size_t line, std::size_t col); // регистрация функции (+ перегрузки)
        void check_function_body(const Parser::FnDecl&, Scope&, std::size_t line, std::size_t col); // проверка тела одной функции (parent — scope namespace/root)
        void verify_main();                                    // наличие main


        Scope& push_scope(Scope&);                          // создать дочерний scope
        Scope& ensure_namespace(Scope&, std::string_view);  // получить/создать scope


        TypeID check_expr(const Parser::Expr&, std::optional<TypeID>);

        void check_stmt(const Parser::Stmt&);               // проверка инструкции
        bool always_returns(const Parser::Stmt&);           // все пути возвращают значение?
        bool is_lvalue(const Parser::Expr&) const;          // можно ли присваивать?
        bool is_mut_root(const Parser::Expr&) const;        // корень lvalue объявлен как mut?


        // общий тип двух фиксированных операндов с учётом неявных расширений.
        // nullopt — типы несовместимы без явного `as` 
        std::optional<TypeID> common_type(TypeID, TypeID);
        // проверить присваиваемость from→to в контексте присваивания
        bool coerce_to(const Parser::Expr& src, TypeID from, TypeID to);
        bool is_unsigned(TypeID) const;         // беззнаковый целочисленный тип?
        int bit_width(TypeID) const;            // ширина типа в битах (для приведений)
        bool int_literal_fits(std::uint64_t, TypeID, bool negated = false) const;  // влезает ли литерал в целевой int (negated — литерал под унарным минусом)
        bool neg_literal_ctx = false;   // одноразовый флаг: операнд унарного минуса — прямой int-литерал
    };


    void print_diagnostics(const DiagBag&, std::string_view, std::ostream&);

    std::optional<std::string> decode_escapes(std::string_view raw, char* bad = nullptr);
}
