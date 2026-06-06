module;
#include <ostream>
#include <string_view>
#include <vector>

export module Ferrous.Printer;
export import Ferrous.Token;
export import Ferrous.AST;
export import Ferrous.Semantic;

export namespace Printer {
    std::string_view kind_name(Lexer::TokenKind);
    void print_tokens(const std::vector<Lexer::Token>&, std::ostream&);
    void print_type (const Parser::TypeRef&,  std::ostream&, int depth = 0);
    void print_expr (const Parser::Expr&,     std::ostream&, int depth = 0,
        const Semantic::AnnotatedAST* annotations = nullptr);
    void print_stmt (const Parser::Stmt&,     std::ostream&, int depth = 0,
        const Semantic::AnnotatedAST* annotations = nullptr);
    void print_block(const Parser::BlockStmt&,std::ostream&, int depth = 0,
        const Semantic::AnnotatedAST* annotations = nullptr);
    void print_decl (const Parser::Decl&,     std::ostream&, int depth = 0,
        const Semantic::AnnotatedAST* annotations = nullptr);
    void print_decls_with_types(const std::vector<Parser::Decl>&,
        const Semantic::AnnotatedAST&, std::ostream&);
}
