#pragma once

// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


// Macros for compiler version + nonstandard keywords, e.g. __builtin_expect.

#include <sys/types.h>  // IWYU pragma: export
#ifdef __clang_analyzer__
#include <stdio.h>  // IWYU pragma: export
#endif

#ifndef JXL_ASAN_POISON_MEMORY_REGION
#define JXL_ASAN_POISON_MEMORY_REGION(kAddress, kSize) ((void)(kAddress), (void)(kSize))
#define JXL_ASAN_UNPOISON_MEMORY_REGION(kAddress, kSize) ((void)(kAddress), (void)(kSize))
#define JXL_MSAN_POISON(kAddress, kSize) ((void)(kAddress), (void)(kSize))
#define JXL_MSAN_UNPOISON(kAddress, kSize) ((void)(kAddress), (void)(kSize))
#define JXL_MSAN_ALLOCATED(kAddress, kSize) ((void)(kAddress), (void)(kSize))
#endif

#if JXL_ADDRESS_SANITIZER || JXL_MEMORY_SANITIZER || JXL_THREAD_SANITIZER
#endif                                        // defined(*_SANITIZER)

// #if is shorter and safer than #ifdef. *_VERSION are zero if not detected,
// otherwise 100 * major + minor version. Note that other packages check for
// #ifdef COMPILER_MSVC, so we cannot use that same name.

#ifdef _MSC_VER
#define JXL_COMPILER_MSVC _MSC_VER
#else
#define JXL_COMPILER_MSVC 0
#endif

#ifdef __GNUC__
#define JXL_COMPILER_GCC (__GNUC__ * 100 + __GNUC_MINOR__)
#else
#define JXL_COMPILER_GCC 0
#endif

#ifdef __clang__
#define JXL_COMPILER_CLANG (__clang_major__ * 100 + __clang_minor__)
// Clang pretends to be GCC for compatibility.
#undef JXL_COMPILER_GCC
#define JXL_COMPILER_GCC 0
#else
#define JXL_COMPILER_CLANG 0
#endif

#if JXL_COMPILER_MSVC
#define JXL_RESTRICT __restrict
#elif JXL_COMPILER_GCC || JXL_COMPILER_CLANG
#define JXL_RESTRICT __restrict__
#else
#define JXL_RESTRICT
#endif

#if JXL_COMPILER_MSVC
#define JXL_INLINE __forceinline
#define JXL_NOINLINE __declspec(noinline)
#else
#define JXL_INLINE inline __attribute__((always_inline))
#define JXL_NOINLINE __attribute__((noinline))
#endif

#if JXL_COMPILER_MSVC
#define JXL_NORETURN __declspec(noreturn)
#elif JXL_COMPILER_GCC || JXL_COMPILER_CLANG
#define JXL_NORETURN __attribute__((noreturn))
#else
#define JXL_NORETURN
#endif

#if JXL_COMPILER_MSVC
#define JXL_MAYBE_UNUSED
#else
// Encountered "attribute list cannot appear here" when using the C++17
// [[maybe_unused]], so only use the old style attribute for now.
#define JXL_MAYBE_UNUSED __attribute__((unused))
#endif

// MSAN execution won't hurt if some code it not inlined, but this can greatly
// improve compilation time. Unfortunately this macro can not be used just
// everywhere - inside header files it leads to "multiple definition" error;
// though it would be better not to have JXL_INLINE in header overall.
#if JXL_MEMORY_SANITIZER || JXL_ADDRESS_SANITIZER || JXL_THREAD_SANITIZER
#define JXL_MAYBE_INLINE JXL_MAYBE_UNUSED
#else
#define JXL_MAYBE_INLINE JXL_INLINE
#endif

#if JXL_COMPILER_MSVC
// Unsupported, __assume is not the same.
#define JXL_LIKELY(expr) expr
#define JXL_UNLIKELY(expr) expr
#else
#define JXL_LIKELY(expr) __builtin_expect(!!(expr), 1)
#define JXL_UNLIKELY(expr) __builtin_expect(!!(expr), 0)
#endif

// Returns a void* pointer which the compiler then assumes is N-byte aligned.
// Example: float* JXL_RESTRICT aligned = (float*)JXL_ASSUME_ALIGNED(in, 32);
//
// The assignment semantics are required by GCC/Clang. ICC provides an in-place
// __assume_aligned, whereas MSVC's __assume appears unsuitable.
#if JXL_COMPILER_CLANG
// Early versions of Clang did not support __builtin_assume_aligned.
#define JXL_HAS_ASSUME_ALIGNED __has_builtin(__builtin_assume_aligned)
#elif JXL_COMPILER_GCC
#define JXL_HAS_ASSUME_ALIGNED 1
#else
#define JXL_HAS_ASSUME_ALIGNED 0
#endif

