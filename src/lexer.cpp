
#include "lexer.hpp"
#include "token.hpp"
#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
    if(is_end()) return '\0';

    return source[pos++];
}

void Lexer::skip_comma_space() { 
    while (!is_end()) {

        if(isspace(static_cast<unsigned char>(peek()))) {    // для проверки кириллицы 
            advance();
        } else if (peek() == '/' && peek_next() == '/' ) {
            while (peek() != '\n' && !is_end()) advance(); 
        } else if (peek() == '/' && peek_next() == '*' ) { 
            advance();
            advance();
            while(!is_end() && !(peek() == '*' && peek_next() == '/')){
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

    size_t start = pos;
    bool is_float = false;

    while(isdigit(peek())) advance();

    if((peek() == '.') && (isdigit(peek_next()))) {
        advance();
        while(isdigit(peek())) advance();
        is_float = true;
    }

    if(isalpha(peek())) { 
        size_t strat_suf = pos;
        while(isalpha(peek()) || isdigit(peek())) advance();

        std::string_view suffix = source.substr(strat_suf, pos - strat_suf);
        if(!suf.contains(suffix)) { 
            std::cerr << "invailid suffix: " << suffix;
            return Token{TokenKind::Undefined, "suffix"};
        }
    }
    
    std::string_view lexeme = source.substr(start, pos - start);
    return Token{is_float ? TokenKind::LitFloat : TokenKind::LitInt, lexeme};

}

Token Lexer::lex_str() { 

    char quote = peek();

    advance();
    size_t start_lexeme = pos;

    while(!is_end() && peek() != quote){
        advance();
    }

    // если на этом этапе оказались в конце, значит не закрыли
    if(is_end()) { 
        std::cerr << "unterminated str literal\n";
        return Token{TokenKind::Undefined, "err"};
    }

    if(peek() == quote){
        std::string_view lexeme = source.substr(start_lexeme, pos - start_lexeme);
        advance(); // пропускаем вторую кавычку
        return Token{TokenKind::LitString, lexeme};
    } else { 
        std::cerr << "ковычки не одинаковые\n";
        return Token{TokenKind::Undefined, "err"}; 
    }
 
}
Token Lexer::lex_op_or_sep() { 

    std::string_view lexeme_2_sym = source.substr(pos, 2);

    auto it_ops1 = ops.find(lexeme_2_sym);
    auto it_sep1 = sep.find(lexeme_2_sym);

    if (it_ops1 != ops.end() && it_sep1 == sep.end()){
        advance(); 
        advance();
        return Token{it_ops1 -> second, lexeme_2_sym};
    } else if (it_ops1 == ops.end() && it_sep1 != sep.end()){
        advance(); 
        advance();
        return Token{it_sep1 -> second, lexeme_2_sym};
    } else { 

        std::string_view lexeme_1_sym = source.substr(pos, 1);
        auto it_ops = ops.find(lexeme_1_sym);
        auto it_sep = sep.find(lexeme_1_sym);

        if (it_ops != ops.end() && it_sep == sep.end()){
            advance();
            return Token{it_ops -> second, lexeme_1_sym};
        } else if (it_ops == ops.end() && it_sep != sep.end()){
            advance();
            return Token{it_sep -> second, lexeme_1_sym};
        }   
    }
    
    return Token{TokenKind::Undefined, "undefined"};
}

std::vector<Token> Lexer::tokenize() {

    std::vector<Token> array_tokens;
    Token new_token;
    
    while (!is_end()) {

        Lexer::skip_comma_space();
        if(is_end()) break;

        char c = Lexer::peek();
        if (isalpha(c) || c == '_') {
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