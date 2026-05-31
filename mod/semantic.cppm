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

    class Semantic{
    public:
        DiagBag check(const std::vector<Parser::Decl>&);

    private:
        TypeRegistry registry;
        DiagBag diag;
    };

    void print_diagnostics(const DiagBag&, std::string_view, std::ostream&);

}
