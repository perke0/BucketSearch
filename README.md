# BucketSearch

BucketSearch is a high performance search algorithm for **sorted arrays**, designed to outperform classic binary search by reducing branches and comparisons using fixed size buckets.

It is intended for performance critical code paths where millions of lookups are performed on large, mostly static datasets.

---

## Features

* Faster than binary search and `libc bsearch()` in practice
* Predictable performance
* Configurable bucket count (`K`)
* Small and bounded extra memory usage
* Simple C API
* Zero external dependencies

---

## How It Works

The array is divided into `K` buckets. Arithmetic is used to jump directly to the most likely bucket for a given key, followed by a small local search inside that bucket.

This avoids repeated branching and makes better use of modern CPU pipelines and caches.

---

## Complexity

Let:

* `n` = number of elements
* `K` = number of buckets

| Case    | Complexity                      |
| ------- | ------------------------------- |
| Best    | O(1)                            |
| Average | O(1) (reasonable distributions) |
| Worst   | O(n / K)                        |

In real workloads with tuned `K`, worst‑case buckets remain small.

---

## Memory Usage

Extra memory:

```
K * sizeof(size_t)
```

Examples:

* `K = 32`  → 256 bytes
* `K = 1024` → 8 KB

Negligible compared to typical datasets.


Returns index of `key` if found, otherwise `-1`.

The input array **must be sorted**.


## Benchmarks

Command:

```
./bucket_search 5000000 25000000 24 10 123
```

Output:

```
n=5000000  queries=25000000  K=24  hit%=10  seed=123
(warm=11556699546894)

Binary search                71.678 ns/query   (sink=6243204293233)
libc bsearch                 72.688 ns/query   (sink=6243204293233)
Interpolation                74.428 ns/query   (sink=6243204293233)
BucketSearch                 15.906 ns/query   (sink=6243204293233)
```

BucketSearch shows ~4.5× speedup over binary search in this configuration.

---

## Choosing K

Typical values:

* `n ≈ 1e6`  → `K = 16–64`
* `n ≈ 1e7`  → `K = 32–128`

Optimal `K` depends on cache size, key distribution, and workload.

---

## Requirements

* C99 or newer
* Sorted input data

---

## License

Apache 2.0 License.

