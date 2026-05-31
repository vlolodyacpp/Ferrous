export module Ferrous.Lexer;

import std;
export import Ferrous.Token;

export namespace Lexer {

    class Lexer {
    public:
        explicit Lexer(const std::string_view str) : source(str), pos(0), len(str.length()), line(1), column(1){}
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
    };
} // namespace Lexer
