module;
#include <deque>
export module Ferrous.Semantic;
import std;
export import Ferrous.AST;

export namespace Semantic {

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

    // реестр типов
    class TypeRegistry {
    public:
        struct StructType {
            std::string name;
            std::vector<std::pair<std::string, TypeID>> fields;
        };
        struct ArrayType {
            TypeID elem;
            std::uint64_t size;
        };

        TypeRegistry();
        TypeID builtin(Lexer::TokenKind) const;
        TypeID untyped_int() const;
        TypeID untyped_float() const;
        TypeID error_type() const;
        TypeID void_type() const;
        bool equal(TypeID, TypeID) const;
        bool is_untyped(TypeID) const;
        bool is_untyped_int(TypeID) const;
        bool is_untyped_float(TypeID) const;
        bool is_fixed_int(TypeID) const;
        bool is_fixed_float(TypeID) const;
        bool is_float(TypeID) const;
        TypeID array(TypeID elem, std::uint64_t size);
        TypeID struct_placeholder(std::string_view name);
        void finalize_struct(std::string_view name,
            std::vector<std::pair<std::string_view, TypeID>> fields);
        TypeID declare_alias(std::string_view name);
        void register_alias(std::string_view name, TypeID target);
        std::optional<TypeID> by_name(std::string_view name) const;
        const StructType* get_struct(TypeID) const;
        const ArrayType* get_array(TypeID) const;
        bool is_struct(TypeID) const;
        bool is_array(TypeID) const;
        const std::vector<TypeID>& all_structs() const;
        TypeID resolve_alias(TypeID) const;
        std::string name(TypeID) const;
    private:
        struct Builtin {
            Lexer::TokenKind kind;
        };
        struct Untyped {
            enum class Kind { Int, Float };
            Kind kind;
        };
        struct AliasType {
            std::string name;
            TypeID target;
            bool resolved = false;
        };
        struct ArrayKey {
            TypeID elem;
            std::uint64_t size;
            bool operator==(const ArrayKey&) const = default;
        };
        struct ArrayKeyHash {
            std::size_t operator()(const ArrayKey& key) const;
        };

        using TypeEntry = std::variant<Builtin, StructType, ArrayType, AliasType, Untyped>;

        std::vector<TypeEntry> entries;
        std::unordered_map<Lexer::TokenKind, TypeID> by_kind;
        std::unordered_map<std::string, TypeID> by_name_map;
        std::unordered_map<ArrayKey, TypeID, ArrayKeyHash> array_cache;
        std::vector<TypeID> struct_ids;
        TypeID error_id{};
        TypeID void_id{};
        TypeID untyped_int_id{};
        TypeID untyped_float_id{};

        TypeID normalize(TypeID) const;
    };

    struct AnnotatedAST {
        std::unordered_map<const Parser::Expr*, TypeID> expr_type;
        std::unordered_map<const Parser::LetStmt*, TypeID> let_type;
        std::unordered_map<const Parser::CallExpr*, std::size_t> resolved_overload;
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

    // семантический анализатор (главный класс)
    class Semantic{
    public:
        DiagBag check(const std::vector<Parser::Decl>&);
        AnnotatedAST aast;

    private:
        TypeRegistry registry;
        DiagBag diag;

        Scope root_scope;
        std::deque<Scope> scope_storage;
        Scope* current_scope = nullptr;
        TypeID current_return_type{};
        bool in_loop = false;

        // разрешение типа из TypeRef → TypeID
        std::optional<TypeID> resolve_type(const Parser::TypeRef&);

        // фазы анализа
        void install_builtins();
        void pass1(const std::vector<Parser::Decl>&);
        void pass1_types(const std::vector<Parser::Decl>&);

        void pass1(const std::vector<Parser::Decl>&, Scope&);
        void pass1_types(const std::vector<Parser::Decl>&, Scope&);

        void pass2(const std::vector<Parser::Decl>&, Scope&);
        void check_recursive_structs();
        void pass1_fn(Scope&, const Parser::FnDecl&);
        void check_function_body(const Parser::FnDecl&);
        void verify_main();

        // работа со областью видимости
        Scope& push_scope(Scope&);
        Scope& ensure_namespace(Scope&, std::string_view);

        // проверка выражений и инструкций
        TypeID check_expr(const Parser::Expr&, std::optional<TypeID>);
        void check_stmt(const Parser::Stmt&);
        bool always_returns(const Parser::Stmt&);
        bool is_lvalue(const Parser::Expr&) const;
        bool is_mut_root(const Parser::Expr&) const;

        // утилиты для типов
        TypeID unify_binary(TypeID, TypeID);
        bool types_compatible(TypeID, TypeID);
        bool is_unsigned(TypeID) const;
        int bit_width(TypeID) const;
    };


    void print_diagnostics(const DiagBag&, std::string_view, std::ostream&);
}
