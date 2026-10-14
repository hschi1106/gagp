#pragma once

// Private block-local execution reuse. Admission is narrower than typed-view
// execution: every declared input is the same sequence measure, all constants
// are scalar, and control flow is forward-only. No payload or capture can be
// observed. A miss always runs the original phase; this is not a domain limit.
inline constexpr int DMEASURE_CACHE_MAX = 64;
struct DMeasurePhaseCacheEntry {
  long long value;
  int spent_fuel;
  int error; // -2 unavailable, -1 success; otherwise ErrCode
};
