# Memory Allocator

A minimal C library for manual virtual memory management, providing compact implementations of:
- `os_malloc(size_t size)`
- `os_calloc(size_t nmemb, size_t size)`
- `os_realloc(void *ptr, size_t size)`
- `os_free(void *ptr)`

The allocator relies on Linux memory-management syscalls (`brk`/`sbrk`, `mmap`, `munmap`) and maintains a block list to reuse freed memory, reduce fragmentation, and minimize the number of syscalls.

---

## Features

- **Heap allocation** via `brk()` / `sbrk()` for small chunks (below `MMAP_THRESHOLD`).
- **Mapped allocation** via `mmap()` for large chunks (above `MMAP_THRESHOLD`).
- **8-byte alignment** for both metadata and payload.
- **Block reuse** by marking heap blocks as free (without returning heap memory to the OS).
- **Block splitting** to reduce internal fragmentation.
- **Block coalescing** (merging adjacent free blocks) to reduce external fragmentation.
- **Best-fit** strategy: selects the closest-sized free block for each request.
- **Heap preallocation**: on first heap use, a larger chunk (e.g., 128 KiB) may be reserved to reduce future `brk/sbrk` calls.
- `os_realloc()` attempts **in-place expansion** on the heap (with incremental coalescing) before moving the allocation.

---

# Project Structure

```text
.
├── Makefile
├── osmem.c
├── README.md
└── utils/
    ├── osmem.h
    ├── block_meta.h
    ├── printf.c (heap-free printf)
    └── ... (helpers / macros)

```

* `osmem.c` contains the allocator implementation.
* `utils/osmem.h` defines the public API.
* `utils/block_meta.h` defines the block metadata structure and related constants.
* `utils/printf.c` provides a `printf()` implementation that does **not** use the heap (useful for debugging without allocator recursion).

---

## Requirements

* Linux (x86_64 recommended)
* GCC / Clang
* `make`
* Kernel support for `mmap/munmap` and `brk/sbrk`

---

## Build

From the project root directory:

```bash
make

```

To clean:

```bash
make clean

```
```

