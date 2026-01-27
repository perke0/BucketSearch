// bucket_search_u64.c
#include "bucket_search_u64.h"

#if defined(__GNUC__) || defined(__clang__)
  #define BS_CLZ64(x) __builtin_clzll(x)
#else
  static uint32_t BS_CLZ64_fallback(uint64_t x){
    uint32_t n = 0;
    while ((x & (1ull<<63)) == 0 && x) { x <<= 1; n++; }
    return n;
  }
  #define BS_CLZ64(x) BS_CLZ64_fallback(x)
#endif

static inline uint32_t bit_width_u64(uint64_t x) {
  if (x == 0) return 1;
  return 64u - (uint32_t)BS_CLZ64(x);
}

static inline uint32_t prefix_u64(uint64_t x, uint32_t W, uint32_t K) {
  if (W >= K) return (uint32_t)(x >> (W - K));
  return (uint32_t)(x << (K - W));
}

static inline size_t lower_bound_u64(const uint64_t *a, size_t lo, size_t hi, uint64_t x) {
  while (lo < hi) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (a[mid] < x) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

int bucketsearch_u64_build(const uint64_t *a, size_t n, uint32_t K, size_t *start) {
  if (!start) return -1;
  if (K == 0 || K > 24) return -2;          // keep table reasonable (you can raise)
  const uint32_t B = 1u << K;

  // Determine meaningful width W from max element (array sorted)
  uint64_t maxv = (n ? a[n - 1] : 0);
  uint32_t W = bit_width_u64(maxv);

  // init start[] to n (unset)
  for (uint32_t p = 0; p <= B; p++) start[p] = n;

  // first occurrence per prefix
  for (size_t i = 0; i < n; i++) {
    uint32_t p = prefix_u64(a[i], W, K);
    if (start[p] == n) start[p] = i;
  }
  start[B] = n;

  // fill holes backwards
  size_t last = n;
  for (int32_t p = (int32_t)B - 1; p >= 0; p--) {
    if (start[p] == n) start[p] = last;
    else last = start[p];
  }
  return 0;
}

ptrdiff_t bucketsearch_u64_find(const uint64_t *a, size_t n,
                               uint32_t K, const size_t *start,
                               uint64_t x) {
  if (!a || !start || n == 0) return -1;
  if (K == 0 || K > 24) return -1;
  const uint32_t B = 1u << K;

  // Same W rule as build: depends on max element (a[n-1])
  uint32_t W = bit_width_u64(a[n - 1]);

  uint32_t p = prefix_u64(x, W, K);
  if (p >= B) return -1;

  size_t lo = start[p];
  size_t hi = start[p + 1];
  if (lo == hi) return -1;

  // quick reject
  if (x < a[lo] || x > a[hi - 1]) return -1;

  size_t i = lower_bound_u64(a, lo, hi, x);
  if (i != hi && a[i] == x) return (ptrdiff_t)i;
  return -1;
}