#if JXL_HAS_ASSUME_ALIGNED
#define JXL_ASSUME_ALIGNED(ptr, align) __builtin_assume_aligned((ptr), (align))
#else
#define JXL_ASSUME_ALIGNED(ptr, align) (ptr) /* not supported */
#endif

#ifdef __has_attribute
#define JXL_HAVE_ATTRIBUTE(x) __has_attribute(x)
#else
#define JXL_HAVE_ATTRIBUTE(x) 0
#endif

// Raises warnings if the function return value is unused. Should appear as the
// first part of a function definition/declaration.
#if JXL_HAVE_ATTRIBUTE(nodiscard)
#define JXL_MUST_USE_RESULT [[nodiscard]]
#elif JXL_COMPILER_CLANG && JXL_HAVE_ATTRIBUTE(warn_unused_result)
#define JXL_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define JXL_MUST_USE_RESULT
#endif

// Disable certain -fsanitize flags for functions that are expected to include
// things like unsigned integer overflow. For example use in the function
// declaration JXL_NO_SANITIZE("unsigned-integer-overflow") to silence unsigned
// integer overflow ubsan messages.
#if JXL_COMPILER_CLANG && JXL_HAVE_ATTRIBUTE(no_sanitize)
#define JXL_NO_SANITIZE(X) __attribute__((no_sanitize(X)))
#else
#define JXL_NO_SANITIZE(X)
#endif

#if JXL_HAVE_ATTRIBUTE(__format__)
#define JXL_FORMAT(idx_fmt, idx_arg) \
  __attribute__((__format__(__printf__, idx_fmt, idx_arg)))
#else
#define JXL_FORMAT(idx_fmt, idx_arg)
#endif

// C++ standard.
#if defined(_MSC_VER) && !defined(__clang__) && defined(_MSVC_LANG) && \
    _MSVC_LANG > __cplusplus
#define JXL_CXX_LANG _MSVC_LANG
#else
#define JXL_CXX_LANG __cplusplus
#endif

// Known / distinguished C++ standards.
#define JXL_CXX_17 201703

// In most cases we consider build as "debug". Use `NDEBUG` for release build.
#if defined(JXL_IS_DEBUG_BUILD)
#undef JXL_IS_DEBUG_BUILD
#define JXL_IS_DEBUG_BUILD 1
#elif defined(NDEBUG)
#define JXL_IS_DEBUG_BUILD 0
#else
#define JXL_IS_DEBUG_BUILD 1
#endif

#if defined(JXL_CRASH_ON_ERROR)
#undef JXL_CRASH_ON_ERROR
#define JXL_CRASH_ON_ERROR 1
#else
#define JXL_CRASH_ON_ERROR 0
#endif

#if JXL_CRASH_ON_ERROR && !JXL_IS_DEBUG_BUILD
#error "JXL_CRASH_ON_ERROR requires JXL_IS_DEBUG_BUILD"
#endif

// Pass -DJXL_DEBUG_ON_ALL_ERROR at compile time to print debug messages on
// all error (fatal and non-fatal) status.
#if defined(JXL_DEBUG_ON_ALL_ERROR)
#undef JXL_DEBUG_ON_ALL_ERROR
#define JXL_DEBUG_ON_ALL_ERROR 1
#else
#define JXL_DEBUG_ON_ALL_ERROR 0
#endif

#if JXL_DEBUG_ON_ALL_ERROR && !JXL_IS_DEBUG_BUILD
#error "JXL_DEBUG_ON_ALL_ERROR requires JXL_IS_DEBUG_BUILD"
#endif

// Pass -DJXL_DEBUG_ON_ABORT={0} to disable the debug messages on
// (debug) JXL_ENSURE and JXL_DASSERT.
#if !defined(JXL_DEBUG_ON_ABORT)
#define JXL_DEBUG_ON_ABORT JXL_IS_DEBUG_BUILD
#endif  // JXL_DEBUG_ON_ABORT

#if JXL_DEBUG_ON_ABORT && !JXL_IS_DEBUG_BUILD
#error "JXL_DEBUG_ON_ABORT requires JXL_IS_DEBUG_BUILD"
#endif

#if JXL_ADDRESS_SANITIZER || JXL_MEMORY_SANITIZER || JXL_THREAD_SANITIZER
#define JXL_PRINT_STACK_TRACE() ((void)0)
#else
#define JXL_PRINT_STACK_TRACE()
#endif

#if JXL_COMPILER_MSVC
#define JXL_CRASH() __debugbreak(), (void)abort()
#else
#define JXL_CRASH() (void)__builtin_trap()
#endif


// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.


// Shared constants and helper functions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

#if JXL_COMPILER_MSVC
#include <intrin.h>
#endif


