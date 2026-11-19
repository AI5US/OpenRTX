/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Included by mbe_single_thread.h in the mbelib-neo source files built with
 * MBE_FLOAT_MATH defined, together with -fsingle-precision-constant, to
 * compute in single precision what mbelib-neo computes in double precision,
 * among others its speech analysis and tone detection, run for each encoded
 * frame. Cortex-M FPUs are single precision only, so double precision
 * arithmetic and maths functions are emulated in software and made the
 * encoders too slow for real time. On test speech, the encoded frames are
 * the same as in double precision.
 *
 * The system headers are included first, so that their declarations are
 * not changed.
 */

#include <assert.h>
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define double float

#define acos(x)     acosf(x)
#define asin(x)     asinf(x)
#define atan(x)     atanf(x)
#define atan2(y, x) atan2f(y, x)
#define ceil(x)     ceilf(x)
#define cos(x)      cosf(x)
#define exp(x)      expf(x)
#define exp2(x)     exp2f(x)
#define fabs(x)     fabsf(x)
#define floor(x)    floorf(x)
#define fmax(x, y)  fmaxf(x, y)
#define fmin(x, y)  fminf(x, y)
#define fmod(x, y)  fmodf(x, y)
#define hypot(x, y) hypotf(x, y)
#define log(x)      logf(x)
#define log10(x)    log10f(x)
#define log2(x)     log2f(x)
#define lround(x)   lroundf(x)
#define pow(x, y)   powf(x, y)
#define round(x)    roundf(x)
#define sin(x)      sinf(x)
#define sqrt(x)     sqrtf(x)
#define tan(x)      tanf(x)
#define tanh(x)     tanhf(x)
