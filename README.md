# Hermes V1 lazy compilation repro

Repro for a Hermes V1 lazy-compilation bug exposed by large, eagerly evaluated
Metro-style module graphs. Repeated whole-module `VariableScope` scans make the
lazy-compilation setup work grow quadratically.

This is a synthetic compiler/runtime repro for
[Expo #48298](https://github.com/expo/expo/issues/48298), not a complete Expo
app. The synthetic input is also slow on legacy Hermes 0.14.1, so standalone
timing is not enough to attribute the exact SDK 57 app regression only to V1.

## Run

Requirements: Node.js, CMake, Ninja, and a C++ toolchain. `ccache` is optional.

```sh
git clone --depth 1 \
  --branch hermes-v250829098.0.16 \
  https://github.com/facebook/hermes.git \
  /tmp/hermes-v1

./run.sh /tmp/hermes-v1
```

The default case creates 24,000 eagerly invoked module wrappers with eight
nested functions each, builds a debugger-enabled Hermes runtime using all
available cores, evaluates the source with `SmartCompilation`, and verifies the
checksum. On the M4 Pro test machine, unpatched `.16` runs took
43.3–255.5 seconds.

For a faster scaling check:

```sh
./run.sh /tmp/hermes-v1 4000 8
./run.sh /tmp/hermes-v1 8000 8
```

Useful overrides:

```sh
HERMES_BUILD_JOBS=12 ./run.sh /tmp/hermes-v1
HERMES_BUILD_TYPE=Debug \
  HERMES_BUILD_DIR="$PWD/.build-debug" \
  ./run.sh /tmp/hermes-v1 4000 8
```

The benchmark binary can also compare compilation modes:

```sh
.build/bin/hermes-lazy-bench \
  eager bundle-24000x8.js 287988000
.build/bin/hermes-lazy-bench \
  lazy bundle-24000x8.js 287988000
```

## Profile

Build a small case first, generate the full input, and attach a profiler to the
benchmark:

```sh
./run.sh /tmp/hermes-v1 100 8
node generate-bundle.mjs 24000 8 bundle-24000x8.js
```

macOS:

```sh
.build/bin/hermes-lazy-bench \
  smart bundle-24000x8.js 287988000 &
benchmark_pid=$!
sample "$benchmark_pid" 10 -file hermes-v1.sample.txt
wait "$benchmark_pid"
```

Linux:

```sh
perf record -g -- \
  .build/bin/hermes-lazy-bench smart bundle-24000x8.js 287988000
perf report
```

The included
[`hermes-v1-16-24000x8.sample.txt`](./hermes-v1-16-24000x8.sample.txt)
comes from one of the reproducible slower unpatched `.16` runs. Its call graph
supports the diagnosis: 94.3% of active lazy-compilation samples were in the two
full-scope traversals. No speedup claim is based on that profile alone.

## Cause

Hermes `SmartCompilation` initially defers the bodies of large functions. Metro
registers application modules as wrappers and then eagerly invokes the graph.
Hermes reuses one IR `Module` across these lazy compilations.

After every lazy body:

1. `Module::assignIndexToVariables()` revisits every accumulated
   `VariableScope`.
2. `Module::resetForMoreCompilation()` revisits the same full list to find
   unused scopes.

Compilation number `N` therefore repeats work for scopes from compilations
`1...N-1`. The measured scaling is consistent with quadratic growth:

| Modules | Unpatched `.16` | Patched `.16` |
| ---: | ---: | ---: |
| 2,000 | 0.246 s | 0.115 s |
| 4,000 | 0.708 s | 0.233 s |
| 8,000 | 2.513 s | 0.497 s |
| 12,000 | 6.895 s | 0.816 s |
| 16,000 | 13.144 s | 1.100 s |
| 24,000 | 43.273–255.505 s (`n=6`) | 1.776–1.834 s (`n=7`) |

At 24,000×8, six unpatched trials ranged from 43.273–255.505 seconds
(50.673-second median; 114.756-second mean), while seven patched trials ranged
from 1.776–1.834 seconds (1.803-second median; 1.809-second mean). The patch was
28.1× faster by median and at least 23.6× faster when comparing the fastest
unpatched trial with the slowest patched trial. Every run returned checksum
`287988000`. Raw measurements are in [`results.csv`](./results.csv).

## SDK/Hermes matrix

The engine revisions below come from the corresponding React Native
`sdks/hermes-engine/version.properties` files. Times are this standalone
24,000×8 benchmark, not Expo launch times.

| Expo SDK path | Hermes revision | Smart evaluation |
| --- | --- | ---: |
| SDK 55 default | `hermes-v0.14.1` | 35.781 s |
| SDK 55 V1 opt-in | `hermes-v250829098.0.4` | 248.635 s |
| SDK 56 V1 | `hermes-v250829098.0.10` | 46.774 s |
| SDK 57 V1 | `hermes-v250829098.0.14` | 49.944 s |
| Latest tested V1 | `hermes-v250829098.0.16` | 43.273–255.505 s (`n=6`) |

The `.4` and legacy rows are important controls: this synthetic benchmark is
not a one-to-one model of the exact SDK 57 application behavior.

Current `static_h` at `ad3c5661a` is also affected: four unpatched runs
averaged 54.475 seconds, while seven patched runs averaged 1.788 seconds, a
30.5× improvement.

## Candidate patch

[`hermes-v1-variable-scope-worklists.patch`](./hermes-v1-variable-scope-worklists.patch)
replaces both repeated full-list scans with incremental worklists:

- newly created scopes are indexed once;
- scopes are checked for deletion only when they are new or lose their last IR
  user;
- all scope deletion paths remove the scope from both worklists.

Tracking the last-user transition is required. An earlier prototype only
scanned live functions during reset, which missed scopes after
`BytecodeFunction` destroyed their retained Function IR. The included
`DeleteScopeAfterLastUserIsRemoved` regression test fails on that prototype and
passes on this candidate.

The patch is based on `static_h` commit
`ad3c5661aab4666bef84e26b57b8b468f4fba225` and was also applied and tested on
`hermes-v250829098.0.16`.

```sh
git -C /tmp/hermes-v1 apply --check \
  "$PWD/hermes-v1-variable-scope-worklists.patch"
git -C /tmp/hermes-v1 apply \
  "$PWD/hermes-v1-variable-scope-worklists.patch"

HERMES_BUILD_DIR="$PWD/.build-patched" ./run.sh /tmp/hermes-v1
```

This remains a candidate for Hermes maintainers to review, not a claim that it
is ready to land unchanged.

## Validation

All builds used AppleClang 21 on arm64 macOS 26.5.2, with the debugger enabled
and Intl disabled.

| Source/configuration | Pass | Expected fail | Unsupported | Unexpected fail |
| --- | ---: | ---: | ---: | ---: |
| V1 `.16` Release | 2,700 | 6 | 69 | 0 |
| V1 `.16` Debug | 2,728 | 6 | 78 | 0 |
| V1 `.16` AddressSanitizer | 2,696 | 6 | 71 | 0 |
| V1 `.16` UndefinedBehaviorSanitizer | 2,696 | 6 | 72 | 0 |
| current `static_h` Release | 4,313 | 6 | 91 | 0 |

The current `static_h` run also passed all 41 runnable Node.js Node-API cases
(23 skipped) and the Node-API CTS smoke case. The patch passed `clang-format`,
`git diff --check`, and `clang-tidy` without a changed-line finding.

One `.16` Debug invocation produced a transient
`debugger/eval.js` source-location abort. The test then passed 20/20 isolated
repetitions and the next complete Debug suite; it is not counted as a clean
run, and the successful complete rerun is the row reported above.

The focused regression test also passed under ThreadSanitizer. A complete TSan
result is intentionally not reported as clean: under the full suite, the
intentional recursive stack-overflow tests either crashed inside the macOS TSan
runtime/HadesGC worker or failed to reach the stack limit after 11 minutes.
Those same tests passed immediately in isolation, and the stalled run was
terminated.

## Files

- [`hermes-lazy-bench.cpp`](./hermes-lazy-bench.cpp): minimal JSI host.
- [`generate-bundle.mjs`](./generate-bundle.mjs): deterministic source
  generator.
- [`run.sh`](./run.sh): configure, build, run, and checksum verification.
- [`results.csv`](./results.csv): raw benchmark measurements.
- [`hermes-v1-16-24000x8.sample.txt`](./hermes-v1-16-24000x8.sample.txt):
  sanitized unpatched sample profile.
- [`hermes-v1-variable-scope-worklists.patch`](./hermes-v1-variable-scope-worklists.patch):
  candidate fix and regression test.

Related source:

- [Expo #48298](https://github.com/expo/expo/issues/48298)
- [Hermes #2048 profile discussion](https://github.com/facebook/hermes/issues/2048#issuecomment-4859788189)
- [Hermes lazy/eval compilation design](https://github.com/facebook/hermes/blob/static_h/doc/LazyEvalCompilation.md)
