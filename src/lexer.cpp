module Ferrous.Lexer;

import std;

namespace Lexer {

    bool Lexer::is_end() const {
        return pos >= len;
    }

    char Lexer::peek() const {
        if (is_end()) return '\0';
        return source[pos];
    }

    char Lexer::peek_next() const {
        if (pos + 1 >= len) return '\0';
        return source[pos + 1];
    }

    char Lexer::advance() {
        if (is_end()) return '\0';

        const char c = source[pos++];
        if (c == '\n') {
            ++line;
            column = 1;
        } else {
            ++column;
        }

        return c;
    }

    
    void Lexer::skip_comma_space() {
        while (!is_end()) {

            if (std::isspace(static_cast<unsigned char>(peek()))) {    // для проверки кириллицы
                advance();
            } else if (peek() == '/' && peek_next() == '/') {
                while (peek() != '\n' && !is_end()) advance();
            } else if (peek() == '/' && peek_next() == '*') {
                advance();
                advance();
                while (!is_end() && !(peek() == '*' && peek_next() == '/')) {
                    advance();
                }
                if (is_end()) {
                    return;
                } else {
                    advance();
                    advance();
                }

            } else {
                break;
            }

        }
    }


    Token Lexer::lex_ident_or_kw() {
        const std::size_t start = pos;
        const std::size_t start_line = line;
        const std::size_t start_column = column;

        while (!is_end() && (std::isalnum(peek()) || peek() == '_')) {
            advance();
        }

        const std::string_view lexeme = source.substr(start, pos - start);

        if (auto it = keywords.find(lexeme); it != keywords.end()) {
            return Token{it->second, lexeme, start_line, start_column};
        }
        return Token{TokenKind::Ident, lexeme, start_line, start_column};
    }

    Token Lexer::lex_num() {

        const std::size_t start = pos;
        const std::size_t start_line = line;
        const std::size_t start_column = column;
        bool is_float = false;
        while (std::isdigit(peek())) advance();

        if ((peek() == '.') && (std::isdigit(peek_next()))) {
            advance();
            while (std::isdigit(peek())) advance();
            is_float = true;
        }

        if (std::isalpha(peek())) {
            const std::size_t start_suf = pos;
            while (std::isalpha(peek()) || std::isdigit(peek())) advance();

            if (const std::string_view suffix = source.substr(start_suf, pos - start_suf);
                !suf.contains(suffix)) {
                std::cerr << "invalid suffix: " << suffix;
                return Token{TokenKind::Undefined, "suffix", start_line, start_column};
            }
        }

        const std::string_view lexeme = source.substr(start, pos - start);
        return Token{is_float ? TokenKind::LitFloat : TokenKind::LitInt,
            lexeme, start_line, start_column};

    }

    Token Lexer::lex_str() {

        const std::size_t start_line = line;
        const std::size_t start_column = column;
        const char quote = peek();

        advance();
        const std::size_t start_lexeme = pos;

        while (!is_end() && peek() != quote) {
            advance();
        }

        // если на этом этапе оказались в конце, значит не закрыли
        if (is_end()) {
            std::cerr << "unterminated str literal\n";
            return Token{TokenKind::Undefined, "err", start_line, start_column};
        }

        if (peek() == quote) {
            const std::string_view lexeme = source.substr(start_lexeme, pos - start_lexeme);
            advance(); // пропускаем вторую кавычку
            return Token{TokenKind::LitString, lexeme, start_line, start_column};
        }

        std::cerr << "кавычки не одинаковые\n";
        return Token{TokenKind::Undefined, "err", start_line, start_column};
    }

