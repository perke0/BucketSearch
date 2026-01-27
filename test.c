// bench_search.c
// Benchmark: linear scan vs binary search vs libc bsearch vs interpolation search vs BucketSearch
// Scenario: sorted uint64_t array, n=5,000,000, values in [1 .. 10 trillion] with skips.
// Queries: mix of hits/misses (configurable).
//
// Build (Linux/glibc):
//   gcc -O3 -march=native -DNDEBUG bench_search.c -o bench_search
// Run:
//   ./bench_search 5000000 2000000 16 50 123
//     n=5M, q=2M, K=16, hit%=50, seed=123
//
// Notes:
// - Linear scan is included as a baseline (will be extremely slow at 5M).
// - libc bsearch uses a comparator (function call overhead), often slower than inlined binary.
// - Interpolation search can be great when data ~uniform; can be bad when skewed (but safe here).
// - BucketSearch uses top-K bits of the *meaningful* width W (based on max value), then lower_bound in bucket.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
  #define LIKELY(x)   (__builtin_expect(!!(x), 1))
  #define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
  #define LIKELY(x) (x)
  #define UNLIKELY(x) (x)
#endif

// ---------------- timing ----------------

static inline uint64_t ns_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

// ---------------- rng ----------------

typedef struct {
  uint64_t s;
} rng64_t;

static inline uint64_t splitmix64(rng64_t *r) {
  uint64_t x = (r->s += 0x9E3779B97F4A7C15ull);
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}

// ---------------- data gen ----------------

// Generate sorted, strictly increasing values with random gaps within [1..maxV].
// Simple model: v += 1 + (rand % avg_gap*2).
static void gen_sorted_sparse_u64(uint64_t *a, size_t n, uint64_t maxV, uint64_t avg_gap, uint64_t seed) {
  rng64_t r = { seed ? seed : 1ull };
  uint64_t v = 1;
  for (size_t i = 0; i < n; i++) {
    uint64_t gap = 1 + (splitmix64(&r) % (avg_gap * 2 + 1));
    if (i == 0) v = 1;
    else v += gap;

    // Keep within maxV; if we exceed, wrap gently by compressing tail.
    if (v > maxV - (uint64_t)(n - i)) v = maxV - (uint64_t)(n - i);

    a[i] = v;
  }

  // Ensure strictly increasing (in case of tail compression collisions)
  for (size_t i = 1; i < n; i++) {
    if (a[i] <= a[i - 1]) a[i] = a[i - 1] + 1;
  }
}

// Create queries with a hit-rate:
// - hit: pick an existing element from array
// - miss: pick a random value in [1..maxV] and "nudge" it away from existing by adding 1 (likely miss)
static void gen_queries_u64(uint64_t *q, size_t qn,
                            const uint64_t *a, size_t n,
                            uint64_t maxV, int hit_percent, uint64_t seed) {
  rng64_t r = { seed ? seed : 2ull };
  for (size_t i = 0; i < qn; i++) {
    uint64_t x = splitmix64(&r);
    int hit = (int)(x % 100) < hit_percent;
    if (hit) {
      size_t idx = (size_t)(splitmix64(&r) % n);
      q[i] = a[idx];
    } else {
      uint64_t v = 1 + (splitmix64(&r) % maxV);
      q[i] = v | 1ull; // small perturbation; still may hit by chance but rare
    }
  }
}

// ---------------- searches ----------------


// Inlined binary search exact match. Returns index or -1.
static inline ptrdiff_t binary_find_u64(const uint64_t *a, size_t n, uint64_t x) {
  size_t lo = 0, hi = n;
  while (lo < hi) {
    size_t mid = lo + ((hi - lo) >> 1);
    uint64_t v = a[mid];
    if (v < x) lo = mid + 1;
    else hi = mid;
  }
  if (lo < n && a[lo] == x) return (ptrdiff_t)lo;
  return -1;
}

// libc bsearch comparator
static int cmp_u64(const void *key, const void *elem) {
  uint64_t k = *(const uint64_t*)key;
  uint64_t e = *(const uint64_t*)elem;
  return (k < e) ? -1 : (k > e) ? 1 : 0;
}

