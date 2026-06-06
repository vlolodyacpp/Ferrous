module Ferrous.Semantic;
import std;
namespace Semantic {

    using TokenKind = Lexer::TokenKind;

    namespace {
        // хэш для TypeID
        struct TypeIdHash {
            std::size_t operator()(TypeID id) const {
                return std::hash<std::uint32_t>{}(id.id);
            }
        };

        // парсинг целого числа
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

        // суффиксы целочисленных литералов
        enum class IntSuffix {
            I8, I16, I32, I64,
            U8, U16, U32, U64,
        };

        // суффиксы литералов с плавающей точкой
        enum class FloatSuffix {
            F32, F64,
        };

        // разобранный целочисленный литерал
        struct ParsedIntLiteral {
            std::uint64_t value;
            std::optional<IntSuffix> suffix;
        };

        // разобранный float-литерал
        struct ParsedFloatLiteral {
            std::string_view core;
            std::optional<FloatSuffix> suffix;
        };

        // разбор целочисленного литерала
        std::optional<ParsedIntLiteral> parse_int_lexeme(std::string_view text) {
            std::string_view core = text;
            std::optional<IntSuffix> suffix;
            auto match_suffix = [&](std::string_view s, IntSuffix kind) {
                if (text.size() > s.size()
                    && text.compare(text.size() - s.size(), s.size(), s) == 0) {
                    core = text.substr(0, text.size() - s.size());
                    suffix = kind;
                    return true;
                }
                return false;
            };

            match_suffix("i8", IntSuffix::I8)
                || match_suffix("i16", IntSuffix::I16)
                || match_suffix("i32", IntSuffix::I32)
                || match_suffix("i64", IntSuffix::I64)
                || match_suffix("u8", IntSuffix::U8)
                || match_suffix("u16", IntSuffix::U16)
                || match_suffix("u32", IntSuffix::U32)
                || match_suffix("u64", IntSuffix::U64);

            auto value = parse_uint(core);
            if (!value) {
                return std::nullopt;
            }
            return ParsedIntLiteral{*value, suffix};
        }

        // разбор float-литерала
        ParsedFloatLiteral parse_float_lexeme(std::string_view text) {
            std::string_view core = text;
            std::optional<FloatSuffix> suffix;
            auto match_suffix = [&](std::string_view s, FloatSuffix kind) {
                if (text.size() > s.size()
                    && text.compare(text.size() - s.size(), s.size(), s) == 0) {
                    core = text.substr(0, text.size() - s.size());
                    suffix = kind;
                    return true;
                }
                return false;
            };

            match_suffix("f32", FloatSuffix::F32)
                || match_suffix("f64", FloatSuffix::F64);

            return ParsedFloatLiteral{core, suffix};
        }
    }

    // реестр типов

    std::size_t TypeRegistry::ArrayKeyHash::operator()(const ArrayKey& key) const {
        return (static_cast<std::size_t>(key.elem.id) << 1) ^ std::hash<std::uint64_t>{}(key.size);
    }

