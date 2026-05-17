module Ferrous.Parser;

import std;

namespace Parser {

    bool Parser::is_end() const {
        return tokens[pos].kind == Lexer::TokenKind::Eof;
    }

    Lexer::Token Parser::peek() const {
        return tokens[pos];
    }

    Lexer::Token Parser::peek_next() const {
        if (pos + 1 >= tokens.size()) {
            return tokens.back();
        }
        return tokens[pos + 1];
    }

    Lexer::Token Parser::advance() {
        Lexer::Token cur = tokens[pos];
        if (!is_end()) {
            ++pos;
        }
        return cur;
    }

    bool Parser::check(Lexer::TokenKind k) const {
        return tokens[pos].kind == k;
    }

    bool Parser::match(Lexer::TokenKind k) {
        if (!check(k)) return false;
        advance();
        return true;
    }

    Lexer::Token Parser::expect(Lexer::TokenKind k) {
        if (!check(k)) {
            return Lexer::Token{Lexer::TokenKind::Eof, "err"}; // временная заглушка
        }
        return advance();
    }

    std::vector<Decl> Parser::parse() {
        return {};
    }

} // namespace Parser
