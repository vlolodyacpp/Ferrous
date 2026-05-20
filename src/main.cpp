import std;
import Ferrous.Token;
import Ferrous.Lexer;
import Ferrous.Parser;
import Ferrous.Printer;

int main(int argc, char **argv) {
    bool dump_tokens = false;
    bool dump_ast    = false;
    const char* path = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if      (a == "--dump-tokens") dump_tokens = true;
        else if (a == "--dump-ast")   dump_ast   = true;
        else                           path = argv[i];
    }

    if (!path) {
        std::cerr << "usage: Ferrous [--dump-tokens|--dump-expr] <file.fer>\n";
        return 1;
    }

    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open: " << path << std::endl;
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
                      << Printer::kind_name(t.kind) << "\t'" << t.lexeme << "'\n";
        }
        return 0;
    }

    if (dump_ast) {
        Parser::Parser p(std::move(tokens));
        auto decls = p.parse();
        for (const auto& decl : decls) {
            Printer::print_decl(decl, std::cout, 0);
        }
        return 0;
    }

    return 0;
}
