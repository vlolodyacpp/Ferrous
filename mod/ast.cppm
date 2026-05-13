export module Ferrous.AST;

import std;
import Ferrous.Token;

export struct TypeRef;
export struct Expr; 
export struct Stmt;
export struct Decl;


export struct BuiltinTypeRef  {    // def type
    TokenKind type_kind;
};

export struct NamedTypeRef {       // own types
    std::string_view name;
}; 

export struct ArrayTypeRef {      // type in array // let arr: [int32, 5] = ...
    std::unique_ptr<TypeRef> elem;
    std::string_view size;
};

export struct TypeRef { 
    std::variant<BuiltinTypeRef, NamedTypeRef, ArrayTypeRef> node;
};



export struct LitIntExpr {
    std::string_view value;  // int число
};

export struct LitFloatExpr { 
    std::string_view value;     //float число
};

export struct LitBoolExpr { 
    bool value;             // bool
};

export struct LitStringExpr { // "string"
    std::string_view value;
};

export struct IdentExpr{     // ident
    std::string_view value;
};


export struct PathExpr { 
    std::vector<std::string_view> segments;  // namespace::object
};

export struct UnaryExpr {    // -x
    std::unique_ptr<Expr> operand;
    TokenKind op;     
}; 

export struct BinaryExpr { 
    std::unique_ptr<Expr> lhs, rhs;
    TokenKind op;
};

export struct GroupExpr {     // (1 + 2)
    std::unique_ptr<Expr> inner;
};

export struct CastExpr {   // int32 as float32
    std::unique_ptr<Expr> expr;
    TypeRef target;
};

export struct CallExpr { // f(1, 2)
    std::unique_ptr<Expr> call;
    std::vector<Expr> args;
};

export struct IndexExpr {  // arr[2]
    std::unique_ptr<Expr> array;
    std::unique_ptr<Expr> index;
};

export struct FieldExpr {  // struct.struct_obj
    std::unique_ptr<Expr> object;
    std::string_view field;
};

export struct ArrayLitExpr{    //  [1, 2]
    std::vector<Expr> elems;
};


export struct StructLitExpr { // структура
    std::string_view name;
    std::vector<std::pair<std::string_view, std::unique_ptr<Expr>>> fields;
};

export struct Expr { 
    std::variant<LitIntExpr, LitFloatExpr, LitBoolExpr, LitStringExpr,
    PathExpr, IdentExpr, BinaryExpr, UnaryExpr, GroupExpr, CastExpr, CallExpr,
    IndexExpr, FieldExpr, ArrayLitExpr, StructLitExpr > node;
};



export struct LetStmt {        // let x: int32 = 5
    bool is_mut;
    std::string_view name;
    std::optional<TypeRef> type;
    std::unique_ptr<Expr> expr_init;
};

export struct ExprStmt {       // a = 1 + b;     
    std::unique_ptr<Expr> expr;
}; 


export struct BlockStmt {     // {}
    std::vector<Stmt> elems;      
}; 

export struct IfStmt {         // if
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_body;
    std::unique_ptr<Stmt> else_body;
};

export struct WhileStmt {      // while
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

export struct ReturnStmt {     // return
    std::optional<Expr> value;
};


export struct BreakStmt {};
export struct ContinueStmt {};
export struct NullStmt {};

export struct Stmt { 
    std::variant<LetStmt, ExprStmt, BlockStmt, IfStmt, WhileStmt,
     ReturnStmt, BreakStmt, ContinueStmt, NullStmt> node;
};


export struct FnDecl {             // fn(param1, ...) -> type {}
    std::string_view name;
    std::vector<std::pair<std::string_view, TypeRef>> params;
    std::optional<TypeRef> return_type;
    BlockStmt body;
};

export struct StructDecl {     // struct name_struct { field: type, ... }
    std::string_view name;
    std::vector<std::pair<std::string_view, TypeRef>> field;
        
};

export struct TypeAliasDecl { // type name_type = type;
    std::string_view name;
    TypeRef type;
}; 

export struct NameSpaceDecl { // namespace name {}
    std::string_view name;
    std::vector<Decl> decls;    
}; 

export struct Decl { 
    std::variant<FnDecl, StructDecl, TypeAliasDecl, NameSpaceDecl> node;
};