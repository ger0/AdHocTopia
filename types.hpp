#ifndef ADHTP_TYPES_HDR
#define ADHTP_TYPES_HDR

#define DEBUG // remove later on

#include <cstdint>
#include <fmt/ranges.h>
#include <fmt/core.h>
#include <inttypes.h>

using i32   = int_fast32_t;
using uint  = uint32_t;
using u64   = uint64_t;
using byte  = unsigned char;
using c_str = const char*;

#ifndef defer
struct defer_dummy {};
template <class F> struct deferrer { F f; ~deferrer() { f(); } };
template <class F> deferrer<F> operator*(defer_dummy, F f) { return {f}; }
#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer auto DEFER(__LINE__) = defer_dummy{} *[&]()
#endif // defer

#define LOG(...) \
    fmt::print(stdout, "{}\n", fmt::format(__VA_ARGS__))
#define LOG_ERR(...) \
    fmt::print(stderr, "\033[1;31m{}\033[0m\n", fmt::format(__VA_ARGS__))

#ifdef DEBUG 
#define LOG_DBG(...) \
    fmt::print(stderr, "\033[1;33m{}\033[0m\n", fmt::format(__VA_ARGS__))
#else 
#define LOG_DBG(...) ;
#endif

#endif //ADHTP_TYPES_HDR
