/*
 * Shared CBMC harness helpers.
 *
 * PG_REACH marks an outcome the proof must be able to reach. A proof whose
 * assumptions rule out every interesting input passes vacuously, so check.sh
 * also runs each harness with -DPG_VACUITY, where every PG_REACH becomes an
 * assertion that the outcome never happens. CBMC must refute all of them.
 */
#ifndef PROOFGATE_HARNESS_H
#define PROOFGATE_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

uint8_t nondet_u8(void);
uint16_t nondet_u16(void);
size_t nondet_size(void);

#ifdef PG_VACUITY
#define PG_REACH(cond, name) __CPROVER_assert(!(cond), "REACH: " name)
#else
#define PG_REACH(cond, name) ((void)(cond))
#endif

#define PG_PROVE(cond, name) __CPROVER_assert((cond), "PROVE: " name)

#define PG_SLICE_INSIDE(s, base, size)                                                \
    ((s).ptr >= (base) && (size_t)((s).ptr - (base)) <= (size) &&                     \
     (s).len <= (size) - (size_t)((s).ptr - (base)))

#endif
