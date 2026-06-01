module Ferrous.Semantic;
import std;


namespace Semantic {

    using TokenKind = Lexer::TokenKind;

    namespace {
        struct TypeIdHash {
            std::size_t operator()(TypeID id) const {
                return std::hash<std::uint32_t>{}(id.id);
            }
        };

        std::optional<std::uint64_t> parse_uint(std::string_view text) {
            if (text.empty()) {
                return std::nullopt;
            }

            if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
                std::uint64_t value = 0;
                const char* begin = text.data() + 2;
                const char* end = text.data() + text.size();
                auto [ptr, ec] = std::from_chars(begin, end, value, 16);
                if (ec != std::errc{} || ptr != end) {
                    return std::nullopt;
                }
                return value;
            }

            if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
                std::uint64_t value = 0;
                for (std::size_t i = 2; i < text.size(); ++i) {
                    const char c = text[i];
                    if (c != '0' && c != '1') {
                        return std::nullopt;
                    }
                    value = (value << 1) | static_cast<std::uint64_t>(c - '0');
                }
                return value;
            }

            std::uint64_t value = 0;
            const char* begin = text.data();
            const char* end = text.data() + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, value, 10);
            if (ec != std::errc{} || ptr != end) {
                return std::nullopt;
            }
            return value;
        }
    }

    std::size_t TypeRegistry::ArrayKeyHash::operator()(const ArrayKey& key) const {
        return (static_cast<std::size_t>(key.elem.id) << 1) ^ std::hash<std::uint64_t>{}(key.size);
    }

    TypeRegistry::TypeRegistry() {
        auto add_builtin = [&](TokenKind k) {
            TypeID type_id {static_cast<std::uint32_t>(entries.size())};
            entries.emplace_back(Builtin{k});
            by_kind[k] = type_id;
            return type_id;
        };

        // типы
        add_builtin(TokenKind::KwInt8);
        add_builtin(TokenKind::KwInt16);
        add_builtin(TokenKind::KwInt32);
        add_builtin(TokenKind::KwInt64);

        add_builtin(TokenKind::KwUint8);
        add_builtin(TokenKind::KwUint16);
        add_builtin(TokenKind::KwUint32);
        add_builtin(TokenKind::KwUint64);

        add_builtin(TokenKind::KwFloat32);
        add_builtin(TokenKind::KwFloat64);

        add_builtin(TokenKind::KwBool);
        add_builtin(TokenKind::KwString);
        add_builtin(TokenKind::KwChar);

        void_id = add_builtin(TokenKind::KwVoid);
        error_id = add_builtin(TokenKind::Undefined);
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
        return resolve_alias(a) == resolve_alias(b);
    }

    TypeID TypeRegistry::resolve_alias(TypeID id) const {
        std::size_t guard = 0;
        while (guard++ < entries.size()) {
            if (id.id >= entries.size()) return id;
            const auto* alias = std::get_if<AliasType>(&entries[id.id]);
            if (!alias || !alias->resolved) {
                return id;
            }
            id = alias->target;
        }
        return id;
    }

    TypeID TypeRegistry::normalize(TypeID id) const {
        return resolve_alias(id);
    }

    TypeID TypeRegistry::array(TypeID elem, std::uint64_t size) {
        TypeID norm = normalize(elem);
        ArrayKey key{norm, size};
        if (auto it = array_cache.find(key); it != array_cache.end()) {
            return it->second;
        }
        // интернируем массивы, чтобы один и тот же тип не плодился.
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(ArrayType{norm, size});
        array_cache.emplace(key, id);
        return id;
    }

    TypeID TypeRegistry::struct_placeholder(std::string_view name) {
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        // поля заполняются позже, после pass B.
        entries.emplace_back(StructType{std::string(name), {}});
        by_name_map.emplace(std::string(name), id);
        struct_ids.push_back(id);
        return id;
    }

    void TypeRegistry::finalize_struct(std::string_view name,
        std::vector<std::pair<std::string_view, TypeID>> fields) {
        auto it = by_name_map.find(std::string(name));
        if (it == by_name_map.end()) {
            return;
        }
        auto* st = std::get_if<StructType>(&entries[it->second.id]);
        if (!st) {
            return;
        }
        st->fields.clear();
        st->fields.reserve(fields.size());
        for (const auto& [fname, ftype] : fields) {
            st->fields.emplace_back(std::string(fname), ftype);
        }
    }

    TypeID TypeRegistry::declare_alias(std::string_view name) {
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        // Алиас без таргета: нужен для forward reference.
        entries.emplace_back(AliasType{std::string(name), error_id, false});
        by_name_map.emplace(std::string(name), id);
        return id;
    }

    void TypeRegistry::register_alias(std::string_view name, TypeID target) {
        auto it = by_name_map.find(std::string(name));
        if (it == by_name_map.end()) {
            TypeID id{static_cast<std::uint32_t>(entries.size())};
            entries.emplace_back(AliasType{std::string(name), normalize(target), true});
            by_name_map.emplace(std::string(name), id);
            return;
        }
        auto* alias = std::get_if<AliasType>(&entries[it->second.id]);
        if (!alias) {
            return;
        }
        alias->target = normalize(target);
        alias->resolved = true;
    }

    std::optional<TypeID> TypeRegistry::by_name(std::string_view name) const {
        auto it = by_name_map.find(std::string(name));
        if (it == by_name_map.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    const TypeRegistry::StructType* TypeRegistry::get_struct(TypeID id) const {
        id = normalize(id);
        if (id.id >= entries.size()) return nullptr;
        return std::get_if<StructType>(&entries[id.id]);
    }

    const TypeRegistry::ArrayType* TypeRegistry::get_array(TypeID id) const {
        id = normalize(id);
        if (id.id >= entries.size()) return nullptr;
        return std::get_if<ArrayType>(&entries[id.id]);
    }

    bool TypeRegistry::is_struct(TypeID id) const {
        return get_struct(id) != nullptr;
    }

    bool TypeRegistry::is_array(TypeID id) const {
        return get_array(id) != nullptr;
    }

    const std::vector<TypeID>& TypeRegistry::all_structs() const {
        return struct_ids;
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
            } else if constexpr (std::is_same_v<T, Parser::NamedTypeRef>) {
                if (auto tid = registry.by_name(n.name)) {
                    return *tid;
                }
                diag.error(0, 0, "unknown type '" + std::string(n.name) + "'");
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Parser::ArrayTypeRef>) {
                auto elem = resolve_type(*n.elem);
                if (!elem) {
                    return std::nullopt;
                }
                // Размер массива — литерал, проверяем что он положительный.
                auto size = parse_uint(n.size);
                if (!size || *size == 0) {
                    diag.error(0, 0, "array size must be positive");
                    return std::nullopt;
                }
                return registry.array(*elem, *size);
            } else {
                diag.error(0, 0, "unexpected type form");
                return std::nullopt;
            }
        }, tr.node);
    }

    // подготовка регистраций ф-ции, собираем сигнатуры
    void Semantic::pass1(const std::vector<Parser::Decl>& decls) {
        // Pass A/B for type declarations first, so function signatures can use them.
        pass1_types(decls);
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node)) {
                pass1_fn(root_scope, *fn);
            }
        }
    }

    void Semantic::pass1_types(const std::vector<Parser::Decl>& decls) {
        for (const auto& d : decls) {
            if (auto* st = std::get_if<Parser::StructDecl>(&d.node)) {
                if (registry.by_name(st -> name)) {
                    diag.error(0, 0, "type '" + std::string(st -> name) + "' already defined");
                    continue;
                }
                TypeID id = registry.struct_placeholder(st -> name);
                if (!root_scope.insert(Symbol{SymbolKind::Struct, st -> name, TypeSymbol{id}})) {
                    diag.error(0, 0, "'" + std::string(st -> name) + "' already declared");
                }
            } else if (auto* ta = std::get_if<Parser::TypeAliasDecl>(&d.node)) {
                if (registry.by_name(ta -> name)) {
                    diag.error(0, 0, "type '" + std::string(ta -> name) + "' already defined");
                    continue;
                }
                TypeID id = registry.declare_alias(ta -> name);
                if (!root_scope.insert(Symbol{SymbolKind::TypeAllias, ta -> name, TypeSymbol{id}})) {
                    diag.error(0, 0, "'" + std::string(ta -> name) + "' already declared");
                }
            }
        }
        for (const auto& d : decls) {
            if (auto* ta = std::get_if<Parser::TypeAliasDecl>(&d.node)) {
                if (auto target = resolve_type(ta -> type)) {
                    registry.register_alias(ta -> name, *target);
                }
            }
        }

        for (const auto& d : decls) {
            if (auto* st = std::get_if<Parser::StructDecl>(&d.node)) {
                std::unordered_set<std::string_view> seen_fields;
                std::vector<std::pair<std::string_view, TypeID>> fields;
                fields.reserve(st -> field.size());

                for (const auto& [fname, ftype] : st -> field) {
                    if (!seen_fields.insert(fname).second) {
                        diag.error(0, 0, "duplicate field '" + std::string(fname) + "'");
                        continue;
                    }
                    auto tid = resolve_type(ftype);
                    if (!tid) {
                        continue;
                    }
                    fields.emplace_back(fname, *tid);
                }
                registry.finalize_struct(st -> name, std::move(fields));
            }
        }

        check_recursive_structs();
    }

    void Semantic::check_recursive_structs() {
        auto has_value_cycle = [&](TypeID tid,
            auto&& self,
            std::unordered_set<TypeID, TypeIdHash>& stack) -> bool {
            TypeID norm = registry.resolve_alias(tid);
            if (auto* st = registry.get_struct(norm)) {
                if (stack.contains(norm)) {
                    return true;
                }
                stack.insert(norm);
                for (const auto& [fname, ftype] : st -> fields) {
                    (void)fname;
                    if (self(ftype, self, stack)) {
                        return true;
                    }
                }
                stack.erase(norm);
                return false;
            }
            if (auto* arr = registry.get_array(norm)) {
                return self(arr -> elem, self, stack);
            }
            return false;
        };

        for (TypeID tid : registry.all_structs()) {
            std::unordered_set<TypeID, TypeIdHash> stack;
            if (has_value_cycle(tid, has_value_cycle, stack)) {
                if (const auto* st = registry.get_struct(tid)) {
                    diag.error(0, 0, "recursive struct '" + st -> name + "' has infinite size");
                }
            }
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