namespace ba {
// Some enums and typedefs used by more than one header file.

constexpr size_t kBitsPerByte = 8;  // more clear than CHAR_BIT

constexpr inline size_t RoundUpBitsToByteMultiple(size_t bits) {
  return (bits + 7) & ~static_cast<size_t>(7);
}

constexpr inline size_t RoundUpToBlockDim(size_t dim) {
  return (dim + 7) & ~static_cast<size_t>(7);
}

template <typename U,
          class = typename std::enable_if<std::is_unsigned<U>::value>::type>
static inline bool SafeAdd(const U a, const U b, U& sum) {
  sum = a + b;
  return sum >= a;  // no need to check b - either sum >= both or < both.
}

static inline bool SafeMul(size_t a, size_t b, size_t& product) {
  product = 0;
  if (a == 0 || b == 0) return true;
  if (b > (std::numeric_limits<size_t>::max() / a)) return false;
  product = a * b;
  return true;
}

static inline bool SubOverflow(const int32_t a, const int32_t b, int32_t& c) {
  // Clang 3.8+ / GCC 5.1+
#if JXL_COMPILER_GCC || JXL_COMPILER_CLANG
  return __builtin_sub_overflow(a, b, &c);
#elif JXL_COMPILER_MSVC >= 1937 && (defined(_M_AMD64) || defined(_M_IX86))
  return _sub_overflow_i32(/*carry*/ 0, a, b, &c);
#else
  uint32_t ua = static_cast<uint32_t>(a);
  uint32_t ub = static_cast<uint32_t>(b);
  uint32_t uc = ua - ub;
  c = static_cast<int32_t>(uc);
  return !!(((ua ^ ub) & (ua ^ uc)) >> 31);
#endif
}

template <typename T1, typename T2>
constexpr inline T1 DivCeil(T1 a, T2 b) {
  return (a + b - 1) / b;
}

// Works for any `align`; if a power of two, compiler emits ADD+AND.
constexpr inline size_t RoundUpTo(size_t what, size_t align) {
  return DivCeil(what, align) * align;
}

// `align <= 1` means no rounding.
static inline bool SafeRoundUpTo(size_t what, size_t align, size_t& result) {
  if (align < 2) {
    result = what;
    return true;
  }
  size_t reminder = what % align;
  if (reminder == 0) {
    result = what;
    return true;
  }
  return SafeAdd(what, align - reminder, result);
}

constexpr double kPi = 3.14159265358979323846264338327950288;

// Multiplier for conversion of log2(x) result to ln(x).
// print(1.0 / math.log2(math.e))
constexpr float kInvLog2e = 0.6931471805599453;

// Reasonable default for sRGB, matches common monitors. We map white to this
// many nits (cd/m^2) by default. Butteraugli was tuned for 250 nits, which is
// very close.
// NB: This constant is not very "base", but it is shared between modules.
static constexpr float kDefaultIntensityTarget = 255;

template <typename T>
constexpr T Pi(T multiplier) {
  return static_cast<T>(multiplier * kPi);
}

typedef std::array<float, 3> Color;

// Backported std::experimental::to_array

template <typename T>
using remove_cv_t = typename std::remove_cv<T>::type;

template <size_t... I>
struct index_sequence {};

template <size_t N, size_t... I>
struct make_index_sequence : make_index_sequence<N - 1, N - 1, I...> {};

template <size_t... I>
struct make_index_sequence<0, I...> : index_sequence<I...> {};

namespace detail {

template <typename T, size_t N, size_t... I>
constexpr auto to_array(T (&&arr)[N], index_sequence<I...> _)
    -> std::array<remove_cv_t<T>, N> {
  return {{std::move(arr[I])...}};
}

}  // namespace detail

template <typename T, size_t N>
constexpr auto to_array(T (&&arr)[N]) -> std::array<remove_cv_t<T>, N> {
  return detail::to_array(std::move(arr), make_index_sequence<N>());
}

template <typename T>
JXL_INLINE T Clamp1(T val, T low, T hi) {
  return val < low ? low : val > hi ? hi : val;
}

// conversion from integer to string.
template <typename T>
std::string ToString(T n) {
  char data[32] = {};
  if (std::is_floating_point<T>::value) {
    // float
    snprintf(data, sizeof(data), "%g", static_cast<double>(n));
  } else if (std::is_unsigned<T>::value) {
    // unsigned
    snprintf(data, sizeof(data), "%llu", static_cast<unsigned long long>(n));
  } else {
    // signed
    snprintf(data, sizeof(data), "%lld", static_cast<long long>(n));
  }
  return data;
}

#define JXL_JOIN(x, y) JXL_DO_JOIN(x, y)
#define JXL_DO_JOIN(x, y) x##y

}  // namespace ba



// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_STATUS_H_
#define LIB_JXL_BASE_STATUS_H_

