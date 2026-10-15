# Validation evidence

- `gagp-support-ctest-final.txt`: final runtime,128 CTest entries;126 passed immediately. The two failures are preserved in the log.
- `gagp-support-final-retest.txt`: both corrected entries pass. Exact int64 arithmetic replaces the old rounding expectation; legacy migration retains all six unchanged golden presets.
- `gagp-support-focused-external-final.txt`: three focused external regressions pass against the final runtime. The full optional fixed-parent benchmark test was not invoked.
- `gagp-support-tools-final2.txt`:100 tests,4 existing skips.
- `gagp-support-repository-delivery.txt`:24 repository checks after report integration.
- `gagp-support-repository-final2.txt`: earlier identical repository suite evidence.
- `gagp-support-source-arithmetic-test.txt`: intermediate macro prototype's single edge test, retained for history only. The prototype was removed. Final primitive lowering is covered by `gagp_test_fixed_asgp_source_arithmetic` in the final CTest log (two Python methods), CPU execution-session and GPU parity/region tests.

Build completed with `cmake --build cpp/build/release -j4` after the diagnostic observer and legacy migration fixes. No formal timing matrix was run as part of validation.
