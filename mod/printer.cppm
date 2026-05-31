export module Ferrous.Printer;

import std;
export import Ferrous.Token;
export import Ferrous.AST;




export namespace Printer {
    std::string_view kind_name(Lexer::TokenKind);
    void print_tokens(const std::vector<Lexer::Token>&, std::ostream&);
    void print_type (const Parser::TypeRef&,  std::ostream&, int depth = 0);
    void print_expr (const Parser::Expr&,     std::ostream&, int depth = 0);
    void print_stmt (const Parser::Stmt&,     std::ostream&, int depth = 0);
    void print_block(const Parser::BlockStmt&,std::ostream&, int depth = 0);
    void print_decl (const Parser::Decl&,     std::ostream&, int depth = 0);
}