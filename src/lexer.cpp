module;
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

module Ferrous.Lexer;

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
                const std::size_t c_line = line;     // позиция открывающего /*
                const std::size_t c_col = column;
                advance();                       // '/'
                advance();                       // '*'
                int depth = 1;                   // вложенности: /* /* */ */
                while (!is_end() && depth > 0) {
                    if (peek() == '/' && peek_next() == '*') {
                        advance(); advance(); ++depth;   // вложенное открытие
                    } else if (peek() == '*' && peek_next() == '/') {
                        advance(); advance(); --depth;   // закрытие
                    } else {
                        advance();
                    }
                }
                if (depth > 0) {                 // обрыв файла внутри комментария
                    diag.error(c_line, c_col, "unterminated block comment");
                    return;
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

        while (!is_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
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


        // hex/binary rule
        bool is_hex_or_binary = false;
        if (peek() == '0' && (peek_next() == 'x' || peek_next() == 'X')) {
            advance();
            advance();          // 0x, 0X
            while (std::isxdigit(static_cast<unsigned char>(peek()))) advance();
            is_hex_or_binary = true;
        } else if (peek() == '0' && (peek_next() == 'b' || peek_next() == 'B')) {
            advance();
            advance();          // 0b, 0B
            while (peek() == '0' || peek() == '1') advance();
            is_hex_or_binary = true;
        }
        if (!is_hex_or_binary) {
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }

        // float/exp
        if ((peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) || peek_next() == 'e' || peek_next() == 'E') {
            advance();
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
            is_float = true;
        }

        // экспоненциальная запись
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                diag.error(start_line, start_column,
                           "invalid float literal: expected digit after exponent");
                return Token{TokenKind::Undefined, "err", start_line, start_column};
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }

        // суфикс
        if (std::isalpha(static_cast<unsigned char>(peek()))) {
            const std::size_t start_suf = pos;
            while (std::isalpha(static_cast<unsigned char>(peek())) || std::isdigit(static_cast<unsigned char>(peek()))) advance();

            if (const std::string_view suffix = source.substr(start_suf, pos - start_suf);
                !suf.contains(suffix)) {
                diag.error(start_line, start_column,
                           "invalid suffix: " + std::string(suffix));
                return Token{TokenKind::Undefined, "suffix", start_line, start_column};
            }
        }

        const std::string_view lexeme = source.substr(start, pos - start);
        return Token{is_float ? TokenKind::LitFloat : TokenKind::LitInt,
            lexeme, start_line, start_column};
    }

    Token Lexer::lex_char() {
        const std::size_t start_line = line;
        const std::size_t start_column = column;

        advance();
        const std::size_t start_pos = pos;

        if (peek() == '\0' || peek() == '\n') {
            diag.error(start_line, start_column, "unterminated char literal");
            return Token{TokenKind::Undefined, "err", start_line, start_column};
        }

        // пустой char
        if (peek() == '\'') {
            advance();
            diag.error(start_line, start_column, "empty char literal");
            return Token{TokenKind::Undefined, "err", start_line, start_column};
        }

        // escape последовательности
        if (peek() == '\\') {
            advance();
            if (peek() == '\0' || peek() == '\n') {
                diag.error(start_line, start_column, "unterminated char literal");
                return Token{TokenKind::Undefined, "err", start_line, start_column};
            }
            advance();
        } else {
            advance();
        }


        // проверка на адекватность
        if (peek() != '\'') {
            while (!is_end() && peek() != '\n' && peek() != '\'') {
                advance();
            }
            if (peek() == '\'') {
                advance();
                diag.error(start_line, start_column, "char literal too long");
                return Token{TokenKind::Undefined, "err", start_line, start_column};
            }
            diag.error(start_line, start_column, "unterminated char literal");
            return Token{TokenKind::Undefined, "err", start_line, start_column};
        }

        const std::string_view lexeme = source.substr(start_pos, pos - start_pos);
        advance(); // пропускаем вторую кавычку
        return Token{TokenKind::LitChar, lexeme, start_line, start_column};
    }


    Token Lexer::lex_str() {

        const std::size_t start_line = line;
        const std::size_t start_column = column;
        const char quote = peek();

        advance();
        const std::size_t start_lexeme = pos;

        while (!is_end() && peek() != quote && peek() != '\n') {
            if (peek() == '\\') {
                advance();                              // '\'
                if (is_end() || peek() == '\n') break;  // оборванный escape
                advance();                              // экранированный символ
            } else {
                advance();
            }
        }

        // если на этом этапе не на закрывающей кавычке - строка не закрыта
        if (is_end() || peek() == '\n') {
            diag.error(start_line, start_column, "unterminated string literal");
            return Token{TokenKind::Undefined, "err", start_line, start_column};
        }

        const std::string_view lexeme = source.substr(start_lexeme, pos - start_lexeme);
        advance(); // пропускаем вторую кавычку
        return Token{TokenKind::LitString, lexeme, start_line, start_column};
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

        // нераспознанный символ: сообщаем с позицией и обязательно сдвигаемся,
        const std::string_view bad = source.substr(pos, 1);
        diag.error(start_line, start_column,
                   "unexpected character '" + std::string(bad) + "'");
        advance();
        return Token{TokenKind::Undefined, "undefined", start_line, start_column};
    }

    std::vector<Token> Lexer::tokenize() {

        std::vector<Token> array_tokens;
        Token new_token;

        while (!is_end()) {

            skip_comma_space();
            if (is_end()) break;

            if (const char c = peek(); std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                new_token = lex_ident_or_kw();
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                new_token = lex_num();
            } else if (c == '"') {
                new_token = lex_str();
            } else if (c == '\'') {
                new_token = lex_char();
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
        {"char", TokenKind::KwChar},
        {"nan", TokenKind::KwNaN},
        {"inf", TokenKind::KwInf},

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
        {"/", TokenKind::OpSlash},
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
        {"&", TokenKind::OpAmp},
        {"|", TokenKind::OpPipe},
        {"^", TokenKind::OpCaret},
        {"~", TokenKind::OpTilde},
        {"<<", TokenKind::OpShl},
        {">>", TokenKind::OpShr},
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
