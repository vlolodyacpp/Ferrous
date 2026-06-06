module;
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>

module Ferrous.Codegen;
import Ferrous.Printer;
namespace Codegen {

    using TokenKind = Lexer::TokenKind;

    // TypeRef → TypeID (для codegen, аналог Semantic::resolve_type)
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
            return fallback;
        }, tr.node);
    }

    // конструктор по умолчанию — все ресурсы создаются в generate()
    Codegen::Codegen() = default;

    // освобождение LLVM-ресурсов
    Codegen::~Codegen() {
        if (builder) { LLVMDisposeBuilder(builder); builder = nullptr; }
        if (mod)     { LLVMDisposeModule(mod);     mod = nullptr;     }
        if (ctx)     { LLVMContextDispose(ctx);     ctx = nullptr;    }
    }

    // преобразование Ferrous-типа в LLVM-тип
    LLVMTypeRef Codegen::to_llvm_type(Semantic::TypeID tid) const {
        // string = { i8*, i64 }
        if (types->equal(tid, types->builtin(TokenKind::KwString))) {
            LLVMTypeRef fields[] = {
                LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),
                LLVMInt64TypeInContext(ctx)
            };
            return LLVMStructTypeInContext(ctx, fields, 2, false);
        }

        // void
        if (types->equal(tid, types->builtin(TokenKind::KwVoid)))
            return LLVMVoidTypeInContext(ctx);

        // bool → i1
        if (types->equal(tid, types->builtin(TokenKind::KwBool)))
            return LLVMInt1TypeInContext(ctx);

        // char → i32 (UTF-32 codepoint)
        if (types->equal(tid, types->builtin(TokenKind::KwChar)))
            return LLVMInt32TypeInContext(ctx);

        // целые
        if (types->equal(tid, types->builtin(TokenKind::KwInt8)))
            return LLVMInt8TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwInt16)))
            return LLVMInt16TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwInt32)))
            return LLVMInt32TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwInt64)))
            return LLVMInt64TypeInContext(ctx);

        // беззнаковые (те же LLVM-типы)
        if (types->equal(tid, types->builtin(TokenKind::KwUint8)))
            return LLVMInt8TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwUint16)))
            return LLVMInt16TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwUint32)))
            return LLVMInt32TypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwUint64)))
            return LLVMInt64TypeInContext(ctx);

        // float
        if (types->equal(tid, types->builtin(TokenKind::KwFloat32)))
            return LLVMFloatTypeInContext(ctx);
        if (types->equal(tid, types->builtin(TokenKind::KwFloat64)))
            return LLVMDoubleTypeInContext(ctx);

        // массив
        if (const auto* arr = types->get_array(tid)) {
            LLVMTypeRef elem = to_llvm_type(arr->elem);
            return LLVMArrayType(elem, static_cast<unsigned>(arr->size));
        }

        // структура
        if (const auto* st = types->get_struct(tid)) {
            std::string name = st->name;
            LLVMTypeRef sty = LLVMGetTypeByName2(ctx, name.c_str());
            if (sty) return sty;
            return LLVMStructCreateNamed(ctx, name.c_str());
        }

        // псевдоним (разворачиваем)
        Semantic::TypeID resolved = types->resolve_alias(tid);
        if (resolved.id != tid.id)
            return to_llvm_type(resolved);

        // сюда попадать не должны — ошибка в семантике
        std::cerr << "codegen: unhandled type " << types->name(tid) << '\n';
        return LLVMInt32TypeInContext(ctx);
    }

    // размещает alloca в entry-блоке функции (до первой инструкции)
    LLVMValueRef Codegen::create_entry_alloca(LLVMValueRef fn,
                                              const std::string& name,
                                              LLVMTypeRef type) {
        LLVMBuilderRef tmp = LLVMCreateBuilderInContext(ctx);
        LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(fn);
        LLVMValueRef first = LLVMGetFirstInstruction(entry);
        if (first)
            LLVMPositionBuilderBefore(tmp, first);
        else
            LLVMPositionBuilderAtEnd(tmp, entry);
        LLVMValueRef alloca = LLVMBuildAlloca(tmp, type, name.c_str());
        LLVMDisposeBuilder(tmp);
        return alloca;
    }

    // создание дочернего скоупа (при входе в блок)
    void Codegen::push_cg_scope() {
        auto& s = scope_storage.emplace_back();
        s.parent = cg_current_scope;
        cg_current_scope = &s;
    }

    void Codegen::pop_cg_scope() {
        if (cg_current_scope)
            cg_current_scope = cg_current_scope->parent;
    }

    // объявление всех runtime-функций в модуле LLVM
    // panic, bounds/div check, print/println, input, строковые операции
    void Codegen::declare_runtime_functions() {
        LLVMTypeRef void_ty  = LLVMVoidTypeInContext(ctx);
        LLVMTypeRef i8p_ty   = LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);
        LLVMTypeRef i64_ty   = LLVMInt64TypeInContext(ctx);
        LLVMTypeRef double_ty = LLVMDoubleTypeInContext(ctx);

        // строковый тип {i8*, i64}
        LLVMTypeRef str_fields[] = { i8p_ty, i64_ty };
        LLVMTypeRef str_ty = LLVMStructTypeInContext(ctx, str_fields, 2, false);

        auto decl = [&](const char* name, LLVMTypeRef ret,
                        std::vector<LLVMTypeRef> params) {
            LLVMTypeRef ft = LLVMFunctionType(ret, params.data(),
                static_cast<unsigned>(params.size()), false);
            functions[name] = LLVMAddFunction(mod, name, ft);
            function_types[name] = ft;
        };

        // panic + assert
        decl("__ferrous_panic",       void_ty, {i8p_ty, i64_ty, i64_ty});
        decl("__ferrous_assert_fail", void_ty, {i8p_ty, i64_ty, i64_ty});

        // проверки
        decl("__ferrous_div_check",   i64_ty, {i64_ty, i64_ty});
        decl("__ferrous_mod_check",   i64_ty, {i64_ty, i64_ty});
        decl("__ferrous_bounds_check", void_ty, {i64_ty, i64_ty, i64_ty});

        // вывод
        decl("__ferrous_print_int64",    void_ty, {i64_ty});
        decl("__ferrous_print_float64",  void_ty, {double_ty});
        decl("__ferrous_print_string",   void_ty, {i8p_ty, i64_ty});
        decl("__ferrous_println_int64",  void_ty, {i64_ty});
        decl("__ferrous_println_float64",void_ty, {double_ty});
        decl("__ferrous_println_string", void_ty, {i8p_ty, i64_ty});

        // ввод
        decl("__ferrous_input", str_ty, {});

        // строковые операции
        decl("__ferrous_str_concat", str_ty, {i8p_ty, i64_ty, i8p_ty, i64_ty});
        decl("__ferrous_str_eq",     LLVMInt8TypeInContext(ctx),
             {i8p_ty, i64_ty, i8p_ty, i64_ty});

        // преобразования
        decl("__ferrous_int_to_str",   str_ty, {i64_ty});
        decl("__ferrous_float_to_str", str_ty, {double_ty});
    }

    // регистрация структур в модуле: LLVMStructCreateNamed + поля
    void Codegen::declare_structs(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* st = std::get_if<Parser::StructDecl>(&d.node)) {
                std::string name(st->name);
                LLVMTypeRef sty = LLVMGetTypeByName2(ctx, name.c_str());
                if (!sty) continue;

                std::vector<LLVMTypeRef> field_types;
                for (const auto& [fname, ftype] : st->field) {
                    Semantic::TypeID ftid = std::visit(
                        [&](const auto& t) -> Semantic::TypeID {
                            using T = std::decay_t<decltype(t)>;
                            if constexpr (std::is_same_v<T, Parser::BuiltinTypeRef>)
                                return types->builtin(t.type_kind);
                            if constexpr (std::is_same_v<T, Parser::NamedTypeRef>) {
                                auto tid = types->by_name(std::string(t.name));
                                return tid.value_or(types->builtin(TokenKind::KwVoid));
                            }
                            return types->builtin(TokenKind::KwVoid);
                        }, ftype.node);
                    field_types.push_back(to_llvm_type(ftid));
                }
                LLVMStructSetBody(sty, field_types.data(),
                    static_cast<unsigned>(field_types.size()), false);
            }
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                declare_structs(ns->decls);
        }
    }

    // регистрация функций пользователя (main — без манглинга, остальные — с манглингом по типам)
    void Codegen::declare_functions_rec(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node)) {
                std::string name(fn->name);

                // main не манглим
                if (name == "main") {
                    Semantic::TypeID rtid = fn->return_type
                        ? resolve_typeid(*fn->return_type, *types,
                            types->builtin(TokenKind::KwInt32))
                        : types->builtin(TokenKind::KwInt32);
                    LLVMTypeRef rt = to_llvm_type(rtid);
                    LLVMTypeRef ft = LLVMFunctionType(rt, nullptr, 0, false);
                    functions["main"] = LLVMAddFunction(mod, "main", ft);
                    function_types["main"] = ft;
                    continue;
                }

                // манглинг: name + типы параметров для перегрузок
                std::string mangled = name;
                std::vector<LLVMTypeRef> param_types;
                for (const auto& [pname, ptype] : fn->params) {
                    auto tid = resolve_typeid(ptype, *types,
                        types->builtin(TokenKind::KwVoid));
                    LLVMTypeRef lt = to_llvm_type(tid);
                    param_types.push_back(lt);
                    mangled += "_" + std::to_string(
                        static_cast<int>(LLVMGetTypeKind(lt)));
                }

                Semantic::TypeID rtid = fn->return_type
                    ? resolve_typeid(*fn->return_type, *types,
                        types->void_type())
                    : types->void_type();
                LLVMTypeRef rt = to_llvm_type(rtid);

                LLVMTypeRef ft = LLVMFunctionType(rt, param_types.data(),
                    static_cast<unsigned>(param_types.size()), false);
                functions[mangled] = LLVMAddFunction(mod, mangled.c_str(), ft);
                function_types[mangled] = ft;
            }
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                declare_functions_rec(ns->decls);
        }
    }

    void Codegen::declare_functions(const std::vector<Parser::Decl>& decls) {
        declare_functions_rec(decls);
    }



    // switch по std::variant — делегирует конкретному gen_* методу
    LLVMValueRef Codegen::gen_expr(const Parser::Expr& e) {
        return std::visit([&](const auto& n) -> LLVMValueRef {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::LitIntExpr>)
                return gen_lit_int(n);
            if constexpr (std::is_same_v<T, Parser::LitFloatExpr>)
                return gen_lit_float(n);
            if constexpr (std::is_same_v<T, Parser::LitBoolExpr>)
                return gen_lit_bool(n);
            if constexpr (std::is_same_v<T, Parser::LitStringExpr>)
                return gen_lit_string(n);
            if constexpr (std::is_same_v<T, Parser::LitCharExpr>)
                return gen_lit_char(n);
            if constexpr (std::is_same_v<T, Parser::ErrorExpr>)
                return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
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
                return gen_array_lit(n);
            if constexpr (std::is_same_v<T, Parser::StructLitExpr>)
                return gen_struct_lit(n);
            return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
        }, e.node);
    }


    // целочисленный литерал: парсинг (dec/hex/bin) + суффикс → ConstInt
    LLVMValueRef Codegen::gen_lit_int(const Parser::LitIntExpr& n) {
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
        auto it = aast->expr_type.find(&static_cast<const Parser::Expr&>(
            *reinterpret_cast<const Parser::Expr*>(&n)));
        Semantic::TypeID tid = (it != aast->expr_type.end())
            ? it->second : types->builtin(TokenKind::KwInt32);
        tid = types->resolve_alias(tid);

        LLVMTypeRef lt = to_llvm_type(tid);
        return LLVMConstInt(lt, val, 0);
    }


    // float-литерал: from_chars + nan/inf → ConstReal
    LLVMValueRef Codegen::gen_lit_float(const Parser::LitFloatExpr& n) {
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

        auto it = aast->expr_type.find(&static_cast<const Parser::Expr&>(
            *reinterpret_cast<const Parser::Expr*>(&n)));
        Semantic::TypeID tid = (it != aast->expr_type.end())
            ? it->second : types->builtin(TokenKind::KwFloat64);
        tid = types->resolve_alias(tid);

        LLVMTypeRef lt = to_llvm_type(tid);
        return LLVMConstReal(lt, val);
    }

    // булев литерал → i1: 0 или 1
    LLVMValueRef Codegen::gen_lit_bool(const Parser::LitBoolExpr& n) {
        return LLVMConstInt(LLVMInt1TypeInContext(ctx), n.value ? 1 : 0, false);
    }


    // строковый литерал: GlobalStringPtr + упаковка в {i8*, i64}
    LLVMValueRef Codegen::gen_lit_string(const Parser::LitStringExpr& n) {
        std::string str(n.value);
        LLVMValueRef global = LLVMBuildGlobalStringPtr(builder,
            str.c_str(), "str");
        LLVMValueRef len = LLVMConstInt(LLVMInt64TypeInContext(ctx),
            str.size(), false);

        LLVMValueRef s = LLVMGetUndef(to_llvm_type(
            types->builtin(TokenKind::KwString)));
        s = LLVMBuildInsertValue(builder, s, global, 0, "");
        s = LLVMBuildInsertValue(builder, s, len, 1, "");
        return s;
    }



    // символьный литерал: разбор escape → i32 codepoint
    LLVMValueRef Codegen::gen_lit_char(const Parser::LitCharExpr& n) {
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

        return LLVMConstInt(LLVMInt32TypeInContext(ctx), cp, false);
    }

    // ── gen_ident ──────────────────────────────────────────────────────

    // загрузка переменной: поиск в цепочке CGScope → Load из alloca
    LLVMValueRef Codegen::gen_ident(const Parser::IdentExpr& n) {
        std::string name(n.value);
        for (CGScope* s = cg_current_scope; s; s = s->parent) {
            auto it = s->vars.find(name);
            if (it != s->vars.end()) {
                auto tit = s->var_types.find(name);
                LLVMTypeRef ty = (tit != s->var_types.end())
                    ? tit->second : LLVMInt32TypeInContext(ctx);
                return LLVMBuildLoad2(builder, ty, it->second, name.c_str());
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
    }

    // ── gen_path ───────────────────────────────────────────────────────

    // доступ к переменной через namespace: обход CGScope по последнему сегменту
    LLVMValueRef Codegen::gen_path(const Parser::PathExpr& n) {
        std::string_view last = n.segments.back();
        for (CGScope* s = cg_current_scope; s; s = s->parent) {
            auto it = s->vars.find(std::string(last));
            if (it != s->vars.end()) {
                LLVMTypeRef ty = LLVMTypeOf(it->second);
                return LLVMBuildLoad2(builder,
                    LLVMGetElementType(ty), it->second,
                    std::string(last).c_str());
            }
        }
        return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
    }

    // ── gen_unary ──────────────────────────────────────────────────────

    // унарный минус (Neg/FNeg) или логическое НЕ (Not)
    LLVMValueRef Codegen::gen_unary(const Parser::UnaryExpr& n) {
        LLVMValueRef op = gen_expr(*n.operand);
        LLVMTypeKind tk = LLVMGetTypeKind(LLVMTypeOf(op));

        if (n.op == TokenKind::OpMinus) {
            if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind)
                return LLVMBuildFNeg(builder, op, "neg");
            return LLVMBuildNeg(builder, op, "neg");
        }
        if (n.op == TokenKind::OpBang)
            return LLVMBuildNot(builder, op, "not");
        return op;
    }

    // ── gen_binary ─────────────────────────────────────────────────────

    // бинарные операторы: арифметика, сравнения, логика, присваивание, строки
    LLVMValueRef Codegen::gen_binary(const Parser::BinaryExpr& n) {
        // присваивание
        if (n.op == TokenKind::OpEq) {
            LLVMValueRef rhs = gen_expr(*n.rhs);
            LLVMValueRef ptr = gen_lvalue_ptr(*n.lhs);
            LLVMBuildStore(builder, rhs, ptr);
            return rhs;
        }

        LLVMValueRef lhs = gen_expr(*n.lhs);
        LLVMValueRef rhs = gen_expr(*n.rhs);
        LLVMTypeRef ty = LLVMTypeOf(lhs);
        LLVMTypeKind tk = LLVMGetTypeKind(ty);
        bool is_float = (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind);

        switch (n.op) {
            case TokenKind::OpPlus: {
                // строковая конкатенация
                if (LLVMGetTypeKind(ty) == LLVMStructTypeKind) {
                    LLVMValueRef a_ptr = LLVMBuildExtractValue(builder, lhs, 0, "a.ptr");
                    LLVMValueRef a_len = LLVMBuildExtractValue(builder, lhs, 1, "a.len");
                    LLVMValueRef b_ptr = LLVMBuildExtractValue(builder, rhs, 0, "b.ptr");
                    LLVMValueRef b_len = LLVMBuildExtractValue(builder, rhs, 1, "b.len");
                    LLVMValueRef fn = functions["__ferrous_str_concat"];
                    LLVMTypeRef ft = function_types["__ferrous_str_concat"];
                    LLVMValueRef a[] = {a_ptr, a_len, b_ptr, b_len};
                    return LLVMBuildCall2(builder, ft, fn, a, 4, "concat");
                }
                if (is_float) return LLVMBuildFAdd(builder, lhs, rhs, "add");
                return LLVMBuildAdd(builder, lhs, rhs, "add");
            }
            case TokenKind::OpMinus:
                if (is_float) return LLVMBuildFSub(builder, lhs, rhs, "sub");
                return LLVMBuildSub(builder, lhs, rhs, "sub");
            case TokenKind::OpStar:
                if (is_float) return LLVMBuildFMul(builder, lhs, rhs, "mul");
                return LLVMBuildMul(builder, lhs, rhs, "mul");
            case TokenKind::OpSlash: {
                if (is_float) return LLVMBuildFDiv(builder, lhs, rhs, "div");
                // проверка деления на ноль
                LLVMValueRef div_fn = functions["__ferrous_div_check"];
                LLVMTypeRef div_ft = function_types["__ferrous_div_check"];
                LLVMValueRef line = LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, false);
                LLVMValueRef div_args[] = {rhs, line};
                LLVMValueRef checked = LLVMBuildCall2(builder, div_ft, div_fn,
                    div_args, 2, "div_check");
                return LLVMBuildSDiv(builder, lhs, checked, "div");
            }
            case TokenKind::OpPercent: {
                // проверка деления на ноль
                LLVMValueRef mod_fn = functions["__ferrous_mod_check"];
                LLVMTypeRef mod_ft = function_types["__ferrous_mod_check"];
                LLVMValueRef line = LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, false);
                LLVMValueRef mod_args[] = {rhs, line};
                LLVMValueRef checked = LLVMBuildCall2(builder, mod_ft, mod_fn,
                    mod_args, 2, "mod_check");
                return LLVMBuildSRem(builder, lhs, checked, "rem");
            }

            case TokenKind::OpEqEq:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealOEQ, lhs, rhs, "eq");
                return LLVMBuildICmp(builder, LLVMIntEQ, lhs, rhs, "eq");
            case TokenKind::OpBangEq:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealONE, lhs, rhs, "ne");
                return LLVMBuildICmp(builder, LLVMIntNE, lhs, rhs, "ne");
            case TokenKind::OpLt:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealOLT, lhs, rhs, "lt");
                return LLVMBuildICmp(builder, LLVMIntSLT, lhs, rhs, "lt");
            case TokenKind::OpLtEq:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealOLE, lhs, rhs, "le");
                return LLVMBuildICmp(builder, LLVMIntSLE, lhs, rhs, "le");
            case TokenKind::OpGt:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealOGT, lhs, rhs, "gt");
                return LLVMBuildICmp(builder, LLVMIntSGT, lhs, rhs, "gt");
            case TokenKind::OpGtEq:
                if (is_float) return LLVMBuildFCmp(builder, LLVMRealOGE, lhs, rhs, "ge");
                return LLVMBuildICmp(builder, LLVMIntSGE, lhs, rhs, "ge");

            case TokenKind::OpAndAnd:
                return LLVMBuildAnd(builder, lhs, rhs, "and");
            case TokenKind::OpOrOr:
                return LLVMBuildOr(builder, lhs, rhs, "or");

            default:
                return lhs;
        }
    }

    // ── gen_group ──────────────────────────────────────────────────────

    // скобки: прозрачно возвращает inner
    LLVMValueRef Codegen::gen_group(const Parser::GroupExpr& n) {
        return gen_expr(*n.inner);
    }

    // ── gen_cast ───────────────────────────────────────────────────────

    // приведение типа: SExt/Trunc/SIToFP/FPToSI по таблице
    LLVMValueRef Codegen::gen_cast(const Parser::CastExpr& n) {
        LLVMValueRef src = gen_expr(*n.expr);
        Semantic::TypeID dst_tid = resolve_typeid(n.target, *types,
            types->builtin(TokenKind::KwInt32));
        LLVMTypeRef dst = to_llvm_type(dst_tid);
        LLVMTypeRef src_ty = LLVMTypeOf(src);
        LLVMTypeKind stk = LLVMGetTypeKind(src_ty);
        LLVMTypeKind dtk = LLVMGetTypeKind(dst);

        // одинаковые типы
        if (stk == dtk && LLVMGetIntTypeWidth(src_ty) == LLVMGetIntTypeWidth(dst))
            return src;

        // float ↔ int
        if (stk == LLVMFloatTypeKind || stk == LLVMDoubleTypeKind) {
            if (dtk == LLVMIntegerTypeKind)
                return LLVMBuildFPToSI(builder, src, dst, "cast");
            // float → float (fpext/fptrunc)
            if (LLVMGetIntTypeWidth(src_ty) < LLVMGetIntTypeWidth(dst) ||
                stk == LLVMFloatTypeKind)
                return LLVMBuildFPExt(builder, src, dst, "cast");
            return LLVMBuildFPTrunc(builder, src, dst, "cast");
        }
        if (dtk == LLVMFloatTypeKind || dtk == LLVMDoubleTypeKind)
            return LLVMBuildSIToFP(builder, src, dst, "cast");

        // int ↔ int (sext/trunc)
        if (LLVMGetIntTypeWidth(src_ty) < LLVMGetIntTypeWidth(dst))
            return LLVMBuildSExt(builder, src, dst, "cast");
        return LLVMBuildTrunc(builder, src, dst, "cast");
    }


    // вызов функции: разрешение имени → встроенная или пользовательская
    LLVMValueRef Codegen::gen_call(const Parser::CallExpr& n) {
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
            return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
        }

        // аргументы
        std::vector<LLVMValueRef> args;
        for (const auto& arg : n.args)
            args.push_back(gen_expr(arg));

        // встроенные функции
        if (fn_name == "print" || fn_name == "println" ||
            fn_name == "input" || fn_name == "len" ||
            fn_name == "exit" || fn_name == "panic" ||
            fn_name == "assert") {
            return gen_builtin_call(fn_name, args, 0);
        }

        // пользовательская функция — ищем по манглингу
        std::string mangled = fn_name;
        for (const auto& a : args) {
            LLVMTypeRef at = LLVMTypeOf(a);
            mangled += "_" + std::to_string(static_cast<int>(LLVMGetTypeKind(at)));
        }

        LLVMValueRef callee = nullptr;
        if (auto it = functions.find(mangled); it != functions.end())
            callee = it->second;
        else if (auto it = functions.find(fn_name); it != functions.end())
            callee = it->second;

        if (!callee)
            return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);

        LLVMTypeRef ft = nullptr;
        if (auto tit = function_types.find(mangled); tit != function_types.end())
            ft = tit->second;
        else if (auto tit = function_types.find(fn_name); tit != function_types.end())
            ft = tit->second;
        if (!ft) ft = LLVMFunctionType(LLVMInt32TypeInContext(ctx), nullptr, 0, false);

        return LLVMBuildCall2(builder, ft, callee, args.data(),
            static_cast<unsigned>(args.size()), "");
    }

    // индексация массива: bounds check + GEP + Load
    LLVMValueRef Codegen::gen_index(const Parser::IndexExpr& n) {
        LLVMValueRef idx = gen_expr(*n.index);
        LLVMValueRef ptr = gen_lvalue_ptr(*n.array);

        // bounds check
        LLVMTypeRef arr_ty = LLVMGetAllocatedType(ptr)
            ? LLVMGetAllocatedType(ptr) : LLVMTypeOf(ptr);
        if (LLVMGetTypeKind(arr_ty) == LLVMArrayTypeKind) {
            LLVMValueRef len = LLVMConstInt(LLVMInt64TypeInContext(ctx),
                LLVMGetArrayLength(arr_ty), false);
            LLVMValueRef promoted_idx = idx;
            if (LLVMGetIntTypeWidth(LLVMTypeOf(promoted_idx)) < 64)
                promoted_idx = LLVMBuildSExt(builder, promoted_idx,
                    LLVMInt64TypeInContext(ctx), "idx64");
            LLVMValueRef bc_fn = functions["__ferrous_bounds_check"];
            LLVMTypeRef bc_ft = function_types["__ferrous_bounds_check"];
            LLVMValueRef line = LLVMConstInt(LLVMInt64TypeInContext(ctx), 0, false);
            LLVMValueRef bc_args[] = {promoted_idx, len, line};
            LLVMBuildCall2(builder, bc_ft, bc_fn, bc_args, 3, "");
        }

        LLVMValueRef gep_indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false), idx
        };
        LLVMValueRef elem_ptr = LLVMBuildGEP2(builder,
            LLVMGetAllocatedType(ptr) ? LLVMGetAllocatedType(ptr)
                : LLVMTypeOf(ptr),
            ptr, gep_indices, 2, "idx");
        return LLVMBuildLoad2(builder,
            LLVMGetAllocatedType(ptr)
                ? LLVMGetElementType(LLVMGetAllocatedType(ptr))
                : LLVMInt32TypeInContext(ctx),
            elem_ptr, "elem");
    }


    // доступ к полю структуры: индекс поля из TypeRegistry → GEP + Load
    LLVMValueRef Codegen::gen_field(const Parser::FieldExpr& n) {
        LLVMValueRef obj_ptr = gen_lvalue_ptr(*n.object);
        LLVMTypeRef sty = LLVMGetAllocatedType(obj_ptr);
        if (!sty) sty = LLVMTypeOf(obj_ptr);

        unsigned idx = 0;
        // получаем тип объекта из аннотаций и ищем индекс поля
        auto it = aast->expr_type.find(n.object.get());
        if (it != aast->expr_type.end()) {
            Semantic::TypeID obj_tid = types->resolve_alias(it->second);
            if (const auto* st = types->get_struct(obj_tid)) {
                for (unsigned i = 0; i < st->fields.size(); ++i) {
                    if (st->fields[i].first == n.field) {
                        idx = i;
                        break;
                    }
                }
            }
        }

        LLVMValueRef gep_indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false),
            LLVMConstInt(LLVMInt32TypeInContext(ctx), idx, false)
        };
        LLVMValueRef field_ptr = LLVMBuildGEP2(builder, sty, obj_ptr,
            gep_indices, 2, "field");
        return LLVMBuildLoad2(builder,
            LLVMGetElementType(sty)
                ? LLVMGetElementType(sty)
                : LLVMInt32TypeInContext(ctx),
            field_ptr, n.field.data());
    }

    // литерал массива: insertvalue каждого элемента в undef
    LLVMValueRef Codegen::gen_array_lit(const Parser::ArrayLitExpr& n) {
        auto it = aast->expr_type.find(
            &static_cast<const Parser::Expr&>(
                *reinterpret_cast<const Parser::Expr*>(&n)));
        Semantic::TypeID tid = (it != aast->expr_type.end())
            ? it->second : types->builtin(TokenKind::KwInt32);
        LLVMTypeRef aty = to_llvm_type(tid);

        LLVMValueRef arr = LLVMGetUndef(aty);
        for (std::size_t i = 0; i < n.elems.size(); ++i) {
            LLVMValueRef elem = gen_expr(n.elems[i]);
            arr = LLVMBuildInsertValue(builder, arr, elem,
                static_cast<unsigned>(i), "arr");
        }
        return arr;
    }


    // литерал структуры: insertvalue каждого поля (индекс из TypeRegistry)
    LLVMValueRef Codegen::gen_struct_lit(const Parser::StructLitExpr& n) {
        std::string name(n.name);
        LLVMTypeRef sty = LLVMGetTypeByName2(ctx, name.c_str());
        if (!sty) return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);

        LLVMValueRef s = LLVMGetUndef(sty);

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
            LLVMValueRef val = gen_expr(*fexpr);
            s = LLVMBuildInsertValue(builder, s, val, idx, "");
        }
        return s;
    }



    // получение указателя для присваивания: IdentExpr → alloca, FieldExpr → GEP
    LLVMValueRef Codegen::gen_lvalue_ptr(const Parser::Expr& e) {
        return std::visit([&](const auto& n) -> LLVMValueRef {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::IdentExpr>) {
                std::string name(n.value);
                for (CGScope* s = cg_current_scope; s; s = s->parent) {
                    auto it = s->vars.find(name);
                    if (it != s->vars.end()) return it->second;
                }
                return nullptr;
            }
            if constexpr (std::is_same_v<T, Parser::FieldExpr>) {
                LLVMValueRef obj_ptr = gen_lvalue_ptr(*n.object);
                LLVMTypeRef sty = LLVMGetAllocatedType(obj_ptr);

                // индекс поля по имени
                unsigned fidx = 0;
                auto it = aast->expr_type.find(n.object.get());
                if (it != aast->expr_type.end()) {
                    Semantic::TypeID obj_tid = types->resolve_alias(it->second);
                    if (const auto* st = types->get_struct(obj_tid)) {
                        for (unsigned i = 0; i < st->fields.size(); ++i) {
                            if (st->fields[i].first == n.field) {
                                fidx = i;
                                break;
                            }
                        }
                    }
                }

                LLVMValueRef gep[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false),
                    LLVMConstInt(LLVMInt32TypeInContext(ctx), fidx, false)
                };
                return LLVMBuildGEP2(builder, sty, obj_ptr, gep, 2, "fld");
            }
            if constexpr (std::is_same_v<T, Parser::IndexExpr>) {
                LLVMValueRef arr_ptr = gen_lvalue_ptr(*n.array);
                LLVMValueRef idx = gen_expr(*n.index);
                LLVMTypeRef aty = LLVMGetAllocatedType(arr_ptr);
                LLVMValueRef gep[] = {
                    LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false), idx
                };
                return LLVMBuildGEP2(builder, aty, arr_ptr, gep, 2, "idx");
            }
            return nullptr;
        }, e.node);
    }


    // switch по std::variant — делегирует конкретному gen_* методу
    void Codegen::gen_stmt(const Parser::Stmt& s) {
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
    void Codegen::gen_let(const Parser::LetStmt& n) {
        LLVMValueRef init = gen_expr(*n.expr_init);
        LLVMTypeRef ty = LLVMTypeOf(init);

        LLVMValueRef alloca = create_entry_alloca(cg_fn,
            std::string(n.name), ty);
        LLVMBuildStore(builder, init, alloca);
        cg_current_scope->vars[std::string(n.name)] = alloca;
        cg_current_scope->var_types[std::string(n.name)] = ty;
    }


    void Codegen::gen_expr_stmt(const Parser::ExprStmt& n) {
        gen_expr(*n.expr);
    }


    // блок: push/pop CGScope, обход инструкций
    void Codegen::gen_block(const Parser::BlockStmt& n) {
        push_cg_scope();
        for (const auto& s : n.elems) gen_stmt(s);
        pop_cg_scope();
    }


    // условный оператор: cond_br с then/else/merge базовыми блоками
    void Codegen::gen_if(const Parser::IfStmt& n) {
        LLVMValueRef cond = gen_expr(*n.condition);
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(builder, LLVMIntNE, cond,
                LLVMConstInt(LLVMTypeOf(cond), 0, false), "tobool");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "then");
        LLVMBasicBlockRef else_bb = n.else_body
            ? LLVMAppendBasicBlockInContext(ctx, cg_fn, "else") : nullptr;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "if.end");

        LLVMBuildCondBr(builder, cond, then_bb,
            else_bb ? else_bb : merge_bb);

        // then
        LLVMPositionBuilderAtEnd(builder, then_bb);
        gen_stmt(*n.then_body);
        if (!LLVMGetBasicBlockTerminator(then_bb))
            LLVMBuildBr(builder, merge_bb);

        // else
        if (else_bb) {
            LLVMPositionBuilderAtEnd(builder, else_bb);
            gen_stmt(*n.else_body);
            if (!LLVMGetBasicBlockTerminator(else_bb))
                LLVMBuildBr(builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(builder, merge_bb);
    }



    // цикл: cond_bb → body_bb → exit_bb, стек break/continue
    void Codegen::gen_while(const Parser::WhileStmt& n) {
        LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "while.cond");
        LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "while.body");
        LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "while.end");

        // сохраняем старые точки для break/continue
        break_stack.push_back(exit_bb);
        continue_stack.push_back(cond_bb);

        LLVMBuildBr(builder, cond_bb);

        // cond
        LLVMPositionBuilderAtEnd(builder, cond_bb);
        LLVMValueRef cond = gen_expr(*n.condition);
        if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
            LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1) {
            cond = LLVMBuildICmp(builder, LLVMIntNE, cond,
                LLVMConstInt(LLVMTypeOf(cond), 0, false), "tobool");
        }
        LLVMBuildCondBr(builder, cond, body_bb, exit_bb);

        // body
        LLVMPositionBuilderAtEnd(builder, body_bb);
        bool old_loop = cg_in_loop;
        cg_in_loop = true;
        gen_stmt(*n.body);
        cg_in_loop = old_loop;
        if (!LLVMGetBasicBlockTerminator(body_bb))
            LLVMBuildBr(builder, cond_bb);

        // exit
        LLVMPositionBuilderAtEnd(builder, exit_bb);
        break_stack.pop_back();
        continue_stack.pop_back();
    }



    // возврат из функции: ret val / ret void
    void Codegen::gen_return(const Parser::ReturnStmt& n) {
        if (n.value) {
            LLVMValueRef val = gen_expr(*n.value);
            LLVMBuildRet(builder, val);
        } else {
            LLVMBuildRetVoid(builder);
        }
    }



    void Codegen::gen_break() {
        LLVMBuildBr(builder, break_stack.back());
    }

    void Codegen::gen_continue() {
        LLVMBuildBr(builder, continue_stack.back());
    }



    // встроенные: print/println (диспетчер по типу), input, len, exit, panic, assert
    LLVMValueRef Codegen::gen_builtin_call(const std::string& name,
                                           const std::vector<LLVMValueRef>& args,
                                           std::size_t line) {
        (void)line;

        if (name == "print" || name == "println") {
            bool ln = (name == "println");
            if (args.empty()) return nullptr;

            LLVMValueRef val = args[0];
            LLVMTypeRef ty = LLVMTypeOf(val);
            LLVMTypeKind tk = LLVMGetTypeKind(ty);

            // string
            if (LLVMGetTypeKind(ty) == LLVMStructTypeKind) {
                LLVMValueRef ptr = LLVMBuildExtractValue(builder, val, 0, "");
                LLVMValueRef len = LLVMBuildExtractValue(builder, val, 1, "");
                const char* fname = ln
                    ? "__ferrous_println_string"
                    : "__ferrous_print_string";
                LLVMValueRef fn = functions[fname];
                LLVMTypeRef ft = function_types[fname];
                LLVMValueRef a[] = {ptr, len};
                return LLVMBuildCall2(builder, ft, fn, a, 2, "");
            }

            // float
            if (tk == LLVMFloatTypeKind || tk == LLVMDoubleTypeKind) {
                if (tk == LLVMFloatTypeKind)
                    val = LLVMBuildFPExt(builder, val,
                        LLVMDoubleTypeInContext(ctx), "promote");
                const char* fname = ln
                    ? "__ferrous_println_float64"
                    : "__ferrous_print_float64";
                LLVMValueRef fn = functions[fname];
                LLVMTypeRef ft = function_types[fname];
                return LLVMBuildCall2(builder, ft, fn, &val, 1, "");
            }

            // int
            if (LLVMGetIntTypeWidth(ty) < 64)
                val = LLVMBuildSExt(builder, val,
                    LLVMInt64TypeInContext(ctx), "promote");
            const char* fname_i = ln
                ? "__ferrous_println_int64"
                : "__ferrous_print_int64";
            LLVMValueRef fn = functions[fname_i];
            LLVMTypeRef ft = function_types[fname_i];
            return LLVMBuildCall2(builder, ft, fn, &val, 1, "");
        }

        if (name == "input") {
            LLVMValueRef fn = functions["__ferrous_input"];
            LLVMTypeRef ft = function_types["__ferrous_input"];
            return LLVMBuildCall2(builder, ft, fn, nullptr, 0, "");
        }

        if (name == "len") {
            LLVMValueRef str = args[0];
            LLVMValueRef len = LLVMBuildExtractValue(builder, str, 1, "len");
            return LLVMBuildTrunc(builder, len,
                LLVMInt32TypeInContext(ctx), "len32");
        }

        if (name == "exit") {
            if (args.empty()) return nullptr;
            LLVMValueRef promoted = args[0];
            if (LLVMGetIntTypeWidth(LLVMTypeOf(promoted)) < 64)
                promoted = LLVMBuildSExt(builder, promoted,
                    LLVMInt64TypeInContext(ctx), "code");
            LLVMBuildRet(builder, promoted);
            return promoted;
        }

        if (name == "panic") {
            LLVMValueRef str_val = args[0];
            LLVMValueRef ptr = LLVMBuildExtractValue(builder, str_val, 0, "");
            LLVMValueRef len = LLVMBuildExtractValue(builder, str_val, 1, "");
            LLVMValueRef line_val = LLVMConstInt(
                LLVMInt64TypeInContext(ctx), 0, false);
            LLVMValueRef fn = functions["__ferrous_panic"];
            LLVMTypeRef ft = function_types["__ferrous_panic"];
            LLVMValueRef a[] = {ptr, len, line_val};
            return LLVMBuildCall2(builder, ft, fn, a, 3, "");
        }

        if (name == "assert") {
            LLVMValueRef cond = args[0];
            if (LLVMGetTypeKind(LLVMTypeOf(cond)) != LLVMIntegerTypeKind ||
                LLVMGetIntTypeWidth(LLVMTypeOf(cond)) != 1)
                cond = LLVMBuildICmp(builder, LLVMIntNE, cond,
                    LLVMConstInt(LLVMTypeOf(cond), 0, false), "tobool");

            LLVMBasicBlockRef ok_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "assert.ok");
            LLVMBasicBlockRef fail_bb = LLVMAppendBasicBlockInContext(ctx, cg_fn, "assert.fail");
            LLVMBuildCondBr(builder, cond, ok_bb, fail_bb);

            LLVMPositionBuilderAtEnd(builder, fail_bb);
            const char* msg = "assertion failed";
            LLVMValueRef msg_global = LLVMBuildGlobalStringPtr(builder,
                msg, "assert_msg");
            LLVMValueRef msg_len = LLVMConstInt(
                LLVMInt64TypeInContext(ctx), strlen(msg), false);
            LLVMValueRef line_val = LLVMConstInt(
                LLVMInt64TypeInContext(ctx), 0, false);
            LLVMValueRef fn = functions["__ferrous_assert_fail"];
            LLVMTypeRef ft = function_types["__ferrous_assert_fail"];
            LLVMValueRef a[] = {msg_global, msg_len, line_val};
            LLVMBuildCall2(builder, ft, fn, a, 3, "");
            LLVMBuildUnreachable(builder);

            LLVMPositionBuilderAtEnd(builder, ok_bb);
            return LLVMConstInt(LLVMInt1TypeInContext(ctx), 1, false);
        }

        return LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, false);
    }

    // ── define_function ────────────────────────────────────────────────

    // генерация тела функции: параметры → allocas, обход тела
    void Codegen::define_function(const Parser::FnDecl& fn) {
        std::string name(fn.name);
        LLVMValueRef llvm_fn = nullptr;
        if (name == "main") {
            auto m_it = functions.find("main");
            if (m_it != functions.end()) llvm_fn = m_it->second;
            else return;
        } else {
            std::string mangled = name;
            for (const auto& [pname, ptype] : fn.params) {
                auto tid = resolve_typeid(ptype, *types,
                    types->builtin(TokenKind::KwVoid));
                LLVMTypeRef lt = to_llvm_type(tid);
                mangled += "_" + std::to_string(
                    static_cast<int>(LLVMGetTypeKind(lt)));
            }
            auto it = functions.find(mangled);
            if (it != functions.end()) llvm_fn = it->second;
            else return;
        }

        cg_fn = llvm_fn;
        if (!cg_fn) return;

        // entry-блок
        LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, cg_fn, "entry");
        LLVMPositionBuilderAtEnd(builder, entry);

        // возвращаемый тип
        cg_return_type = fn.return_type
            ? resolve_typeid(*fn.return_type, *types, types->void_type())
            : types->void_type();

        // скоуп параметров
        push_cg_scope();

        for (unsigned i = 0; i < fn.params.size(); ++i) {
            const auto& [pname, ptype] = fn.params[i];
            LLVMValueRef param = LLVMGetParam(cg_fn, i);
            LLVMValueRef alloca = create_entry_alloca(cg_fn,
                std::string(pname), LLVMTypeOf(param));
            LLVMBuildStore(builder, param, alloca);
            cg_current_scope->vars[std::string(pname)] = alloca;
            cg_current_scope->var_types[std::string(pname)] = LLVMTypeOf(param);
        }

        // скоуп тела
        push_cg_scope();
        cg_in_loop = false;

        for (const auto& s : fn.body.elems)
            gen_stmt(s);

        // если void и нет return — вставляем ret void
        if (!LLVMGetBasicBlockTerminator(LLVMGetLastBasicBlock(cg_fn)) &&
            types->equal(cg_return_type, types->void_type())) {
            LLVMBuildRetVoid(builder);
        }

        pop_cg_scope();
        pop_cg_scope();
        cg_fn = nullptr;
    }

    // ── define_functions ───────────────────────────────────────────────

    void Codegen::define_functions_rec(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node))
                define_function(*fn);
            if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node))
                define_functions_rec(ns->decls);
        }
    }

    void Codegen::define_functions(const std::vector<Parser::Decl>& decls) {
        define_functions_rec(decls);
    }

    // ── generate: главный метод ───────────────────────────────────────

    // основной метод: declare → define → verify → запись .ll → clang++ → executable
    void Codegen::generate(const std::vector<Parser::Decl>& decls,
                           const Semantic::AnnotatedAST& aast,
                           const std::string& output_path) {
        this->aast = &aast;
        this->types = aast.types;

        ctx = LLVMContextCreate();
        mod = LLVMModuleCreateWithNameInContext("ferrous_module", ctx);
        builder = LLVMCreateBuilderInContext(ctx);

        // инициализация x86-таргета
        LLVMInitializeX86TargetInfo();
        LLVMInitializeX86Target();
        LLVMInitializeX86TargetMC();
        LLVMInitializeX86AsmPrinter();
        LLVMInitializeX86AsmParser();

        declare_runtime_functions();
        declare_structs(decls);
        declare_functions(decls);
        define_functions(decls);

        // верификация
        char* vfy_msg = nullptr;
        LLVMBool vfy_failed = LLVMVerifyModule(mod, LLVMReturnStatusAction, &vfy_msg);
        if (vfy_failed) {
            std::cerr << "LLVM verification error: " << (vfy_msg ? vfy_msg : "") << '\n';
            LLVMDisposeMessage(vfy_msg);
        }

        // запись .ll
        std::string ll_path = output_path + ".ll";
        char* print_msg = nullptr;
        LLVMPrintModuleToFile(mod, ll_path.c_str(), &print_msg);
        if (print_msg) {
            std::cerr << "cannot write " << ll_path << ": " << print_msg << '\n';
            LLVMDisposeMessage(print_msg);
            return;
        }

        // компиляция рантайма + линковка исполняемого файла
        std::string rt_o = output_path + ".rt.o";
        std::string cmd_rt = "clang++ -stdlib=libc++ -std=c++23 -c rt/ferrous_rt.cpp -o " + rt_o;
        int ret_rt = std::system(cmd_rt.c_str());
        if (ret_rt != 0)
            std::cerr << "runtime compilation failed (exit " << ret_rt << ")\n";

        std::string cmd = "clang++ -stdlib=libc++ -O1 " + ll_path + " " + rt_o + " -o " + output_path;
        int ret = std::system(cmd.c_str());
        if (ret != 0)
            std::cerr << "linking failed (exit code " << ret << "): " << cmd << "\n";
    }

} // namespace Codegen
