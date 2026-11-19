/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <string.h>
#include "mbe_heap.h"

/*
 * Heap of mbelib-neo and PFFFT on the C62, in PSRAM (see mbe_psram_heap.h),
 * also holding the 48 KB codec thread stack of the AMBE codecs. PSRAM is not
 * cleared at boot and the heap is set up on first use: nothing of it runs at
 * boot.
 *
 * One D-STAR or DMR encoder takes about 42 KB, and only one codec runs at a
 * time.
 */
#define MBE_HEAP_SIZE (128 * 1024)

static uint8_t heapMem[MBE_HEAP_SIZE] __aligned(8)
    __attribute__((section(".psram_section")));
static struct sys_heap heap;
static bool heapReady = false;
static K_MUTEX_DEFINE(heapMutex);

void *mbe_heap_aligned_alloc(size_t align, size_t size)
{
    k_mutex_lock(&heapMutex, K_FOREVER);

    if (heapReady == false) {
        sys_heap_init(&heap, heapMem, sizeof(heapMem));
        heapReady = true;
    }

    void *ptr = sys_heap_aligned_alloc(&heap, align, size);
    k_mutex_unlock(&heapMutex);

    return ptr;
}

void *mbe_heap_malloc(size_t size)
{
    return mbe_heap_aligned_alloc(sizeof(void *), size);
}

void *mbe_heap_calloc(size_t nmemb, size_t size)
{
    size_t bytes;
    if (__builtin_mul_overflow(nmemb, size, &bytes))
        return NULL;

    void *ptr = mbe_heap_malloc(bytes);
    if (ptr != NULL)
        memset(ptr, 0, bytes);

    return ptr;
}

void mbe_heap_free(void *ptr)
{
    if (ptr == NULL)
        return;

    k_mutex_lock(&heapMutex, K_FOREVER);
    sys_heap_free(&heap, ptr);
    k_mutex_unlock(&heapMutex);
}