    // конструктор: встроенные типы
    TypeRegistry::TypeRegistry() {
        auto add_builtin = [&](TokenKind k) {
            TypeID type_id {static_cast<std::uint32_t>(entries.size())};
            entries.emplace_back(Builtin{k});
            by_kind[k] = type_id;
            return type_id;
        };

        // целочисленные
        add_builtin(TokenKind::KwInt8);
        add_builtin(TokenKind::KwInt16);
        add_builtin(TokenKind::KwInt32);
        add_builtin(TokenKind::KwInt64);

        add_builtin(TokenKind::KwUint8);
        add_builtin(TokenKind::KwUint16);
        add_builtin(TokenKind::KwUint32);
        add_builtin(TokenKind::KwUint64);

        // с плавающей точкой
        add_builtin(TokenKind::KwFloat32);
        add_builtin(TokenKind::KwFloat64);

        // остальные встроенные
        add_builtin(TokenKind::KwBool);
        add_builtin(TokenKind::KwString);
        add_builtin(TokenKind::KwChar);

        void_id = add_builtin(TokenKind::KwVoid);
        error_id = add_builtin(TokenKind::Undefined);

        // нетипизированные (литералы без суффикса)
        untyped_int_id = TypeID{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(Untyped{Untyped::Kind::Int});
        untyped_float_id = TypeID{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(Untyped{Untyped::Kind::Float});
    }

    // доступ по виду токена
    TypeID TypeRegistry::builtin(TokenKind k) const {
        return by_kind.at(k);
    }
    TypeID TypeRegistry::untyped_int() const {
        return untyped_int_id;
    }
    TypeID TypeRegistry::untyped_float() const {
        return untyped_float_id;
    }
    TypeID TypeRegistry::error_type() const {
        return error_id;
    }
    TypeID TypeRegistry::void_type() const {
        return void_id;
    }

    // равенство типов (с учётом псевдонимов)
    bool TypeRegistry::equal(TypeID a, TypeID b) const {
        return resolve_alias(a) == resolve_alias(b);
    }

    bool TypeRegistry::is_untyped(TypeID id) const {
        id = resolve_alias(id);
        return id == untyped_int_id || id == untyped_float_id;
    }

    bool TypeRegistry::is_untyped_int(TypeID id) const {
        return resolve_alias(id) == untyped_int_id;
    }

    bool TypeRegistry::is_untyped_float(TypeID id) const {
        return resolve_alias(id) == untyped_float_id;
    }

    bool TypeRegistry::is_fixed_int(TypeID id) const {
        id = resolve_alias(id);
        return equal(id, builtin(TokenKind::KwInt8))
            || equal(id, builtin(TokenKind::KwInt16))
            || equal(id, builtin(TokenKind::KwInt32))
            || equal(id, builtin(TokenKind::KwInt64))
            || equal(id, builtin(TokenKind::KwUint8))
            || equal(id, builtin(TokenKind::KwUint16))
            || equal(id, builtin(TokenKind::KwUint32))
            || equal(id, builtin(TokenKind::KwUint64));
    }

    bool TypeRegistry::is_fixed_float(TypeID id) const {
        id = resolve_alias(id);
        return equal(id, builtin(TokenKind::KwFloat32))
            || equal(id, builtin(TokenKind::KwFloat64));
    }

    bool TypeRegistry::is_float(TypeID id) const {
        return is_fixed_float(resolve_alias(id));
    }

    // разрешение цепочки псевдонимов
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

    // создание типа-массива
    TypeID TypeRegistry::array(TypeID elem, std::uint64_t size) {
        TypeID norm = normalize(elem);
        ArrayKey key{norm, size};
        if (auto it = array_cache.find(key); it != array_cache.end()) {
            return it->second;
        }
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(ArrayType{norm, size});
        array_cache.emplace(key, id);
        return id;
    }

    // заглушка для структуры
    TypeID TypeRegistry::struct_placeholder(std::string_view name) {
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(StructType{std::string(name), {}});
        by_name_map.emplace(std::string(name), id);
        struct_ids.push_back(id);
        return id;
    }

    // заполнение полей структуры
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

    // объявление псевдонима (без цели)
    TypeID TypeRegistry::declare_alias(std::string_view name) {
        TypeID id{static_cast<std::uint32_t>(entries.size())};
        entries.emplace_back(AliasType{std::string(name), error_id, false});
        by_name_map.emplace(std::string(name), id);
        return id;
    }

    // регистрация цели псевдонима
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

    // поиск типа по имени
    std::optional<TypeID> TypeRegistry::by_name(std::string_view name) const {
        auto it = by_name_map.find(std::string(name));
        if (it == by_name_map.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    // получение структуры по TypeID
    const TypeRegistry::StructType* TypeRegistry::get_struct(TypeID id) const {
        id = normalize(id);
        if (id.id >= entries.size()) return nullptr;
        return std::get_if<StructType>(&entries[id.id]);
    }

    // получение массива по TypeID
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

    // имя типа
    std::string TypeRegistry::name(TypeID id) const {
        if (id.id >= entries.size()) {
            return "<unknown>";
        }

        const auto& entry = entries[id.id];
        if (const auto* builtin = std::get_if<Builtin>(&entry)) {
            switch (builtin->kind) {
                case TokenKind::KwInt8: return "int8";
                case TokenKind::KwInt16: return "int16";
                case TokenKind::KwInt32: return "int32";
                case TokenKind::KwInt64: return "int64";
                case TokenKind::KwUint8: return "uint8";
                case TokenKind::KwUint16: return "uint16";
                case TokenKind::KwUint32: return "uint32";
                case TokenKind::KwUint64: return "uint64";
                case TokenKind::KwFloat32: return "float32";
                case TokenKind::KwFloat64: return "float64";
                case TokenKind::KwBool: return "bool";
                case TokenKind::KwString: return "string";
                case TokenKind::KwChar: return "char";
                case TokenKind::KwVoid: return "void";
                case TokenKind::Undefined: return "error";
                default: return "?";
            }
        }
        if (const auto* ut = std::get_if<Untyped>(&entry)) {
            return ut->kind == Untyped::Kind::Int ? "untyped_int" : "untyped_float";
        }
        if (const auto* st = std::get_if<StructType>(&entry)) {
            return st->name;
        }
        if (const auto* arr = std::get_if<ArrayType>(&entry)) {
            return std::format("[{}; {}]", name(arr->elem), arr->size);
        }
        if (const auto* alias = std::get_if<AliasType>(&entry)) {
            return alias->name;
        }
        return "?";
    }

    // диагностика

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

    // область видимости

    // поиск в текущем скоупе
    Symbol* Scope::lookup_local(std::string_view name) {
        auto it = table.find(name);
        return it == table.end() ? nullptr : &it->second;
    }

    // поиск по цепочке
    Symbol* Scope::lookup_chain(std::string_view name) {
        for (Scope* s = this; s != nullptr; s = s->parent) {
            if (auto* sym = s->lookup_local(name)) {
                return sym;
            }
        }
        return nullptr;
    }

    // вставка символа
    bool Scope::insert(Symbol sym) {
        return table.emplace(sym.name, std::move(sym)).second;
    }

    // семантический анализатор (главный класс)

    // главный метод
    DiagBag Semantic::check(const std::vector<Parser::Decl>& decls) {
        install_builtins();          // встроенные функции
        pass1(decls);                // регистрация имён
        verify_main();               // проверка main
        current_scope = &root_scope;
        pass2(decls, root_scope);    // проверка тел функций
        aast.types = &registry;
        return std::move(diag);
    }

    // встроенные функции
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

        // print, println для всех типов
        for (auto t : {int8, int16, int32, int64, uint8,
            uint16, uint32, uint64, float32, float64, bool_, str, char_, void_ }) {
            add_fn("print", {t}, void_);
            add_fn("println", {t}, void_);
        }

        add_fn("input", {}, str);
        add_fn("len", {str}, int32);
        add_fn("exit", {int32}, void_);
        add_fn("panic", {str}, void_);

        add_fn("assert", {bool_}, void_);
        add_fn("assert", {bool_, str}, void_);
    }

    // разрешение типа: TypeRef → TypeID
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

    // проход 1: регистрация имён

    // корневой обход
    void Semantic::pass1(const std::vector<Parser::Decl>& decls) {
        pass1(decls, root_scope);
    }

    void Semantic::pass1(const std::vector<Parser::Decl>& decls, Scope& scope) {
        // сначала типы
        pass1_types(decls, scope);
        for (const auto& d : decls) {
            if (auto* fn = std::get_if<Parser::FnDecl>(&d.node)) {
                pass1_fn(scope, *fn);
            } else if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node)) {
                Scope& ns_scope = ensure_namespace(scope, ns->name);
                pass1(ns->decls, ns_scope);
            }
        }
    }

    // регистрация типов
    void Semantic::pass1_types(const std::vector<Parser::Decl>& decls) {
        pass1_types(decls, root_scope);
    }

    void Semantic::pass1_types(const std::vector<Parser::Decl>& decls, Scope& scope) {
        // подпроход 1: заглушки структур и псевдонимов
        for (const auto& d : decls) {
            if (auto* st = std::get_if<Parser::StructDecl>(&d.node)) {
                if (registry.by_name(st -> name)) {
                    diag.error(0, 0, "type '" + std::string(st -> name) + "' already defined");
                    continue;
                }
                TypeID id = registry.struct_placeholder(st -> name);
                if (!scope.insert(Symbol{SymbolKind::Struct, st -> name, TypeSymbol{id}})) {
                    diag.error(0, 0, "'" + std::string(st -> name) + "' already declared");
                }
            } else if (auto* ta = std::get_if<Parser::TypeAliasDecl>(&d.node)) {
                if (registry.by_name(ta -> name)) {
                    diag.error(0, 0, "type '" + std::string(ta -> name) + "' already defined");
                    continue;
                }
                TypeID id = registry.declare_alias(ta -> name);
                if (!scope.insert(Symbol{SymbolKind::TypeAllias, ta -> name, TypeSymbol{id}})) {
                    diag.error(0, 0, "'" + std::string(ta -> name) + "' already declared");
                }
            } else if (auto* ns = std::get_if<Parser::NameSpaceDecl>(&d.node)) {
                Scope& ns_scope = ensure_namespace(scope, ns->name);
                pass1_types(ns->decls, ns_scope);
            }
        }
        // подпроход 2: цели псевдонимов
        for (const auto& d : decls) {
            if (auto* ta = std::get_if<Parser::TypeAliasDecl>(&d.node)) {
                if (auto target = resolve_type(ta -> type)) {
                    registry.register_alias(ta -> name, *target);
                }
            }
        }

        // подпроход 3: поля структур
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

        // проверка рекурсивных структур
        check_recursive_structs();
    }

    // проверка циклической вложенности структур
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

    // регистрация сигнатуры функции
    void Semantic::pass1_fn(Scope& scope, const Parser::FnDecl& fn) {
        // типы параметров
        std::vector<TypeID> params;
        for (const auto& [param_name, param_type] : fn.params) {
            if (auto tid = resolve_type(param_type)) {
                params.push_back(*tid);
            } else{
                params.push_back(registry.error_type());
            }
        }

        // возвращаемый тип (по умолчанию void)
        TypeID return_type = fn.return_type
            ? resolve_type(*fn.return_type).value_or(registry.error_type())
            : registry.void_type();
        FuncSig sig{ std::move(params), return_type, &fn };

        // конфликт имён
        Symbol* existing = scope.lookup_local(fn.name);
        if (existing && existing->kind != SymbolKind::Function) {
            diag.error(0, 0, "'" + std::string(fn.name)
                + "' is already declared as non-function");
            return;
        }
        // создаём/дополняем символ функции
        if (!existing) {
            scope.insert(Symbol{SymbolKind::Function, fn.name, FuncSymbol{}});
            existing = scope.lookup_local(fn.name);
        }
        auto& fs = std::get<FuncSymbol>(existing -> data);
        // дубликат сигнатуры
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

    // управление скоупами

    // создание дочернего скоупа
    Scope& Semantic::push_scope(Scope& parent) {
        auto& child = scope_storage.emplace_back();
        child.parent = &parent;
        return child;
    }

    // создание/получение скоупа namespace
    Scope& Semantic::ensure_namespace(Scope& scope, std::string_view name) {
        Symbol* existing = scope.lookup_local(name);
        if (existing && existing->kind == SymbolKind::Namespace) {
            return *std::get<NamespaceSymbol>(existing->data).scope;
        }
        auto& ns_scope = scope_storage.emplace_back();
        ns_scope.parent = &scope;
        scope.insert(Symbol{SymbolKind::Namespace, name, NamespaceSymbol{&ns_scope}});
        return ns_scope;
    }

    // вспомогательные проверки

    // lvalue?
    bool Semantic::is_lvalue(const Parser::Expr& e) const {
        return std::visit([&](const auto& n) -> bool {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::IdentExpr>) return true;
            if constexpr (std::is_same_v<T, Parser::FieldExpr>) return true;
            if constexpr (std::is_same_v<T, Parser::IndexExpr>) return true;
            return false;
        }, e.node);
    }

    // mut корень?
    bool Semantic::is_mut_root(const Parser::Expr& e) const {
        return std::visit([&](const auto& n) -> bool {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::IdentExpr>) {
                Symbol* sym = current_scope->lookup_chain(n.value);
                if (sym && sym->kind == SymbolKind::Variable) {
                    return std::get<VarSymbol>(sym->data).is_mut;
                }
                return false;
            }
            if constexpr (std::is_same_v<T, Parser::FieldExpr>) {
                return is_mut_root(*n.object);
            }
            if constexpr (std::is_same_v<T, Parser::IndexExpr>) {
                return is_mut_root(*n.array);
            }
            return false;
        }, e.node);
    }

