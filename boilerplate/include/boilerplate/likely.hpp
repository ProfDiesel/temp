#pragma once

#define LIKELY(x) __builtin_expect(bool(x), true)
#define UNLIKELY(x) __builtin_expect(bool(x), false)

