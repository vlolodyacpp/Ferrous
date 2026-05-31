module;
#include <string_view>
#include <utility>
#include <vector>
module Ferrous.Semantic;
import std;


namespace Semantic {

    using TokenKind = Lexer::TokenKind;

    TypeRegistry::TypeRegistry() {
        auto add = [&](TokenKind k) {
            TypeID type_id {static_cast<std::uint32_t>(builtins.size())};
            builtins.push_back({k});
            by_kind[k] = type_id;
            return type_id;
        };

        // типы
        add(TokenKind::KwInt8);
        add(TokenKind::KwInt16);
        add(TokenKind::KwInt32);
        add(TokenKind::KwInt64);

        add(TokenKind::KwUint8);
        add(TokenKind::KwUint16);
        add(TokenKind::KwUint32);
        add(TokenKind::KwUint64);

        add(TokenKind::KwFloat32);
        add(TokenKind::KwFloat64);

        add(TokenKind::KwBool);
        add(TokenKind::KwString);
        add(TokenKind::KwChar);

        void_id = add(TokenKind::KwVoid);
        error_id = add(TokenKind::Undefined);
    }


    TypeID TypeRegistry::builtin(TokenKind k) const {
        return by_kind.at(k);
    }
    TypeID TypeRegistry::error_type() const {
        return error_id;
    }
    TypeID TypeRegistry::void_type() const {
        return void_id;
    }
    bool TypeRegistry::equal(TypeID a, TypeID b) const {
        return a == b; // tbd
    }


    void DiagBag::error(std::size_t line, std::size_t col, std::string msg) {
        items.push_back({DiagSeverity::Error, std::move(msg), line, col});
    }
    void DiagBag::warning(std::size_t line, std::size_t col, std::string msg) {
        items.push_back({DiagSeverity::Warning, std::move(msg), line, col});
    }
    bool DiagBag::has_errors() const {
        for (auto& d : items) {
            if (d.severity == DiagSeverity::Error) {
                return true;
            }
        }
        return false;
    }
    const std::vector<Diagnostic>& DiagBag::all() const {
        return items;
    }


    // проверка нахождения символов в текущей цепочке/scope
    Symbol* Scope::lookup_local(std::string_view name) {
        auto it = table.find(name);
        return it == table.end() ? nullptr : &it->second;
    }

    Symbol* Scope::lookup_chain(std::string_view name) {
        for (Scope* s = this; s != nullptr; s = s->parent) {
            if (auto* sym = s->lookup_local(name)) {
                return sym;
            }
        }
        return nullptr;
    }

    bool Scope::insert(Symbol sym) {
        return table.emplace(sym.name, std::move(sym)).second;
    }


    DiagBag Semantic::check(const std::vector<Parser::Decl>& decls) {
        install_builtins();
        pass1(decls);
        verify_main();
        return std::move(diag);  // заглушка
    }



    void Semantic::install_builtins() {
        auto add_fn = [&](std::string_view name, std::vector<TypeID> params, TypeID return_type) {
            Symbol *s = root_scope.lookup_local(name);

            if (!s) {
                root_scope.insert(Symbol{SymbolKind::Function, name, FuncSymbol{}});
                s = root_scope.lookup_local(name);
            }
            std::get<FuncSymbol>(s -> data).overloads.push_back(
                FuncSig{std::move(params), return_type, nullptr}
            );
        };

        TypeID int8 = registry.builtin(TokenKind::KwInt8);
        TypeID int16 = registry.builtin(TokenKind::KwInt16);
        TypeID int32 = registry.builtin(TokenKind::KwInt32);
        TypeID int64 = registry.builtin(TokenKind::KwInt64);
        TypeID uint8 = registry.builtin(TokenKind::KwUint8);
        TypeID uint16 = registry.builtin(TokenKind::KwUint16);
        TypeID uint32 = registry.builtin(TokenKind::KwUint32);
        TypeID uint64 = registry.builtin(TokenKind::KwUint64);

        TypeID float32 = registry.builtin(TokenKind::KwFloat32);
        TypeID float64 = registry.builtin(TokenKind::KwFloat64);

        TypeID bool_ = registry.builtin(TokenKind::KwBool);
        TypeID str = registry.builtin(TokenKind::KwString);
        TypeID char_ = registry.builtin(TokenKind::KwChar);
        TypeID void_ = registry.builtin(TokenKind::KwVoid);


        for (auto t : {int8, int16, int32, int64, uint8,
            uint16, uint32, uint64, float32, float64, bool_, str, char_, void_ }) {
            add_fn("print", {t}, void_);
            add_fn("println", {t}, void_);
        }

        add_fn("input", {}, void_);
        add_fn("len", {str}, void_);
        add_fn("exit", {int32}, void_);
        add_fn("panic", {str}, void_);

        add_fn("assert", {bool_}, void_);
        add_fn("assert", {bool_, str}, void_);
    }


