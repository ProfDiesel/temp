#pragma once

#if !defined(CRIPPLED)
#  define LIKELY(x)   __builtin_expect((x), true)
#  define UNLIKELY(x) __builtin_expect((x), false)
#else // !defined(CRIPPLED)
#  define LIKELY(x)   (x)
#  define UNLIKELY(x) (x)
#endif // !defined(CRIPPLED)

