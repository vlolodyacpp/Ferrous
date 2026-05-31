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

    // вспомогательный для диагностики
    enum class DiagSeverity {
        Error, Warning, Note,
    };


    // Сообщение диагностики
    struct Diagnostic {
        DiagSeverity severity;
        std::string message;
        std::size_t line;
        std::size_t column;
    };


    // накопление варнингов, ошибок
    class DiagBag {
    public:
        void error(std::size_t, std::size_t, std::string);
        void warning(std::size_t, std::size_t, std::string);
        bool has_errors() const;
        const std::vector<Diagnostic>& all() const;
    private:
        std::vector<Diagnostic> items;
    };



    // таблица встроенных типов
    class TypeRegistry {
    public:
        TypeRegistry();
        TypeID builtin(Lexer::TokenKind) const;
        TypeID error_type() const;
        TypeID void_type() const;
        bool equal(TypeID, TypeID) const;
    private:
        struct Builtin {
            Lexer::TokenKind kind;
        };
        std::vector<Builtin> builtins;
        std::unordered_map<Lexer::TokenKind, TypeID> by_kind;
        TypeID error_id{};
        TypeID void_id{};
    };

    //Проверка типов

    //
    enum class SymbolKind {
        Variable, Function, Struct, TypeAllias, Namespace,
    };


    struct VarSymbol {
        TypeID type;
        bool is_mut;
        std::size_t line, col;
    };

    struct FuncSig {
       std::vector<TypeID> params;
       TypeID return_type;
       const Parser::FnDecl *decl;  //чтобы помечать builtin функции через nullptr

    };
    struct FuncSymbol {
        std::vector<FuncSig> overloads;
    };

    struct TypeSymbol {
        TypeID id;
    };

    struct NamespaceSymbol {
        class Scope* scope;
    };

    struct Symbol {
        SymbolKind kind;
        std::string_view name;
        std::variant<VarSymbol, FuncSymbol, TypeSymbol, NamespaceSymbol> data;
    };

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

    private:
        TypeRegistry registry;
        DiagBag diag;

        Scope root_scope;
        std::deque<Scope> scope_storage;

        std::optional<TypeID> resolve_type(const Parser::TypeRef&);

        void install_builtins();
        void pass1(const std::vector<Parser::Decl>&);
        void pass1_fn(Scope&, const Parser::FnDecl&);
        void verify_main();
    };

    void print_diagnostics(const DiagBag&, std::string_view, std::ostream&);
}
