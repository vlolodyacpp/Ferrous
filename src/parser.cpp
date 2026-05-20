module Ferrous.Parser;

import std;

namespace Parser {
    namespace {
        using TokenKind = Lexer::TokenKind;
        const std::unordered_set<Lexer::TokenKind> builtin_type = {
            TokenKind::KwInt8, TokenKind::KwInt16, TokenKind::KwInt32, TokenKind::KwInt64,
            TokenKind::KwUint8, TokenKind::KwUint16, TokenKind::KwUint32, TokenKind::KwUint64,
            TokenKind::KwFloat32, TokenKind::KwFloat64, TokenKind::KwBool, TokenKind::KwString, TokenKind::KwVoid,
        };
        bool is_builtin_type(Lexer::TokenKind k) {
            if (builtin_type.contains(k)) return true;
            return false;
        }
    }

    // вспомогательные ф-ции

    bool Parser::is_end() const {
        return tokens[pos].kind == Lexer::TokenKind::Eof;
    }

    Lexer::Token Parser::peek() const {
        return tokens[pos];
    }

    Lexer::Token Parser::peek_next() const {
        if (pos + 1 >= tokens.size()) {
            return tokens.back();
        }
        return tokens[pos + 1];
    }

    Lexer::Token Parser::advance() {
        Lexer::Token cur = tokens[pos];
        if (!is_end()) {
            ++pos;
        }
        return cur;
    }

    bool Parser::check(TokenKind k) const {
        return tokens[pos].kind == k;
    }

    bool Parser::match(TokenKind k) {
        if (!check(k)) return false;
        advance();
        return true;
    }

    Lexer::Token Parser::expect(TokenKind k) {
        if (!check(k)) {
            return Lexer::Token{TokenKind::Eof, "err"}; // временная заглушка
        }
        return advance();
    }

    // вспомогательные ф-ции для парсинга выражений

    int Parser::get_infix_prec(TokenKind kind) {
        switch (kind) {
            case TokenKind::OpEq:
                return 1;
            case TokenKind::OpOrOr:
                return 2;
            case TokenKind::OpAndAnd:
                return 3;
            case TokenKind::OpEqEq:
            case TokenKind::OpBangEq:
                return 4;
            case TokenKind::OpGt:
            case TokenKind::OpLt:
            case TokenKind::OpLtEq:
            case TokenKind::OpGtEq:
                return 5;
            case TokenKind::OpPlus:
            case TokenKind::OpMinus:
                return 6;
            case TokenKind::OpStar:
            case TokenKind::OpSlash:
                return 7;
            default:
                return 0;
        }
    }

    bool Parser::is_right_assoc(TokenKind k) {
        if (k == TokenKind::OpEq) return true;
        return false;
    }


    // парсинг типов
    TypeRef Parser::parse_type() {
        const auto t = peek();
        if (is_builtin_type(t.kind)) {
            advance();
            return TypeRef{BuiltinTypeRef{t.kind}};
        }
        if (t.kind == TokenKind::Ident) {
            advance();
            return TypeRef{NamedTypeRef{t.lexeme}};
        }
        if (t.kind == TokenKind::SepLBracket) {
            advance();
            TypeRef elem = parse_type();

            expect(TokenKind::SepComma);
            Lexer::Token token = expect(TokenKind::LitInt);

            expect(TokenKind::SepRBracket);
            return TypeRef{ArrayTypeRef{
                std::make_unique<TypeRef>(std::move(elem)),
                token.lexeme
            }};
        }

        std::cerr << "parse error at" << t.line << ":" << t.column <<
            "expected type, got `" << t.lexeme << "`\n";
        return TypeRef{BuiltinTypeRef{TokenKind::Undefined}};
    }

