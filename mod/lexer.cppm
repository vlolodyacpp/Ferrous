module;
#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

export module Ferrous.Lexer;
export import Ferrous.Token;
import Ferrous.Semantic;

export namespace Lexer {

    class Lexer {
    public:
        Lexer(const std::string_view str, Semantic::DiagBag& diag)
            : source(str), pos(0), len(str.length()), line(1), column(1), diag(diag) {}
        std::vector<Token> tokenize();
    private:

        static const std::unordered_map<std::string_view, TokenKind> ops;
        static const std::unordered_map<std::string_view, TokenKind> keywords;
        static const std::unordered_map<std::string_view, TokenKind> sep;
        static const std::unordered_set<std::string_view> suf;

        Token lex_ident_or_kw();
        Token lex_num();
        Token lex_char();
        Token lex_str();
        Token lex_op_or_sep();

        void skip_comma_space();
        [[nodiscard]] bool is_end() const;
        [[nodiscard]] char peek() const;
        [[nodiscard]] char peek_next() const;
        char advance();


        std::string_view source;
        std::size_t pos;
        std::size_t len;

        std::size_t line; // для отладки ошибок
        std::size_t column;

        Semantic::DiagBag& diag;
    };
} // namespace Lexer
