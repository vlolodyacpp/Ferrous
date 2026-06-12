#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

import Ferrous.Token;
import Ferrous.Lexer;
import Ferrous.Parser;
import Ferrous.Printer;
import Ferrous.Semantic;
import Ferrous.Codegen;

int main(int argc, char **argv) {
    bool dump_tokens = false;
    bool dump_ast    = false;
    bool dump_sema   = false;
    const char* path = nullptr;
    const char* output_path = nullptr;

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if      (a == "--dump-tokens") dump_tokens = true;
        else if (a == "--dump-ast")   dump_ast   = true;
        else if (a == "--dump-sema")  dump_sema  = true;
        else if (a == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        }
        else if (!path) {
            path = argv[i];
        }
        else {
            std::cerr << "unexpected argument: " << a << '\n';
            return 1;
        }
    }

    if (!path) {
        std::cerr << "usage: Ferrous [--dump-tokens|--dump-ast|--dump-sema] [-o <out>] <file.fer>\n"
                  << "  (default output: a.out)\n";
        return 1;
    }

    // без -o имя исходника
    std::string derived_output;
    if (!output_path) {
        std::string_view src = path;
        if (auto slash = src.find_last_of("/\\"); slash != std::string_view::npos)
            src.remove_prefix(slash + 1);
        if (src.size() > 4 && src.substr(src.size() - 4) == ".fer")
            derived_output = std::string(src.substr(0, src.size() - 4));
        else
            derived_output = std::string(src) + ".out";
        if (derived_output.empty()) derived_output = "a.out";
        output_path = derived_output.c_str();
    }

    std::ifstream in(path);
    if (!in) {
        std::cerr << "cannot open: " << path << std::endl;
        return 1;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();

    Semantic::DiagBag bag;
    Lexer::Lexer lex(source, bag);
    auto tokens = lex.tokenize();

    if (dump_tokens) {
        for (const auto& t : tokens) {
            std::cout << t.line << ':' << t.column << '\t'
                      << Printer::kind_name(t.kind) << "\t'" << t.lexeme << "'\n";
        }
        if (bag.has_errors()) {
            Semantic::print_diagnostics(bag, path, std::cerr);
            return 1;
        }
        return 0;
    }

    if (dump_ast) {
        Parser::Parser p(std::move(tokens), bag);
        auto decls = p.parse();
        for (const auto& decl : decls) {
            Printer::print_decl(decl, std::cout, 0);
        }
        if (bag.has_errors()) {
            Semantic::print_diagnostics(bag, path, std::cerr);
            return 1;
        }
        return 0;
    }

    if (dump_sema) {
        Parser::Parser p(std::move(tokens), bag);
        auto decls = p.parse();
        Semantic::Semantic sema;
        auto sema_bag = sema.check(decls);
        for (const auto& d : sema_bag.all()) {
            bag.error(d.line, d.column, d.message);
        }
        Semantic::print_diagnostics(bag, path, std::cerr);
        Printer::print_decls_with_types(decls, sema.aast, std::cout);
        return bag.has_errors() ? 1 : 0;
    }

    // ошибки лексера (если есть) — прекращаем до разбора
    if (bag.has_errors()) {
        Semantic::print_diagnostics(bag, path, std::cerr);
        return 1;
    }

    Parser::Parser p(std::move(tokens), bag);
    auto decls = p.parse();

    if (bag.has_errors()) {
        Semantic::print_diagnostics(bag, path, std::cerr);
        return 1;
    }

    Semantic::Semantic sema;
    auto sema_bag = sema.check(decls);

    // ошибки парсера и семантики в один поток
    for (const auto& d : sema_bag.all()) {
        bag.error(d.line, d.column, d.message);
    }

    if (bag.has_errors()) {
        Semantic::print_diagnostics(bag, path, std::cerr);
        return 1;
    }


    Codegen::Codegen cg;
    bool ok = cg.generate(decls, sema.aast, output_path);
    return ok ? 0 : 1;
}