    // парсинг выражений
    Expr Parser::parse_prefix() {
        const auto t = peek();

        if (t.kind == TokenKind::SepLParen) {
            advance();
            auto inner = parse_expr(0);
            expect(TokenKind::SepRParen);
            return parse_postfix(Expr {GroupExpr{std::make_unique<Expr>(std::move(inner))}});
        }
        if (t.kind == TokenKind::OpMinus || t.kind == TokenKind::OpBang) {
            advance();
            auto inner = parse_prefix();
            return parse_postfix(Expr {UnaryExpr{
                std::make_unique<Expr>(std::move(inner)),
                t.kind
            }});
        }
        if (t.kind == TokenKind::LitInt) {
            advance();
            return parse_postfix(Expr{LitIntExpr{t.lexeme}});
        }
        if (t.kind == TokenKind::LitFloat) {
            advance();
            return parse_postfix(Expr{LitFloatExpr{t.lexeme}});
        }
        if (t.kind == TokenKind::LitString) {
            advance();
            return parse_postfix(Expr{LitStringExpr{t.lexeme}});
        }
        if (t.kind == TokenKind::KwTrue) {
            advance();
            return parse_postfix(Expr{LitBoolExpr{true}});
        }
        if (t.kind == TokenKind::KwFalse) {
            advance();
            return parse_postfix(Expr{LitBoolExpr{false}});
        }
        if (t.kind == TokenKind::Ident) {
            advance();
            return parse_postfix(Expr{IdentExpr{t.lexeme}});
        }


        std::cerr << "parse error at" << t.line << ":" << t.column <<
            "expected prefix, got `" << t.lexeme << "`\n";
        return Expr{LitIntExpr{"0"}};
    }

    Expr Parser::parse_postfix(Expr lhs) {
        while (true) {
            auto k = peek().kind;
            if (k == TokenKind::SepLParen) {
                // f(a, b, c)
                advance();
                std::vector<Expr> args;
                if (!check(TokenKind::SepRParen)) {
                    args.push_back(parse_expr(0));
                    while (match(TokenKind::SepComma)) {
                        args.push_back(parse_expr(0));
                    }
                }
                expect(TokenKind::SepRParen);
                lhs = Expr{CallExpr{
                    std::make_unique<Expr>(std::move(lhs)),
                    std::move(args)
                }};
                continue;
            }
            if (k == TokenKind::SepLBracket) {
                // arr[2]
                advance();
                auto index = parse_expr(0);
                expect(TokenKind::SepRBracket);
                lhs = Expr{IndexExpr{
                    std::make_unique<Expr>(std::move(lhs)),
                    std::make_unique<Expr>(std::move(index)),
                }};
                continue;
            }
            if (k == TokenKind::SepDot) {
                // obj.field
                advance();
                auto name = expect(TokenKind::Ident);
                lhs = Expr{FieldExpr{
                    std::make_unique<Expr>(std::move(lhs)),
                    name.lexeme
                }};
                continue;
            }
            if (k == TokenKind::SepColonColon) {
                // ns::foo  →  PathExpr
                // лежащий в lhs должен быть IdentExpr или уже PathExpr
                std::vector<std::string_view> segments;

                if (auto* id = std::get_if<IdentExpr>(&lhs.node)) {
                    segments.push_back(id->value);
                } else if (auto* path = std::get_if<PathExpr>(&lhs.node)) {
                    segments = std::move(path->segments);
                } else {
                    std::cerr << "parse error at " << peek().line << ":" <<
                    peek().column << ": '::' applies only to identifiers\n";
                    return lhs;
                }

                while (match(TokenKind::SepColonColon)) {
                    Lexer::Token seg = expect(TokenKind::Ident);
                    segments.push_back(seg.lexeme);
                }
                lhs = Expr{PathExpr{std::move(segments)}};
                continue;
            }
            if (k == TokenKind::KwAs) {
                // x as Type
                advance();
                auto target = parse_type();
                lhs = Expr{CastExpr{
                    std::make_unique<Expr>(std::move(lhs)),
                    std::move(target)
                }};
                continue;
            }
            return lhs;
        }
    }

    // парсинг инструкций
    Expr Parser::parse_expr(int min_prec) {
        Expr lhs = parse_prefix();
        while (true) {
            const auto op_kind = peek().kind;
            int prec = get_infix_prec(op_kind);

            if (prec == 0 || prec < min_prec) {
                break;
            }
            advance();
            int next_min = is_right_assoc(op_kind) ? prec : prec + 1;
            Expr rhs = parse_expr(next_min);
            BinaryExpr bin{
                std::make_unique<Expr>(std::move(lhs)),
                std::make_unique<Expr>(std::move(rhs)),
                op_kind
            };
            lhs = Expr{std::move(bin)};
        }
        return lhs;
    }


