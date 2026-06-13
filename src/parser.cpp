module;
#include <format>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

module Ferrous.Parser;

namespace Parser {
    namespace {
        using TokenKind = Lexer::TokenKind;
        const std::unordered_set builtin_type = {
            TokenKind::KwInt8, TokenKind::KwInt16, TokenKind::KwInt32, TokenKind::KwInt64,
            TokenKind::KwUint8, TokenKind::KwUint16, TokenKind::KwUint32, TokenKind::KwUint64,
            TokenKind::KwFloat32, TokenKind::KwFloat64, TokenKind::KwBool, TokenKind::KwString, TokenKind::KwVoid,
            TokenKind::KwChar,
        };
        bool is_builtin_type(Lexer::TokenKind k) {
            if (builtin_type.contains(k)) return true;
            return false;
        }

        // человекочитаемая метка ожидаемого токена для диагностики
        std::string_view token_label(TokenKind k) {
            switch (k) {
                case TokenKind::SepSemicolon: return "`;`";
                case TokenKind::SepColon:     return "`:`";
                case TokenKind::SepColonColon:return "`::`";
                case TokenKind::SepComma:     return "`,`";
                case TokenKind::SepLParen:    return "`(`";
                case TokenKind::SepRParen:    return "`)`";
                case TokenKind::SepLBrace:    return "`{`";
                case TokenKind::SepRBrace:    return "`}`";
                case TokenKind::SepLBracket:  return "`[`";
                case TokenKind::SepRBracket:  return "`]`";
                case TokenKind::OpEq:         return "`=`";
                case TokenKind::SepArrow:     return "`->`";
                case TokenKind::Ident:        return "identifier";
                case TokenKind::LitInt:       return "integer literal";
                case TokenKind::KwLet:        return "`let`";
                case TokenKind::KwReturn:     return "`return`";
                default:                      return "token";
            }
        }
    }

    // вспомогательные ф-ции

