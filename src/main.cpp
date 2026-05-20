import std;
import Ferrous.Token;
import Ferrous.Lexer;
import Ferrous.Parser;

using Lexer::TokenKind;

static std::string_view kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::Ident:         return "Ident";
        case TokenKind::KwLet:         return "KwLet";
        case TokenKind::KwMut:         return "KwMut";
        case TokenKind::KwFn:          return "KwFn";
        case TokenKind::KwReturn:      return "KwReturn";
        case TokenKind::KwIf:          return "KwIf";
        case TokenKind::KwElse:        return "KwElse";
        case TokenKind::KwWhile:       return "KwWhile";
        case TokenKind::KwBreak:       return "KwBreak";
        case TokenKind::KwContinue:    return "KwContinue";
        case TokenKind::KwStruct:      return "KwStruct";
        case TokenKind::KwType:        return "KwType";
        case TokenKind::KwNamespace:   return "KwNamespace";
        case TokenKind::KwAs:          return "KwAs";
        case TokenKind::KwTrue:        return "KwTrue";
        case TokenKind::KwFalse:       return "KwFalse";
        case TokenKind::KwVoid:        return "KwVoid";
        case TokenKind::KwInt8:        return "KwInt8";
        case TokenKind::KwInt16:       return "KwInt16";
        case TokenKind::KwInt32:       return "KwInt32";
        case TokenKind::KwInt64:       return "KwInt64";
        case TokenKind::KwUint8:       return "KwUint8";
        case TokenKind::KwUint16:      return "KwUint16";
        case TokenKind::KwUint32:      return "KwUint32";
        case TokenKind::KwUint64:      return "KwUint64";
        case TokenKind::KwFloat32:     return "KwFloat32";
        case TokenKind::KwFloat64:     return "KwFloat64";
        case TokenKind::KwBool:        return "KwBool";
        case TokenKind::KwString:      return "KwString";
        case TokenKind::LitInt:        return "LitInt";
        case TokenKind::LitFloat:      return "LitFloat";
        case TokenKind::LitString:     return "LitString";
        case TokenKind::SepSemicolon:  return "SepSemicolon";
        case TokenKind::SepComma:      return "SepComma";
        case TokenKind::SepColon:      return "SepColon";
        case TokenKind::SepDot:        return "SepDot";
        case TokenKind::SepLBrace:     return "SepLBrace";
        case TokenKind::SepRBrace:     return "SepRBrace";
        case TokenKind::SepLBracket:   return "SepLBracket";
        case TokenKind::SepRBracket:   return "SepRBracket";
        case TokenKind::SepLParen:     return "SepLParen";
        case TokenKind::SepRParen:     return "SepRParen";
        case TokenKind::SepArrow:      return "SepArrow";
        case TokenKind::SepColonColon: return "SepColonColon";
        case TokenKind::OpPlus:        return "OpPlus";
        case TokenKind::OpMinus:       return "OpMinus";
        case TokenKind::OpStar:        return "OpStar";
        case TokenKind::OpSlash:       return "OpStash";
        case TokenKind::OpPercent:     return "OpPercent";
        case TokenKind::OpEq:          return "OpEq";
        case TokenKind::OpEqEq:        return "OpEqEq";
        case TokenKind::OpBangEq:      return "OpBangEq";
        case TokenKind::OpLt:          return "OpLt";
        case TokenKind::OpLtEq:        return "OpLtEq";
        case TokenKind::OpGt:          return "OpGt";
        case TokenKind::OpGtEq:        return "OpGtEq";
        case TokenKind::OpAndAnd:      return "OpAndAnd";
        case TokenKind::OpOrOr:        return "OpOrOr";
        case TokenKind::OpBang:        return "OpBang";
        case TokenKind::Eof:           return "Eof";
        case TokenKind::Undefined:     return "undefined";
    }
    return "?";
}



static std::string_view op_to_str(TokenKind k) {
    switch (k) {
        case TokenKind::OpPlus:    return "+";
        case TokenKind::OpMinus:   return "-";
        case TokenKind::OpStar:    return "*";
        case TokenKind::OpSlash:   return "/";
        case TokenKind::OpPercent: return "%";
        case TokenKind::OpEq:      return "=";
        case TokenKind::OpEqEq:    return "==";
        case TokenKind::OpBangEq:  return "!=";
        case TokenKind::OpLt:      return "<";
        case TokenKind::OpLtEq:    return "<=";
        case TokenKind::OpGt:      return ">";
        case TokenKind::OpGtEq:    return ">=";
        case TokenKind::OpAndAnd:  return "&&";
        case TokenKind::OpOrOr:    return "||";
        case TokenKind::OpBang:    return "!";
        default:                   return "?";
    }
}

static void print_indent(std::ostream& os, int depth) {
    for (int i = 0; i < depth; ++i) os << "  ";
}