static inline ptrdiff_t libc_bsearch_find_u64(const uint64_t *a, size_t n, uint64_t x) {
  const uint64_t *p = (const uint64_t*)bsearch(&x, a, n, sizeof(uint64_t), cmp_u64);
  if (!p) return -1;
  return (ptrdiff_t)(p - a);
}

// Interpolation search exact match (safe-ish for monotone, may degrade on skew).
static inline ptrdiff_t interpolation_find_u64(const uint64_t *a, size_t n, uint64_t x) {
  if (n == 0) return -1;
  size_t lo = 0, hi = n - 1;
  uint64_t alo = a[lo], ahi = a[hi];

  while (lo <= hi && x >= alo && x <= ahi) {
    if (ahi == alo) break;

    // pos = lo + (x-alo) * (hi-lo) / (ahi-alo)
    __uint128_t num = (__uint128_t)(x - alo) * (hi - lo);
    size_t pos = lo + (size_t)(num / (ahi - alo));
    uint64_t v = a[pos];

    if (v == x) return (ptrdiff_t)pos;
    if (v < x) {
      lo = pos + 1;
      if (lo >= n) break;
      alo = a[lo];
    } else {
      if (pos == 0) break;
      hi = pos - 1;
      ahi = a[hi];
    }
  }

  // fallback small binary in remaining window (keeps correctness)
  size_t l = lo, r = (hi + 1 <= n) ? (hi + 1) : n;
  while (l < r) {
    size_t mid = l + ((r - l) >> 1);
    if (a[mid] < x) l = mid + 1;
    else r = mid;
  }
  if (l < n && a[l] == x) return (ptrdiff_t)l;
  return -1;
}

// ---------------- BucketSearch (sorted array) ----------------

static inline uint32_t bit_width_u64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
  return x ? (64u - (uint32_t)__builtin_clzll(x)) : 1u;
#else
  uint32_t w = 0; do { w++; x >>= 1; } while (x); return w;
#endif
}

static inline uint32_t prefix_u64(uint64_t x, uint32_t W, uint32_t K) {
  if (LIKELY(W >= K)) return (uint32_t)(x >> (W - K));
  return (uint32_t)(x << (K - W));
}

static inline size_t lower_bound_u64_range(const uint64_t *a, size_t lo, size_t hi, uint64_t x) {
  while (lo < hi) {
    size_t mid = lo + ((hi - lo) >> 1);
    if (a[mid] < x) lo = mid + 1;
    else hi = mid;
  }
  return lo;
}

// Build start[0..B] table. start size must be (1<<K)+1.
static int bucketsearch_build_u64(const uint64_t *a, size_t n, uint32_t K, size_t *start) {
  if (!start) return -1;
  if (K == 0 || K > 24) return -2;
  const uint32_t B = 1u << K;

  uint32_t W = bit_width_u64(n ? a[n - 1] : 0);

  for (uint32_t p = 0; p <= B; p++) start[p] = n;

  for (size_t i = 0; i < n; i++) {
    uint32_t p = prefix_u64(a[i], W, K);
    if (start[p] == n) start[p] = i;
  }
  start[B] = n;

  size_t last = n;
  for (int32_t p = (int32_t)B - 1; p >= 0; p--) {
    if (start[p] == n) start[p] = last;
    else last = start[p];
  }
  return 0;
}

// Exact match using bucket table. Returns index or -1.
static inline ptrdiff_t bucketsearch_find_u64(const uint64_t *a, size_t n, uint32_t K, const size_t *start, uint64_t x) {
  if (UNLIKELY(n == 0)) return -1;
  const uint32_t B = 1u << K;
  uint32_t W = bit_width_u64(a[n - 1]);

  uint32_t p = prefix_u64(x, W, K);
  if (UNLIKELY(p >= B)) return -1;

  size_t lo = start[p];
  size_t hi = start[p + 1];
  if (lo == hi) return -1;

  // fast reject
  if (x < a[lo] || x > a[hi - 1]) return -1;

  size_t i = lower_bound_u64_range(a, lo, hi, x);
  if (i != hi && a[i] == x) return (ptrdiff_t)i;
  return -1;
}

// ---------------- benchmark harness ----------------

typedef ptrdiff_t (*find_fn)(const uint64_t*, size_t, uint64_t);

