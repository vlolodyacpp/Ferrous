#pragma once 

#include "token.hpp"
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>


class Lexer { 
public:
    explicit Lexer(const std::string_view str) : source(str), pos(0), len(str.length()){}
    std::vector<Token> tokenize();
private:

    static const std::unordered_map<std::string_view, TokenKind> ops;
    static const std::unordered_map<std::string_view, TokenKind> keywords;
    static const std::unordered_map<std::string_view, TokenKind> sep;
    static const std::unordered_set<std::string_view> suf;

    Token lex_ident_or_kw();
    Token lex_num();
    Token lex_str();
    Token lex_op_or_sep();
    Token make_token(TokenKind, size_t);

    void skip_comma_space();
    bool is_end() const;
    char peek() const;
    char peek_next() const;
    char advance();


    std::string_view source;
    std::size_t pos;
    std::size_t len; 

};
