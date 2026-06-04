export module Ferrous.Parser;

import std;
export import Ferrous.AST;
export import Ferrous.Token;
export import Ferrous.Semantic;

export namespace Parser {

    class Parser {
    public:

        explicit Parser(std::vector<Lexer::Token> tokens, Semantic::DiagBag& bag)
            : tokens(std::move(tokens)), pos(0), diag(&bag) {}
        explicit Parser(std::vector<Lexer::Token> tokens)
            : tokens(std::move(tokens)), pos(0), diag(nullptr) {}
        std::vector<Decl> parse();

        // для отладки(временно)
        TypeRef debug_parse_type() { return parse_type(); }
        Expr    debug_parse_expr() { return parse_expr(0); }
        Stmt    debug_parse_stmt() { return parse_stmt(); }

    private:

        std::vector<Lexer::Token> tokens;
        std::size_t pos = 0;
        bool is_conditional = false;
        Semantic::DiagBag* diag = nullptr;

        Lexer::Token peek() const;
        Lexer::Token peek_next() const;
        Lexer::Token advance();

        bool check(Lexer::TokenKind) const;
        bool match(Lexer::TokenKind);
        Lexer::Token expect(Lexer::TokenKind);
        bool is_end() const;

        void report_error(const Lexer::Token&, std::string_view);




        // типы
        TypeRef parse_type();

        // декларации
        Decl parse_decl();
        FnDecl parse_fn();
        StructDecl parse_struct();
        TypeAliasDecl parse_type_alias();
        NameSpaceDecl parse_namespace();

        // инструкции
        Stmt parse_stmt();
        LetStmt parse_let();
        BlockStmt parse_block();
        IfStmt parse_if();
        WhileStmt parse_while();
        ReturnStmt parse_return();

        // выражения
        Expr parse_expr(int);
        Expr parse_prefix();
        Expr parse_postfix(Expr);
        Expr parse_struct_lit();

        int get_infix_prec(Lexer::TokenKind);
        bool is_right_assoc(Lexer::TokenKind);

    };

} // namespace Parser