static uint64_t bench_find(const char *name, find_fn fn, const uint64_t *a, size_t n, const uint64_t *q, size_t qn) {
  // Prevent optimizing away by accumulating results.
  volatile uint64_t sink = 0;

  uint64_t t0 = ns_now();
  for (size_t i = 0; i < qn; i++) {
    ptrdiff_t idx = fn(a, n, q[i]);
    sink += (uint64_t)(idx + 1); // -1 becomes 0
  }
  uint64_t t1 = ns_now();

  uint64_t dt = t1 - t0;
  double ns_per = (double)dt / (double)qn;
  printf("%-24s  %9.3f ns/query   (sink=%llu)\n", name, ns_per, (unsigned long long)sink);
  return dt;
}

// Wrappers to match find_fn signature
static ptrdiff_t w_binary(const uint64_t *a, size_t n, uint64_t x) { return binary_find_u64(a, n, x); }
static ptrdiff_t w_libc_bsearch(const uint64_t *a, size_t n, uint64_t x) { return libc_bsearch_find_u64(a, n, x); }
static ptrdiff_t w_interp(const uint64_t *a, size_t n, uint64_t x) { return interpolation_find_u64(a, n, x); }

// BucketSearch wrapper uses globals for start/K (keeps call signature like bsearch(array,target))
static const size_t *g_start = NULL;
static uint32_t g_K = 16;
static ptrdiff_t w_bucket(const uint64_t *a, size_t n, uint64_t x) {
  return bucketsearch_find_u64(a, n, g_K, g_start, x);
}

int main(int argc, char **argv) {
  size_t   n = (argc > 1) ? (size_t)strtoull(argv[1], NULL, 10) : 5000000ull;
  size_t   qn = (argc > 2) ? (size_t)strtoull(argv[2], NULL, 10) : 2000000ull;
  uint32_t K = (argc > 3) ? (uint32_t)strtoul(argv[3], NULL, 10) : 16u;
  int hit_percent = (argc > 4) ? atoi(argv[4]) : 50;
  uint64_t seed = (argc > 5) ? (uint64_t)strtoull(argv[5], NULL, 10) : 123ull;

  const uint64_t maxV = 10ull * 1000ull * 1000ull * 1000ull * 1000ull; // 10 trillion
  const uint64_t avg_gap = 1000; // controls sparsity (increase for more gaps)

  printf("n=%zu  queries=%zu  K=%u  hit%%=%d  seed=%llu\n",
         n, qn, K, hit_percent, (unsigned long long)seed);

  uint64_t *a = (uint64_t*)malloc(n * sizeof(uint64_t));
  uint64_t *q = (uint64_t*)malloc(qn * sizeof(uint64_t));
  if (!a || !q) {
    fprintf(stderr, "alloc failed\n");
    return 1;
  }

  gen_sorted_sparse_u64(a, n, maxV, avg_gap, seed);
  gen_queries_u64(q, qn, a, n, maxV, hit_percent, seed ^ 0xDEADBEEFCAFEBABEull);

  // Build BucketSearch table
  if (K == 0 || K > 24) {
    fprintf(stderr, "Choose K in [1..24]\n");
    return 1;
  }
  size_t B = (size_t)1u << K;
  size_t *start = (size_t*)malloc((B + 1) * sizeof(size_t));
  if (!start) {
    fprintf(stderr, "start alloc failed\n");
    return 1;
  }
  if (bucketsearch_build_u64(a, n, K, start) != 0) {
    fprintf(stderr, "bucketsearch_build failed\n");
    return 1;
  }
  g_start = start;
  g_K = K;

  // Warm-up (touch memory)
  volatile uint64_t warm = 0;
  for (size_t i = 0; i < n; i += (n / 1024 + 1)) warm ^= a[i];
  for (size_t i = 0; i < qn; i += (qn / 1024 + 1)) warm ^= q[i];
  printf("(warm=%llu)\n\n", (unsigned long long)warm);

  // Benchmarks
  // NOTE: linear scan is extremely slow for 5M; you can comment it out if needed.
  bench_find("Binary search",      w_binary,       a, n, q, qn);
  bench_find("libc bsearch",       w_libc_bsearch, a, n, q, qn);
  bench_find("Interpolation",      w_interp,       a, n, q, qn);
  bench_find("BucketSearch",       w_bucket,       a, n, q, qn);

  free(start);
  free(q);
  free(a);
  return 0;
}