    Stmt Parser::parse_stmt() {
        switch (const auto token = peek(); token.kind) {
            case TokenKind::KwLet:
                return Stmt {parse_let()};
            case TokenKind::KwReturn:
                return Stmt {parse_return()};
            case TokenKind::KwIf:
                return Stmt {parse_if()};
            case TokenKind::KwWhile:
                return Stmt {parse_while()};
            case TokenKind::KwBreak:
                advance();
                expect(TokenKind::SepSemicolon);
                return Stmt{BreakStmt {}};
            case TokenKind::KwContinue:
                advance();
                expect(TokenKind::SepSemicolon);
                return Stmt{ContinueStmt {}};
            case TokenKind::SepLBrace:
                return Stmt{parse_block()};
            case TokenKind::SepSemicolon:
                advance();
                return Stmt{NullStmt {}};
            default:
                Expr e = parse_expr(0);
                expect(TokenKind::SepSemicolon);
                return Stmt{ExprStmt{
                    std::make_unique<Expr>(std::move(e))
                }};
        }
    }

    LetStmt Parser::parse_let() {
        // "let" [ "mut" ] IDENT [ ":" Type ] "=" Expr ";" ;
        expect(TokenKind::KwLet);
        bool is_mut = match(TokenKind::KwMut);
        Lexer::Token name = expect(TokenKind::Ident);

        std::optional<TypeRef> type;
        if (match(TokenKind::SepColon)) {
            type = parse_type();
        }
        expect(TokenKind::OpEq);
        Expr init = parse_expr(0);
        expect(TokenKind::SepSemicolon);
        return LetStmt{
            is_mut,
            name.lexeme,
            std::move(type),
            std::make_unique<Expr>(std::move(init))
        };
    }

    ReturnStmt Parser::parse_return() {
        // "return" [ Expr ] ";" ;
        expect(TokenKind::KwReturn);
        if (match(TokenKind::SepSemicolon)) {
            return ReturnStmt {std::nullopt};
        }
        Expr e = parse_expr(0);
        expect(TokenKind::SepSemicolon);
        return ReturnStmt{std::move(e)};
    }


    BlockStmt Parser::parse_block() {
        // "{" { Stmt } "}" ;
        expect(TokenKind::SepLBrace);
        std::vector<Stmt> elems;
        while (!check(TokenKind::SepRBrace) && !is_end()) {
            elems.push_back(parse_stmt());
        }
        expect(TokenKind::SepRBrace);
        return BlockStmt{std::move(elems)};
    }


    IfStmt Parser::parse_if() {
        // "if" ExprNoStruct Block [ "else" ( IfStmt | Block ) ] ;
        expect(TokenKind::KwIf);

        auto conditional = parse_expr(0);
        auto then_body = parse_block();
        auto then_stmt = std::make_unique<Stmt>(Stmt{std::move(then_body)});
        std::unique_ptr<Stmt> else_stmt = nullptr;

        if (match(TokenKind::KwElse)) {
            if (check(TokenKind::KwIf)) {
                auto else_if_block = parse_if();
                else_stmt = std::make_unique<Stmt>(std::move(else_if_block));
            } else {
                auto else_block = parse_block();
                else_stmt = std::make_unique<Stmt>(std::move(else_block));
            }
        }

        return IfStmt{
            std::make_unique<Expr>(std::move(conditional)),
            std::move(then_stmt),
            std::move(else_stmt)
        };

    }

    WhileStmt Parser::parse_while() {
        // "while" ExprNoStruct Block;
        expect(TokenKind::KwWhile);

        auto conditional = parse_expr(0);
        auto then_body = parse_block();
        auto then_stmt = std::make_unique<Stmt>(Stmt{std::move(then_body)});

        return WhileStmt{
            std::make_unique<Expr>(std::move(conditional)),
            std::move(then_stmt),
        };
    }

    // FnDecl Parser::parse_fn() {
    //     expect(TokenKind::KwFn);
    //     auto name = expect(TokenKind::Ident);
    //
    //     expect(TokenKind::SepLParen);
    //     std::vector<std::pair<std::string, std::string>> params;
    //     while (!check(TokenKind::SepRParen) && !is_end()) {
    //         params.push_back();
    //     }
    // }





    std::vector<Decl> Parser::parse() {
        return {};
    }

} // namespace Parser
