/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * Force-included (-include) in every mbelib-neo source file built for an
 * embedded target, to turn its thread-local variables into plain static ones:
 * thread-local storage may not be available there, and OpenRTX uses the
 * vocoder from a single thread.
 *
 * mbe_compiler.h defines MBE_THREAD_LOCAL unconditionally: include it first,
 * so that its include guard keeps the redefinition below in place.
 */

#include "mbe_compiler.h"

#undef MBE_THREAD_LOCAL
#define MBE_THREAD_LOCAL
