module;
#include <cstddef>
#include <string_view>

export module Ferrous.Token;

export namespace Lexer {

    enum class TokenKind {

        // идентификаторы
        Ident,


        // ключевые слова
        KwLet, // let
        KwMut, // mut
        KwFn, // fn
        KwReturn, // return
        KwIf, // if
        KwElse, // else
        KwWhile, // while
        KwBreak, // break
        KwContinue, // continue
        KwStruct, // struct
        KwType, // type
        KwNamespace, // namespace
        KwAs, // as
        KwTrue, // true
        KwFalse, // false
        KwVoid, // void
        KwNaN,
        KwInf,


        KwChar, // char
        KwInt8,
        KwInt16,
        KwInt32,
        KwInt64,
        KwUint8,
        KwUint16,
        KwUint32,
        KwUint64,

        KwFloat32,
        KwFloat64,

        KwBool,
        KwString,



        // литералы
        LitInt,
        LitFloat,
        LitString,
        LitChar,


        // разделители

        SepSemicolon, // ;
        SepComma,  // ,
        SepColon,  // :
        SepDot,    // .
        SepLBrace, // {
        SepRBrace, // }
        SepLBracket, // [
        SepRBracket, // ]
        SepLParen,  // (
        SepRParen, // )
        SepArrow,  // ->
        SepColonColon, // ::

        // операторы

        OpPlus,
        OpMinus,
        OpStar,
        OpSlash,
        OpPercent,
        OpEq,
        OpEqEq,
        OpBangEq,
        OpLt,
        OpLtEq,
        OpGt,
        OpGtEq,
        OpAndAnd,
        OpOrOr,
        OpBang,


        OpAmp,    // &  битовое И
        OpPipe,   // |  битовое ИЛИ
        OpCaret,  // ^  битовое исключающее ИЛИ (XOR)
        OpTilde,  // ~  битовое НЕ (унарный)
        OpShl,    // << сдвиг влево
        OpShr,    // >> сдвиг вправо

        // составные присваивания (A.1.9)
        OpPlusEq,    // +=
        OpMinusEq,   // -=
        OpStarEq,    // *=
        OpSlashEq,   // /=
        OpPercentEq, // %=
        OpAmpEq,     // &=
        OpPipeEq,    // |=
        OpCaretEq,   // ^=
        OpShlEq,     // <<=
        OpShrEq,     // >>=


        //
        Eof,
        Undefined,
    };

    struct Token {
        TokenKind kind;
        std::string_view lexeme;

        std::size_t line; // для отладки ошибок
        std::size_t column;
    };

    inline bool is_compound_assign(TokenKind k) {
        switch (k) {
            case TokenKind::OpPlusEq:  case TokenKind::OpMinusEq:
            case TokenKind::OpStarEq:  case TokenKind::OpSlashEq:
            case TokenKind::OpPercentEq:
            case TokenKind::OpAmpEq:   case TokenKind::OpPipeEq:
            case TokenKind::OpCaretEq:
            case TokenKind::OpShlEq:   case TokenKind::OpShrEq:
                return true;
            default:
                return false;
        }
    }

    // базовый бинарный оператор составного присваивания (`+=` → `+`)
    inline TokenKind underlying_op(TokenKind k) {
        switch (k) {
            case TokenKind::OpPlusEq:    return TokenKind::OpPlus;
            case TokenKind::OpMinusEq:   return TokenKind::OpMinus;
            case TokenKind::OpStarEq:    return TokenKind::OpStar;
            case TokenKind::OpSlashEq:   return TokenKind::OpSlash;
            case TokenKind::OpPercentEq: return TokenKind::OpPercent;
            case TokenKind::OpAmpEq:     return TokenKind::OpAmp;
            case TokenKind::OpPipeEq:    return TokenKind::OpPipe;
            case TokenKind::OpCaretEq:   return TokenKind::OpCaret;
            case TokenKind::OpShlEq:     return TokenKind::OpShl;
            case TokenKind::OpShrEq:     return TokenKind::OpShr;
            default:                     return k;
        }
    }

} // namespace Lexer
