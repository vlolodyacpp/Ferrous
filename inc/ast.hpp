#pragma once
#include "token.hpp"
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>



struct TypeRef;
struct Expr; 
struct Stmt;
struct Decl;



struct BuiltinTypeRef  {    // def type
    TokenKind type_kind;
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

struct IdentExpr{     // ident
    std::string_view value;
};


struct PathExpr { 
    std::vector<std::string_view> segments;  // namespace::object
};

struct UnaryExpr {    // -x
    std::unique_ptr<Expr> operand;
    TokenKind op;     
}; 

struct BinaryExpr { 
    std::unique_ptr<Expr> lhs, rhs;
    TokenKind op;
};

struct GroupExpr {     // (1 + 2)
    std::unique_ptr<Expr> inner;
};

struct CastExpr {   // int32 as float32
    std::unique_ptr<Expr> expr;
    TypeRef target;
};

struct CallExpr { // f(1, 2)
    std::unique_ptr<Expr> call;
    std::vector<Expr> args;
};

struct IndexExpr {  // arr[2]
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
};

struct FieldExpr {  // struct.struct_obj
    std::unique_ptr<Expr> object;
    std::string_view field;
};

struct ArrayLitExpr{    //  [1, 2]
    std::vector<Expr> elems;
};


struct StructLitExpr { // структура
    std::string_view name;
    std::vector<std::pair<std::string_view, std::unique_ptr<Expr>>> fields;
};

struct Expr { 
    std::variant<LitIntExpr, LitFloatExpr, LitBoolExpr, LitStringExpr,
    PathExpr, IdentExpr, BinaryExpr, UnaryExpr, GroupExpr, CastExpr, CallExpr,
    IndexExpr, FieldExpr, ArrayLitExpr, StructLitExpr > node;
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
};


struct FnDecl {             // fn(param1, ...) -> type {}
    std::string_view name;
    std::vector<std::pair<std::string_view, TypeRef>> params;
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
};