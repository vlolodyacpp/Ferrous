module;
#include <string_view>
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
        return a == b;
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
    DiagBag Semantic::check(const std::vector<Parser::Decl>& /*tbd*/) {
        return std::move(diag);  // заглушка
    }

    void print_diagnostics(const DiagBag& bag, std::string_view file, std::ostream &os) {
        for (const auto& d : bag.all()) {
            std::string_view sev = d.severity == DiagSeverity::Error ? "error" : d.severity == DiagSeverity::Warning ? "warning" : "note";
            os << file << ':' << d.line << ':' << d.column
               << ": " << sev << ": " << d.message << '\n';
        }
    }







}
