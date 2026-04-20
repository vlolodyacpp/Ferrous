#pragma once
#include <string_view>

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
    OpStash,
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
    

    //
    Eof,
    Undefined,
};

struct Token { 
    TokenKind kind;
    std::string_view lexeme;
};