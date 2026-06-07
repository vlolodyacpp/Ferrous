#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

constexpr std::size_t ARENA_SIZE = 1024 * 1024;  // 1 МиБ
static char arena[ARENA_SIZE];
static std::size_t arena_used = 0;

static auto arena_alloc(std::size_t n) -> void* {
    if (arena_used + n > ARENA_SIZE) {           // переполнение пула — фатально
        std::cerr << "runtime error: out of memory\n";
        std::exit(1);
    }
    void* p = &arena[arena_used];
    arena_used += n;
    return p;
}

// представление строки - указатель на байты (UTF-8, без \0) + длина.
struct FerStr { char* ptr; std::int64_t len; };

// кодирует Unicode-codepoint (UTF-32) в UTF-8 и пишет в stdout — для типа char.
static void put_utf8(std::uint32_t cp) {
    auto put = [](unsigned b) { std::cout.put(static_cast<char>(b & 0xFF)); };
    if (cp < 0x80) {
        put(cp);
    } else if (cp < 0x800) {
        put(0xC0 | (cp >> 6));  put(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        put(0xE0 | (cp >> 12)); put(0x80 | ((cp >> 6) & 0x3F)); put(0x80 | (cp & 0x3F));
    } else {
        put(0xF0 | (cp >> 18)); put(0x80 | ((cp >> 12) & 0x3F));
        put(0x80 | ((cp >> 6) & 0x3F)); put(0x80 | (cp & 0x3F));
    }
}

extern "C" {

    void __ferrous_panic(const char* msg, std::int64_t len, std::int64_t line) {
        std::cerr << "runtime error: "
                  << std::string_view(msg, static_cast<std::size_t>(len))
                  << " at line " << line << '\n';
        std::exit(1);
    }

    void __ferrous_assert_fail(const char* msg, std::int64_t len, std::int64_t line) {
        std::cerr << "runtime error: assertion failed: "
                  << std::string_view(msg, static_cast<std::size_t>(len))
                  << " at line " << line << '\n';
        std::exit(1);
    }


    // div/mod: проверяют делитель и возвращают его.
    std::int64_t __ferrous_div_check(std::int64_t divisor, std::int64_t line) {
        if (divisor == 0) {
            std::cerr << "runtime error: division by zero at line " << line << '\n';
            std::exit(1);
        }
        return divisor;
    }

    std::int64_t __ferrous_mod_check(std::int64_t divisor, std::int64_t line) {
        if (divisor == 0) {
            std::cerr << "runtime error: modulo by zero at line " << line << '\n';
            std::exit(1);
        }
        return divisor;
    }

    // проверка границ массива
    void __ferrous_bounds_check(std::int64_t index, std::int64_t len, std::int64_t line) {
        if (index < 0 || index >= len) {
            std::cerr << "runtime error: index out of bounds at line " << line << '\n';
            std::exit(1);
        }
    }


    void __ferrous_print_int8 (std::int8_t  v) { std::cout << static_cast<int>(v); }
    void __ferrous_print_int16(std::int16_t v) { std::cout << v; }
    void __ferrous_print_int32(std::int32_t v) { std::cout << v; }
    void __ferrous_print_int64(std::int64_t v) { std::cout << v; }
    void __ferrous_print_uint8 (std::uint8_t  v) { std::cout << static_cast<unsigned>(v); }
    void __ferrous_print_uint16(std::uint16_t v) { std::cout << v; }
    void __ferrous_print_uint32(std::uint32_t v) { std::cout << v; }
    void __ferrous_print_uint64(std::uint64_t v) { std::cout << v; }
    void __ferrous_print_float32(float  v) { std::cout << v; }
    void __ferrous_print_float64(double v) { std::cout << v; }
    void __ferrous_print_bool(std::int8_t v) { std::cout << (v ? "true" : "false"); }
    void __ferrous_print_char(std::uint32_t cp) { put_utf8(cp); }
    void __ferrous_print_string(const char* s, std::int64_t len) {
        // строка не нуль-терминирована — печатаем по явной длине
        std::cout << std::string_view(s, static_cast<std::size_t>(len));
    }

    void __ferrous_println_int8 (std::int8_t  v) { std::cout << static_cast<int>(v) << '\n'; }
    void __ferrous_println_int16(std::int16_t v) { std::cout << v << '\n'; }
    void __ferrous_println_int32(std::int32_t v) { std::cout << v << '\n'; }
    void __ferrous_println_int64(std::int64_t v) { std::cout << v << '\n'; }
    void __ferrous_println_uint8 (std::uint8_t  v) { std::cout << static_cast<unsigned>(v) << '\n'; }
    void __ferrous_println_uint16(std::uint16_t v) { std::cout << v << '\n'; }
    void __ferrous_println_uint32(std::uint32_t v) { std::cout << v << '\n'; }
    void __ferrous_println_uint64(std::uint64_t v) { std::cout << v << '\n'; }
    void __ferrous_println_float32(float  v) { std::cout << v << '\n'; }
    void __ferrous_println_float64(double v) { std::cout << v << '\n'; }
    void __ferrous_println_bool(std::int8_t v) { std::cout << (v ? "true" : "false") << '\n'; }
    void __ferrous_println_char(std::uint32_t cp) { put_utf8(cp); std::cout << '\n'; }
    void __ferrous_println_string(const char* s, std::int64_t len) {
        std::cout << std::string_view(s, static_cast<std::size_t>(len)) << '\n';
    }

    FerStr __ferrous_input() {
        std::string line;
        if (!std::getline(std::cin, line)) line.clear();
        std::size_t n = line.size();
        char* p = static_cast<char*>(arena_alloc(n));
        std::memcpy(p, line.data(), n);
        return {p, static_cast<std::int64_t>(n)};
    }

    FerStr __ferrous_str_concat(const char* a, std::int64_t la,
                                const char* b, std::int64_t lb) {
        std::size_t total = static_cast<std::size_t>(la + lb);
        char* p = static_cast<char*>(arena_alloc(total));
        std::memcpy(p, a, static_cast<std::size_t>(la));
        std::memcpy(p + la, b, static_cast<std::size_t>(lb));
        return {p, static_cast<std::int64_t>(total)};
    }

    std::int8_t __ferrous_str_eq(const char* a, std::int64_t la,
                                 const char* b, std::int64_t lb) {
        return (la == lb && std::memcmp(a, b, static_cast<std::size_t>(la)) == 0) ? 1 : 0;
    }

} // extern "C"