    // typeref -> typeid
    std::optional<TypeID> Semantic::resolve_type(const Parser::TypeRef& tr) {
        return std::visit([&](const auto& n) -> std::optional<TypeID> {

            using T = std::decay_t<decltype(n)>;

            if constexpr (std::is_same_v<T, Parser::BuiltinTypeRef>) {
                return registry.builtin(n.type_kind);
            } else {
                diag.error(0, 0, "user-defined and array types: phase not ready");
                return std::nullopt;
            }
        }, tr.node);
    }

    // подготовка регистраций ф-ции, собираем сигнатуры
    void Semantic::pass1(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node)) {
                pass1_fn(root_scope, *fn);
            }
            // структуры/алиасы/namespace tbd
        }
    }

    // ргеитсрируем одну функцию
    void Semantic::pass1_fn(Scope& scope, const Parser::FnDecl& fn) {
        std::vector<TypeID> params;
        for (const auto& [param_name, param_type] : fn.params) {
            if (auto tid = resolve_type(param_type)) {
                params.push_back(*tid);
            } else{
                params.push_back(registry.error_type());
            }
        }

        TypeID return_type = fn.return_type
            ? resolve_type(*fn.return_type).value_or(registry.error_type())
            : registry.void_type();
        FuncSig sig{ std::move(params), return_type, &fn };

        Symbol* existing = scope.lookup_local(fn.name);
        if (existing && existing->kind != SymbolKind::Function) {
            diag.error(0, 0, "'" + std::string(fn.name)
                + "' is already declared as non-function");
            return;
        }
        if (!existing) {
            scope.insert(Symbol{SymbolKind::Function, fn.name, FuncSymbol{}});
            existing = scope.lookup_local(fn.name);
        }
        auto& fs = std::get<FuncSymbol>(existing -> data);
        for (const auto& other : fs.overloads) {
            if (other.params == sig.params) {
                diag.error(0, 0, "duplicate overload of '" + std::string(fn.name) + "'");
                return;
            }
        }
        fs.overloads.push_back(std::move(sig));
    }


    // проверка main
    void Semantic::verify_main() {
        Symbol* m = root_scope.lookup_local("main");

        if (!m || m -> kind != SymbolKind::Function) {
            diag.error(0, 0, "no entry point: 'main' is required");
            return;
        }

        auto& fs = std::get<FuncSymbol>(m->data);
        if (fs.overloads.size() != 1) {
            diag.error(0, 0, "function 'main' must not be overloaded");
            return;
        }

        const auto& sig = fs.overloads.front();
        if (!sig.params.empty())
            diag.error(0, 0, "function 'main' must take no parameters");
        if (!registry.equal(sig.return_type, registry.builtin(TokenKind::KwInt32)))
            diag.error(0, 0, "function 'main' must return int32");
    }




    void print_diagnostics(const DiagBag& bag, std::string_view file, std::ostream &os) {
        for (const auto& d : bag.all()) {
            std::string_view sev = d.severity == DiagSeverity::Error ? "error" : d.severity == DiagSeverity::Warning ? "warning" : "note";
            os << file << ':' << d.line << ':' << d.column
               << ": " << sev << ": " << d.message << '\n';
        }
    }
}