    // всегда ли возвращает?
    bool Semantic::always_returns(const Parser::Stmt& stmt) {
        return std::visit([&](const auto& n) -> bool {
            using T = std::decay_t<decltype(n)>;
            if constexpr (std::is_same_v<T, Parser::ReturnStmt>) return true;
            if constexpr (std::is_same_v<T, Parser::BlockStmt>) {
                for (const auto& s : n.elems) {
                    if (always_returns(s)) return true;
                }
                return false;
            }
            if constexpr (std::is_same_v<T, Parser::IfStmt>) {
                return n.else_body && always_returns(*n.then_body) && always_returns(*n.else_body);
            }
            if constexpr (std::is_same_v<T, Parser::WhileStmt>) {
                // while true — всегда возвращает
                bool cond_is_true = false;
                if (const auto* lit = std::get_if<Parser::LitBoolExpr>(&n.condition->node)) {
                    cond_is_true = lit->value;
                }
                return cond_is_true && always_returns(*n.body);
            }
            return false;
        }, stmt.node);
    }

    // разрешение перегрузок

    struct OverloadMatch {
        std::size_t index;
        std::vector<TypeID> resolved_args;
    };

    // поиск перегрузки по типам аргументов
    static std::optional<OverloadMatch> resolve_overload(
        const std::vector<FuncSig>& overloads,
        const std::vector<TypeID>& arg_types,
        TypeRegistry& registry)
    {
        for (std::size_t i = 0; i < overloads.size(); ++i) {
            const auto& sig = overloads[i];
            if (sig.params.size() != arg_types.size()) continue;
            bool match = true;
            std::vector<TypeID> resolved = arg_types;
            for (std::size_t j = 0; j < sig.params.size(); ++j) {
                TypeID param = registry.resolve_alias(sig.params[j]);
                TypeID arg   = registry.resolve_alias(arg_types[j]);
                if (registry.equal(param, arg)) continue;
                // неявное приведение нетипизированного литерала
                if (registry.is_untyped_int(arg) && registry.is_fixed_int(param)) {
                    resolved[j] = param;
                    continue;
                }
                if (registry.is_untyped_float(arg) && registry.is_fixed_float(param)) {
                    resolved[j] = param;
                    continue;
                }
                match = false;
                break;
            }
            if (match) return OverloadMatch{i, std::move(resolved)};
        }
        return std::nullopt;
    }

