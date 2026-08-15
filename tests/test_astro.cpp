/*
 * test_astro.c — Unit tests for core astronomical calculations

 * Compile with:
 *   gcc -std=c99 -O2 -Isrc -Ilib -DTLESCOPE_VERSION=\"test\" \
 *       tests/test_astro.c src/astro.c src/config.c \
 *       -lm -o tests/test_astro
 *
 * Run with:
 *   ./tests/test_astro
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* core headers (no UI dependencies) */
#include "astro.h"
#include "config.h"

/* ── Test Framework (minimal) ────────────────────────────────────────────── */

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    do                                                                         \
    {                                                                          \
        printf("  TEST: %s ... ", name);

#define END_TEST(result)                                                       \
    if (result)                                                                \
    {                                                                          \
        printf("PASSED\n");                                                    \
        tests_passed++;                                                        \
    }                                                                          \
    else                                                                       \
    {                                                                          \
        printf("FAILED\n");                                                    \
        tests_failed++;                                                        \
    }                                                                          \
    }                                                                          \
    while (0)

#define ASSERT(cond)                                                           \
    do                                                                         \
    {                                                                          \
        if (!(cond))                                                           \
        {                                                                      \
            printf("\n    ASSERTION FAILED: %s (line %d)\n", #cond, __LINE__); \
            return false;                                                      \
        }                                                                      \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                                 \
    do                                                                         \
    {                                                                          \
        double _a = (a), _b = (b), _eps = (eps);                               \
        if (fabs(_a - _b) > _eps)                                              \
        {                                                                      \
            printf("\n    ASSERTION FAILED: %s ≈ %s (line %d)\n", #a, #b,      \
                   __LINE__);                                                  \
            printf("      actual: %f, expected: %f, eps: %f\n", _a, _b,        \
                   _eps);                                                      \
            return false;                                                      \
        }                                                                      \
    } while (0)

/* ── Test: Epoch Conversion ──────────────────────────────────────────────── */

static bool test_epoch_conversion(void)
{
    /* get_current_real_time_epoch() returns YYYYDDD.FFFF format */
    double epoch = get_current_real_time_epoch();
    int year = (int)(epoch / 1000.0);

    /* year should be reasonable (2024-2026 range) */
    ASSERT(year >= 2024 && year <= 2026);

    /* day of year should be 1-366 */
    double doy = fmod(epoch, 1000.0);
    ASSERT(doy >= 1.0 && doy <= 367.0);

    return true;
}

static bool test_normalize_epoch(void)
{
    /* test that normalize_epoch handles year boundaries */
    double epoch;

    /* mid-year should stay the same */
    epoch = normalize_epoch(2025180.5);
    ASSERT_NEAR(epoch, 2025180.5, 0.001);

    /* day 367 in a non-leap year should roll over */
    epoch = normalize_epoch(2025367.0);
    ASSERT((int)(epoch / 1000.0) == 2026);
    ASSERT(fmod(epoch, 1000.0) >= 1.0);

    /* day 0 should roll back */
    epoch = normalize_epoch(2025000.5);
    ASSERT((int)(epoch / 1000.0) == 2024);

    return true;
}

static bool test_unix_epoch_conversion(void)
{
    /* test get_unix_from_epoch with a known date */
    /* 2025-01-01 00:00:00 UTC = 2025001.0 in our format */
    double epoch = 2025001.0;
    double unix = get_unix_from_epoch(epoch);

    /* 2025-01-01 00:00:00 UTC = 1735689600 Unix */
    ASSERT_NEAR(unix, 1735689600.0, 86400.0); /* within a day */

    return true;
}

/* ── Test: Geodetic Conversion ───────────────────────────────────────────── */

static bool test_geodetic_to_ecef(void)
{
    double x, y, z;

    /* Test equator at prime meridian (0,0,0) */
    geodetic_to_ecef(0.0, 0.0, 0.0, &x, &y, &z);
    ASSERT_NEAR(x, 6378.137, 0.1); /* WGS84 semi-major axis */
    ASSERT_NEAR(y, 0.0, 0.001);
    ASSERT_NEAR(z, 0.0, 0.001);

    /* Test north pole (90,0,0) */
    geodetic_to_ecef(90.0, 0.0, 0.0, &x, &y, &z);
    ASSERT_NEAR(x, 0.0, 0.001);
    ASSERT_NEAR(y, 0.0, 0.001);
    /* z should be ~6356.752 km (WGS84 semi-minor axis) */
    ASSERT_NEAR(z, 6356.752, 1.0);

    /* Test with altitude */
    geodetic_to_ecef(0.0, 0.0, 1000000.0, &x, &y, &z); /* 1000km altitude */
    ASSERT(x > 6378.137 + 1000.0); /* should be further from center */

    return true;
}

/* ── Test: TLE Parsing ───────────────────────────────────────────────────── */

static bool test_tle_parsing(void)
{
    /* Sample TLE for ISS (ZARYA) */
    const char *tle_line1 = "1 25544U 98067A   25001.50000000  .00000000  00000+0  00000+0 0  9999";
    const char *tle_line2 = "2 25544  51.6420  0.0000 0007000  0.0000  0.0000 15.50157000360000";

    int sat_count_before = sat_count;
    int result = parse_tle(tle_line1, tle_line2, NULL);

    /* Should have parsed successfully */
    ASSERT(result == 0 || sat_count > sat_count_before);

    return true;
}

/* ── Test: Satellite Position Calculation ────────────────────────────────── */

static bool test_satellite_position(void)
{
    /* If we have satellites loaded, test position calculation */
    if (sat_count > 0)
    {
        double epoch = get_current_real_time_epoch();
        Satellite *sat = &satellites[0];

        /* calculate_position should not crash */
        calculate_position(sat, epoch);

        /* position should be finite */
        ASSERT(isfinite(sat->pos.x));
        ASSERT(isfinite(sat->pos.y));
        ASSERT(isfinite(sat->pos.z));

        /* velocity should be finite */
        ASSERT(isfinite(sat->vel.x));
        ASSERT(isfinite(sat->vel.y));
        ASSERT(isfinite(sat->vel.z));
    }

    return true;
}

/* ── Test: Sun Position ──────────────────────────────────────────────────── */

static bool test_sun_position(void)
{
    double epoch = get_current_real_time_epoch();
    Vector3 sun_pos = calculate_sun_position(epoch);

    /* Sun position should be finite */
    ASSERT(isfinite(sun_pos.x));
    ASSERT(isfinite(sun_pos.y));
    ASSERT(isfinite(sun_pos.z));

    /* Sun should be far away (not at origin) */
    double dist = sqrt(sun_pos.x * sun_pos.x + sun_pos.y * sun_pos.y + sun_pos.z * sun_pos.z);
    ASSERT(dist > 1000.0); /* at least 1000km away */

    return true;
}

/* ── Test: Config Parsing ────────────────────────────────────────────────── */

static bool test_config_defaults(void)
{
    AppConfig cfg;

    /* Test loading a non-existent file (should use defaults) */
    LoadAppConfig("nonexistent_file.json", &cfg);

    ASSERT(cfg.window_width == 1280);
    ASSERT(cfg.window_height == 720);
    ASSERT(cfg.target_fps == 60);
    ASSERT(cfg.ui_scale == 1.0f);
    ASSERT(strcmp(cfg.theme, "default") == 0);
    ASSERT(cfg.show_markers == true);
    ASSERT(cfg.show_statistics == false);
    ASSERT(cfg.highlight_sunlit == false);

    return true;
}

/* ── Main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("TLEscope Core Logic Tests\n");
    printf("=========================\n\n");

    /* Epoch conversion tests */
    printf("[Epoch Conversion]\n");
    TEST("get_current_real_time_epoch returns valid year");
    END_TEST(test_epoch_conversion());

    TEST("normalize_epoch handles year boundaries");
    END_TEST(test_normalize_epoch());

    TEST("get_unix_from_epoch converts correctly");
    END_TEST(test_unix_epoch_conversion());

    /* Geodetic conversion tests */
    printf("\n[Geodetic Conversion]\n");
    TEST("geodetic_to_ecef at equator/prime meridian");
    END_TEST(test_geodetic_to_ecef());

    /* TLE parsing tests */
    printf("\n[TLE Parsing]\n");
    TEST("parse_tle handles valid TLE data");
    END_TEST(test_tle_parsing());

    /* Satellite position tests */
    printf("\n[Satellite Position]\n");
    TEST("calculate_position produces finite values");
    END_TEST(test_satellite_position());

    /* Sun position tests */
    printf("\n[Sun Position]\n");
    TEST("calculate_sun_position produces finite values");
    END_TEST(test_sun_position());

    /* Config tests */
    printf("\n[Configuration]\n");
    TEST("LoadAppConfig uses defaults for missing file");
    END_TEST(test_config_defaults());

    /* Summary */
    printf("\n=========================\n");
    printf("Results: %d passed, %d failed out of %d tests\n",
           tests_passed, tests_failed, tests_passed + tests_failed);

    return tests_failed > 0 ? 1 : 0;
}