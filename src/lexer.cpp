
#include "lexer.hpp"
#include "token.hpp"
#include <cstdio>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cctype>


bool Lexer::is_end() const {
    return pos >= len;
}

char Lexer::peek() const {
    if(is_end()) return '\0';
    return source[pos];
}

char Lexer::peek_next() const {
    if(pos + 1 >= len) return '\0';
    return source[pos + 1]; 
}
char Lexer::advance() {
    return source[pos++];
}

void Lexer::skip_comma_space() { 
    while (!is_end()) {

        if(isspace(peek())) { 
            advance();
        } else if (peek() == '/' && peek_next() == '/' ) {
            while (peek() != '\n' && !is_end()) advance(); 
        } else if (peek() == '/' && peek_next() == '*' ) { 
            while (peek() != '*' && peek() != '/') advance();
            if(peek() == '*' && peek_next() == '/'){ 
                advance();
                advance();
            }
        } else {
            break;
        }

    }
}


Token Lexer::lex_ident_or_kw(){
    size_t start = pos;

    while(!is_end() && (isalnum(peek()) || peek() == '_' )){
        advance(); 
    }

    std::string_view lexeme = source.substr(start, pos - start);
    auto it = keywords.find(lexeme);
    
    if(it != keywords.end()) { 
        return Token{it -> second, lexeme};  
    } else { 
        return Token{TokenKind::Ident, lexeme}; 
    }

}

Token Lexer::lex_num() { 
    return Token{TokenKind::LitInt, "2"};  // tbd - заглуша для теста lex_ident_or_kw
}

Token Lexer::lex_str() { 
    return Token{TokenKind::LitString, "str"};   // tbd - заглуша для теста lex_ident_or_kw
}
Token Lexer::lex_op_or_sep() { 
    return Token{TokenKind::OpAndAnd, "&&"};   // tbd - заглуша для теста lex_ident_or_kw
}

std::vector<Token> Lexer::tokenize() {

    std::vector<Token> array_tokens;
    Token new_token;
    
    while (!is_end()) {

        Lexer::skip_comma_space();
        if(is_end()) break;

        char c = Lexer::peek();
        if (isalpha(c)) {
            new_token = Lexer::lex_ident_or_kw();
        } else if (isdigit(c)) {
            new_token = Lexer::lex_num();
        } else if (c == '"' || c == '\''){
            new_token = Lexer::lex_str();
        } else {
            new_token = Lexer::lex_op_or_sep();
        }
      
        array_tokens.push_back(new_token);
    
    }

    Token token_eof;
    token_eof.kind = TokenKind::Eof;
    token_eof.lexeme = "\0";
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