    // проверка выражений
    TypeID Semantic::check_expr(const Parser::Expr& e, std::optional<TypeID> expected) {
        TypeID result = registry.error_type();

        auto check_rec = [&](const Parser::Expr& ex, std::optional<TypeID> exp) -> TypeID {
            return this->check_expr(ex, exp);
        };
        result = std::visit([&](const auto& n) -> TypeID {
            using T = std::decay_t<decltype(n)>;

            // целочисленный литерал
            if constexpr (std::is_same_v<T, Parser::LitIntExpr>) {
                auto parsed = parse_int_lexeme(n.value);
                if (!parsed) {
                    diag.error(0, 0, "invalid integer literal '" + std::string(n.value) + "'");
                    return registry.error_type();
                }
                if (parsed->suffix) {
                    // с суффиксом — конкретный тип
                    TypeID fixed = registry.error_type();
                    switch (*parsed->suffix) {
                        case IntSuffix::I8: fixed = registry.builtin(TokenKind::KwInt8); break;
                        case IntSuffix::I16: fixed = registry.builtin(TokenKind::KwInt16); break;
                        case IntSuffix::I32: fixed = registry.builtin(TokenKind::KwInt32); break;
                        case IntSuffix::I64: fixed = registry.builtin(TokenKind::KwInt64); break;
                        case IntSuffix::U8: fixed = registry.builtin(TokenKind::KwUint8); break;
                        case IntSuffix::U16: fixed = registry.builtin(TokenKind::KwUint16); break;
                        case IntSuffix::U32: fixed = registry.builtin(TokenKind::KwUint32); break;
                        case IntSuffix::U64: fixed = registry.builtin(TokenKind::KwUint64); break;
                    }
                    return fixed;
                }
                // без суффикса
                if (expected && !registry.is_untyped(*expected)) {
                    return *expected;
                }
                return registry.untyped_int();
            }

            // float-литерал
            if constexpr (std::is_same_v<T, Parser::LitFloatExpr>) {
                if (expected && !registry.is_untyped(*expected)) {
                    return *expected;
                }
                return registry.untyped_float();
            }

            // bool-литерал
            if constexpr (std::is_same_v<T, Parser::LitBoolExpr>) {
                return registry.builtin(TokenKind::KwBool);
            }

            // строковый литерал
            if constexpr (std::is_same_v<T, Parser::LitStringExpr>) {
                return registry.builtin(TokenKind::KwString);
            }

            // символьный литерал
            if constexpr (std::is_same_v<T, Parser::LitCharExpr>) {
                std::string_view raw = n.value;
                std::uint32_t codepoint = 0;

                if (raw.size() >= 2 && raw[0] == '\\') {
                    // escape-последовательность
                    if (raw == "\\n")      codepoint = 0x0A;
                    else if (raw == "\\t") codepoint = 0x09;
                    else if (raw == "\\r") codepoint = 0x0D;
                    else if (raw == "\\\\") codepoint = '\\';
                    else if (raw == "\\'") codepoint = '\'';
                    else if (raw == "\\\"") codepoint = '"';
                    else if (raw == "\\0") codepoint = 0x00;
                    else {
                        diag.error(0, 0, "unknown escape sequence '\\"
                            + std::string(raw.substr(1)) + "'");
                        return registry.error_type();
                    }
                } else if (raw.size() == 1) {
                    codepoint = static_cast<std::uint8_t>(raw[0]);
                } else {
                    diag.error(0, 0, "invalid char literal");
                    return registry.error_type();
                }

                if (codepoint > 0x10FFFF) {
                    diag.error(0, 0, "char codepoint out of range");
                }
                return registry.builtin(TokenKind::KwChar);
            }

            // ошибочное выражение
            if constexpr (std::is_same_v<T, Parser::ErrorExpr>) {
                return registry.error_type();
            }

            // идентификатор
            if constexpr (std::is_same_v<T, Parser::IdentExpr>) {
                Symbol* sym = current_scope->lookup_chain(n.value);
                if (!sym) {
                    diag.error(0, 0, "undefined identifier '" + std::string(n.value) + "'");
                    return registry.error_type();
                }
                if (sym->kind == SymbolKind::Variable) {
                    return std::get<VarSymbol>(sym->data).type;
                }
                if (sym->kind == SymbolKind::Function) {
                    diag.error(0, 0, "cannot use function '" + std::string(n.value) + "' as a value");
                    return registry.error_type();
                }
                if (sym->kind == SymbolKind::Struct || sym->kind == SymbolKind::TypeAllias) {
                    diag.error(0, 0, "cannot use type '" + std::string(n.value) + "' as a value");
                    return registry.error_type();
                }
                diag.error(0, 0, "cannot use '" + std::string(n.value) + "' as an expression");
                return registry.error_type();
            }

            // путь в namespace
            if constexpr (std::is_same_v<T, Parser::PathExpr>) {
                Scope* s = current_scope;
                for (std::size_t i = 0; i < n.segments.size() - 1; ++i) {
                    Symbol* ns_sym = s->lookup_chain(n.segments[i]);
                    if (!ns_sym || ns_sym->kind != SymbolKind::Namespace) {
                        diag.error(0, 0, "unknown namespace '" + std::string(n.segments[i]) + "'");
                        return registry.error_type();
                    }
                    s = std::get<NamespaceSymbol>(ns_sym->data).scope;
                }
                std::string_view last = n.segments.back();
                Symbol* sym = s->lookup_local(last);
                if (!sym) {
                    diag.error(0, 0, "undefined identifier '" + std::string(last) + "' in path");
                    return registry.error_type();
                }
                if (sym->kind == SymbolKind::Variable) {
                    return std::get<VarSymbol>(sym->data).type;
                }
                if (sym->kind == SymbolKind::Function) {
                    diag.error(0, 0, "cannot use function '" + std::string(last) + "' as a value");
                    return registry.error_type();
                }
                diag.error(0, 0, "cannot use '" + std::string(last) + "' as an expression");
                return registry.error_type();
            }

            // унарные операторы
            if constexpr (std::is_same_v<T, Parser::UnaryExpr>) {
                    TypeID inner = check_rec(*n.operand, std::nullopt);
                if (registry.equal(inner, registry.error_type())) {
                    return registry.error_type();
                }
                // унарный минус
                if (n.op == TokenKind::OpMinus) {
                    TypeID norm = registry.resolve_alias(inner);
                    if (registry.is_untyped(norm)) {
                        TypeID def = registry.builtin(TokenKind::KwInt32);
                        if (registry.is_untyped_float(norm)) {
                            def = registry.builtin(TokenKind::KwFloat64);
                        }
                        aast.expr_type[n.operand.get()] = def;
                        return def;
                    }
                    if (!registry.is_fixed_int(norm) && !registry.is_fixed_float(norm)) {
                        diag.error(0, 0, "unary minus requires numeric operand, got " + registry.name(inner));
                        return registry.error_type();
                    }
                    return inner;
                }
                // логическое НЕ
                if (n.op == TokenKind::OpBang) {
                    TypeID norm = registry.resolve_alias(inner);
                    if (!registry.equal(norm, registry.builtin(TokenKind::KwBool))) {
                        diag.error(0, 0, "logical not requires bool operand, got " + registry.name(inner));
                        return registry.error_type();
                    }
                    return registry.builtin(TokenKind::KwBool);
                }
                diag.error(0, 0, "unknown unary operator");
                return registry.error_type();
            }

            // бинарные операторы
            if constexpr (std::is_same_v<T, Parser::BinaryExpr>) {
                bool is_assign = (n.op == TokenKind::OpEq);
                // не присваивание
                if (!is_assign) {
                    TypeID lhs_t = check_expr(*n.lhs, std::nullopt);
                    TypeID rhs_t = check_expr(*n.rhs, std::nullopt);
                    if (registry.equal(lhs_t, registry.error_type()) ||
                        registry.equal(rhs_t, registry.error_type())) {
                        return registry.error_type();
                    }
                    TypeID lhs_norm = registry.resolve_alias(lhs_t);
                    TypeID rhs_norm = registry.resolve_alias(rhs_t);

                    auto is_numeric = [&](TypeID id) {
                        return registry.is_fixed_int(id) || registry.is_fixed_float(id)
                            || registry.is_untyped(id);
                    };

                    switch (n.op) {
                        // арифметика
                        case TokenKind::OpPlus:
                        case TokenKind::OpMinus:
                        case TokenKind::OpStar:
                        case TokenKind::OpSlash:
                        case TokenKind::OpPercent: {
                            // резолв untyped перед арифметикой
                            if (!registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { rhs_t = lhs_t; rhs_norm = lhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && !registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t; lhs_norm = rhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t = registry.builtin(TokenKind::KwInt32);
                                  lhs_norm = rhs_norm = lhs_t; }
                            if (!is_numeric(lhs_norm) || !is_numeric(rhs_norm)) {
                                diag.error(0, 0, "arithmetic on non-numeric types: "
                                    + registry.name(lhs_t) + " and " + registry.name(rhs_t));
                                return registry.error_type();
                            }
                            // % только для целых
                            if (n.op == TokenKind::OpPercent &&
                                (registry.is_float(lhs_norm) || registry.is_float(rhs_norm))) {
                                diag.error(0, 0, "modulo on float types");
                                return registry.error_type();
                            }
                            // конкатенация строк
                            if (n.op == TokenKind::OpPlus &&
                                registry.equal(lhs_norm, registry.builtin(TokenKind::KwString)) &&
                                registry.equal(rhs_norm, registry.builtin(TokenKind::KwString))) {
                                return registry.builtin(TokenKind::KwString);
                            }
                            aast.expr_type[n.lhs.get()] = lhs_t;
                            aast.expr_type[n.rhs.get()] = rhs_t;
                            return unify_binary(lhs_t, rhs_t);
                        }

                        // сравнение
                        case TokenKind::OpEqEq:
                        case TokenKind::OpBangEq:
                            // резолв untyped перед сравнением
                            if (!registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { rhs_t = lhs_t; rhs_norm = lhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && !registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t; lhs_norm = rhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t = registry.builtin(TokenKind::KwInt32);
                                  lhs_norm = rhs_norm = lhs_t; }
                            if (!types_compatible(lhs_norm, rhs_norm)) {
                                diag.error(0, 0, "cannot compare "
                                    + registry.name(lhs_t) + " and " + registry.name(rhs_t));
                                return registry.error_type();
                            }
                            aast.expr_type[n.lhs.get()] = lhs_t;
                            aast.expr_type[n.rhs.get()] = rhs_t;
                            return registry.builtin(TokenKind::KwBool);

                        // неравенства
                        case TokenKind::OpLt:
                        case TokenKind::OpLtEq:
                        case TokenKind::OpGt:
                        case TokenKind::OpGtEq:
                            // резолв untyped перед сравнением
                            if (!registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { rhs_t = lhs_t; rhs_norm = lhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && !registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t; lhs_norm = rhs_norm; }
                            else if (registry.is_untyped(lhs_norm) && registry.is_untyped(rhs_norm))
                                { lhs_t = rhs_t = registry.builtin(TokenKind::KwInt32);
                                  lhs_norm = rhs_norm = lhs_t; }
                            if (!is_numeric(lhs_norm) || !is_numeric(rhs_norm)) {
                                diag.error(0, 0, "comparison on non-numeric types: "
                                    + registry.name(lhs_t) + " and " + registry.name(rhs_t));
                                return registry.error_type();
                            }
                            aast.expr_type[n.lhs.get()] = lhs_t;
                            aast.expr_type[n.rhs.get()] = rhs_t;
                            return registry.builtin(TokenKind::KwBool);

                        // логические
                        case TokenKind::OpAndAnd:
                        case TokenKind::OpOrOr:
                            if (!registry.equal(lhs_norm, registry.builtin(TokenKind::KwBool)) ||
                                !registry.equal(rhs_norm, registry.builtin(TokenKind::KwBool))) {
                                diag.error(0, 0, "logical operator requires bool operands");
                                return registry.error_type();
                            }
                            return registry.builtin(TokenKind::KwBool);

                        default:
                            diag.error(0, 0, "unknown binary operator");
                            return registry.error_type();
                    }
                } else {
                    // присваивание
                    TypeID rhs_t = check_expr(*n.rhs, std::nullopt);
                    TypeID lhs_t = check_expr(*n.lhs, std::nullopt);
                    if (!is_lvalue(*n.lhs)) {
                        diag.error(0, 0, "left-hand side of assignment must be an lvalue");
                        return registry.error_type();
                    }
                    if (!is_mut_root(*n.lhs)) {
                        diag.error(0, 0, "cannot assign to immutable variable");
                        return registry.error_type();
                    }
                    TypeID lhs_norm = registry.resolve_alias(lhs_t);
                    TypeID rhs_norm = registry.resolve_alias(rhs_t);
                    if (!types_compatible(lhs_norm, rhs_norm)) {
                        diag.error(0, 0, "cannot assign " + registry.name(rhs_t)
                            + " to " + registry.name(lhs_t));
                        return registry.error_type();
                    }
                    return lhs_t;
                }
            }

            // группировка (expr)
            if constexpr (std::is_same_v<T, Parser::GroupExpr>) {
                return check_expr(*n.inner, expected);
            }

            // приведение типа (x as Type)
            if constexpr (std::is_same_v<T, Parser::CastExpr>) {
                TypeID src = check_expr(*n.expr, std::nullopt);
                auto dst_opt = resolve_type(n.target);
                if (!dst_opt) return registry.error_type();
                TypeID dst = *dst_opt;
                TypeID src_norm = registry.resolve_alias(src);
                TypeID dst_norm = registry.resolve_alias(dst);

                auto is_char  = [&](TypeID id) { return registry.equal(id, registry.builtin(TokenKind::KwChar)); };
                auto is_num   = [&](TypeID id) { return registry.is_fixed_int(id) || registry.is_fixed_float(id); };
                auto is_un    = [&](TypeID id) { return registry.is_untyped(id); };

                // char <-> uint32
                if (is_char(src_norm) && registry.equal(dst_norm, registry.builtin(TokenKind::KwUint32))) {
                    return dst;
                }
                if (registry.equal(src_norm, registry.builtin(TokenKind::KwUint32)) && is_char(dst_norm)) {
                    return dst;
                }
                // числовые типы
                if ((is_num(src_norm) || is_un(src_norm)) && (is_num(dst_norm) || is_un(dst_norm))) {
                    return dst;
                }
                diag.error(0, 0, "invalid cast from " + registry.name(src) + " to " + registry.name(dst));
                return registry.error_type();
            }

            // вызов функции
            if constexpr (std::is_same_v<T, Parser::CallExpr>) {
                // имя функции
                std::string_view fn_name;
                bool is_path = false;
                std::vector<std::string_view> path_segments;
                if (const auto* id = std::get_if<Parser::IdentExpr>(&n.call->node)) {
                    fn_name = id->value;
                } else if (const auto* path = std::get_if<Parser::PathExpr>(&n.call->node)) {
                    is_path = true;
                    path_segments = path->segments;
                    fn_name = path_segments.back();
                } else {
                    diag.error(0, 0, "invalid function call target");
                    return registry.error_type();
                }

                // область видимости (с учётом namespace)
                Scope* fn_scope = current_scope;
                if (is_path) {
                    for (std::size_t i = 0; i < path_segments.size() - 1; ++i) {
                        Symbol* ns = fn_scope->lookup_chain(path_segments[i]);
                        if (!ns || ns->kind != SymbolKind::Namespace) {
                            diag.error(0, 0, "unknown namespace '" + std::string(path_segments[i]) + "'");
                            return registry.error_type();
                        }
                        fn_scope = std::get<NamespaceSymbol>(ns->data).scope;
                    }
                }

                // символ функции
                Symbol* sym = fn_scope->lookup_chain(fn_name);
                if (!sym || sym->kind != SymbolKind::Function) {
                    diag.error(0, 0, "unknown function '" + std::string(fn_name) + "'");
                    return registry.error_type();
                }

                auto& fs = std::get<FuncSymbol>(sym->data);

                // типы аргументов
                std::vector<TypeID> arg_types;
                for (const auto& arg : n.args) {
                    arg_types.push_back(check_expr(arg, std::nullopt));
                }

                // разрешение перегрузки
                auto match = resolve_overload(fs.overloads, arg_types, registry);
                if (!match) {
                    std::string msg = "no matching overload for '" + std::string(fn_name) + "'(";
                    for (std::size_t i = 0; i < arg_types.size(); ++i) {
                        if (i) msg += ", ";
                        msg += registry.name(arg_types[i]);
                    }
                    msg += ")";
                    diag.error(0, 0, msg);
                    return registry.error_type();
                }

                // индекс разрешённой перегрузки
                aast.resolved_overload[&n] = match->index;

                // подмена типов нетипизированных аргументов
                for (std::size_t i = 0; i < n.args.size(); ++i) {
                    if (registry.is_untyped(arg_types[i]) && i < match->resolved_args.size()) {
                        aast.expr_type[&n.args[i]] = match->resolved_args[i];
                    }
                }

                return fs.overloads[match->index].return_type;
            }

            // индексация массива
            if constexpr (std::is_same_v<T, Parser::IndexExpr>) {
                TypeID arr_t = check_expr(*n.array, std::nullopt);
                TypeID idx_t = check_expr(*n.index, std::nullopt);
                TypeID arr_norm = registry.resolve_alias(arr_t);
                if (!registry.is_array(arr_norm)) {
                    diag.error(0, 0, "cannot index non-array type " + registry.name(arr_t));
                    return registry.error_type();
                }
                TypeID idx_norm = registry.resolve_alias(idx_t);
                if (registry.is_untyped(idx_norm)) {
                    idx_t = registry.builtin(TokenKind::KwInt32);
                    aast.expr_type[n.index.get()] = idx_t;
                } else if (!registry.is_fixed_int(idx_norm)) {
                    diag.error(0, 0, "array index must be integer, got " + registry.name(idx_t));
                    return registry.error_type();
                }
                return registry.get_array(arr_norm)->elem;
            }

            // доступ к полю структуры
            if constexpr (std::is_same_v<T, Parser::FieldExpr>) {
                TypeID obj_t = check_expr(*n.object, std::nullopt);
                TypeID obj_norm = registry.resolve_alias(obj_t);
                if (!registry.is_struct(obj_norm)) {
                    diag.error(0, 0, "cannot access field of non-struct type " + registry.name(obj_t));
                    return registry.error_type();
                }
                const auto* st = registry.get_struct(obj_norm);
                for (const auto& [fname, ftype] : st->fields) {
                    if (fname == n.field) return ftype;
                }
                diag.error(0, 0, "unknown field '" + std::string(n.field)
                    + "' in struct " + registry.name(obj_t));
                return registry.error_type();
            }

            // литерал массива [a, b, c]
            if constexpr (std::is_same_v<T, Parser::ArrayLitExpr>) {
                if (n.elems.empty()) {
                    diag.error(0, 0, "cannot infer type of empty array literal");
                    return registry.error_type();
                }
                TypeID elem_type = check_expr(n.elems[0], std::nullopt);
                if (registry.is_untyped(elem_type)) {
                    elem_type = registry.builtin(TokenKind::KwInt32);
                    aast.expr_type[&n.elems[0]] = elem_type;
                }
                for (std::size_t i = 1; i < n.elems.size(); ++i) {
                    TypeID t = check_expr(n.elems[i], elem_type);
                    if (registry.is_untyped(t)) {
                        t = elem_type;
                        aast.expr_type[&n.elems[i]] = t;
                    }
                    if (!registry.equal(registry.resolve_alias(t), registry.resolve_alias(elem_type))) {
                        diag.error(0, 0, "array element type mismatch: expected "
                            + registry.name(elem_type) + ", got " + registry.name(t));
                    }
                }
                return registry.array(elem_type, n.elems.size());
            }

            // литерал структуры
            if constexpr (std::is_same_v<T, Parser::StructLitExpr>) {
                Symbol* sym = current_scope->lookup_chain(n.name);
                if (!sym || sym->kind != SymbolKind::Struct) {
                    diag.error(0, 0, "unknown struct '" + std::string(n.name) + "'");
                    return registry.error_type();
                }
                TypeID st_id = std::get<TypeSymbol>(sym->data).id;
                const auto* st = registry.get_struct(st_id);
                if (!st) {
                    return registry.error_type();
                }
                std::unordered_set<std::string_view> seen;
                for (const auto& [fname, fexpr] : n.fields) {
                    if (!seen.insert(fname).second) {
                        diag.error(0, 0, "duplicate field '" + std::string(fname) + "' in struct literal");
                        continue;
                    }
                    // тип поля в структуре
                    TypeID ft = registry.error_type();
                    bool found = false;
                    for (const auto& [sfn, sft] : st->fields) {
                        if (sfn == fname) { ft = sft; found = true; break; }
                    }
                    if (!found) {
                        diag.error(0, 0, "unknown field '" + std::string(fname)
                            + "' in struct " + std::string(n.name));
                        continue;
                    }
                    TypeID init_t = check_expr(*fexpr, ft);
                    if (!registry.equal(registry.resolve_alias(init_t), registry.resolve_alias(ft))) {
                        diag.error(0, 0, "field '" + std::string(fname)
                            + "' expects " + registry.name(ft)
                            + ", got " + registry.name(init_t));
                    }
                }
                // пропущенные поля
                for (const auto& [sfn, sft] : st->fields) {
                    (void)sft;
                    if (!seen.contains(sfn)) {
                        diag.error(0, 0, "missing field '" + std::string(sfn)
                            + "' in struct literal for " + std::string(n.name));
                    }
                }
                return st_id;
            }

            return registry.error_type();
        }, e.node);

        aast.expr_type[&e] = result;
        return result;
    }

    // проверка инструкций
    void Semantic::check_stmt(const Parser::Stmt& stmt) {
        std::visit([&](const auto& n) {
            using T = std::decay_t<decltype(n)>;

            // объявление переменной
            if constexpr (std::is_same_v<T, Parser::LetStmt>) {
                std::optional<TypeID> expected;
                if (n.type) {
                    expected = resolve_type(*n.type);
                    if (!expected) return;
                }
                TypeID init_t = check_expr(*n.expr_init, expected);
                if (registry.is_untyped(init_t)) {
                    init_t = expected.value_or(registry.builtin(TokenKind::KwInt32));
                    aast.expr_type[n.expr_init.get()] = init_t;
                }
                if (expected && !registry.equal(registry.resolve_alias(init_t),
                                                registry.resolve_alias(*expected))) {
                    diag.error(0, 0, "type mismatch: variable '" + std::string(n.name)
                        + "' expects " + registry.name(*expected)
                        + ", got " + registry.name(init_t));
                }
                if (!current_scope->insert(Symbol{
                    SymbolKind::Variable, n.name,
                    VarSymbol{init_t, n.is_mut, 0, 0}})) {
                    diag.error(0, 0, "duplicate variable '" + std::string(n.name) + "'");
                }
                aast.let_type[&n] = init_t;
            }

            // выражение-инструкция
            else if constexpr (std::is_same_v<T, Parser::ExprStmt>) {
                check_expr(*n.expr, std::nullopt);
            }

            // блок
            else if constexpr (std::is_same_v<T, Parser::BlockStmt>) {
                Scope& block_scope = push_scope(*current_scope);
                Scope* old = current_scope;
                current_scope = &block_scope;
                for (const auto& s : n.elems) {
                    check_stmt(s);
                }
                current_scope = old;
            }

            // if
            else if constexpr (std::is_same_v<T, Parser::IfStmt>) {
                TypeID cond_t = check_expr(*n.condition, std::nullopt);
                TypeID cond_norm = registry.resolve_alias(cond_t);
                if (!registry.equal(cond_norm, registry.builtin(TokenKind::KwBool))) {
                    diag.error(0, 0, "if condition must be bool, got " + registry.name(cond_t));
                }
                check_stmt(*n.then_body);
                if (n.else_body) check_stmt(*n.else_body);
            }

            // while
            else if constexpr (std::is_same_v<T, Parser::WhileStmt>) {
                TypeID cond_t = check_expr(*n.condition, std::nullopt);
                TypeID cond_norm = registry.resolve_alias(cond_t);
                if (!registry.equal(cond_norm, registry.builtin(TokenKind::KwBool))) {
                    diag.error(0, 0, "while condition must be bool, got " + registry.name(cond_t));
                }
                bool old_loop = in_loop;
                in_loop = true;
                check_stmt(*n.body);
                in_loop = old_loop;
            }

            // return
            else if constexpr (std::is_same_v<T, Parser::ReturnStmt>) {
                if (n.value) {
                    TypeID val_t = check_expr(*n.value, current_return_type);
                    if (registry.is_untyped(val_t)) {
                        val_t = current_return_type;
                        aast.expr_type[&*n.value] = val_t;
                    }
                    if (!registry.equal(registry.resolve_alias(val_t),
                                        registry.resolve_alias(current_return_type))) {
                        diag.error(0, 0, "return type mismatch: expected "
                            + registry.name(current_return_type)
                            + ", got " + registry.name(val_t));
                    }
                } else {
                    if (!registry.equal(current_return_type, registry.void_type())) {
                        diag.error(0, 0, "return with no value in non-void function");
                    }
                }
            }

            // break
            else if constexpr (std::is_same_v<T, Parser::BreakStmt>) {
                if (!in_loop) {
                    diag.error(0, 0, "break outside loop");
                }
            }

            // continue
            else if constexpr (std::is_same_v<T, Parser::ContinueStmt>) {
                if (!in_loop) {
                    diag.error(0, 0, "continue outside loop");
                }
            }

            // пустая инструкция (;)
            else if constexpr (std::is_same_v<T, Parser::NullStmt>) {
                // ничего не делаем
            }

        }, stmt.node);
    }

    // проверка тела функции
    void Semantic::check_function_body(const Parser::FnDecl& fn) {
        Scope& fn_scope = push_scope(root_scope);
        Scope* old_scope = current_scope;
        current_scope = &fn_scope;

        TypeID old_ret = current_return_type;
        current_return_type = fn.return_type
            ? resolve_type(*fn.return_type).value_or(registry.error_type())
            : registry.void_type();

        bool old_loop = in_loop;
        in_loop = false;

        // параметры как локальные переменные
        for (const auto& [pname, ptype] : fn.params) {
            auto tid = resolve_type(ptype);
            if (tid) {
                fn_scope.insert(Symbol{SymbolKind::Variable, pname,
                    VarSymbol{*tid, false, 0, 0}});
            }
        }

        // проверка тела
        for (const auto& stmt : fn.body.elems) {
            check_stmt(stmt);
        }

        // проверка return в не-void
        if (!registry.equal(current_return_type, registry.void_type())) {
            bool has_return = false;
            for (const auto& s : fn.body.elems) {
                if (always_returns(s)) { has_return = true; break; }
            }
            if (!has_return) {
                diag.error(0, 0, "non-void function '" + std::string(fn.name)
                    + "' does not return a value on all paths");
            }
        }

        current_scope = old_scope;
        current_return_type = old_ret;
        in_loop = old_loop;
    }

    // проход 2: проверка тел функций
    void Semantic::pass2(const std::vector<Parser::Decl>& decls, Scope& scope) {
        for (const auto& d : decls) {
            std::visit([&](const auto& n) {
                using T = std::decay_t<decltype(n)>;
                if constexpr (std::is_same_v<T, Parser::FnDecl>) {
                    check_function_body(n);
                } else if constexpr (std::is_same_v<T, Parser::NameSpaceDecl>) {
                    Symbol* ns_sym = scope.lookup_local(n.name);
                    if (ns_sym && ns_sym->kind == SymbolKind::Namespace) {
                        pass2(n.decls, *std::get<NamespaceSymbol>(ns_sym->data).scope);
                    }
                }
            }, d.node);
        }
    }

    // унификация типов в бинарных операциях
    TypeID Semantic::unify_binary(TypeID lhs, TypeID rhs) {
        TypeID ln = registry.resolve_alias(lhs);
        TypeID rn = registry.resolve_alias(rhs);

        // оба нетипизированы
        if (registry.is_untyped_int(ln) && registry.is_untyped_int(rn))
            return registry.builtin(TokenKind::KwInt32);
        if (registry.is_untyped_float(ln) && registry.is_untyped_float(rn))
            return registry.builtin(TokenKind::KwFloat64);

        // один нетипизирован
        if (registry.is_untyped(ln)) return rhs;
        if (registry.is_untyped(rn)) return lhs;

        // оба конкретные
        if (registry.is_float(ln) || registry.is_float(rn)) {
            // оба float — более широкий
            if (registry.is_float(ln) && registry.is_float(rn)) {
                if (registry.equal(ln, registry.builtin(TokenKind::KwFloat64)) ||
                    registry.equal(rn, registry.builtin(TokenKind::KwFloat64)))
                    return registry.builtin(TokenKind::KwFloat64);
                return registry.builtin(TokenKind::KwFloat32);
            }
            // float + int → float
            return registry.is_float(ln) ? lhs : rhs;
        }

        // оба целые
        if (registry.is_fixed_int(registry.resolve_alias(ln)) &&
            registry.is_fixed_int(registry.resolve_alias(rn))) {
            // оба беззнаковые — более широкий
            if (is_unsigned(ln) && is_unsigned(rn)) {
                return bit_width(ln) >= bit_width(rn) ? lhs : rhs;
            }
            // смешанные signed/unsigned — более широкий
            if (is_unsigned(ln) || is_unsigned(rn)) {
                if (bit_width(ln) > bit_width(rn)) return lhs;
                if (bit_width(rn) > bit_width(ln)) return rhs;
                return registry.equal(ln, rn) ? lhs : rhs;
            }
            // оба signed
            return bit_width(ln) >= bit_width(rn) ? lhs : rhs;
        }
        return lhs;
    }

    // совместимость типов (сравнение, присваивание)
    bool Semantic::types_compatible(TypeID a, TypeID b) {
        TypeID an = registry.resolve_alias(a);
        TypeID bn = registry.resolve_alias(b);
        if (registry.equal(an, bn)) return true;
        if (registry.is_untyped(an) || registry.is_untyped(bn)) return true;
        if (registry.is_fixed_int(an) && registry.is_fixed_int(bn)) return true;
        if (registry.is_fixed_float(an) && registry.is_fixed_float(bn)) return true;
        return false;
    }

    // беззнаковый?
    bool Semantic::is_unsigned(TypeID id) const {
        TypeID norm = registry.resolve_alias(id);
        return registry.equal(norm, registry.builtin(TokenKind::KwUint8))
            || registry.equal(norm, registry.builtin(TokenKind::KwUint16))
            || registry.equal(norm, registry.builtin(TokenKind::KwUint32))
            || registry.equal(norm, registry.builtin(TokenKind::KwUint64));
    }

    // разрядность в битах
    int Semantic::bit_width(TypeID id) const {
        TypeID norm = registry.resolve_alias(id);
        if (registry.equal(norm, registry.builtin(TokenKind::KwInt8))
         || registry.equal(norm, registry.builtin(TokenKind::KwUint8))) return 8;
        if (registry.equal(norm, registry.builtin(TokenKind::KwInt16))
         || registry.equal(norm, registry.builtin(TokenKind::KwUint16))) return 16;
        if (registry.equal(norm, registry.builtin(TokenKind::KwInt32))
         || registry.equal(norm, registry.builtin(TokenKind::KwUint32))) return 32;
        if (registry.equal(norm, registry.builtin(TokenKind::KwInt64))
         || registry.equal(norm, registry.builtin(TokenKind::KwUint64))) return 64;
        if (registry.equal(norm, registry.builtin(TokenKind::KwFloat32))) return 32;
        if (registry.equal(norm, registry.builtin(TokenKind::KwFloat64))) return 64;
        return 0;
    }

    // печать диагностик
    void print_diagnostics(const DiagBag& bag, std::string_view file, std::ostream &os) {
        for (const auto& d : bag.all()) {
            std::string_view sev = d.severity == DiagSeverity::Error ? "error" : d.severity == DiagSeverity::Warning ? "warning" : "note";
            os << file << ':' << d.line << ':' << d.column
               << ": " << sev << ": " << d.message << '\n';
        }
    }
}
