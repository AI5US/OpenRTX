/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef MBE_HEAP_H
#define MBE_HEAP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Allocate memory from the mbelib-neo heap in PSRAM, set up on first
 * use. Same semantics as malloc().
 *
 * @param size: number of bytes.
 * @return the allocated memory, NULL if the heap is full.
 */
void *mbe_heap_malloc(size_t size);

/**
 * \brief Allocate aligned memory from the mbelib-neo heap, e.g. a thread
 * stack.
 *
 * @param align: alignment, a power of two.
 * @param size: number of bytes.
 * @return the allocated memory, NULL if the heap is full.
 */
void *mbe_heap_aligned_alloc(size_t align, size_t size);

/**
 * \brief Allocate zeroed memory from the mbelib-neo heap. Same semantics as
 * calloc().
 *
 * @param nmemb: number of elements.
 * @param size: size of each element, in bytes.
 * @return the allocated memory, NULL if the heap is full.
 */
void *mbe_heap_calloc(size_t nmemb, size_t size);

/**
 * \brief Free memory allocated from the mbelib-neo heap.
 *
 * @param ptr: memory to free, NULL is accepted.
 */
void mbe_heap_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* MBE_HEAP_H */