    Token Lexer::lex_op_or_sep() {

        const std::size_t start_line = line;
        const std::size_t start_column = column;

        const std::string_view lexeme_2_sym = source.substr(pos, 2);

        const auto it_ops1 = ops.find(lexeme_2_sym);

        if (const auto it_sep1 = sep.find(lexeme_2_sym);
            it_ops1 != ops.end() && it_sep1 == sep.end()) {
            advance();
            advance();
            return Token{it_ops1->second, lexeme_2_sym, start_line, start_column};
        } else {
            if (it_ops1 == ops.end() && it_sep1 != sep.end()) {
                advance();
                advance();
                return Token{it_sep1->second, lexeme_2_sym, start_line, start_column};
            }
            const std::string_view lexeme_1_sym = source.substr(pos, 1);
            const auto it_ops = ops.find(lexeme_1_sym);

            if (const auto it_sep = sep.find(lexeme_1_sym);
                it_ops != ops.end() && it_sep == sep.end()) {
                advance();
                return Token{it_ops->second, lexeme_1_sym, start_line, start_column};
            } else if (it_ops == ops.end() && it_sep != sep.end()) {
                advance();
                return Token{it_sep->second, lexeme_1_sym, start_line, start_column};
            }
        }

        return Token{TokenKind::Undefined, "undefined",  start_line, start_column};
    }

    std::vector<Token> Lexer::tokenize() {

        std::vector<Token> array_tokens;
        Token new_token;

        while (!is_end()) {

            skip_comma_space();
            if (is_end()) break;

            if (const char c = peek(); std::isalpha(c) || c == '_') {
                new_token = lex_ident_or_kw();
            } else if (std::isdigit(c)) {
                new_token = lex_num();
            } else if (c == '"' || c == '\'') {
                new_token = lex_str();
            } else {
                new_token = lex_op_or_sep();
            }

            array_tokens.push_back(new_token);

        }

        Token token_eof;
        token_eof.kind = TokenKind::Eof;
        token_eof.lexeme = "\0";
        token_eof.line = line;
        token_eof.column = column;
        array_tokens.push_back(token_eof);
        return array_tokens;
    }


    const std::unordered_map<std::string_view, TokenKind> Lexer::keywords = {
        {"let", TokenKind::KwLet},
        {"mut", TokenKind::KwMut},
        {"fn", TokenKind::KwFn},
        {"return", TokenKind::KwReturn},
        {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},
        {"while", TokenKind::KwWhile},
        {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"struct", TokenKind::KwStruct},
        {"type", TokenKind::KwType},
        {"namespace", TokenKind::KwNamespace},
        {"as", TokenKind::KwAs},
        {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},
        {"void", TokenKind::KwVoid},


        {"int8", TokenKind::KwInt8},
        {"int16", TokenKind::KwInt16},
        {"int32", TokenKind::KwInt32},
        {"int64", TokenKind::KwInt64},
        {"uint8", TokenKind::KwUint8},
        {"uint16", TokenKind::KwUint16},
        {"uint32", TokenKind::KwUint32},
        {"uint64", TokenKind::KwUint64},
        {"float32", TokenKind::KwFloat32},
        {"float64", TokenKind::KwFloat64},
        {"bool", TokenKind::KwBool},
        {"string", TokenKind::KwString},

    };

    const std::unordered_map<std::string_view, TokenKind> Lexer::ops = {
        {"+", TokenKind::OpPlus},
        {"-", TokenKind::OpMinus},
        {"*", TokenKind::OpStar},
        {"/", TokenKind::OpStash},
        {"%", TokenKind::OpPercent},
        {"=", TokenKind::OpEq},
        {"==", TokenKind::OpEqEq},
        {"!=", TokenKind::OpBangEq},
        {"<", TokenKind::OpLt},
        {"<=", TokenKind::OpLtEq},
        {">", TokenKind::OpGt},
        {">=", TokenKind::OpGtEq},
        {"&&", TokenKind::OpAndAnd},
        {"||", TokenKind::OpOrOr},
        {"!", TokenKind::OpBang},
    };

    const std::unordered_map<std::string_view, TokenKind> Lexer::sep = {
        {";", TokenKind::SepSemicolon},
        {",", TokenKind::SepComma},
        {":", TokenKind::SepColon},
        {".", TokenKind::SepDot},
        {"{", TokenKind::SepLBrace},
        {"}", TokenKind::SepRBrace},
        {"[", TokenKind::SepLBracket},
        {"]", TokenKind::SepRBracket},
        {"(", TokenKind::SepLParen},
        {")", TokenKind::SepRParen},
        {"->", TokenKind::SepArrow},
        {"::", TokenKind::SepColonColon},
    };


    const std::unordered_set<std::string_view> Lexer::suf = {
        "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64",
        "f32", "f64"
    };

} // namespace Lexer
