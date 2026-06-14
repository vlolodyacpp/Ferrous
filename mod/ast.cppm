module;
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Ferrous.AST;
export import Ferrous.Token;

export namespace Parser {

    // AST: Реализация каждого возможного узла для построения будущего AST программы

    struct TypeRef;
    struct Expr;
    struct Stmt;
    struct Decl;
    
    struct BuiltinTypeRef  {    // def type
        Lexer::TokenKind type_kind;
    };

    struct NamedTypeRef {       // own types
        std::string_view name;
    };

    struct ArrayTypeRef {      // type in array // let arr: [int32, 5] = ...
        std::unique_ptr<TypeRef> elem;
        std::string_view size;
    };

    struct TypeRef {
        std::variant<BuiltinTypeRef, NamedTypeRef, ArrayTypeRef> node;
    };



    struct LitIntExpr {
        std::string_view value;  // int число
    };

    struct LitFloatExpr {
        std::string_view value;     //float число
    };

    struct LitBoolExpr {
        bool value;             // bool
    };

    struct LitStringExpr { // "string"
        std::string_view value;
    };

    struct LitCharExpr {
        std::string_view value;
    };

    struct ErrorExpr {
        Lexer::Token token;
    };

    struct IdentExpr {     // ident
        std::string_view value;
    };


    struct PathExpr {
        std::vector<std::string_view> segments;  // namespace::object
    };

    struct UnaryExpr {    // -x
        std::unique_ptr<Expr> operand;
        Lexer::TokenKind op;
    };

    struct BinaryExpr {
        std::unique_ptr<Expr> lhs, rhs;
        Lexer::TokenKind op;
        std::size_t line = 0;   // строка оператора (для runtime-ошибок div/mod)
    };

    struct GroupExpr {     // (1 + 2)
        std::unique_ptr<Expr> inner;
    };

    struct CastExpr {   // int32 as float32
        std::unique_ptr<Expr> expr;
        TypeRef target;
    };

    struct CallExpr { // f(1, 2) или f(1, c=3)
        std::unique_ptr<Expr> call;
        std::vector<Expr> args;
        // параллельно args: имя именованного аргумента; пустое = позиционный
        std::vector<std::string_view> arg_names;
        std::size_t line = 0;   // строка вызова (для panic/assert)
    };


    struct IndexExpr {  // arr[2]
        std::unique_ptr<Expr> array;
        std::unique_ptr<Expr> index;
        std::size_t line = 0;   // строка индексации (для bounds-check)
    };

    struct FieldExpr {  // struct.struct_obj
        std::unique_ptr<Expr> object;
        std::string_view field;
    };

    struct ArrayLitExpr {    //  [1, 2]
        std::vector<Expr> elems;
    };


    struct StructLitExpr { // структура
        std::string_view name;
        std::vector<std::pair<std::string_view, std::unique_ptr<Expr>>> fields;
    };

    struct Expr {
        std::variant<LitIntExpr, LitFloatExpr, LitBoolExpr, LitStringExpr, LitCharExpr,
        ErrorExpr, PathExpr, IdentExpr, BinaryExpr, UnaryExpr, GroupExpr, CastExpr, CallExpr,
        IndexExpr, FieldExpr, ArrayLitExpr, StructLitExpr> node;
        std::size_t line = 0;     // позиция выражения в исходнике (для диагностики)
        std::size_t column = 0;
    };



    struct LetStmt {        // let x: int32 = 5
        bool is_mut;
        std::string_view name;
        std::optional<TypeRef> type;
        std::unique_ptr<Expr> expr_init;
    };

    struct ExprStmt {       // a = 1 + b;
        std::unique_ptr<Expr> expr;
    };


    struct BlockStmt {     // {}
        std::vector<Stmt> elems;
    };

    struct IfStmt {         // if
        std::unique_ptr<Expr> condition;
        std::unique_ptr<Stmt> then_body;
        std::unique_ptr<Stmt> else_body;
    };

    struct WhileStmt {      // while
        std::unique_ptr<Expr> condition;
        std::unique_ptr<Stmt> body;
    };

    struct ReturnStmt {     // return
        std::optional<Expr> value;
    };


    struct BreakStmt {};
    struct ContinueStmt {};
    struct NullStmt {};

    struct Stmt {
        std::variant<LetStmt, ExprStmt, BlockStmt, IfStmt, WhileStmt,
         ReturnStmt, BreakStmt, ContinueStmt, NullStmt> node;
        std::size_t line = 0;     // позиция инструкции в исходнике (для диагностики)
        std::size_t column = 0;
    };


    // параметр функции: имя, тип и необязательное значение по умолчанию 
    struct Param {
        std::string_view name;
        TypeRef type;
        std::unique_ptr<Expr> default_value;  // nullptr = нет значения по умолчанию
    };

    struct FnDecl {             // fn(param1, ...) -> type {}
        std::string_view name;
        std::vector<Param> params;
        std::optional<TypeRef> return_type;
        BlockStmt body;
    };

    struct StructDecl {     // struct name_struct { field: type, ... }
        std::string_view name;
        std::vector<std::pair<std::string_view, TypeRef>> field;
    };

    struct TypeAliasDecl { // type name_type = type;
        std::string_view name;
        TypeRef type;
    };

    struct NameSpaceDecl { // namespace name {}
        std::string_view name;
        std::vector<Decl> decls;
    };

    struct Decl {
        std::variant<FnDecl, StructDecl, TypeAliasDecl, NameSpaceDecl> node;
        std::size_t line = 0;
        std::size_t column = 0;
    };

} // namespace Parser
