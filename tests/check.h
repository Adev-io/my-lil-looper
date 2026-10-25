// check.h: the tiny test harness. Every check prints ok / FAIL; main() returns the failure count.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static int failures = 0;

inline void check (bool ok, const char* what, double got = 0.0)
{
    std::printf ("%s  %s  (%g)\n", ok ? "ok  " : "FAIL", what, got);
    if (! ok) ++failures;
}
inline bool near (double a, double b, double tol = 1e-5) { return std::abs (a - b) <= tol; }
