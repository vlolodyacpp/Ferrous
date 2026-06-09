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

} // namespace Lexer