// Error handling: Status return type + helper macros.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <utility>

#include <future>
#include <thread>
#include <vector>
#include <functional>

namespace ba {

// The Verbose level for the library
#ifndef JXL_DEBUG_V_LEVEL
#define JXL_DEBUG_V_LEVEL 0
#endif  // JXL_DEBUG_V_LEVEL

#ifdef USE_ANDROID_LOGGER
#include <android/log.h>
#define LIBJXL_ANDROID_LOG_TAG ("libjxl")
inline void android_vprintf(const char* format, va_list args) {
  char* message = nullptr;
  int res = vasprintf(&message, format, args);
  if (res != -1) {
    __android_log_write(ANDROID_LOG_DEBUG, LIBJXL_ANDROID_LOG_TAG, message);
    free(message);
  }
}
#endif

// Print a debug message on standard error or android logs. You should use the
// JXL_DEBUG macro instead of calling Debug directly. This function returns
// false, so it can be used as a return value in JXL_FAILURE.
JXL_FORMAT(1, 2)
inline JXL_NOINLINE bool Debug(const char* format, ...) {
  va_list args;
  va_start(args, format);
#ifdef USE_ANDROID_LOGGER
  android_vprintf(format, args);
#else
  vfprintf(stderr, format, args);
#endif
  va_end(args);
  return false;
}

// Print a debug message on standard error if "enabled" is true. "enabled" is
// normally a macro that evaluates to 0 or 1 at compile time, so the Debug
// function is never called and optimized out in release builds. Note that the
// arguments are compiled but not evaluated when enabled is false. The format
// string must be a explicit string in the call, for example:
//   JXL_DEBUG(JXL_DEBUG_MYMODULE, "my module message: %d", some_var);
// Add a header at the top of your module's .cc or .h file (depending on whether
// you have JXL_DEBUG calls from the .h as well) like this:
//   #ifndef JXL_DEBUG_MYMODULE
//   #define JXL_DEBUG_MYMODULE 0
//   #endif JXL_DEBUG_MYMODULE
#define JXL_DEBUG_TMP(format, ...) \
  ::ba::Debug(("%s:%d: " format "\n"), __FILE__, __LINE__, ##__VA_ARGS__)

#define JXL_DEBUG(enabled, format, ...)     \
  do {                                      \
    if (enabled) {                          \
      JXL_DEBUG_TMP(format, ##__VA_ARGS__); \
    }                                       \
  } while (0)

// JXL_DEBUG version that prints the debug message if the global verbose level
// defined at compile time by JXL_DEBUG_V_LEVEL is greater or equal than the
// passed level.
#if JXL_DEBUG_V_LEVEL > 0
#define JXL_DEBUG_V(level, format, ...) \
  JXL_DEBUG(level <= JXL_DEBUG_V_LEVEL, format, ##__VA_ARGS__)
#else
#define JXL_DEBUG_V(level, format, ...)
#endif

#define JXL_WARNING(format, ...) \
  JXL_DEBUG(JXL_IS_DEBUG_BUILD, format, ##__VA_ARGS__)

#if JXL_IS_DEBUG_BUILD
// Exits the program after printing a stack trace when possible.
JXL_NORETURN inline JXL_NOINLINE bool Abort() {
  JXL_PRINT_STACK_TRACE();
  JXL_CRASH();
}
#endif

#if JXL_IS_DEBUG_BUILD
#define JXL_DEBUG_ABORT(format, ...)                                   \
  do {                                                                 \
    if (JXL_DEBUG_ON_ABORT) {                                          \
      ::ba::Debug(("%s:%d: JXL_DEBUG_ABORT: " format "\n"), __FILE__, \
                   __LINE__, ##__VA_ARGS__);                           \
    }                                                                  \
    ::ba::Abort();                                                    \
  } while (0);
#else
#define JXL_DEBUG_ABORT(format, ...)
#endif

// Use this for code paths that are unreachable unless the code would change
// to make it reachable, in which case it will print a warning and abort in
// debug builds. In release builds no code is produced for this, so only use
// this if this path is really unreachable.
#if JXL_IS_DEBUG_BUILD
#define JXL_UNREACHABLE(format, ...)                                          \
  (::ba::Debug(("%s:%d: JXL_UNREACHABLE: " format "\n"), __FILE__, __LINE__, \
                ##__VA_ARGS__),                                               \
   ::ba::Abort(), JXL_FAILURE(format, ##__VA_ARGS__))
#else  // JXL_IS_DEBUG_BUILD
#define JXL_UNREACHABLE(format, ...) \
  JXL_FAILURE("internal: " format, ##__VA_ARGS__)
#endif

// Only runs in debug builds (builds where NDEBUG is not
// defined). This is useful for slower asserts that we want to run more rarely
// than usual. These will run on asan, msan and other debug builds, but not in
// opt or release.
#if JXL_IS_DEBUG_BUILD
#define JXL_DASSERT(condition)                                      \
  do {                                                              \
    if (!(condition)) {                                             \
      JXL_DEBUG(JXL_DEBUG_ON_ABORT, "JXL_DASSERT: %s", #condition); \
      ::ba::Abort();                                               \
    }                                                               \
  } while (0)
#else
#define JXL_DASSERT(condition)
#endif

// A ba::Status value from a StatusCode or Status which prints a debug message
// when enabled.
#define JXL_STATUS(status, format, ...)                                        \
  ::ba::StatusMessage(::ba::Status(status), "%s:%d: " format "\n", __FILE__, \
                       __LINE__, ##__VA_ARGS__)

// Notify of an error but discard the resulting Status value. This is only
// useful for debug builds or when building with JXL_CRASH_ON_ERROR.
#define JXL_NOTIFY_ERROR(format, ...)                                      \
  (void)JXL_STATUS(::ba::StatusCode::kGenericError, "JXL_ERROR: " format, \
                   ##__VA_ARGS__)

// An error Status with a message. The JXL_STATUS() macro will return a Status
// object with a kGenericError/kUnsupported/kNotEnoughBytes code, but the comma
// operator helps with clang-tidy inference and potentially with optimizations.
#define JXL_FAILURE(format, ...)                                              \
  ((void)JXL_STATUS(::ba::StatusCode::kGenericError, "JXL_FAILURE: " format, \
                    ##__VA_ARGS__),                                           \
   ::ba::Status(::ba::StatusCode::kGenericError))
#define JXL_UNSUPPORTED(format, ...)                            \
  ((void)JXL_STATUS(::ba::StatusCode::kUnsupported,            \
                    "JXL_UNSUPPORTED: " format, ##__VA_ARGS__), \
   ::ba::Status(::ba::StatusCode::kUnsupported))
#define JXL_NOT_ENOUGH_BYTES(format, ...)                            \
  ((void)JXL_STATUS(::ba::StatusCode::kNotEnoughBytes,              \
                    "JXL_NOT_ENOUGH_BYTES: " format, ##__VA_ARGS__), \
   ::ba::Status(::ba::StatusCode::kNotEnoughBytes))

// Always evaluates the status exactly once, so can be used for non-debug calls.
// Returns from the current context if the passed Status expression is an error
// (fatal or non-fatal). The return value is the passed Status.
#define JXL_RETURN_IF_ERROR(status)                                       \
  do {                                                                    \
    ::ba::Status jxl_return_if_error_status = (status);                  \
    if (!jxl_return_if_error_status) {                                    \
      (void)::ba::StatusMessage(                                         \
          jxl_return_if_error_status,                                     \
          "%s:%d: JXL_RETURN_IF_ERROR code=%d: %s\n", __FILE__, __LINE__, \
          static_cast<int>(jxl_return_if_error_status.code()), #status);  \
      return jxl_return_if_error_status;                                  \
    }                                                                     \
  } while (0)

// As above, but without calling StatusMessage. Intended for bundles (see
// fields.h), which have numerous call sites (-> relevant for code size) and do
// not want to generate excessive messages when decoding partial headers.
#define JXL_QUIET_RETURN_IF_ERROR(status)                \
  do {                                                   \
    ::ba::Status jxl_return_if_error_status = (status); \
    if (!jxl_return_if_error_status) {                   \
      return jxl_return_if_error_status;                 \
    }                                                    \
  } while (0)

#if JXL_IS_DEBUG_BUILD
// Debug: fatal check.
#define JXL_ENSURE(condition)                     \
  do {                                            \
    if (!(condition)) {                           \
      ::ba::Debug("JXL_ENSURE: %s", #condition); \
      ::ba::Abort();                             \
    }                                             \
  } while (0)
#else
// Release: non-fatal check of condition. If false, just return an error.
#define JXL_ENSURE(condition)                           \
  do {                                                  \
    if (!(condition)) {                                 \
      return JXL_FAILURE("JXL_ENSURE: %s", #condition); \
    }                                                   \
  } while (0)
#endif

enum class StatusCode : int32_t {
  // Non-fatal errors (negative values).
  kNotEnoughBytes = -1,

  // The only non-error status code.
  kOk = 0,

  // Fatal-errors (positive values)
  kGenericError = 1,
  kUnsupported = 2,
};

// Drop-in replacement for bool that raises compiler warnings if not used
// after being returned from a function. Example:
// Status LoadFile(...) { return true; } is more compact than
// bool JXL_MUST_USE_RESULT LoadFile(...) { return true; }
// In case of error, the status can carry an extra error code in its value which
// is split between fatal and non-fatal error codes.
class JXL_MUST_USE_RESULT Status {
 public:
  // We want implicit constructor from bool to allow returning "true" or "false"
  // on a function when using Status. "true" means kOk while "false" means a
  // generic fatal error.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Status(bool ok)
      : code_(ok ? StatusCode::kOk : StatusCode::kGenericError) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr Status(StatusCode code) : code_(code) {}

  // We also want implicit cast to bool to check for return values of functions.
  // NOLINTNEXTLINE(google-explicit-constructor)
  constexpr operator bool() const { return code_ == StatusCode::kOk; }

  constexpr StatusCode code() const { return code_; }

  // Returns whether the status code is a fatal error.
  constexpr bool IsFatalError() const {
    return static_cast<int32_t>(code_) > 0;
  }

 private:
  StatusCode code_;
};

static constexpr Status OkStatus() { return Status(StatusCode::kOk); }

// Helper function to create a Status and print the debug message or abort when
// needed.
inline JXL_FORMAT(2, 3) Status
    StatusMessage(const Status status, const char* format, ...) {
  // This block will be optimized out when JXL_IS_DEBUG_BUILD is disabled.
  if ((JXL_IS_DEBUG_BUILD && status.IsFatalError()) ||
      (JXL_DEBUG_ON_ALL_ERROR && !status)) {
    va_list args;
    va_start(args, format);
#ifdef USE_ANDROID_LOGGER
    android_vprintf(format, args);
#else
    vfprintf(stderr, format, args);
#endif
    va_end(args);
  }
#if JXL_CRASH_ON_ERROR
  // JXL_CRASH_ON_ERROR means to Abort() only on non-fatal errors.
  if (status.IsFatalError()) {
    ::ba::Abort();
  }
#endif  // JXL_CRASH_ON_ERROR
  return status;
}

template <typename T>
class JXL_MUST_USE_RESULT StatusOr {
  static_assert(!std::is_convertible<StatusCode, T>::value &&
                    !std::is_convertible<T, StatusCode>::value,
                "You cannot make a StatusOr with a type convertible from or to "
                "StatusCode");
  static_assert(std::is_move_constructible<T>::value &&
                    std::is_move_assignable<T>::value,
                "T must be move constructible and move assignable");

 public:
  // NOLINTNEXTLINE(google-explicit-constructor)
  StatusOr(StatusCode code) : code_(code) {
    JXL_DASSERT(code_ != StatusCode::kOk);
  }

  // NOLINTNEXTLINE(google-explicit-constructor)
  StatusOr(Status status) : StatusOr(status.code()) {}

  // NOLINTNEXTLINE(google-explicit-constructor)
  StatusOr(T&& value) : code_(StatusCode::kOk) {
    new (&storage_.data_) T(std::move(value));
  }

  StatusOr(StatusOr&& other) noexcept {
    if (other.ok()) {
      new (&storage_.data_) T(std::move(other.storage_.data_));
    }
    code_ = other.code_;
  }

  StatusOr& operator=(StatusOr&& other) noexcept {
    if (this == &other) return *this;
    if (ok() && other.ok()) {
      storage_.data_ = std::move(other.storage_.data_);
    } else if (other.ok()) {
      new (&storage_.data_) T(std::move(other.storage_.data_));
    } else if (ok()) {
      storage_.data_.~T();
    }
    code_ = other.code_;
    return *this;
  }

  StatusOr(const StatusOr&) = delete;
  StatusOr operator=(const StatusOr&) = delete;

  bool ok() const { return code_ == StatusCode::kOk; }
  Status status() const { return code_; }

  // Only call this if you are absolutely sure that `ok()` is true.
  // Never call this manually: rely on JXL_ASSIGN_OR.
  T value_() && {
    JXL_DASSERT(ok());
    return std::move(storage_.data_);
  }

  ~StatusOr() {
    if (code_ == StatusCode::kOk) {
      storage_.data_.~T();
    }
  }

 private:
  union Storage {
    char placeholder_;
    T data_;
    Storage() {}
    ~Storage() {}
  } storage_;

  StatusCode code_;
};

#define JXL_ASSIGN_OR_RETURN(lhs, statusor) \
  PRIVATE_JXL_ASSIGN_OR_RETURN_IMPL(        \
      JXL_JOIN(assign_or_return_temporary_variable, __LINE__), lhs, statusor)

// NOLINTBEGIN(bugprone-macro-parentheses)
#define PRIVATE_JXL_ASSIGN_OR_RETURN_IMPL(name, lhs, statusor) \
  auto name = statusor;                                        \
  JXL_RETURN_IF_ERROR(name.status());                          \
  lhs = std::move(name).value_();
// NOLINTEND(bugprone-macro-parentheses)

#define JXL_ASSIGN_OR_QUIT(lhs, statusor, message)                     \
  PRIVATE_JXL_ASSIGN_OR_QUIT_IMPL(                                     \
      JXL_JOIN(assign_or_temporary_variable, __LINE__), lhs, statusor, \
      message)

// NOLINTBEGIN(bugprone-macro-parentheses)
#define PRIVATE_JXL_ASSIGN_OR_QUIT_IMPL(name, lhs, statusor, message) \
  auto name = statusor;                                               \
  if (!name.ok()) {                                                   \
    QUIT(message);                                                    \
  }                                                                   \
  lhs = std::move(name).value_();
// NOLINTEND(bugprone-macro-parentheses)

}  // namespace ba


// --- 2FS folded data_parallel (RunOnPool) ---

#ifndef DATA_PARALLEL_H_
#define DATA_PARALLEL_H_

#include <algorithm>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>


namespace ba {

inline unsigned int ParallelWorkerCount() {
  unsigned int n = std::thread::hardware_concurrency();
  if (n == 0) n = 4;
  return std::min(n, 8u);
}

// Parallel row/task loop. Chunks [begin, end) across up to 8 workers
// (same ceiling used by the other 2FSIQA metrics). data_func(task, thread_id)
// must be safe to run concurrently for distinct task values.
template <class DataFunc>
Status RunOnPool(uint32_t begin, uint32_t end, const DataFunc& data_func,
                 const char* caller) {
  if (begin >= end) return true;

  const uint32_t count = end - begin;
  const unsigned int workers =
      std::min(ParallelWorkerCount(), std::max(1u, count));

  if (workers == 1) {
    for (uint32_t i = begin; i < end; ++i) {
      if (!data_func(i, /*thread=*/size_t{0})) {
        return JXL_FAILURE("[%s] failed", caller);
      }
    }
    return true;
  }

  std::vector<std::future<Status>> futures;
  futures.reserve(workers);

  for (unsigned int t = 0; t < workers; ++t) {
    const uint32_t start = begin + count * t / workers;
    const uint32_t stop = begin + count * (t + 1) / workers;
    if (start >= stop) continue;
    futures.push_back(std::async(std::launch::async, [&, start, stop, t]() {
      for (uint32_t i = start; i < stop; ++i) {
        if (!data_func(i, static_cast<size_t>(t))) {
          return Status(false);
        }
      }
      return Status(true);
    }));
  }

  for (auto& f : futures) {
    Status st = f.get();
    if (!st) {
      return JXL_FAILURE("[%s] failed", caller);
    }
  }
  return true;
}

}  // namespace ba

#endif  // DATA_PARALLEL_H_


#endif  // LIB_JXL_BASE_STATUS_H_

// Copyright (c) the JPEG XL Project Authors. All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#ifndef LIB_JXL_BASE_RECT_H_
#define LIB_JXL_BASE_RECT_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <string>
#include <utility>  // std::move


namespace ba {

// Rectangular region in image(s). Factoring this out of Image instead of
// shifting the pointer by x0/y0 allows this to apply to multiple images with
// different resolutions (e.g. color transform and quantization field).
// Can compare using SameSize(rect1, rect2).
template <typename T>
class RectT {
 public:
  // Most windows are xsize_max * ysize_max, except those on the borders where
  // begin + size_max > end.
  constexpr RectT(T xbegin, T ybegin, size_t xsize_max, size_t ysize_max,
                  T xend, T yend)
      : x0_(xbegin),
        y0_(ybegin),
        xsize_(ClampedSize(xbegin, xsize_max, xend)),
        ysize_(ClampedSize(ybegin, ysize_max, yend)) {}

  // Construct with origin and known size (typically from another Rect).
  constexpr RectT(T xbegin, T ybegin, size_t xsize, size_t ysize)
      : x0_(xbegin), y0_(ybegin), xsize_(xsize), ysize_(ysize) {}

  // Construct a rect that covers a whole image/plane/ImageBundle etc.
  template <typename ImageT>
  explicit RectT(const ImageT& image)
      : RectT(0, 0, image.xsize(), image.ysize()) {}

  RectT() : RectT(0, 0, 0, 0) {}

  RectT(const RectT&) = default;
  RectT& operator=(const RectT&) = default;

  // Construct a subrect that resides in an image/plane/ImageBundle etc.
  template <typename ImageT>
  RectT Crop(const ImageT& image) const {
    return Intersection(RectT(image));
  }

  // Construct a subrect that resides in the [0, ysize) x [0, xsize) region of
  // the current rect.
  RectT Crop(size_t area_xsize, size_t area_ysize) const {
    return Intersection(RectT(0, 0, area_xsize, area_ysize));
  }

  JXL_MUST_USE_RESULT RectT Intersection(const RectT& other) const {
    return RectT(std::max(x0_, other.x0_), std::max(y0_, other.y0_), xsize_,
                 ysize_, std::min(x1(), other.x1()),
                 std::min(y1(), other.y1()));
  }

  JXL_MUST_USE_RESULT RectT Translate(int64_t x_offset,
                                      int64_t y_offset) const {
    return RectT(x0_ + x_offset, y0_ + y_offset, xsize_, ysize_);
  }

  template <template <class> class P, typename V>
  V* Row(P<V>* image, size_t y) const {
    JXL_DASSERT(y + y0_ >= 0);
    return image->Row(y + y0_) + x0_;
  }

  template <template <class> class P, typename V>
  const V* Row(const P<V>* image, size_t y) const {
    JXL_DASSERT(y + y0_ >= 0);
    return image->Row(y + y0_) + x0_;
  }

  template <template <class> class MP, typename V>
  V* PlaneRow(MP<V>* image, const size_t c, size_t y) const {
    JXL_DASSERT(y + y0_ >= 0);
    return image->PlaneRow(c, y + y0_) + x0_;
  }

  template <template <class> class P, typename V>
  const V* ConstRow(const P<V>& image, size_t y) const {
    JXL_DASSERT(y + y0_ >= 0);
    return image.ConstRow(y + y0_) + x0_;
  }

  template <template <class> class MP, typename V>
  const V* ConstPlaneRow(const MP<V>& image, size_t c, size_t y) const {
    JXL_DASSERT(y + y0_ >= 0);
    return image.ConstPlaneRow(c, y + y0_) + x0_;
  }

  bool IsInside(const RectT& other) const {
    return x0_ >= other.x0() && x1() <= other.x1() && y0_ >= other.y0() &&
           y1() <= other.y1();
  }

  bool IsSame(const RectT& other) const {
    return x0_ == other.x0_ && xsize_ == other.xsize_ && y0_ == other.y0_ &&
           ysize_ == other.ysize_;
  }

  // Returns true if this Rect fully resides in the given image. ImageT could be
  // Plane<T> or Image3<T>; however if ImageT is Rect, results are nonsensical.
  template <class ImageT>
  bool IsInside(const ImageT& image) const {
    return IsInside(RectT(image));
  }

  T x0() const { return x0_; }
  T y0() const { return y0_; }
  size_t xsize() const { return xsize_; }
  size_t ysize() const { return ysize_; }
  T x1() const { return x0_ + xsize_; }
  T y1() const { return y0_ + ysize_; }

  RectT<T> ShiftLeft(size_t shiftx, size_t shifty) const {
    return RectT<T>(x0_ * (1 << shiftx), y0_ * (1 << shifty), xsize_ << shiftx,
                    ysize_ << shifty);
  }
  RectT<T> ShiftLeft(size_t shift) const { return ShiftLeft(shift, shift); }

  // Requires x0(), y0() to be multiples of 1<<shiftx, 1<<shifty.
  StatusOr<RectT<T>> CeilShiftRight(std::pair<size_t, size_t> shift) const {
    size_t shiftx = shift.first;
    size_t shifty = shift.second;
    JXL_ENSURE((x0_ % (1 << shiftx) == 0) && (y0_ % (1 << shifty) == 0));
    return RectT<T>(x0_ / (1 << shiftx), y0_ / (1 << shifty),
                    DivCeil(xsize_, T{1} << shiftx),
                    DivCeil(ysize_, T{1} << shifty));
  }

  RectT<T> Extend(T border, RectT<T> parent) const {
    T new_x0 = x0() > parent.x0() + border ? x0() - border : parent.x0();
    T new_y0 = y0() > parent.y0() + border ? y0() - border : parent.y0();
    T new_x1 = x1() + border > parent.x1() ? parent.x1() : x1() + border;
    T new_y1 = y1() + border > parent.y1() ? parent.y1() : y1() + border;
    return RectT<T>(new_x0, new_y0, new_x1 - new_x0, new_y1 - new_y0);
  }

  template <typename U>
  RectT<U> As() const {
    return RectT<U>(static_cast<U>(x0_), static_cast<U>(y0_),
                    static_cast<U>(xsize_), static_cast<U>(ysize_));
  }

 private:
  // Returns size_max, or whatever is left in [begin, end).
  static constexpr size_t ClampedSize(T begin, size_t size_max, T end) {
    return (static_cast<T>(begin + size_max) <= end)
               ? size_max
               : (end > begin ? end - begin : 0);
  }

  T x0_;
  T y0_;

  size_t xsize_;
  size_t ysize_;
};

template <typename T>
std::string Description(RectT<T> r) {
  std::ostringstream os;
  os << "[" << r.x0() << ".." << r.x1() << ")x"
     << "[" << r.y0() << ".." << r.y1() << ")";
  return os.str();
}

using Rect = RectT<size_t>;

}  // namespace ba

#endif  // LIB_JXL_BASE_RECT_H_