    bool Parser::is_end() const {
        return tokens[pos].kind == TokenKind::Eof;
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
            const auto t = peek();
            Lexer::Token where = t;
            if (pos > 0) {
                const auto& prev = tokens[pos - 1];
                where.line = prev.line;
                where.column = prev.column + prev.lexeme.size();
            }
            report_error(where, std::format("expected {}, got `{}`", token_label(k), t.lexeme));
            return Lexer::Token{TokenKind::Eof, "err", where.line, where.column};
        }
        return advance();
    }

    void Parser::report_error(const Lexer::Token& t, std::string_view message) {
        // panic-mode: после первой ошибки молчим, пока не синхронизируемся на границе инструкции 
        if (panicking) {
            return;
        }
        panicking = true;
        if (diag) {
            diag->error(t.line, t.column, std::string(message));
        }
    }

    // пропустить токены до надёжной точки перезапуска
    void Parser::synchronize() {
        panicking = false;
        while (!is_end()) {
            if (pos > 0 && tokens[pos - 1].kind == TokenKind::SepSemicolon) {
                return;                       // только что прошли конец инструкции
            }
            switch (peek().kind) {
                case TokenKind::KwLet:
                case TokenKind::KwReturn:
                case TokenKind::KwIf:
                case TokenKind::KwWhile:
                case TokenKind::KwBreak:
                case TokenKind::KwContinue:
                case TokenKind::KwFn:
                case TokenKind::KwStruct:
                case TokenKind::KwType:
                case TokenKind::KwNamespace:
                case TokenKind::SepLBrace:
                case TokenKind::SepRBrace:
                    return;                   // начало нового стейтмента/декла или конец блока
                default:
                    advance();
            }
        }
    }

    // вспомогательные ф-ции для парсинга выражений

    int Parser::get_infix_prec(TokenKind kind) {
        // Чем больше число — тем крепче связывает.
        switch (kind) {
            case TokenKind::OpEq:        // присваивание — самое слабое (право-ассоц.)
                return 1;
            case TokenKind::OpOrOr:
                return 2;
            case TokenKind::OpAndAnd:
                return 3;
            case TokenKind::OpPipe:      // |
                return 4;
            case TokenKind::OpCaret:     // ^
                return 5;
            case TokenKind::OpAmp:       // &
                return 6;
            case TokenKind::OpEqEq:
            case TokenKind::OpBangEq:
                return 7;
            case TokenKind::OpGt:
            case TokenKind::OpLt:
            case TokenKind::OpLtEq:
            case TokenKind::OpGtEq:
                return 8;
            case TokenKind::OpShl:       // << >>
            case TokenKind::OpShr:
                return 9;
            case TokenKind::OpPlus:
            case TokenKind::OpMinus:
                return 10;
            case TokenKind::OpStar:
            case TokenKind::OpSlash:
            case TokenKind::OpPercent:
                return 11;
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

            expect(TokenKind::SepSemicolon);
            Lexer::Token token = expect(TokenKind::LitInt);

            expect(TokenKind::SepRBracket);
            return TypeRef{ArrayTypeRef{
                std::make_unique<TypeRef>(std::move(elem)),
                token.lexeme
            }};
        }

        report_error(t, std::format("expected type, got `{}`", t.lexeme));
        advance();
        return TypeRef{BuiltinTypeRef{TokenKind::Undefined}};
    }

    // парсинг выражений
    Expr Parser::parse_prefix() {
        const auto t = peek();
        const auto t_next  = peek_next();
        // (....)
        if (t.kind == TokenKind::SepLParen) {
            advance();
            auto inner = parse_expr(0);
            expect(TokenKind::SepRParen);
            return parse_postfix(Expr {GroupExpr{std::make_unique<Expr>(std::move(inner))}});
        }
        // -x, !x, ~x  (унарные префиксы; ~ — битовое НЕ, A.1.8)
        if (t.kind == TokenKind::OpMinus || t.kind == TokenKind::OpBang
            || t.kind == TokenKind::OpTilde) {
            advance();
            std::size_t il = peek().line, ic = peek().column;
            auto inner = parse_prefix();
            if (inner.line == 0) { inner.line = il; inner.column = ic; }
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
        if (t.kind == TokenKind::LitChar) {
            advance();
            return parse_postfix(Expr{LitCharExpr{t.lexeme}});
        }
        if (t.kind == TokenKind::KwTrue) {
            advance();
            return parse_postfix(Expr{LitBoolExpr{true}});
        }
        if (t.kind == TokenKind::KwFalse) {
            advance();
            return parse_postfix(Expr{LitBoolExpr{false}});
        }
        if (t.kind == TokenKind::KwNaN) {
            advance();
            return parse_postfix(Expr{LitFloatExpr{"nan"}});
        }
        if (t.kind == TokenKind::KwInf) {
            advance();
            return parse_postfix(Expr{LitFloatExpr{"inf"}});
        }
        if (t.kind == TokenKind::SepLBracket) {
            advance();
            std::vector<Expr> elems;
            if (!check(TokenKind::SepRBracket)) {
                while (true) {
                    elems.push_back(parse_expr(0));
                    if (!match(TokenKind::SepComma)) break;
                    if (check(TokenKind::SepRBracket)) break;
                }
            }
            expect(TokenKind::SepRBracket);
            return parse_postfix(Expr{ArrayLitExpr{std::move(elems)}});
        }
        if (t.kind == TokenKind::Ident) {
            if (!is_conditional && t_next.kind == TokenKind::SepLBrace) {
                return parse_postfix(parse_struct_lit());
            }
            advance();
            return parse_postfix(Expr{IdentExpr{t.lexeme}});
        }



        if (t.kind == TokenKind::Undefined) {
            advance();
            return Expr{ErrorExpr{t}};
        }

        report_error(t, std::format("expected expression, got `{}`", t.lexeme));
        advance();
        return Expr{ErrorExpr{t}};
    }

    Expr Parser::parse_postfix(Expr lhs) {
        while (true) {
            auto k = peek().kind;
            if (k == TokenKind::SepLParen) {
                // f(a, b, c)
                std::size_t call_line = peek().line;
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
                    std::move(args),
                    call_line
                }};
                continue;
            }
            if (k == TokenKind::SepLBracket) {
                // arr[2]
                std::size_t idx_line = peek().line;
                advance();
                auto index = parse_expr(0);
                expect(TokenKind::SepRBracket);
                lhs = Expr{IndexExpr{
                    std::make_unique<Expr>(std::move(lhs)),
                    std::make_unique<Expr>(std::move(index)),
                    idx_line
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
                    report_error(peek(), "'::' applies only to identifiers");
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

    Expr Parser::parse_struct_lit() {
        auto name = expect(TokenKind::Ident);
        expect(TokenKind::SepLBrace);

        std::vector<std::pair<std::string_view, std::unique_ptr<Expr>>> fields;
        if (!check(TokenKind::SepRBrace)) {
            while (true) {
                auto func_name = expect(TokenKind::Ident);
                expect(TokenKind::SepColon);
                Expr val = parse_expr(0);
                fields.emplace_back(
                    func_name.lexeme,
                    std::make_unique<Expr>(std::move(val))
                );

                if (!match(TokenKind::SepComma)) break;
                if (check(TokenKind::SepRBrace)) break;   // trailing comma
            }
        }
        expect(TokenKind::SepRBrace);

        return Expr{ StructLitExpr{ name.lexeme, std::move(fields) } };
    }

    Expr Parser::parse_expr(int min_prec) {
        // позиция начала выражения — для диагностики семантики
        std::size_t start_line = peek().line;
        std::size_t start_col  = peek().column;
        Expr lhs = parse_prefix();
        // листовые/постфиксные выражения позицию ещё не получили — проставляем
        if (lhs.line == 0) { lhs.line = start_line; lhs.column = start_col; }
        while (true) {
            const auto op_kind = peek().kind;
            int prec = get_infix_prec(op_kind);

            if (prec == 0 || prec < min_prec) {
                break;
            }
            std::size_t op_line = peek().line;
            advance();
            int next_min = is_right_assoc(op_kind) ? prec : prec + 1;
            Expr rhs = parse_expr(next_min);
            // позицию lhs нужно запомнить ДО move — бинарное выражение
            // начинается там же, где его левый операнд
            std::size_t ll = lhs.line, lc = lhs.column;
            BinaryExpr bin{
                std::make_unique<Expr>(std::move(lhs)),
                std::make_unique<Expr>(std::move(rhs)),
                op_kind,
                op_line
            };
            lhs = Expr{std::move(bin)};
            lhs.line = ll; lhs.column = lc;
        }
        return lhs;
    }


    Stmt Parser::parse_stmt() {
        const auto token = peek();   // позиция начала инструкции
        Stmt result = [&]() -> Stmt {
            switch (peek().kind) {
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
        }();
        result.line = token.line;
        result.column = token.column;
        return result;
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
            const std::size_t before = pos;
            elems.push_back(parse_stmt());
            if (panicking) {
                synchronize();             // оправиться от ошибки, не плодя каскад
            } else if (pos == before) {
                advance();                 // страховка от зацикливания
            }
        }
        expect(TokenKind::SepRBrace);
        return BlockStmt{std::move(elems)};
    }


    IfStmt Parser::parse_if() {
        // "if" ExprNoStruct Block [ "else" ( IfStmt | Block ) ] ;
        expect(TokenKind::KwIf);

        bool prev = is_conditional;
        is_conditional = true;
        auto conditional = parse_expr(0);
        is_conditional = prev;

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

        bool prev = is_conditional;
        is_conditional = true;
        auto conditional = parse_expr(0);
        is_conditional = prev;

        auto then_body = parse_block();
        auto then_stmt = std::make_unique<Stmt>(Stmt{std::move(then_body)});

        return WhileStmt{
            std::make_unique<Expr>(std::move(conditional)),
            std::move(then_stmt),
        };
    }

    FnDecl Parser::parse_fn() {
        // "fn" IDENT "(" [ ParamList ] ")" [ "->" Type ] Block ;`
        expect(TokenKind::KwFn);
        auto name = expect(TokenKind::Ident);

        expect(TokenKind::SepLParen);

        // Param { "," Param } [ "," ] ;
        std::vector<std::pair<std::string_view, TypeRef>> params;
        if (true) {
            while (!check(TokenKind::SepRParen)) {
                Lexer::Token param_name = expect(TokenKind::Ident);
                expect (TokenKind::SepColon);
                auto param_type = parse_type();
                params.emplace_back(param_name.lexeme, std::move(param_type));

                if (!match(TokenKind::SepComma)) break;
                if (check(TokenKind::SepRParen)) break;
            }
        }

        expect(TokenKind::SepRParen);

        std::optional<TypeRef> return_type;
        if (match(TokenKind::SepArrow)) {
            return_type = parse_type();
        }

        auto body = parse_block();

        return FnDecl{
            name.lexeme,
            std::move(params),
            std::move(return_type),
            std::move(body)
        };
    }

    StructDecl Parser::parse_struct() {

        expect(TokenKind::KwStruct);
        auto name = expect(TokenKind::Ident);
        expect(TokenKind::SepLBrace);

        // Param { "," Param } [ "," ] ;
        std::vector<std::pair<std::string_view, TypeRef>> fields;
        if (!check(TokenKind::SepRBrace)) {
            while (true) {
                Lexer::Token param_name = expect(TokenKind::Ident);
                expect (TokenKind::SepColon);
                auto param_type = parse_type();
                fields.emplace_back(param_name.lexeme, std::move(param_type));

                if (!match(TokenKind::SepComma)) break;
                if (check(TokenKind::SepRBrace)) break;
            }
        }

        expect(TokenKind::SepRBrace);

        return StructDecl{
            name.lexeme,
            std::move(fields)
        };
    }


    TypeAliasDecl Parser::parse_type_alias() {
        // "type" IDENT "=" Type ";" ;
        expect(TokenKind::KwType);
        auto name = expect(TokenKind::Ident);
        expect(TokenKind::OpEq);
        auto type = parse_type();
        expect(TokenKind::SepSemicolon);

        return TypeAliasDecl{
            name.lexeme,
            std::move(type)
        };
    }

    NameSpaceDecl Parser::parse_namespace() {
        // "namespace" IDENT "{" { Decl } "}" ;
        expect(TokenKind::KwNamespace);
        auto name = expect(TokenKind::Ident);
        expect(TokenKind::SepLBrace);

        std::vector<Decl> decls;
        while (!check(TokenKind::SepRBrace) && !is_end()) {
            decls.push_back(parse_decl());
        }
        expect(TokenKind::SepRBrace);
        return NameSpaceDecl{
            name.lexeme,
            std::move(decls)
        };
    }

    Decl Parser::parse_decl() {
        const auto t = peek();   // позиция начала объявления
        Decl result = [&]() -> Decl {
            switch (peek().kind) {
                case TokenKind::KwFn:
                    return Decl{parse_fn()};
                case TokenKind::KwStruct:
                    return Decl{parse_struct()};
                case TokenKind::KwType:
                    return Decl{parse_type_alias()};
                case TokenKind::KwNamespace:
                    return Decl{parse_namespace()};
                default:
                    report_error(t, std::format("expected declaration, got `{}`", t.lexeme));
                    advance();
                    return Decl{FnDecl{}};
            }
        }();
        result.line = t.line;
        result.column = t.column;
        return result;
    }

    std::vector<Decl> Parser::parse() {
        std::vector<Decl> decls;
        while (!is_end()) {
            const std::size_t before = pos;
            decls.push_back(parse_decl());
            if (panicking) {
                synchronize();             // оправиться от ошибки, не плодя каскад
            } else if (pos == before) {
                advance();                 // страховка от зацикливания
            }
        }
        return decls;
    }

} // namespace Parser