static void print_type(const Parser::TypeRef& t, std::ostream& os, int depth) {
    print_indent(os, depth);
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, Parser::BuiltinTypeRef>) {
            os << "Builtin " << kind_name(n.type_kind) << '\n';
        } else if constexpr (std::is_same_v<T, Parser::NamedTypeRef>) {
            os << "Named " << n.name << '\n';
        } else if constexpr (std::is_same_v<T, Parser::ArrayTypeRef>) {
            os << "Array size=" << n.size << '\n';
            print_indent(os, depth + 1); os << "elem:\n";
            print_type(*n.elem, os, depth + 2);
        }
    }, t.node);
}

static void print_expr(const Parser::Expr& e, std::ostream& os, int depth) {
    print_indent(os, depth);
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, Parser::LitIntExpr>) {
            os << "LitInt " << n.value << '\n';
        } else if constexpr (std::is_same_v<T, Parser::LitFloatExpr>) {
            os << "LitFloat " << n.value << '\n';
        } else if constexpr (std::is_same_v<T, Parser::LitBoolExpr>) {
            os << "LitBool " << (n.value ? "true" : "false") << '\n';
        } else if constexpr (std::is_same_v<T, Parser::LitStringExpr>) {
            os << "LitString \"" << n.value << "\"\n";
        } else if constexpr (std::is_same_v<T, Parser::IdentExpr>) {
            os << "Ident " << n.value << '\n';
        } else if constexpr (std::is_same_v<T, Parser::BinaryExpr>) {
            os << "Binary " << op_to_str(n.op) << '\n';
            print_indent(os, depth + 1); os << "lhs:\n";
            print_expr(*n.lhs, os, depth + 2);
            print_indent(os, depth + 1); os << "rhs:\n";
            print_expr(*n.rhs, os, depth + 2);
        } else if constexpr (std::is_same_v<T, Parser::UnaryExpr>) {
            os << "Unary " << op_to_str(n.op) << '\n';
            print_indent(os, depth + 1); os << "operand:\n";
            print_expr(*n.operand, os, depth + 2);
        } else if constexpr (std::is_same_v<T, Parser::GroupExpr>) {
            os << "Group\n";
            print_indent(os, depth + 1); os << "inner:\n";
            print_expr(*n.inner, os, depth + 2);
        } else if constexpr (std::is_same_v<T, Parser::CallExpr>) {
            os << "Call\n";
            print_indent(os, depth + 1); os << "callee:\n";
            print_expr(*n.call, os, depth + 2);
            print_indent(os, depth + 1); os << "args (" << n.args.size() << "):\n";
            for (std::size_t i = 0; i < n.args.size(); ++i) {
                print_indent(os, depth + 2); os << "[" << i << "]:\n";
                print_expr(n.args[i], os, depth + 3);
            }
        } else if constexpr (std::is_same_v<T, Parser::IndexExpr>) {
            os << "Index\n";
            print_indent(os, depth + 1); os << "array:\n";
            print_expr(*n.array, os, depth + 2);
            print_indent(os, depth + 1); os << "index:\n";
            print_expr(*n.index, os, depth + 2);
        } else if constexpr (std::is_same_v<T, Parser::FieldExpr>) {
            os << "Field " << n.field << '\n';
            print_indent(os, depth + 1); os << "object:\n";
            print_expr(*n.object, os, depth + 2);
        } else if constexpr (std::is_same_v<T, Parser::PathExpr>) {
            os << "Path ";
            for (std::size_t i = 0; i < n.segments.size(); ++i) {
                if (i) os << "::";
                os << n.segments[i];
            }
            os << '\n';
        } else if constexpr (std::is_same_v<T, Parser::CastExpr>) {
            os << "Cast\n";
            print_indent(os, depth + 1); os << "expr:\n";
            print_expr(*n.expr, os, depth + 2);
            print_indent(os, depth + 1); os << "target:\n";
            print_type(n.target, os, depth + 2);
        } else {
            os << "<unsupported expr variant>\n";
        }
    }, e.node);
}

int main(int argc, char **argv) {
    bool dump_tokens = false;
    bool dump_type   = false;
    bool dump_expr   = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if      (a == "--dump-tokens") dump_tokens = true;
        else if (a == "--dump-type")   dump_type   = true;
        else if (a == "--dump-expr")   dump_expr   = true;
        else                           path = argv[i];
    }

    if (!path) {
        std::cerr << "usage: Ferrous [--dump-tokens|--dump-type|--dump-expr] <file.fer>\n";
        return 1;
    }

    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open: " << path << "\n";
        return 1;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    Lexer::Lexer lex(source);
    auto tokens = lex.tokenize();

    if (dump_tokens) {
        for (const auto& t : tokens) {
            std::cout << t.line << ':' << t.column << '\t'
                      << kind_name(t.kind) << "\t'" << t.lexeme << "'\n";
        }
        return 0;
    }

    if (dump_type) {
        Parser::Parser p(std::move(tokens));
        Parser::TypeRef t = p.debug_parse_type();
        print_type(t, std::cout, 0);
        return 0;
    }

    if (dump_expr) {
        Parser::Parser p(std::move(tokens));
        Parser::Expr e = p.debug_parse_expr();
        print_expr(e, std::cout, 0);
        return 0;
    }

    return 0;
}
