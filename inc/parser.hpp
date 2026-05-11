#pragma once
#include "ast.hpp"
#include "token.hpp"
#include <vector>


class Parser {
public:

    explicit Parser(std::vector<Token> tokens) : tokens(std::move(tokens)), pos(0) {}
    std::vector<Decl> parse();

private:

    std::vector<Token> tokens;
    std::size_t pos = 0;


    Token peek() const;
    Token peek_next() const;
    Token advance();

    bool check(TokenKind) const;
    bool match(TokenKind);
    Token expect(TokenKind);
    bool is_end() const;




    // типы
    TypeRef parse_type();

    // декдарации
    Decl parse_decl();
    FnDecl parse_fn();
    StructDecl parse_struct();
    TypeAliasDecl parse_type_ali();
    NameSpaceDecl parce_namespace();

    // интсрукции
    Stmt parse_stmt();
    LetStmt parse_let();
    BlockStmt parse_block();
    IfStmt parse_if();
    WhileStmt parse_while();
    ReturnStmt parse_return();

    // выражения 
    Expr parse_expr(int);
    Expr parse_prefix();
    Expr parse_postfix();

    int get_infix_prec(TokenKind);
    bool is_right_assoc(TokenKind);

};