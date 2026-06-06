module;
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Target.h>

export module Ferrous.Codegen;
import std;
export import Ferrous.AST;
export import Ferrous.Semantic;

export namespace Codegen {

    // область видимости кодогена (для локальных переменных)
    struct CGScope {
        CGScope* parent;
        std::unordered_map<std::string, LLVMValueRef> vars;
        std::unordered_map<std::string, LLVMTypeRef> var_types;
    };

    // главный класс кодогенератора
    class Codegen {
    public:
        Codegen();
        ~Codegen();
        void generate(const std::vector<Parser::Decl>& decls,
                      const Semantic::AnnotatedAST& aast,
                      const std::string& output_path);

    private:
        // LLVM C API-контекст
        LLVMContextRef ctx = nullptr;
        LLVMModuleRef mod = nullptr;
        LLVMBuilderRef builder = nullptr;

        // аннотации от семантики
        const Semantic::AnnotatedAST* aast = nullptr;
        const Semantic::TypeRegistry* types = nullptr;

        // маппинг Ferrous → LLVM
        LLVMTypeRef to_llvm_type(Semantic::TypeID) const;

        // таблица функций: имя → LLVM-функция + её сигнатура
        std::unordered_map<std::string, LLVMValueRef> functions;
        std::unordered_map<std::string, LLVMTypeRef> function_types;

        // текущая LLVM-функция
        LLVMValueRef cg_fn = nullptr;
        Semantic::TypeID cg_return_type{};

        // стек областей видимости
        std::deque<CGScope> scope_storage;
        CGScope* cg_current_scope = nullptr;

        void push_cg_scope();
        void pop_cg_scope();
        LLVMValueRef create_entry_alloca(LLVMValueRef fn,
                                         const std::string& name,
                                         LLVMTypeRef type);

        // флаги для break/continue
        std::vector<LLVMBasicBlockRef> break_stack;
        std::vector<LLVMBasicBlockRef> continue_stack;
        bool cg_in_loop = false;

        // declare-фаза
        void declare_runtime_functions();
        void declare_structs(const std::vector<Parser::Decl>& decls);
        void declare_functions(const std::vector<Parser::Decl>& decls);
        void declare_functions_rec(const std::vector<Parser::Decl>& decls);

        // define-фаза
        void define_functions(const std::vector<Parser::Decl>& decls);
        void define_functions_rec(const std::vector<Parser::Decl>& decls);
        void define_function(const Parser::FnDecl& fn);
        void gen_main(const Parser::FnDecl& fn);

        // генерация выражений
        LLVMValueRef gen_expr(const Parser::Expr& e);
        LLVMValueRef gen_lit_int(const Parser::LitIntExpr& n);
        LLVMValueRef gen_lit_float(const Parser::LitFloatExpr& n);
        LLVMValueRef gen_lit_bool(const Parser::LitBoolExpr& n);
        LLVMValueRef gen_lit_string(const Parser::LitStringExpr& n);
        LLVMValueRef gen_lit_char(const Parser::LitCharExpr& n);
        LLVMValueRef gen_ident(const Parser::IdentExpr& n);
        LLVMValueRef gen_path(const Parser::PathExpr& n);
        LLVMValueRef gen_unary(const Parser::UnaryExpr& n);
        LLVMValueRef gen_binary(const Parser::BinaryExpr& n);
        LLVMValueRef gen_group(const Parser::GroupExpr& n);
        LLVMValueRef gen_cast(const Parser::CastExpr& n);
        LLVMValueRef gen_call(const Parser::CallExpr& n);
        LLVMValueRef gen_index(const Parser::IndexExpr& n);
        LLVMValueRef gen_field(const Parser::FieldExpr& n);
        LLVMValueRef gen_array_lit(const Parser::ArrayLitExpr& n);
        LLVMValueRef gen_struct_lit(const Parser::StructLitExpr& n);

        // указатель на lvalue (для присваивания)
        LLVMValueRef gen_lvalue_ptr(const Parser::Expr& e);

        // генерация инструкций
        void gen_stmt(const Parser::Stmt& s);
        void gen_let(const Parser::LetStmt& n);
        void gen_expr_stmt(const Parser::ExprStmt& n);
        void gen_block(const Parser::BlockStmt& n);
        void gen_if(const Parser::IfStmt& n);
        void gen_while(const Parser::WhileStmt& n);
        void gen_return(const Parser::ReturnStmt& n);
        void gen_break();
        void gen_continue();

        // встроенные функции
        LLVMValueRef gen_builtin_call(const std::string& name,
                                      const std::vector<LLVMValueRef>& args,
                                      std::size_t line);
    };

} // namespace Codegen
