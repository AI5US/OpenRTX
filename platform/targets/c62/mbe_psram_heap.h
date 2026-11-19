/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Force-included (-include) in every mbelib-neo and PFFFT source file built
 * for the C62, after mbe_single_thread.h: their allocations (about 42 KB for
 * a D-STAR or DMR encoder, FFT plans included) go to a heap in PSRAM (see
 * mbe_heap.c) instead of Newlib's malloc arena, the SRAM left after static
 * data, too small for them next to the codec thread stack and the modem
 * buffers.
 *
 * stdlib.h is included first, so that its declarations are not renamed.
 */

#include <stdlib.h>
#include "mbe_heap.h"

#define malloc(size)        mbe_heap_malloc(size)
#define calloc(nmemb, size) mbe_heap_calloc(nmemb, size)
#define free(ptr)           mbe_heap_free(ptr)
