/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/clock.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

BUILD_ASSERT(k_ms_to_ticks_ceil32(1) == DIV_ROUND_UP(CONFIG_SYS_CLOCK_TICKS_PER_SEC, 1000));
BUILD_ASSERT(k_ticks_to_ns_floor64(CONFIG_SYS_CLOCK_TICKS_PER_SEC) == NSEC_PER_SEC);

static const k_timeout_t static_timeout = Z_TIMEOUT_MS(100);
static const k_timeout_t static_forever = SYS_TIMEOUT_MS(SYS_FOREVER_MS);
static const uint32_t static_ticks = z_tmcvt_32(32768, 32768, 1000, true, true, false);
static uint8_t static_array[k_ms_to_ticks_ceil32(1)];

static int evaluations;

static uint64_t counted(uint64_t value)
{
	evaluations++;
	return value;
}

static int64_t counted_signed(int64_t value)
{
	evaluations++;
	return value;
}

static uint32_t counted_hz(uint32_t hz)
{
	evaluations++;
	return hz;
}

#define ASSERT_ONCE(convert, value)                                                                \
	do {                                                                                       \
		evaluations = 0;                                                                   \
		const uint64_t runtime = convert(counted(value));                                  \
                                                                                                   \
		zassert_equal(evaluations, 1, #convert "(" #value ") evaluated %d times",          \
			      evaluations);                                                        \
		zassert_equal(runtime, convert(value), #convert "(" #value ")");                   \
	} while (false)

#define DIV_32_FLOOR(t) z_tmcvt_32(t, 10000, 1000, true, false, false)
#define DIV_32_NEAR(t) z_tmcvt_32(t, 10000, 1000, true, false, true)
#define DIV_32_CEIL(t) z_tmcvt_32(t, 10000, 1000, true, true, false)
#define MUL_32(t) z_tmcvt_32(t, 1000, 10000, true, true, false)
#define GEN_32(t) z_tmcvt_32(t, 32768, 1000, true, true, false)
#define DIV_64_CEIL(t) z_tmcvt_64(t, 10000, 1000, true, true, false)
#define MUL_64(t) z_tmcvt_64(t, 1000, 10000, true, true, false)
#define FAST_64(t) z_tmcvt_64(t, 32768, 1000, true, true, false)
#define SLOW_64(t) z_tmcvt_64(t, 32768, 1000000000, true, false, false)
#define RUNTIME_64(t) z_tmcvt_64(t, 32768, 1000000000, false, false, true)

#define ASSERT_ONCE_ALL_PATHS(value)                                                               \
	ASSERT_ONCE(DIV_32_FLOOR, value);                                                          \
	ASSERT_ONCE(DIV_32_NEAR, value);                                                           \
	ASSERT_ONCE(DIV_32_CEIL, value);                                                           \
	ASSERT_ONCE(MUL_32, value);                                                                \
	ASSERT_ONCE(GEN_32, value);                                                                \
	ASSERT_ONCE(DIV_64_CEIL, value);                                                           \
	ASSERT_ONCE(MUL_64, value);                                                                \
	ASSERT_ONCE(FAST_64, value);                                                               \
	ASSERT_ONCE(SLOW_64, value);                                                               \
	ASSERT_ONCE(RUNTIME_64, value)

ZTEST(time_units, test_constant_arguments_stay_constant_expressions)
{
	zassert_equal(static_timeout.ticks, k_ms_to_ticks_ceil64(100));
	zassert_equal(static_forever.ticks, K_TICKS_FOREVER);
	zassert_equal(static_ticks, 1000);
	zassert_equal(sizeof(static_array), k_ms_to_ticks_ceil32(1));
}

ZTEST(time_units, test_conversion_paths_evaluate_argument_once)
{
	ASSERT_ONCE_ALL_PATHS(0);
	ASSERT_ONCE_ALL_PATHS(1);
	ASSERT_ONCE_ALL_PATHS(32767);
	ASSERT_ONCE_ALL_PATHS(32768);
	ASSERT_ONCE_ALL_PATHS(0xfffffff6);
	ASSERT_ONCE_ALL_PATHS(0xfffffff7);
	ASSERT_ONCE_ALL_PATHS(0xffffffff);
	ASSERT_ONCE_ALL_PATHS(0x100000000);
}

ZTEST(time_units, test_runtime_frequency_evaluated_once)
{
	evaluations = 0;
	const uint64_t ns =
		z_tmcvt_64(counted(98304), counted_hz(32768), 1000000000, false, false, true);

	zassert_equal(evaluations, 2);
	zassert_equal(ns, 3000000000);

	evaluations = 0;
	const uint32_t ms = z_tmcvt_32(counted(98304), counted_hz(32768), 1000, false, true, false);

	zassert_equal(evaluations, 2);
	zassert_equal(ms, 3000);
}

ZTEST(time_units, test_timeout_macros_evaluate_argument_once)
{
	evaluations = 0;
	const k_timeout_t ms = Z_TIMEOUT_MS(counted_signed(250));

	zassert_equal(evaluations, 1);
	zassert_equal(ms.ticks, Z_TIMEOUT_MS(250).ticks);

	evaluations = 0;
	const k_timeout_t us = Z_TIMEOUT_US(counted_signed(250));

	zassert_equal(evaluations, 1);
	zassert_equal(us.ticks, Z_TIMEOUT_US(250).ticks);

	evaluations = 0;
	const k_timeout_t ns = Z_TIMEOUT_NS(counted_signed(250));

	zassert_equal(evaluations, 1);
	zassert_equal(ns.ticks, Z_TIMEOUT_NS(250).ticks);

	evaluations = 0;
	const k_timeout_t negative = Z_TIMEOUT_MS(counted_signed(-5));

	zassert_equal(evaluations, 1);
	zassert_equal(negative.ticks, 0);
}

ZTEST(time_units, test_sys_timeout_ms_evaluates_argument_once)
{
	evaluations = 0;
	const k_timeout_t ms = SYS_TIMEOUT_MS(counted_signed(250));

	zassert_equal(evaluations, 1);
	zassert_equal(ms.ticks, SYS_TIMEOUT_MS(250).ticks);

	evaluations = 0;
	const k_timeout_t forever = SYS_TIMEOUT_MS(counted_signed(SYS_FOREVER_MS));

	zassert_equal(evaluations, 1);
	zassert_equal(forever.ticks, K_TICKS_FOREVER);
}

ZTEST(time_units, test_sys_timeout_ms_forever_in_argument_type)
{
	zassert_equal(SYS_TIMEOUT_MS((int32_t)counted_signed(-1)).ticks, K_TICKS_FOREVER);
	zassert_equal(SYS_TIMEOUT_MS((uint32_t)counted(UINT32_MAX)).ticks, K_TICKS_FOREVER);
	zassert_equal(SYS_TIMEOUT_MS(counted_signed(-1)).ticks, K_TICKS_FOREVER);
	zassert_equal(SYS_TIMEOUT_MS(counted(UINT64_MAX)).ticks, K_TICKS_FOREVER);
	zassert_equal(SYS_TIMEOUT_MS((uint16_t)counted(UINT16_MAX)).ticks,
		      SYS_TIMEOUT_MS((uint16_t)UINT16_MAX).ticks);
	zassert_equal(SYS_TIMEOUT_MS((uint32_t)counted(UINT32_MAX - 1)).ticks,
		      SYS_TIMEOUT_MS((uint32_t)(UINT32_MAX - 1)).ticks);
}

static int reads;
static int64_t read_sequence[2];

static int64_t read_clock(void)
{
	return read_sequence[reads++ % ARRAY_SIZE(read_sequence)];
}

/* A deadline that passes between two reads of the clock must not bypass the
 * clamp; at 1000 Hz a -1 ms argument is K_TICKS_FOREVER.
 */
ZTEST(time_units, test_timeout_clamp_uses_a_single_clock_read)
{
	const int64_t now = 1000000;
	const int64_t deadline = now + 1;

	reads = 0;
	read_sequence[0] = now;
	read_sequence[1] = deadline + 1;
	const k_timeout_t timeout = Z_TIMEOUT_MS(deadline - read_clock());

	zassert_equal(reads, 1);
	zassert_equal(timeout.ticks, Z_TIMEOUT_MS(1).ticks);
}

/* Quotient and remainder of the slow 64-bit path come from one read, so reads
 * that straddle a second boundary cannot lose a whole second.
 */
ZTEST(time_units, test_slow_path_uses_a_single_clock_read)
{
	const int64_t hz = CONFIG_SYS_CLOCK_TICKS_PER_SEC;

	reads = 0;
	read_sequence[0] = 1000 * hz - 1;
	read_sequence[1] = 1000 * hz;
	const uint64_t ns = k_ticks_to_ns_floor64(read_clock());

	zassert_equal(reads, 1);
	zassert_equal(ns, k_ticks_to_ns_floor64(1000 * CONFIG_SYS_CLOCK_TICKS_PER_SEC - 1));
}
