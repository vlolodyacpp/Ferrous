#include "lexer.hpp"
#include "token.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

static std::string_view kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::Ident:         return "Ident";
        case TokenKind::KwLet:         return "KwLet";
        case TokenKind::KwMut:         return "KwMut";
        case TokenKind::KwFn:          return "KwFn";
        case TokenKind::KwReturn:      return "KwReturn";
        case TokenKind::KwIf:          return "KwIf";
        case TokenKind::KwElse:        return "KwElse";
        case TokenKind::KwWhile:       return "KwWhile";
        case TokenKind::KwBreak:       return "KwBreak";
        case TokenKind::KwContinue:    return "KwContinue";
        case TokenKind::KwStruct:      return "KwStruct";
        case TokenKind::KwType:        return "KwType";
        case TokenKind::KwNamespace:   return "KwNamespace";
        case TokenKind::KwAs:          return "KwAs";
        case TokenKind::KwTrue:        return "KwTrue";
        case TokenKind::KwFalse:       return "KwFalse";
        case TokenKind::KwVoid:        return "KwVoid";
        case TokenKind::KwInt8:        return "KwInt8";
        case TokenKind::KwInt16:       return "KwInt16";
        case TokenKind::KwInt32:       return "KwInt32";
        case TokenKind::KwInt64:       return "KwInt64";
        case TokenKind::KwUint8:       return "KwUint8";
        case TokenKind::KwUint16:      return "KwUint16";
        case TokenKind::KwUint32:      return "KwUint32";
        case TokenKind::KwUint64:      return "KwUint64";
        case TokenKind::KwFloat32:     return "KwFloat32";
        case TokenKind::KwFloat64:     return "KwFloat64";
        case TokenKind::KwBool:        return "KwBool";
        case TokenKind::KwString:      return "KwString";
        case TokenKind::LitInt:        return "LitInt";
        case TokenKind::LitFloat:      return "LitFloat";
        case TokenKind::LitString:     return "LitString";
        case TokenKind::SepSemicolon:  return "SepSemicolon";
        case TokenKind::SepComma:      return "SepComma";
        case TokenKind::SepColon:      return "SepColon";
        case TokenKind::SepDot:        return "SepDot";
        case TokenKind::SepLBrace:     return "SepLBrace";
        case TokenKind::SepRBrace:     return "SepRBrace";
        case TokenKind::SepLBracket:   return "SepLBracket";
        case TokenKind::SepRBracket:   return "SepRBracket";
        case TokenKind::SepLParen:     return "SepLParen";
        case TokenKind::SepRParen:     return "SepRParen";
        case TokenKind::SepArrow:      return "SepArrow";
        case TokenKind::SepColonColon: return "SepColonColon";
        case TokenKind::OpPlus:        return "OpPlus";
        case TokenKind::OpMinus:       return "OpMinus";
        case TokenKind::OpStar:        return "OpStar";
        case TokenKind::OpStash:       return "OpStash";
        case TokenKind::OpPercent:     return "OpPercent";
        case TokenKind::OpEq:          return "OpEq";
        case TokenKind::OpEqEq:        return "OpEqEq";
        case TokenKind::OpBangEq:      return "OpBangEq";
        case TokenKind::OpLt:          return "OpLt";
        case TokenKind::OpLtEq:        return "OpLtEq";
        case TokenKind::OpGt:          return "OpGt";
        case TokenKind::OpGtEq:        return "OpGtEq";
        case TokenKind::OpAndAnd:      return "OpAndAnd";
        case TokenKind::OpOrOr:        return "OpOrOr";
        case TokenKind::OpBang:        return "OpBang";
        case TokenKind::Eof:           return "Eof";
        case TokenKind::Undefined:     return "undefined";
    }
    return "?";
}



int main(int argc, char **argv) {
    if(argc < 2) { 
        std::cerr << "usage: Ferrous <file.fer>\n";
        return 1;
    }

    std::ifstream in(argv[1]);
    if(!in) { 
        std::cerr << "cannot open: " << argv[1] << "\n";
        return 1;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    Lexer lex(source);
    auto tokens = lex.tokenize();

    for (const auto& t : tokens) {
        std::cout << kind_name(t.kind) << " '" << t.lexeme << "'\n";
    }

    return 0;
}
