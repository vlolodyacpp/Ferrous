module;
#include <memory>
#include <string>
#include <vector>

export module Ferrous.Codegen;
export import Ferrous.AST;
export import Ferrous.Semantic;

export namespace Codegen {

    // Кодоген: финальная фаза. По аннотированному AST генерирует LLVM IR
    // и собирает исполняемый файл. Детали скрыты в Impl (pimpl).
    class Codegen {
    public:
        Codegen();
        ~Codegen();
        bool generate(const std::vector<Parser::Decl>& decls,
                      const Semantic::AnnotatedAST& aast,
                      const std::string& output_path);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace Codegen
