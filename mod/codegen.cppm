module;
#include <memory>
#include <string>
#include <vector>

export module Ferrous.Codegen;
export import Ferrous.AST;
export import Ferrous.Semantic;

export namespace Codegen {

    class Codegen {
    public:
        Codegen();
        ~Codegen();
        void generate(const std::vector<Parser::Decl>& decls,
                      const Semantic::AnnotatedAST& aast,
                      const std::string& output_path);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace Codegen
