#include "../inc/parser.hpp"



bool Parser::is_end() const {
    return tokens[pos].kind == TokenKind::Eof;
}

Token Parser::peek() const {
    return tokens[pos];
}

Token Parser::peek_next() const {
    if (pos + 1 >= tokens.size()) {
        return tokens.back();
    }
    return tokens[pos + 1];
}

Token Parser::advance() {
    Token cur = tokens[pos];
    if (!is_end()) {
        ++pos;
    }
    return cur;
}

bool Parser::check(TokenKind k) const {
    return tokens[pos].kind == k;
}

bool Parser::match(TokenKind k) {
    if (!check(k)) return false;
    advance();
    return true;
}

Token Parser::expect(TokenKind k) {
    if (!check(k)) {
        return Token{TokenKind EOF, "err"}; // временная заглушка
    }
    return advance();
}

std::vector<Decl> Parser::parse() {
    return {};
}


