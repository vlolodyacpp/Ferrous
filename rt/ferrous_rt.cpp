// Runtime-библиотека Ferrous — extern "C" для линковки с LLVM IR
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <string>

constexpr std::size_t ARENA_SIZE = 1024 * 1024;
static char arena[ARENA_SIZE];
static std::size_t arena_used = 0;

static auto arena_alloc(std::size_t n) -> void* {
    if (arena_used + n > ARENA_SIZE) {
        std::cerr << "runtime error: out of memory\n";
        std::exit(1);
    }
    void* p = &arena[arena_used];
    arena_used += n;
    return p;
}

struct FerStr { char* ptr; std::int64_t len; };

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

void __ferrous_bounds_check(std::int64_t index, std::int64_t len, std::int64_t line) {
    if (index < 0 || index >= len) {
        std::cerr << "runtime error: index out of bounds at line " << line << '\n';
        std::exit(1);
    }
}

void __ferrous_print_int64(std::int64_t val)  { std::cout << val; }
void __ferrous_print_float64(double val)       { std::cout << val; }
void __ferrous_print_string(const char* s, std::int64_t len) {
    std::cout << std::string_view(s, static_cast<std::size_t>(len));
}

void __ferrous_println_int64(std::int64_t val)  { std::cout << val << '\n'; }
void __ferrous_println_float64(double val)       { std::cout << val << '\n'; }
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

FerStr __ferrous_int_to_str(std::int64_t val) {
    std::string s = std::to_string(val);
    std::size_t n = s.size();
    char* p = static_cast<char*>(arena_alloc(n));
    std::memcpy(p, s.data(), n);
    return {p, static_cast<std::int64_t>(n)};
}

FerStr __ferrous_float_to_str(double val) {
    std::string s = std::to_string(val);
    std::size_t n = s.size();
    char* p = static_cast<char*>(arena_alloc(n));
    std::memcpy(p, s.data(), n);
    return {p, static_cast<std::int64_t>(n)};
}

} // extern "C"
