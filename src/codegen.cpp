module;
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>   // llvm::AllocaInst (isa в create_entry_alloca)
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>      // llvm::sys::getDefaultTargetTriple
#include <llvm/TargetParser/Triple.h>    // llvm::Triple

module Ferrous.Codegen;
import Ferrous.Printer;

#ifndef FERROUS_RT_SRC
#define FERROUS_RT_SRC "rt/ferrous_rt.cpp"
#endif

namespace Codegen {

    using TokenKind = Lexer::TokenKind;

    // область видимости кодогена
    struct CGScope {
        CGScope* parent;
        std::unordered_map<std::string, llvm::Value*> vars;
        std::unordered_map<std::string, llvm::Type*> var_types;
    };

    // реализация кодогенератора LLVM-типы видны только здесь
    struct Codegen::Impl {
        llvm::LLVMContext* ctx = nullptr;
        llvm::Module*      mod = nullptr;
        llvm::IRBuilder<>* builder = nullptr;

        // короткие аксессоры
        llvm::LLVMContext& C() const {
            return *ctx;
        }
        llvm::Module&      M() const {
            return *mod;
        }
        llvm::IRBuilder<>& B() const {
            return *builder;
        }
        llvm::Value* i32_zero() const {
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), 0);
        }
        // тип выражения из аннотаций (opaque-pointer-safe источник типов)
        Semantic::TypeID expr_tid(const Parser::Expr* e, Semantic::TypeID fb) const {
            auto it = aast->expr_type.find(e);
            return it != aast->expr_type.end()
                ? types->resolve_alias(it->second) : fb;
        }

        // аннотации от семантики
        const Semantic::AnnotatedAST* aast = nullptr;
        const Semantic::TypeRegistry* types = nullptr;

        // маппинг → LLVM
        llvm::Type* to_llvm_type(Semantic::TypeID) const;

        // манглинг по типам
        std::string mangle(const std::string& base,
                           const std::vector<Semantic::TypeID>& params) const {
            std::string m = base;
            for (auto tid : params)
                m += "_" + types->name(types->resolve_alias(tid));
            return m;
        }

        // таблица функций: имя → LLVM-функция + её сигнатура
        std::unordered_map<std::string, llvm::Value*> functions;
        std::unordered_map<std::string, llvm::Type*> function_types;

        // текущая LLVM-функция
        llvm::Value* cg_fn = nullptr;
        Semantic::TypeID cg_return_type{};
        // namespace-префикс текущей функции ("ns1::ns2::")
        std::string cg_ns_prefix;

        // стек областей видимости
        std::deque<CGScope> scope_storage;
        CGScope* cg_current_scope = nullptr;

        void push_cg_scope();
        void pop_cg_scope();
        llvm::Value* create_entry_alloca(llvm::Value* fn,
                                         const std::string& name,
                                         llvm::Type* type);

        // флаги для break/continue
        std::vector<llvm::BasicBlock*> break_stack;
        std::vector<llvm::BasicBlock*> continue_stack;
        bool cg_in_loop = false;
        // выставляется при внутренней рассогласованности кодогена
        bool cg_failed = false;

        // главный вход; false — если кодоген/линковка завершились ошибкой
        bool generate(const std::vector<Parser::Decl>& decls,
                      const Semantic::AnnotatedAST& aast,
                      const std::string& output_path);

        // declare-фаза
        void declare_runtime_functions();
        void declare_structs(const std::vector<Parser::Decl>& decls);
        void declare_functions(const std::vector<Parser::Decl>& decls);
        void declare_functions_rec(const std::vector<Parser::Decl>& decls,
                                   const std::string& prefix = "");

        // define-фаза
        void define_functions(const std::vector<Parser::Decl>& decls);
        void define_functions_rec(const std::vector<Parser::Decl>& decls,
                                  const std::string& prefix = "");
        void define_function(const Parser::FnDecl& fn,
                             const std::string& prefix = "");

        // генерация выражений
        llvm::Value* gen_expr(const Parser::Expr& e);
        llvm::Value* gen_lit_int(const Parser::LitIntExpr& n, const Parser::Expr& e);
        llvm::Value* gen_lit_float(const Parser::LitFloatExpr& n, const Parser::Expr& e);
        llvm::Value* gen_lit_bool(const Parser::LitBoolExpr& n);
        llvm::Value* gen_lit_string(const Parser::LitStringExpr& n);
        llvm::Value* gen_lit_char(const Parser::LitCharExpr& n);
        llvm::Value* gen_ident(const Parser::IdentExpr& n);
        llvm::Value* gen_path(const Parser::PathExpr& n);
        llvm::Value* gen_unary(const Parser::UnaryExpr& n);
        llvm::Value* gen_binary(const Parser::BinaryExpr& n);
        llvm::Value* gen_group(const Parser::GroupExpr& n);
        llvm::Value* gen_cast(const Parser::CastExpr& n);
        llvm::Value* gen_call(const Parser::CallExpr& n);
        llvm::Value* gen_index(const Parser::IndexExpr& n);
        llvm::Value* gen_field(const Parser::FieldExpr& n);
        llvm::Value* gen_array_lit(const Parser::ArrayLitExpr& n, const Parser::Expr& e);
        llvm::Value* gen_struct_lit(const Parser::StructLitExpr& n);

        // указатель на lvalue (для присваивания)
        llvm::Value* gen_lvalue_ptr(const Parser::Expr& e);

        // поэлементное сравнение значений типа
        llvm::Value* gen_value_eq(llvm::Value* a, llvm::Value* b, Semantic::TypeID tid);
        bool is_string_tid(Semantic::TypeID tid) const {
            return types->equal(tid, types->builtin(TokenKind::KwString));
        }

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
        llvm::Value* gen_builtin_call(const std::string& name,
                                      const std::vector<llvm::Value*>& args,
                                      const std::vector<Semantic::TypeID>& arg_tids,
                                      std::size_t line);

        ~Impl();
    };

    // TypeRef → TypeID (для codegen)
    static Semantic::TypeID resolve_typeid(
        const Parser::TypeRef& tr,
        const Semantic::TypeRegistry& types,
        Semantic::TypeID fallback)
    {
        return std::visit([&](const auto& t) -> Semantic::TypeID {
            using T = std::decay_t<decltype(t)>;
            if constexpr (std::is_same_v<T, Parser::BuiltinTypeRef>)
                return types.builtin(t.type_kind);
            if constexpr (std::is_same_v<T, Parser::NamedTypeRef>) {
                auto tid = types.by_name(std::string(t.name));
                return tid.value_or(fallback);
            }
            if constexpr (std::is_same_v<T, Parser::ArrayTypeRef>) {
                // [elem; N] — резолвим элемент и ищем уже созданный тип массива
                Semantic::TypeID elem = resolve_typeid(*t.elem, types, fallback);
                std::string_view s = t.size;
                int base = 10;
                if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
                    s = s.substr(2); base = 16;
                } else if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
                    s = s.substr(2); base = 2;
                }
                std::uint64_t size = 0;
                std::from_chars(s.data(), s.data() + s.size(), size, base);
                return types.find_array(elem, size).value_or(fallback);
            }
            return fallback;
        }, tr.node);
    }


    Codegen::Codegen() : impl(std::make_unique<Impl>()) {}
    Codegen::~Codegen() = default;
    bool Codegen::generate(const std::vector<Parser::Decl>& decls,
                           const Semantic::AnnotatedAST& aast,
                           const std::string& output_path) {
        return impl->generate(decls, aast, output_path);
    }

    // освобождение LLVM-ресурсов
    Codegen::Impl::~Impl() {
        delete builder; builder = nullptr;
        delete mod;     mod = nullptr;
        delete ctx;     ctx = nullptr;
    }

    // преобразование типа в LLVM-тип
    llvm::Type* Codegen::Impl::to_llvm_type(Semantic::TypeID tid) const {
        llvm::LLVMContext& c = C();

        // string = { ptr, i64 }
        if (types->equal(tid, types->builtin(TokenKind::KwString)))
            return llvm::StructType::get(c,
                { llvm::PointerType::getUnqual(c), llvm::Type::getInt64Ty(c) });

        // void
        if (types->equal(tid, types->builtin(TokenKind::KwVoid)))
            return llvm::Type::getVoidTy(c);

        // bool → i1
        if (types->equal(tid, types->builtin(TokenKind::KwBool)))
            return llvm::Type::getInt1Ty(c);

        // char → i32 (UTF-32 codepoint)
        if (types->equal(tid, types->builtin(TokenKind::KwChar)))
            return llvm::Type::getInt32Ty(c);

        // целые
        if (types->equal(tid, types->builtin(TokenKind::KwInt8)))
            return llvm::Type::getInt8Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwInt16)))
            return llvm::Type::getInt16Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwInt32)))
            return llvm::Type::getInt32Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwInt64)))
            return llvm::Type::getInt64Ty(c);

        // беззнаковые (те же LLVM-типы)
        if (types->equal(tid, types->builtin(TokenKind::KwUint8)))
            return llvm::Type::getInt8Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwUint16)))
            return llvm::Type::getInt16Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwUint32)))
            return llvm::Type::getInt32Ty(c);
        if (types->equal(tid, types->builtin(TokenKind::KwUint64)))
            return llvm::Type::getInt64Ty(c);

        // float
        if (types->equal(tid, types->builtin(TokenKind::KwFloat32)))
            return llvm::Type::getFloatTy(c);
        if (types->equal(tid, types->builtin(TokenKind::KwFloat64)))
            return llvm::Type::getDoubleTy(c);

        // untyped-литералы без контекста → дефолтные типы (защита от краша)
        if (types->is_untyped_int(tid))
            return llvm::Type::getInt32Ty(c);
        if (types->is_untyped_float(tid))
            return llvm::Type::getDoubleTy(c);

        // массив
        if (const auto* arr = types->get_array(tid)) {
            llvm::Type* elem = to_llvm_type(arr->elem);
            return llvm::ArrayType::get(elem, arr->size);
        }

        // структура
        if (const auto* st = types->get_struct(tid)) {
            std::string name = st->name;
            if (llvm::StructType* sty = llvm::StructType::getTypeByName(c, name))
                return sty;
            return llvm::StructType::create(c, name);
        }

        // псевдоним (разворачиваем)
        Semantic::TypeID resolved = types->resolve_alias(tid);
        if (resolved.id != tid.id)
            return to_llvm_type(resolved);

        // сюда попадать не должны — ошибка в семантике
        std::cerr << "codegen: unhandled type " << types->name(tid) << '\n';
        return llvm::Type::getInt32Ty(c);
    }

    // размещает alloca в entry-блоке функции (до первой инструкции)
    llvm::Value* Codegen::Impl::create_entry_alloca(llvm::Value* fn,
                                              const std::string& name,
                                              llvm::Type* type) {
        llvm::Function* f = llvm::cast<llvm::Function>(fn);
        llvm::BasicBlock& entry = f->getEntryBlock();
        // точка вставки - после уже существующих alloca - остаются в порядке объявления
        llvm::BasicBlock::iterator ip = entry.begin();
        while (ip != entry.end() && llvm::isa<llvm::AllocaInst>(*ip)) ++ip;
        llvm::IRBuilder<> tmp(&entry, ip);
        return tmp.CreateAlloca(type, nullptr, name);
    }

    // создание дочернего скоупа (при входе в блок)
    void Codegen::Impl::push_cg_scope() {
        auto& s = scope_storage.emplace_back();
        s.parent = cg_current_scope;
        cg_current_scope = &s;
    }

    void Codegen::Impl::pop_cg_scope() {
        if (cg_current_scope)
            cg_current_scope = cg_current_scope->parent;
    }

    // объявление всех runtime-функций в модуле LLVM
    // panic, bounds/div check, print/println, input, строковые операции
    void Codegen::Impl::declare_runtime_functions() {
        llvm::LLVMContext& c = C();
        llvm::Type* void_ty   = llvm::Type::getVoidTy(c);
        llvm::Type* i8p_ty    = llvm::PointerType::getUnqual(c);
        llvm::Type* i64_ty    = llvm::Type::getInt64Ty(c);
        llvm::Type* double_ty = llvm::Type::getDoubleTy(c);

        // строковый тип {ptr, i64}
        llvm::StructType* str_ty = llvm::StructType::get(c, { i8p_ty, i64_ty });

        auto decl = [&](const char* name, llvm::Type* ret,
                        std::vector<llvm::Type*> params) {
            llvm::FunctionType* ft = llvm::FunctionType::get(ret, params, false);
            llvm::Function* f = llvm::Function::Create(ft,
                llvm::Function::ExternalLinkage, name, &M());
            functions[name] = f;
            function_types[name] = ft;
        };

        // panic + assert + exit
        decl("__ferrous_panic",       void_ty, {i8p_ty, i64_ty, i64_ty});
        decl("__ferrous_assert_fail", void_ty, {i8p_ty, i64_ty, i64_ty});
        decl("__ferrous_exit",        void_ty, {i64_ty});

        // проверки
        decl("__ferrous_div_check",   i64_ty, {i64_ty, i64_ty});
        decl("__ferrous_mod_check",   i64_ty, {i64_ty, i64_ty});
        decl("__ferrous_bounds_check", void_ty, {i64_ty, i64_ty, i64_ty});

        // вывод — функция на каждый тип
        llvm::Type* i8_ty  = llvm::Type::getInt8Ty(c);
        llvm::Type* i16_ty = llvm::Type::getInt16Ty(c);
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(c);
        for (const char* pfx : {"print", "println"}) {
            auto d = [&](const std::string& suf, llvm::Type* arg) {
                decl((std::string("__ferrous_") + pfx + "_" + suf).c_str(), void_ty,{arg});
            };
            d("int8",  i8_ty);
            d("int16",  i16_ty);
            d("int32",  i32_ty);
            d("int64",  i64_ty);
            d("uint8", i8_ty);
            d("uint16", i16_ty);
            d("uint32", i32_ty);
            d("uint64", i64_ty);
            d("float32", llvm::Type::getFloatTy(c));
            d("float64", double_ty);
            d("bool", i8_ty);    // bool передаётся как i8 (ZExt из i1)
            d("char", i32_ty);   // codepoint
            decl((std::string("__ferrous_") + pfx + "_string").c_str(),
                 void_ty, {i8p_ty, i64_ty});
        }

        // ввод
        decl("__ferrous_input", str_ty, {});

        // строковые операции
        decl("__ferrous_str_concat", str_ty, {i8p_ty, i64_ty, i8p_ty, i64_ty});
        decl("__ferrous_str_eq",     llvm::Type::getInt8Ty(c),
             {i8p_ty, i64_ty, i8p_ty, i64_ty});

        // преобразования строка <-> число
        decl("__ferrous_int_to_string",   str_ty, {i64_ty});
        decl("__ferrous_uint_to_string",  str_ty, {i64_ty});
        decl("__ferrous_float_to_string", str_ty, {double_ty});
        decl("__ferrous_bool_to_string",  str_ty, {i8_ty});
        decl("__ferrous_string_to_int",   i64_ty,    {i8p_ty, i64_ty});
        decl("__ferrous_string_to_float", double_ty, {i8p_ty, i64_ty});
    }

    // регистрация структур в модуле: StructType::setBody
    void Codegen::Impl::declare_structs(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* st = std::get_if<Parser::StructDecl>(&d.node)) {
                std::string name(st->name);
                llvm::StructType* sty = llvm::StructType::getTypeByName(C(), name);
                if (!sty) sty = llvm::StructType::create(C(), name);

                std::vector<llvm::Type*> field_types;
                for (const auto& [fname, ftype] : st->field) {
                    Semantic::TypeID ftid =
                        resolve_typeid(ftype, *types, types->builtin(TokenKind::KwVoid));
                    field_types.push_back(to_llvm_type(ftid));
                }
                sty->setBody(field_types, false);
            }
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                declare_structs(ns->decls);
        }
    }

    // регистрация функций пользователя (main — без манглинга, остальные — с манглингом по типам)
    void Codegen::Impl::declare_functions_rec(const std::vector<Parser::Decl>& decls,
                                              const std::string& prefix) {
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node)) {
                std::string name = prefix + std::string(fn->name);

                // main не манглим
                if (name == "main") {
                    Semantic::TypeID rtid = fn->return_type
                        ? resolve_typeid(*fn->return_type, *types,
                            types->builtin(TokenKind::KwInt32))
                        : types->builtin(TokenKind::KwInt32);
                    llvm::Type* rt = to_llvm_type(rtid);
                    llvm::FunctionType* ft = llvm::FunctionType::get(rt, false);
                    functions["main"] = llvm::Function::Create(
                        ft, llvm::Function::ExternalLinkage, "main", &M());
                    function_types["main"] = ft;
                    continue;
                }

                // манглинг: name + типы параметров для перегрузок
                std::vector<Semantic::TypeID> ptids;
                std::vector<llvm::Type*> param_types;
                for (const auto& [pname, ptype] : fn->params) {
                    auto tid = resolve_typeid(ptype, *types,
                        types->builtin(TokenKind::KwVoid));
                    ptids.push_back(tid);
                    param_types.push_back(to_llvm_type(tid));
                }
                std::string mangled = mangle(name, ptids);

                Semantic::TypeID rtid = fn->return_type
                    ? resolve_typeid(*fn->return_type, *types,
                        types->void_type())
                    : types->void_type();
                llvm::Type* rt = to_llvm_type(rtid);

                llvm::FunctionType* ft = llvm::FunctionType::get(rt, param_types, false);
                functions[mangled] = llvm::Function::Create(
                    ft, llvm::Function::ExternalLinkage, mangled, &M());
                function_types[mangled] = ft;
            }
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                declare_functions_rec(ns->decls,
                    prefix + std::string(ns->name) + "::");
        }
    }

    void Codegen::Impl::declare_functions(const std::vector<Parser::Decl>& decls) {
        declare_functions_rec(decls);
    }


    llvm::Value* Codegen::Impl::gen_expr(const Parser::Expr& e) {
        return std::visit([&](const auto& n) -> llvm::Value* {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::LitIntExpr>)
                return gen_lit_int(n, e);
            if constexpr (std::is_same_v<T, Parser::LitFloatExpr>)
                return gen_lit_float(n, e);
            if constexpr (std::is_same_v<T, Parser::LitBoolExpr>)
                return gen_lit_bool(n);
            if constexpr (std::is_same_v<T, Parser::LitStringExpr>)
                return gen_lit_string(n);
            if constexpr (std::is_same_v<T, Parser::LitCharExpr>)
                return gen_lit_char(n);
            if constexpr (std::is_same_v<T, Parser::ErrorExpr>)
                return i32_zero();
            if constexpr (std::is_same_v<T, Parser::IdentExpr>)
                return gen_ident(n);
            if constexpr (std::is_same_v<T, Parser::PathExpr>)
                return gen_path(n);
            if constexpr (std::is_same_v<T, Parser::UnaryExpr>)
                return gen_unary(n);
            if constexpr (std::is_same_v<T, Parser::BinaryExpr>)
                return gen_binary(n);
            if constexpr (std::is_same_v<T, Parser::GroupExpr>)
                return gen_group(n);
            if constexpr (std::is_same_v<T, Parser::CastExpr>)
                return gen_cast(n);
            if constexpr (std::is_same_v<T, Parser::CallExpr>)
                return gen_call(n);
            if constexpr (std::is_same_v<T, Parser::IndexExpr>)
                return gen_index(n);
            if constexpr (std::is_same_v<T, Parser::FieldExpr>)
                return gen_field(n);
            if constexpr (std::is_same_v<T, Parser::ArrayLitExpr>)
                return gen_array_lit(n, e);
            if constexpr (std::is_same_v<T, Parser::StructLitExpr>)
                return gen_struct_lit(n);
            return i32_zero();
        }, e.node);
    }


    // целочисленный литерал: парсинг (dec/hex/bin) + суффикс → ConstInt
    llvm::Value* Codegen::Impl::gen_lit_int(const Parser::LitIntExpr& n,
                                            const Parser::Expr& e) {
        std::string_view raw = n.value;
        std::uint64_t val = 0;
        int base = 10;

        if (raw.size() > 2 && raw[0] == '0') {
            if (raw[1] == 'x' || raw[1] == 'X') {
                raw = raw.substr(2);
                base = 16;
            } else if (raw[1] == 'b' || raw[1] == 'B') {
                raw = raw.substr(2);
                base = 2;
            }
        }

        // отрезаем суффикс
        std::string_view digits = raw;
        auto pos = digits.find_first_not_of("0123456789abcdefABCDEF");
        if (pos != std::string_view::npos)
            digits = digits.substr(0, pos);

        auto [ptr, ec] = std::from_chars(digits.data(),
            digits.data() + digits.size(), val, base);
        if (ec != std::errc{}) val = 0;

        // тип из аннотаций (семантика уже резолвила untyped)
        auto it = aast->expr_type.find(&e);
        Semantic::TypeID tid = (it != aast->expr_type.end())
            ? it->second : types->builtin(TokenKind::KwInt32);
        tid = types->resolve_alias(tid);

        llvm::Type* lt = to_llvm_type(tid);

        // int-литерал в вещественном контексте
        if (lt->isFloatingPointTy())
            return llvm::ConstantFP::get(lt, static_cast<double>(val));
        if (!lt->isIntegerTy()) lt = llvm::Type::getInt32Ty(C());
        return llvm::ConstantInt::get(lt, val, false);
    }


    // float-литерал: from_chars + nan/inf → ConstReal
    llvm::Value* Codegen::Impl::gen_lit_float(const Parser::LitFloatExpr& n,
                                              const Parser::Expr& e) {
        double val = 0.0;
        std::string_view raw = n.value;

        if (raw == "nan")
            val = std::numeric_limits<double>::quiet_NaN();
        else if (raw == "inf")
            val = std::numeric_limits<double>::infinity();
        else {
            auto [ptr, ec] = std::from_chars(raw.data(),
                raw.data() + raw.size(), val);
            if (ec != std::errc{}) val = 0.0;
        }

        auto it = aast->expr_type.find(&e);
        Semantic::TypeID tid = (it != aast->expr_type.end())
            ? it->second : types->builtin(TokenKind::KwFloat64);
        tid = types->resolve_alias(tid);

        llvm::Type* lt = to_llvm_type(tid);
        if (!lt->isFloatingPointTy()) lt = llvm::Type::getDoubleTy(C());
        return llvm::ConstantFP::get(lt, val);
    }

    // булев литерал → i1: 0 или 1
    llvm::Value* Codegen::Impl::gen_lit_bool(const Parser::LitBoolExpr& n) {
        return llvm::ConstantInt::get(
            llvm::Type::getInt1Ty(C()), n.value ? 1 : 0);
    }


    // строковый литерал: глобальная строка + упаковка в {ptr, i64}
    llvm::Value* Codegen::Impl::gen_lit_string(const Parser::LitStringExpr& n) {
        std::string str(n.value);
        llvm::Value* global = B().CreateGlobalString(str, "str");
        llvm::Value* len = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(C()), str.size());

        llvm::Value* s = llvm::UndefValue::get(
            to_llvm_type(types->builtin(TokenKind::KwString)));
        s = B().CreateInsertValue(s, global, {0u});
        s = B().CreateInsertValue(s, len, {1u});
        return s;
    }



    // символьный литерал: разбор escape → i32 codepoint
    llvm::Value* Codegen::Impl::gen_lit_char(const Parser::LitCharExpr& n) {
        std::string_view raw = n.value;
        std::uint32_t cp = 0;

        if (raw.size() >= 2 && raw[0] == '\\') {
            if (raw == "\\n") cp = 0x0A;
            else if (raw == "\\t") cp = 0x09;
            else if (raw == "\\r") cp = 0x0D;
            else if (raw == "\\\\") cp = '\\';
            else if (raw == "\\'") cp = '\'';
            else if (raw == "\\\"") cp = '"';
            else if (raw == "\\0") cp = 0x00;
        } else if (raw.size() == 1) {
            cp = static_cast<std::uint8_t>(raw[0]);
        }

        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), cp);
    }

    // загрузка переменной: поиск в цепочке CGScope → Load из alloca
    llvm::Value* Codegen::Impl::gen_ident(const Parser::IdentExpr& n) {
        std::string name(n.value);
        for (CGScope* s = cg_current_scope; s; s = s->parent) {
            auto it = s->vars.find(name);
            if (it != s->vars.end()) {
                auto tit = s->var_types.find(name);
                llvm::Type* ty = (tit != s->var_types.end())
                    ? tit->second : llvm::Type::getInt32Ty(C());
                return B().CreateLoad(ty, it->second, name);
            }
        }
        return i32_zero();
    }


    // доступ к переменной через namespace: обход CGScope по последнему сегменту
    llvm::Value* Codegen::Impl::gen_path(const Parser::PathExpr& n) {
        std::string last(n.segments.back());
        for (CGScope* s = cg_current_scope; s; s = s->parent) {
            auto it = s->vars.find(last);
            if (it != s->vars.end()) {
                auto tit = s->var_types.find(last);
                llvm::Type* ty = (tit != s->var_types.end())
                    ? tit->second : llvm::Type::getInt32Ty(C());
                return B().CreateLoad(ty, it->second, last);
            }
        }
        return i32_zero();
    }


    // унарный минус или логическое не
    llvm::Value* Codegen::Impl::gen_unary(const Parser::UnaryExpr& n) {
        llvm::Value* op = gen_expr(*n.operand);

        if (n.op == TokenKind::OpMinus) {
            if (op->getType()->isFloatingPointTy())
                return B().CreateFNeg(op, "neg");
            return B().CreateNeg(op, "neg");
        }
        if (n.op == TokenKind::OpBang)
            return B().CreateNot(op, "not");
        if (n.op == TokenKind::OpTilde)        // битовое НЕ (A.1.8): ~x = инверсия битов
            return B().CreateNot(op, "bnot");
        return op;
    }

    llvm::Value* Codegen::Impl::gen_value_eq(llvm::Value* a, llvm::Value* b,
                                             Semantic::TypeID tid) {
        tid = types->resolve_alias(tid);
        llvm::Type* i1 = llvm::Type::getInt1Ty(C());

        // строка: __ferrous_str_eq(a,b) → i8 (1/0)
        if (is_string_tid(tid)) {
            auto fn = llvm::cast<llvm::Function>(functions["__ferrous_str_eq"]);
            auto ft = llvm::cast<llvm::FunctionType>(function_types["__ferrous_str_eq"]);
            llvm::Value* eq = B().CreateCall(ft, fn, {
                B().CreateExtractValue(a, {0u}), B().CreateExtractValue(a, {1u}),
                B().CreateExtractValue(b, {0u}), B().CreateExtractValue(b, {1u})
            }, "streq");
            return B().CreateICmpNE(eq, llvm::ConstantInt::get(eq->getType(), 0), "eq");
        }

        // структура: конъюнкция сравнений всех полей
        if (const auto* sty = types->get_struct(tid)) {
            llvm::Value* acc = llvm::ConstantInt::get(i1, 1);
            for (unsigned i = 0; i < sty->fields.size(); ++i) {
                llvm::Value* fa = B().CreateExtractValue(a, {i}, "fa");
                llvm::Value* fb = B().CreateExtractValue(b, {i}, "fb");
                acc = B().CreateAnd(acc, gen_value_eq(fa, fb, sty->fields[i].second), "feq");
            }
            return acc;
        }

        // массив: конъюнкция поэлементных сравнений
        if (const auto* aty = types->get_array(tid)) {
            llvm::Value* acc = llvm::ConstantInt::get(i1, 1);
            for (std::uint64_t i = 0; i < aty->size; ++i) {
                llvm::Value* ea = B().CreateExtractValue(a, {static_cast<unsigned>(i)}, "ea");
                llvm::Value* eb = B().CreateExtractValue(b, {static_cast<unsigned>(i)}, "eb");
                acc = B().CreateAnd(acc, gen_value_eq(ea, eb, aty->elem), "eeq");
            }
            return acc;
        }

        // скаляр
        if (a->getType()->isFloatingPointTy())
            return B().CreateFCmpOEQ(a, b, "eq");
        return B().CreateICmpEQ(a, b, "eq");
    }

    // бинарные операторы: арифметика, сравнения, логика, присваивание, строки
    llvm::Value* Codegen::Impl::gen_binary(const Parser::BinaryExpr& n) {
        // присваивание
        if (n.op == TokenKind::OpEq) {
            llvm::Value* rhs = gen_expr(*n.rhs);
            llvm::Value* ptr = gen_lvalue_ptr(*n.lhs);
            B().CreateStore(rhs, ptr);
            return rhs;
        }

        llvm::Value* lhs = gen_expr(*n.lhs);
        llvm::Value* rhs = gen_expr(*n.rhs);
        llvm::Type* ty = lhs->getType();
        bool is_float = ty->isFloatingPointTy();
        // тип левого операнда из аннотаций — единый источник правды о string/struct/array
        Semantic::TypeID lhs_tid = expr_tid(n.lhs.get(), types->builtin(TokenKind::KwInt32));

        auto call_rt = [&](const char* name, std::vector<llvm::Value*> a,
                           const char* lbl) -> llvm::Value* {
            auto fn = llvm::cast<llvm::Function>(functions[name]);
            auto ft = llvm::cast<llvm::FunctionType>(function_types[name]);
            return B().CreateCall(ft, fn, a, lbl);
        };
        // беззнаковый ли тип выражения
        auto is_unsigned_t = [&](const Parser::Expr* ex) -> bool {
            Semantic::TypeID t = expr_tid(ex, types->builtin(TokenKind::KwInt32));
            for (auto k : { TokenKind::KwUint8, TokenKind::KwUint16,
                            TokenKind::KwUint32, TokenKind::KwUint64 })
                if (types->equal(t, types->builtin(k))) return true;
            return false;
        };
        const bool is_unsigned = is_unsigned_t(n.lhs.get());
        // привести значение сдвига к ширине левого операнда (LLVM требует совпадения)
        auto match_width = [&](llvm::Value* v, llvm::Type* target) -> llvm::Value* {
            unsigned vw = v->getType()->getIntegerBitWidth();
            unsigned tw = target->getIntegerBitWidth();
            if (vw == tw) return v;
            return vw < tw ? B().CreateZExt(v, target, "shw")
                           : B().CreateTrunc(v, target, "shw");
        };

        switch (n.op) {
            case TokenKind::OpPlus: {
                // строковая конкатенация (определяем строку по TypeID, не по LLVM-типу)
                if (is_string_tid(lhs_tid)) {
                    llvm::Value* a_ptr = B().CreateExtractValue(lhs, {0u}, "a.ptr");
                    llvm::Value* a_len = B().CreateExtractValue(lhs, {1u}, "a.len");
                    llvm::Value* b_ptr = B().CreateExtractValue(rhs, {0u}, "b.ptr");
                    llvm::Value* b_len = B().CreateExtractValue(rhs, {1u}, "b.len");
                    return call_rt("__ferrous_str_concat",
                        {a_ptr, a_len, b_ptr, b_len}, "concat");
                }
                return is_float ? B().CreateFAdd(lhs, rhs, "add")
                                           : B().CreateAdd(lhs, rhs, "add");
            }
            case TokenKind::OpMinus:
                return is_float ? B().CreateFSub(lhs, rhs, "sub")
                                           : B().CreateSub(lhs, rhs, "sub");
            case TokenKind::OpStar:
                return is_float ? B().CreateFMul(lhs, rhs, "mul")
                                           : B().CreateMul(lhs, rhs, "mul");
            case TokenKind::OpSlash: {
                if (is_float) return B().CreateFDiv(lhs, rhs, "div");
                llvm::Value* line = llvm::ConstantInt::get(llvm::Type::getInt64Ty(C()), n.line);
                llvm::Value* d64 = rhs->getType()->getIntegerBitWidth() < 64
                    ? (is_unsigned ? B().CreateZExt(rhs, llvm::Type::getInt64Ty(C()), "d64")
                                   : B().CreateSExt(rhs, llvm::Type::getInt64Ty(C()), "d64"))
                    : rhs;
                call_rt("__ferrous_div_check", {d64, line}, "div_check");
                return is_unsigned ? B().CreateUDiv(lhs, rhs, "div")
                                   : B().CreateSDiv(lhs, rhs, "div");
            }
            case TokenKind::OpPercent: {
                llvm::Value* line = llvm::ConstantInt::get(llvm::Type::getInt64Ty(C()), n.line);
                llvm::Value* d64 = rhs->getType()->getIntegerBitWidth() < 64
                    ? (is_unsigned ? B().CreateZExt(rhs, llvm::Type::getInt64Ty(C()), "d64")
                                   : B().CreateSExt(rhs, llvm::Type::getInt64Ty(C()), "d64"))
                    : rhs;
                call_rt("__ferrous_mod_check", {d64, line}, "mod_check");
                return is_unsigned ? B().CreateURem(lhs, rhs, "rem")
                                   : B().CreateSRem(lhs, rhs, "rem");
            }

            case TokenKind::OpEqEq:
                if (is_string_tid(lhs_tid) || types->is_struct(lhs_tid) || types->is_array(lhs_tid))
                    return gen_value_eq(lhs, rhs, lhs_tid);
                return is_float ? B().CreateFCmpOEQ(lhs, rhs, "eq")
                                           : B().CreateICmpEQ(lhs, rhs, "eq");
            case TokenKind::OpBangEq:
                if (is_string_tid(lhs_tid) || types->is_struct(lhs_tid) || types->is_array(lhs_tid))
                    return B().CreateNot(gen_value_eq(lhs, rhs, lhs_tid), "ne");
                return is_float ? B().CreateFCmpONE(lhs, rhs, "ne")
                                           : B().CreateICmpNE(lhs, rhs, "ne");
            case TokenKind::OpLt:
                return is_float ? B().CreateFCmpOLT(lhs, rhs, "lt")
                       : is_unsigned ? B().CreateICmpULT(lhs, rhs, "lt")
                                     : B().CreateICmpSLT(lhs, rhs, "lt");
            case TokenKind::OpLtEq:
                return is_float ? B().CreateFCmpOLE(lhs, rhs, "le")
                       : is_unsigned ? B().CreateICmpULE(lhs, rhs, "le")
                                     : B().CreateICmpSLE(lhs, rhs, "le");
            case TokenKind::OpGt:
                return is_float ? B().CreateFCmpOGT(lhs, rhs, "gt")
                       : is_unsigned ? B().CreateICmpUGT(lhs, rhs, "gt")
                                     : B().CreateICmpSGT(lhs, rhs, "gt");
            case TokenKind::OpGtEq:
                return is_float ? B().CreateFCmpOGE(lhs, rhs, "ge")
                       : is_unsigned ? B().CreateICmpUGE(lhs, rhs, "ge")
                                     : B().CreateICmpSGE(lhs, rhs, "ge");

            case TokenKind::OpAndAnd:
                return B().CreateAnd(lhs, rhs, "and");
            case TokenKind::OpOrOr:
                return B().CreateOr(lhs, rhs, "or");

            case TokenKind::OpAmp:
                return B().CreateAnd(lhs, rhs, "band");
            case TokenKind::OpPipe:
                return B().CreateOr(lhs, rhs, "bor");
            case TokenKind::OpCaret:
                return B().CreateXor(lhs, rhs, "bxor");
            case TokenKind::OpShl:
                return B().CreateShl(lhs, match_width(rhs, ty), "shl");
            case TokenKind::OpShr:
                // знаковый сдвиг hr)
                return is_unsigned_t(n.lhs.get())
                    ? B().CreateLShr(lhs, match_width(rhs, ty), "lshr")
                    : B().CreateAShr(lhs, match_width(rhs, ty), "ashr");

            default:
                return lhs;
        }
    }

    // скобки: прозрачно возвращает inner
    llvm::Value* Codegen::Impl::gen_group(const Parser::GroupExpr& n) {
        return gen_expr(*n.inner);
    }

    // приведение типа: SExt/Trunc/SIToFP/FPToSI по таблице
    llvm::Value* Codegen::Impl::gen_cast(const Parser::CastExpr& n) {
        llvm::Value* src = gen_expr(*n.expr);
        Semantic::TypeID dst_tid = resolve_typeid(n.target, *types,
            types->builtin(TokenKind::KwInt32));
        llvm::Type* dst = to_llvm_type(dst_tid);
        llvm::Type* src_ty = src->getType();

        bool src_float = src_ty->isFloatingPointTy();
        bool dst_float = dst->isFloatingPointTy();

        // одинаковые типы
        if (src_ty == dst)
            return src;

        // float → int
        if (src_float && dst->isIntegerTy())
            return B().CreateFPToSI(src, dst, "cast");
        // int → float
        if (src_ty->isIntegerTy() && dst_float)
            return B().CreateSIToFP(src, dst, "cast");
        // float → float (fpext/fptrunc)
        if (src_float && dst_float) {
            if (src_ty->getPrimitiveSizeInBits() < dst->getPrimitiveSizeInBits())
                return B().CreateFPExt(src, dst, "cast");
            return B().CreateFPTrunc(src, dst, "cast");
        }

        // int → int (sext/trunc)
        if (src_ty->getIntegerBitWidth() < dst->getIntegerBitWidth())
            return B().CreateSExt(src, dst, "cast");
        return B().CreateTrunc(src, dst, "cast");
    }


    // вызов функции: разрешение имени → встроенная или пользовательская
    llvm::Value* Codegen::Impl::gen_call(const Parser::CallExpr& n) {
        // извлекаем имя функции
        std::string fn_name;
        if (auto* id = std::get_if<Parser::IdentExpr>(&n.call->node))
            fn_name = std::string(id->value);
        else if (auto* path = std::get_if<Parser::PathExpr>(&n.call->node)) {
            for (const auto& seg : path->segments) {
                if (!fn_name.empty()) fn_name += "::";
                fn_name += std::string(seg);
            }
        } else {
            std::cerr << "codegen: unsupported call target\n";
            cg_failed = true;
            return i32_zero();
        }

        // аргументы (значения + типы для манглинга)
        std::vector<llvm::Value*> args;
        std::vector<Semantic::TypeID> arg_tids;
        for (const auto& arg : n.args) {
            args.push_back(gen_expr(arg));
            auto it = aast->expr_type.find(&arg);
            arg_tids.push_back(it != aast->expr_type.end()
                ? it->second : types->builtin(TokenKind::KwVoid));
        }

        // встроенные функции
        if (fn_name == "print" || fn_name == "println" ||
            fn_name == "input" || fn_name == "len" ||
            fn_name == "exit" || fn_name == "panic" ||
            fn_name == "assert" || fn_name == "to_string" ||
            fn_name == "to_int" || fn_name == "to_float") {
            return gen_builtin_call(fn_name, args, arg_tids, n.line);
        }

        // пользовательская функция — манглинг по типам
        std::string mangled = mangle(fn_name, arg_tids);

        // кандидаты имён - текущий namespace и все объемлющие, затем глобальная область — зеркалит обход scope-цепочки в семантике
        std::vector<std::string> prefixes;
        for (std::string p = cg_ns_prefix;;) {
            prefixes.push_back(p);
            if (p.empty()) break;
            std::string t = p.substr(0, p.size() - 2);   // убрать хвостовой "::"
            auto pos = t.rfind("::");
            p = (pos == std::string::npos) ? std::string() : t.substr(0, pos + 2);
        }

        llvm::Function* callee = nullptr;
        llvm::FunctionType* ft = nullptr;
        for (const auto& pre : prefixes) {
            for (const std::string& key : {pre + mangled, pre + std::string(fn_name)}) {
                auto it = functions.find(key);
                if (it == functions.end()) continue;
                callee = llvm::cast<llvm::Function>(it->second);
                if (auto tit = function_types.find(key); tit != function_types.end())
                    ft = llvm::cast<llvm::FunctionType>(tit->second);
                break;
            }
            if (callee) break;
        }

        if (!callee || !ft) {
            // семантика пропустила вызов, который кодоген не нашёл
            std::cerr << "codegen: no function '" << mangled
                      << "' (unresolved call to '" << fn_name << "')\n";
            cg_failed = true;
            return i32_zero();
        }

        std::vector<llvm::Value*> cargs;
        cargs.reserve(args.size());
        for (auto a : args) cargs.push_back(a);
        return B().CreateCall(ft, callee, cargs, "");
    }

    // индексация массива: bounds check + GEP + Load (типы из aast)
    llvm::Value* Codegen::Impl::gen_index(const Parser::IndexExpr& n) {
        llvm::Value* idx = gen_expr(*n.index);
        llvm::Value* ptr = gen_lvalue_ptr(*n.array);

        Semantic::TypeID arr_tid = expr_tid(n.array.get(),
            types->builtin(TokenKind::KwInt32));
        const auto* arr_info = types->get_array(arr_tid);
        llvm::Type* arr_ty = to_llvm_type(arr_tid);

        // bounds check
        if (arr_info) {
            llvm::Value* len = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(C()), arr_info->size);
            llvm::Value* promoted = idx;
            if (promoted->getType()->getIntegerBitWidth() < 64)
                promoted = B().CreateSExt(promoted,
                    llvm::Type::getInt64Ty(C()), "idx64");
            auto bc_fn = llvm::cast<llvm::Function>(
                functions["__ferrous_bounds_check"]);
            auto bc_ft = llvm::cast<llvm::FunctionType>(
                function_types["__ferrous_bounds_check"]);
            llvm::Value* line = llvm::ConstantInt::get(llvm::Type::getInt64Ty(C()), n.line);
            B().CreateCall(bc_ft, bc_fn, {promoted, len, line});
        }

        llvm::Value* gep_idx[] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), 0), idx
        };
        llvm::Value* elem_ptr = B().CreateGEP(arr_ty, ptr, gep_idx, "idx");
        llvm::Type* elem_ty = arr_info
            ? to_llvm_type(arr_info->elem)
            : llvm::Type::getInt32Ty(C());
        return B().CreateLoad(elem_ty, elem_ptr, "elem");
    }


    // доступ к полю структуры: индекс и тип поля из TypeRegistry → GEP + Load
    llvm::Value* Codegen::Impl::gen_field(const Parser::FieldExpr& n) {
        llvm::Value* obj_ptr = gen_lvalue_ptr(*n.object);

        Semantic::TypeID obj_tid = expr_tid(n.object.get(),
            types->builtin(TokenKind::KwInt32));
        llvm::Type* sty = to_llvm_type(obj_tid);

        unsigned idx = 0;
        llvm::Type* field_ty = llvm::Type::getInt32Ty(C());
        if (const auto* st = types->get_struct(obj_tid)) {
            for (unsigned i = 0; i < st->fields.size(); ++i) {
                if (st->fields[i].first == n.field) {
                    idx = i;
                    field_ty = to_llvm_type(st->fields[i].second);
                    break;
                }
            }
        }

        llvm::Value* gep_idx[] = {
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), 0),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), idx)
        };
        llvm::Value* field_ptr = B().CreateGEP(sty, obj_ptr, gep_idx, "field");
        return B().CreateLoad(field_ty, field_ptr, std::string(n.field));
    }

    // литерал массива: insertvalue каждого элемента в undef
    llvm::Value* Codegen::Impl::gen_array_lit(const Parser::ArrayLitExpr& n,
                                              const Parser::Expr& e) {
        Semantic::TypeID tid = expr_tid(&e, types->builtin(TokenKind::KwInt32));
        llvm::Type* aty = to_llvm_type(tid);

        llvm::Value* arr = llvm::UndefValue::get(aty);
        for (std::size_t i = 0; i < n.elems.size(); ++i) {
            llvm::Value* elem = gen_expr(n.elems[i]);
            arr = B().CreateInsertValue(arr, elem,
                {static_cast<unsigned>(i)}, "arr");
        }
        return arr;
    }


    // литерал структуры: insertvalue каждого поля (индекс из TypeRegistry)
    llvm::Value* Codegen::Impl::gen_struct_lit(const Parser::StructLitExpr& n) {
        std::string name(n.name);
        llvm::StructType* sty = llvm::StructType::getTypeByName(C(), name);
        if (!sty) return i32_zero();

        llvm::Value* s = llvm::UndefValue::get(sty);

        // получаем структуру из реестра для маппинга полей
        auto tid_opt = types->by_name(name);
        const Semantic::TypeRegistry::StructType* st_info = nullptr;
        if (tid_opt) st_info = types->get_struct(*tid_opt);

        for (const auto& [fname, fexpr] : n.fields) {
            // индекс поля по имени
            unsigned idx = 0;
            if (st_info) {
                for (unsigned i = 0; i < st_info->fields.size(); ++i) {
                    if (st_info->fields[i].first == fname) {
                        idx = i;
                        break;
                    }
                }
            }
            llvm::Value* val = gen_expr(*fexpr);
            s = B().CreateInsertValue(s, val, {idx}, "");
        }
        return s;
    }



    // получение указателя для присваивания: IdentExpr → alloca, Field/Index → GEP
    llvm::Value* Codegen::Impl::gen_lvalue_ptr(const Parser::Expr& e) {
        return std::visit([&](const auto& n) -> llvm::Value* {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::IdentExpr>) {
                std::string name(n.value);
                for (CGScope* s = cg_current_scope; s; s = s->parent) {
                    auto it = s->vars.find(name);
                    if (it != s->vars.end()) return it->second;
                }
                return nullptr;
            }
            else if constexpr (std::is_same_v<T, Parser::FieldExpr>) {
                llvm::Value* obj_ptr = gen_lvalue_ptr(*n.object);
                Semantic::TypeID obj_tid = expr_tid(n.object.get(),
                    types->builtin(TokenKind::KwInt32));
                llvm::Type* sty = to_llvm_type(obj_tid);

                unsigned fidx = 0;
                if (const auto* st = types->get_struct(obj_tid)) {
                    for (unsigned i = 0; i < st->fields.size(); ++i) {
                        if (st->fields[i].first == n.field) {
                            fidx = i;
                            break;
                        }
                    }
                }

                llvm::Value* gep[] = {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), 0),
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), fidx)
                };
                return B().CreateGEP(sty, obj_ptr, gep, "fld");
            }
            else if constexpr (std::is_same_v<T, Parser::IndexExpr>) {
                llvm::Value* arr_ptr = gen_lvalue_ptr(*n.array);
                llvm::Value* idx = gen_expr(*n.index);
                Semantic::TypeID arr_tid = expr_tid(n.array.get(),
                    types->builtin(TokenKind::KwInt32));
                llvm::Type* aty = to_llvm_type(arr_tid);
                llvm::Value* gep[] = {
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()), 0), idx
                };
                return B().CreateGEP(aty, arr_ptr, gep, "idx");
            }
            else {
                return nullptr;
            }
        }, e.node);
    }


    // switch по std::variant — делегирует конкретному gen_* методу
    void Codegen::Impl::gen_stmt(const Parser::Stmt& s) {
        std::visit([&](const auto& n) {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::LetStmt>) gen_let(n);
            else if constexpr (std::is_same_v<T, Parser::ExprStmt>) gen_expr_stmt(n);
            else if constexpr (std::is_same_v<T, Parser::BlockStmt>) gen_block(n);
            else if constexpr (std::is_same_v<T, Parser::IfStmt>) gen_if(n);
            else if constexpr (std::is_same_v<T, Parser::WhileStmt>) gen_while(n);
            else if constexpr (std::is_same_v<T, Parser::ReturnStmt>) gen_return(n);
            else if constexpr (std::is_same_v<T, Parser::BreakStmt>) gen_break();
            else if constexpr (std::is_same_v<T, Parser::ContinueStmt>) gen_continue();
            // NullStmt — ничего
        }, s.node);
    }


    // объявление переменной: alloca + store init, запись в CGScope
    void Codegen::Impl::gen_let(const Parser::LetStmt& n) {
        llvm::Value* init = gen_expr(*n.expr_init);
        llvm::Type* ty = init->getType();

        llvm::Value* alloca = create_entry_alloca(cg_fn,
            std::string(n.name), ty);
        B().CreateStore(init, alloca);
        cg_current_scope->vars[std::string(n.name)] = alloca;
        cg_current_scope->var_types[std::string(n.name)] = ty;
    }


    void Codegen::Impl::gen_expr_stmt(const Parser::ExprStmt& n) {
        gen_expr(*n.expr);
    }


    // блок: push/pop CGScope, обход инструкций
    void Codegen::Impl::gen_block(const Parser::BlockStmt& n) {
        push_cg_scope();
        for (const auto& s : n.elems) {
            gen_stmt(s);
            // код после return/break/continue недостижим
            if (B().GetInsertBlock()->getTerminator()) break;
        }
        pop_cg_scope();
    }


    // условный оператор: cond_br с then/else/merge базовыми блоками
    void Codegen::Impl::gen_if(const Parser::IfStmt& n) {
        llvm::Value* cond = gen_expr(*n.condition);
        if (!cond->getType()->isIntegerTy(1))
            cond = B().CreateICmpNE(cond,
                llvm::ConstantInt::get(cond->getType(), 0), "tobool");

        llvm::Function* fn = llvm::cast<llvm::Function>(cg_fn);
        llvm::BasicBlock* then_bb  = llvm::BasicBlock::Create(C(), "then", fn);
        llvm::BasicBlock* else_bb  = n.else_body
            ? llvm::BasicBlock::Create(C(), "else", fn) : nullptr;
        llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(C(), "if.end", fn);

        B().CreateCondBr(cond, then_bb, else_bb ? else_bb : merge_bb);

        // then — ветвимся из ТЕКУЩЕГО блока: после вложенного control flow
        // активный блок уже не then_bb, а merge/exit вложенной конструкции
        B().SetInsertPoint(then_bb);
        gen_stmt(*n.then_body);
        if (!B().GetInsertBlock()->getTerminator()) B().CreateBr(merge_bb);

        // else
        if (else_bb) {
            B().SetInsertPoint(else_bb);
            gen_stmt(*n.else_body);
            if (!B().GetInsertBlock()->getTerminator()) B().CreateBr(merge_bb);
        }

        B().SetInsertPoint(merge_bb);
    }



    // цикл: cond_bb → body_bb → exit_bb, стек break/continue
    void Codegen::Impl::gen_while(const Parser::WhileStmt& n) {
        llvm::Function* fn = llvm::cast<llvm::Function>(cg_fn);
        llvm::BasicBlock* cond_bb = llvm::BasicBlock::Create(C(), "while.cond", fn);
        llvm::BasicBlock* body_bb = llvm::BasicBlock::Create(C(), "while.body", fn);
        llvm::BasicBlock* exit_bb = llvm::BasicBlock::Create(C(), "while.end", fn);

        // сохраняем точки для break/continue (мосты в C-типизированном стеке)
        break_stack.push_back(exit_bb);
        continue_stack.push_back(cond_bb);

        B().CreateBr(cond_bb);

        // cond
        B().SetInsertPoint(cond_bb);
        llvm::Value* cond = gen_expr(*n.condition);
        if (!cond->getType()->isIntegerTy(1))
            cond = B().CreateICmpNE(cond,
                llvm::ConstantInt::get(cond->getType(), 0), "tobool");
        B().CreateCondBr(cond, body_bb, exit_bb);

        // body
        B().SetInsertPoint(body_bb);
        bool old_loop = cg_in_loop;
        cg_in_loop = true;
        gen_stmt(*n.body);
        cg_in_loop = old_loop;
        // back-edge из ТЕКУЩЕГО блока (после вложенного if тело уже не body_bb)
        if (!B().GetInsertBlock()->getTerminator()) B().CreateBr(cond_bb);

        // exit
        B().SetInsertPoint(exit_bb);
        break_stack.pop_back();
        continue_stack.pop_back();
    }



    // возврат из функции: ret val / ret void
    void Codegen::Impl::gen_return(const Parser::ReturnStmt& n) {
        if (n.value)
            B().CreateRet(gen_expr(*n.value));
        else
            B().CreateRetVoid();
    }



    void Codegen::Impl::gen_break() {
        B().CreateBr(break_stack.back());
    }

    void Codegen::Impl::gen_continue() {
        B().CreateBr(continue_stack.back());
    }



    // встроенные: print/println (диспетчер по типу), input, len, exit, panic, assert
    llvm::Value* Codegen::Impl::gen_builtin_call(const std::string& name,
                                           const std::vector<llvm::Value*>& args,
                                           const std::vector<Semantic::TypeID>& arg_tids,
                                           std::size_t line) {
        // распаковка аргументов в C++-значения
        std::vector<llvm::Value*> av;
        av.reserve(args.size());
        for (auto a : args) av.push_back(a);

        auto call_rt = [&](const char* fname, std::vector<llvm::Value*> a,
                           const char* lbl = "") -> llvm::Value* {
            auto fn = llvm::cast<llvm::Function>(functions[fname]);
            auto ft = llvm::cast<llvm::FunctionType>(function_types[fname]);
            return B().CreateCall(ft, fn, a, lbl);
        };
        llvm::Value* line_val = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(C()), line);

        if (name == "print" || name == "println") {
            const std::string pfx = (name == "println") ? "println" : "print";
            if (av.empty()) return nullptr;
            llvm::Value* val = av[0];
            llvm::Type* ty = val->getType();
            auto rt = [&](const std::string& suf, std::vector<llvm::Value*> a) {
                return call_rt(("__ferrous_" + pfx + "_" + suf).c_str(), a);
            };

            // строка
            if (ty->isStructTy()) {
                llvm::Value* ptr = B().CreateExtractValue(val, {0u});
                llvm::Value* len = B().CreateExtractValue(val, {1u});
                return rt("string", {ptr, len});
            }

            if (ty->isFloatingPointTy())
                return rt(ty->isFloatTy() ? "float32" : "float64", {val});
            // целое / char / bool — выбор функции по типу аргумента
            // (LLVM-ширины недостаточно: int32/uint32/char — все i32)
            Semantic::TypeID tid = arg_tids.empty()
                ? types->builtin(TokenKind::KwInt32)
                : types->resolve_alias(arg_tids[0]);
            auto is = [&](TokenKind k) { return types->equal(tid, types->builtin(k)); };
            const char* suf =
                is(TokenKind::KwInt8)   ? "int8"   : is(TokenKind::KwInt16)  ? "int16"  :
                is(TokenKind::KwInt32)  ? "int32"  : is(TokenKind::KwInt64)  ? "int64"  :
                is(TokenKind::KwUint8)  ? "uint8"  : is(TokenKind::KwUint16) ? "uint16" :
                is(TokenKind::KwUint32) ? "uint32" : is(TokenKind::KwUint64) ? "uint64" :
                is(TokenKind::KwChar)   ? "char"   : is(TokenKind::KwBool)   ? "bool"   :
                "int32";  // fallback (untyped/unknown)
            if (is(TokenKind::KwBool))  // i1 → i8 для рантайма
                val = B().CreateZExt(val, llvm::Type::getInt8Ty(C()), "boolb");
            return rt(suf, {val});
        }

        if (name == "input")
            return call_rt("__ferrous_input", {});

        if (name == "len") {
            if (!arg_tids.empty()) {
                Semantic::TypeID t = types->resolve_alias(arg_tids[0]);
                if (const auto* at = types->get_array(t)) {
                    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(C()),
                        at->size);
                }
            }
            llvm::Value* len = B().CreateExtractValue(av[0], {1u}, "len");
            return B().CreateTrunc(len,
                llvm::Type::getInt32Ty(C()), "len32");
        }

        // число → строка (T3): выбираем рантайм по типу аргумента
        if (name == "to_string") {
            if (av.empty()) return i32_zero();
            llvm::Value* v = av[0];
            Semantic::TypeID tid = types->resolve_alias(arg_tids[0]);
            auto is = [&](TokenKind k) { return types->equal(tid, types->builtin(k)); };
            llvm::Type* i64t = llvm::Type::getInt64Ty(C());
            if (is(TokenKind::KwFloat32))
                v = B().CreateFPExt(v, llvm::Type::getDoubleTy(C()), "f2d");
            if (is(TokenKind::KwFloat32) || is(TokenKind::KwFloat64))
                return call_rt("__ferrous_float_to_string", {v});
            if (is(TokenKind::KwBool)) {
                v = B().CreateZExt(v, llvm::Type::getInt8Ty(C()), "boolb");
                return call_rt("__ferrous_bool_to_string", {v});
            }
            // целочисленные: расширяем до i64 (знаково/беззнаково) и зовём нужный рантайм
            bool uns = is(TokenKind::KwUint8) || is(TokenKind::KwUint16)
                    || is(TokenKind::KwUint32) || is(TokenKind::KwUint64);
            if (v->getType()->getIntegerBitWidth() < 64)
                v = uns ? B().CreateZExt(v, i64t, "z64")
                        : B().CreateSExt(v, i64t, "s64");
            return call_rt(uns ? "__ferrous_uint_to_string"
                               : "__ferrous_int_to_string", {v});
        }

        // строка → число (T3)
        if (name == "to_int" || name == "to_float") {
            llvm::Value* ptr = B().CreateExtractValue(av[0], {0u});
            llvm::Value* len = B().CreateExtractValue(av[0], {1u});
            return call_rt(name == "to_int" ? "__ferrous_string_to_int"
                                            : "__ferrous_string_to_float", {ptr, len});
        }

        if (name == "exit") {
            if (av.empty()) return i32_zero();
            llvm::Value* code = av[0];
            if (code->getType()->getIntegerBitWidth() < 64)
                code = B().CreateSExt(code, llvm::Type::getInt64Ty(C()), "code");
            call_rt("__ferrous_exit", {code});
            return i32_zero();
        }

        if (name == "panic") {
            llvm::Value* ptr = B().CreateExtractValue(av[0], {0u});
            llvm::Value* len = B().CreateExtractValue(av[0], {1u});
            return call_rt("__ferrous_panic", {ptr, len, line_val});
        }

        if (name == "assert") {
            llvm::Value* cond = av[0];
            if (!cond->getType()->isIntegerTy(1))
                cond = B().CreateICmpNE(cond,
                    llvm::ConstantInt::get(cond->getType(), 0), "tobool");

            llvm::Function* fn = llvm::cast<llvm::Function>(cg_fn);
            llvm::BasicBlock* ok_bb   = llvm::BasicBlock::Create(C(), "assert.ok", fn);
            llvm::BasicBlock* fail_bb = llvm::BasicBlock::Create(C(), "assert.fail", fn);
            B().CreateCondBr(cond, ok_bb, fail_bb);

            B().SetInsertPoint(fail_bb);
            llvm::Value* msg_ptr;
            llvm::Value* msg_len;
            if (av.size() >= 2) {
                // assert(cond, msg) — сообщение пользователя
                msg_ptr = B().CreateExtractValue(av[1], {0u});
                msg_len = B().CreateExtractValue(av[1], {1u});
            } else {
                const char* msg = "assertion failed";
                msg_ptr = B().CreateGlobalString(msg, "assert_msg");
                msg_len = llvm::ConstantInt::get(
                    llvm::Type::getInt64Ty(C()), std::strlen(msg));
            }
            call_rt("__ferrous_assert_fail", {msg_ptr, msg_len, line_val});
            B().CreateUnreachable();

            B().SetInsertPoint(ok_bb);
            return llvm::ConstantInt::get(llvm::Type::getInt1Ty(C()), 1);
        }

        return i32_zero();
    }
    

    // генерация тела функции: параметры → allocas, обход тела
    void Codegen::Impl::define_function(const Parser::FnDecl& fn,
                                        const std::string& prefix) {
        std::string name = prefix + std::string(fn.name);
        llvm::Value* llvm_fn = nullptr;
        if (name == "main") {
            auto m_it = functions.find("main");
            if (m_it != functions.end()) llvm_fn = m_it->second;
            else return;
        } else {
            // тот же манглинг по типам, что и в declare/gen_call
            std::vector<Semantic::TypeID> ptids;
            for (const auto& [pname, ptype] : fn.params)
                ptids.push_back(resolve_typeid(ptype, *types,
                    types->builtin(TokenKind::KwVoid)));
            std::string mangled = mangle(name, ptids);
            auto it = functions.find(mangled);
            if (it != functions.end()) llvm_fn = it->second;
            else return;
        }

        cg_fn = llvm_fn;
        if (!cg_fn) return;
        cg_ns_prefix = prefix;   // для разрешения бесфиксных вызовов внутри namespace

        // entry-блок
        llvm::Function* fn_v = llvm::cast<llvm::Function>(cg_fn);
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(C(), "entry", fn_v);
        B().SetInsertPoint(entry);

        // возвращаемый тип
        cg_return_type = fn.return_type
            ? resolve_typeid(*fn.return_type, *types, types->void_type())
            : types->void_type();

        // скоуп параметров
        push_cg_scope();

        for (unsigned i = 0; i < fn.params.size(); ++i) {
            const auto& [pname, ptype] = fn.params[i];
            llvm::Argument* param = fn_v->getArg(i);
            llvm::Value* alloca = create_entry_alloca(cg_fn,
                std::string(pname), param->getType());
            B().CreateStore(param, alloca);
            cg_current_scope->vars[std::string(pname)] = alloca;
            cg_current_scope->var_types[std::string(pname)] = param->getType();
        }

        // скоуп тела
        push_cg_scope();
        cg_in_loop = false;

        for (const auto& s : fn.body.elems) {
            gen_stmt(s);
            if (B().GetInsertBlock()->getTerminator()) break;  // далее недостижимо (M5)
        }

        // хвост функции без терминатора: void → ret void; иначе блок недостижим
        if (!B().GetInsertBlock()->getTerminator()) {
            if (types->equal(cg_return_type, types->void_type()))
                B().CreateRetVoid();
            else
                B().CreateUnreachable();
        }

        pop_cg_scope();
        pop_cg_scope();
        cg_fn = nullptr;
    }


    void Codegen::Impl::define_functions_rec(const std::vector<Parser::Decl>& decls,
                                             const std::string& prefix) {
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node))
                define_function(*fn, prefix);
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                define_functions_rec(ns->decls,
                    prefix + std::string(ns->name) + "::");
        }
    }

    void Codegen::Impl::define_functions(const std::vector<Parser::Decl>& decls) {
        define_functions_rec(decls);
    }
    
    // основной метод: declare → define → verify → запись .ll → clang++ → executable
    bool Codegen::Impl::generate(const std::vector<Parser::Decl>& decls,
                           const Semantic::AnnotatedAST& aast,
                           const std::string& output_path) {
        this->aast = &aast;
        this->types = aast.types;

        ctx = new llvm::LLVMContext();
        mod = new llvm::Module("ferrous_module", *ctx);
        // triple хоста — иначе clang++ перетирает пустой triple
        mod->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));
        builder = new llvm::IRBuilder<>(*ctx);

        // инициализация нативного таргета
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();

        declare_runtime_functions();
        declare_structs(decls);
        declare_functions(decls);
        define_functions(decls);

        // внутренний рассинхрон фаз (неразрешённый вызов и т.п.) — прерываем
        if (cg_failed) {
            std::cerr << "codegen aborted due to internal errors\n";
            return false;
        }

        // верификация — при ошибке прерываем (не пишем .ll, не линкуем)
        std::string vfy_err;
        llvm::raw_string_ostream vfy_os(vfy_err);
        if (llvm::verifyModule(M(), &vfy_os)) {
            std::cerr << "LLVM verification error:\n" << vfy_err << '\n';
            return false;
        }

        // запись .ll
        std::string ll_path = output_path + ".ll";
        std::error_code ec;
        llvm::raw_fd_ostream ll_out(ll_path, ec);
        if (ec) {
            std::cerr << "cannot write " << ll_path << ": " << ec.message() << '\n';
            return false;
        }
        M().print(ll_out, nullptr);
        ll_out.flush();

        // компиляция рантайма + линковка исполняемого файла.
        std::string rt_o = output_path + ".rt.o";
        std::string cmd_rt =
            std::string("clang++ -std=c++23 -c ") + FERROUS_RT_SRC + " -o " + rt_o;
        int ret_rt = std::system(cmd_rt.c_str());
        if (ret_rt != 0) {
            std::cerr << "runtime compilation failed (exit " << ret_rt << ")\n";
            return false;
        }

        std::string cmd = "clang++ -O1 " + ll_path + " " + rt_o + " -o " + output_path;
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::cerr << "linking failed (exit code " << ret << "): " << cmd << "\n";
            return false;
        }
        return true;
    }

} // namespace Codegen
