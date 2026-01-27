#pragma once
#include <stdint.h>
#include <stddef.h>

// Build prefix-bucket start table for sorted array a[0..n).
// Returns 0 on success, nonzero on error.
int bucketsearch_u64_build(const uint64_t *a, size_t n, uint32_t K, size_t *start);

// Returns index i if found, or -1 if not found.
ptrdiff_t bucketsearch_u64_find(const uint64_t *a, size_t n,
                               uint32_t K, const size_t *start,
                               uint64_t x);

