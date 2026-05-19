module Ferrous.Parser;

import std;

namespace Parser {
    namespace {
        using K = Lexer::TokenKind;
        const std::unordered_set<Lexer::TokenKind> builtin_type = {
            K::KwInt8, K::KwInt16, K::KwInt32, K::KwInt64,
            K::KwUint8, K::KwUint16, K::KwUint32, K::KwUint64,
            K::KwFloat32, K::KwFloat64, K::KwBool, K::KwString, K::KwVoid,
        };
        bool is_builtin_type(Lexer::TokenKind k) {
            if (builtin_type.contains(k)) return true;
            return false;
        }
    }

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

    bool Parser::check(Lexer::TokenKind k) const {
        return tokens[pos].kind == k;
    }

    bool Parser::match(Lexer::TokenKind k) {
        if (!check(k)) return false;
        advance();
        return true;
    }

    Lexer::Token Parser::expect(Lexer::TokenKind k) {
        if (!check(k)) {
            return Lexer::Token{Lexer::TokenKind::Eof, "err"}; // временная заглушка
        }
        return advance();
    }


    TypeRef Parser::parse_type() {
        const auto t = peek();
        if (is_builtin_type(t.kind)) {
            advance();
            return TypeRef{BuiltinTypeRef{t.kind}};
        }
        if (t.kind == K::Ident) {
            advance();
            return TypeRef{NamedTypeRef{t.lexeme}};
        }
        if (t.kind == K::SepLBracket) {
            advance();
            TypeRef elem = parse_type();

            expect(K::SepComma);
            Lexer::Token token = expect(K::LitInt);

            expect(K::SepRBracket);
            return TypeRef{ArrayTypeRef{
                std::make_unique<TypeRef>(std::move(elem)),
                token.lexeme
            }};
        }

        std::cerr << "parse error at" << t.line << ":" << t.column <<
            "expected type, got `" << t.lexeme << "`\n";
        return TypeRef{BuiltinTypeRef{K::Undefined}};
    }


    Expr Parser::parse_prefix() {
        const auto t = peek();
        if (t.kind == K::LitInt) {
            advance();
            return Expr{LitIntExpr{t.lexeme}};
        }
        if (t.kind == K::LitFloat) {
            advance();
            return Expr{LitFloatExpr{t.lexeme}};
        }
        if (t.kind == K::LitString) {
            advance();
            return Expr{LitStringExpr{t.lexeme}};
        }
        if (t.kind == K::KwTrue) {
            advance();
            return Expr{LitBoolExpr{true}};
        }
        if (t.kind == K::KwFalse) {
            advance();
            return Expr{LitBoolExpr{false}};
        }
        if (t.kind == K::Ident) {
            advance();
            return Expr{IdentExpr{t.lexeme}};
        }

        std::cerr << "parse error at" << t.line << ":" << t.column <<
            "expected prefix, got `" << t.lexeme << "`\n";
        return Expr{LitIntExpr{"0"}};
    }




    std::vector<Decl> Parser::parse() {
        return {};
    }

} // namespace Parser
