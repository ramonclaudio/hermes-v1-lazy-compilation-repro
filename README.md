# Hermes V1 lazy compilation repro

This repository isolates a Hermes lazy-compilation performance problem exposed
by large, eagerly evaluated Metro-style module graphs. Repeated whole-module
`VariableScope` scans make compilation setup grow quadratically.

## Maintainer summary

- **Symptom:** evaluating a 24,000-module development bundle can take from
  43 to 255 seconds in Hermes V1 `.16`.
- **Root cause:** every lazily compiled body revisits all previously accumulated
  `VariableScope` objects to assign indices and find unused scopes.
- **Candidate fix:** maintain incremental worklists for new scopes and scopes
  that lose their last IR user.
- **Measured result:** the same `.16` benchmark completes in 1.776–1.834
  seconds after the change.
- **Correctness:** every benchmark returned the expected checksum, and the
  complete Release, Debug, ASan, UBSan, and tested `static_h` suites had zero
  unexpected failures.

The standalone benchmark, raw measurements, sample profile, patch, and
regression test are all included in this repository.

## Quick comparison

Run these commands from this repository's root. The 16,000-module case is the
fastest useful demonstration on the test machine: approximately 13.1 seconds
unpatched and 1.1 seconds patched, excluding the initial native build time.

Requirements: Node.js, CMake, Ninja, Bash, and a C++ toolchain. `ccache` is
optional.

```sh
git clone --depth 1 \
  --branch hermes-v250829098.0.16 \
  https://github.com/facebook/hermes.git \
  /tmp/hermes-v1

./run.sh /tmp/hermes-v1 16000 8

git -C /tmp/hermes-v1 apply --check \
  "$PWD/hermes-v1-variable-scope-worklists.patch"
git -C /tmp/hermes-v1 apply \
  "$PWD/hermes-v1-variable-scope-worklists.patch"

HERMES_BUILD_DIR="$PWD/.build-patched" \
  ./run.sh /tmp/hermes-v1 16000 8
```

Expected evaluation results on the M4 Pro test machine:

| Source | Evaluation | Checksum |
| --- | ---: | ---: |
| `.16`, unpatched | 13.144 s | `127992000` |
| `.16`, patched | 1.100 s | `127992000` |

`run.sh` verifies the checksum automatically. Timings vary by machine; the
important comparison is the same generated source before and after the patch.

For the full stress case used in the repeated comparison:

```sh
./run.sh /tmp/hermes-v1
```

This generates 24,000 eagerly invoked wrappers with eight nested functions
each. On the test machine, unpatched `.16` runs took 43.3–255.5 seconds.

## What this benchmark isolates

The C++ host evaluates generated JavaScript directly through JSI with Hermes
`SmartCompilation`. The generated source registers Metro-style module wrappers
and eagerly invokes the complete graph.

This removes Metro startup, file discovery, transformation, serialization,
bundle transfer, simulator startup, React Native initialization, and rendering
from the measured window. It is a compiler/runtime repro, not an Expo app or a
complete time-to-first-frame benchmark.

The original Expo report independently places the long delay inside evaluation
of the application import graph, after warm bundling and bundle transfer have
completed.

## Root cause

`SmartCompilation` initially defers the bodies of large functions. When the
generated module wrappers execute, Hermes compiles those bodies while reusing
one IR `Module`.

After each lazy body, the bytecode generation path:

1. calls `Module::assignIndexToVariables()`, which revisits every accumulated
   `VariableScope`;
2. calls `Module::resetForMoreCompilation()`, which scans the same full list to
   find unused scopes.

Compilation number `N` therefore repeats work for scopes created during
compilations `1...N-1`. The scaling measurements are consistent with quadratic
growth.

The included macOS sample from an unpatched 255.505-second `.16` run showed the
same concentration:

| Selected frame | Samples | Share |
| --- | ---: | ---: |
| `Module::resetForMoreCompilation()` | 2,721 | 71.1% |
| `VariableScope::assignIndexToVariables()` | 888 | 23.2% |
| Other `generateAddedFunctions()` work | 189 | 4.9% |

The two full-scope traversals account for 94.3% of the active
lazy-compilation samples in that profile.

## Candidate fix

[`hermes-v1-variable-scope-worklists.patch`](./hermes-v1-variable-scope-worklists.patch)
replaces the repeated full-list scans with two incremental worklists:

- newly created scopes are indexed once;
- scopes are checked for deletion only when they are new or lose their last IR
  user;
- every scope deletion path removes the scope from both worklists.

Tracking the last-user transition is required. An earlier prototype checked
only new scopes and missed scopes that became unused when `BytecodeFunction`
destroyed retained Function IR.

The included `DeleteScopeAfterLastUserIsRemoved` regression test reproduces
that lifecycle. It fails on the earlier prototype and passes with the candidate
patch.

The patch was produced and fully tested on `static_h` commit
`ad3c5661aab4666bef84e26b57b8b468f4fba225`. It was also applied and fully
tested on `hermes-v250829098.0.16`.

The latest upstream compatibility check was `static_h` commit
`cf6f044862989b73ad238f87426feeb89e7ad733`; the patch applies cleanly there.
That newer commit only changed VM/GC files, so the complete `static_h` test
counts below remain explicitly tied to `ad3c5661a`.

This is a proposed implementation for maintainer review, not a claim that the
worklist design must land unchanged.

## Performance evidence

### Scaling

| Modules | Unpatched `.16` | Patched `.16` |
| ---: | ---: | ---: |
| 2,000 | 0.246 s | 0.115 s |
| 4,000 | 0.708 s | 0.233 s |
| 8,000 | 2.513 s | 0.497 s |
| 12,000 | 6.895 s | 0.816 s |
| 16,000 | 13.144 s | 1.100 s |
| 24,000 | 43.273–255.505 s (`n=6`) | 1.776–1.834 s (`n=7`) |

At 24,000×8, the six unpatched trials had a 50.673-second median and a
114.756-second mean. The seven patched trials had a 1.803-second median and a
1.809-second mean.

The patch was 28.1× faster by median. Comparing the fastest unpatched trial
with the slowest patched trial still gives a conservative 23.6× improvement.
Every trial returned checksum `287988000`.

The large unpatched range is reproducible and is reported rather than discarded
as an outlier. Raw measurements are in [`results.csv`](./results.csv).

### Expo SDK and Hermes revisions

The engine revisions come from the corresponding React Native
`sdks/hermes-engine/version.properties` files. These are standalone 24,000×8
evaluation times, not Expo launch times.

| Expo SDK path | Hermes revision | Smart evaluation |
| --- | --- | ---: |
| SDK 55 default | `hermes-v0.14.1` | 35.781 s |
| SDK 55 V1 opt-in | `hermes-v250829098.0.4` | 248.635 s |
| SDK 56 V1 | `hermes-v250829098.0.10` | 46.774 s |
| SDK 57 V1 | `hermes-v250829098.0.14` | 49.944 s |
| Latest tested V1 | `hermes-v250829098.0.16` | 43.273–255.505 s (`n=6`) |

The `.4` and legacy rows are controls. The same synthetic input is slow on
legacy Hermes 0.14.1, so this standalone benchmark alone cannot attribute the
exact SDK 57 application regression exclusively to V1.

The candidate patch has not been backported to the `.10` or `.14` source
revisions. Supporting SDK 56 or SDK 57 would require adapting and validating
the source change, rebuilding the native Hermes artifacts, and rebuilding
development clients.

### Tested `static_h`

At `ad3c5661a`, four unpatched runs averaged 54.475 seconds. Seven patched runs
averaged 1.788 seconds, a 30.5× improvement. Every run produced the expected
checksum.

## Validation

All builds used AppleClang 21 on arm64 macOS 26.5.2, with the debugger enabled
and Intl disabled.

| Source/configuration | Pass | Expected fail | Unsupported | Unexpected fail |
| --- | ---: | ---: | ---: | ---: |
| V1 `.16` Release | 2,700 | 6 | 69 | 0 |
| V1 `.16` Debug | 2,728 | 6 | 78 | 0 |
| V1 `.16` AddressSanitizer | 2,696 | 6 | 71 | 0 |
| V1 `.16` UndefinedBehaviorSanitizer | 2,696 | 6 | 72 | 0 |
| `static_h` `ad3c5661a` Release | 4,313 | 6 | 91 | 0 |

The tested `static_h` source also passed all 41 runnable Node.js Node-API cases
(23 skipped) and the Node-API CTS smoke case. The patch passed `clang-format`,
`git diff --check`, and `clang-tidy` without a changed-line finding.

One `.16` Debug invocation produced a transient `debugger/eval.js`
source-location abort. The test passed 20/20 isolated repetitions and the next
complete Debug suite. The successful complete rerun is the row reported above.

The focused regression test passed under ThreadSanitizer. A complete TSan
result is intentionally not reported as clean: during the full suite, the
intentional recursive stack-overflow tests either crashed inside the macOS TSan
runtime/HadesGC worker or failed to reach the stack limit after 11 minutes.
Those same tests passed immediately in isolation, and the stalled runs were
terminated.

## Profiling

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
is the sanitized profile summarized above. A single profile supports the
location of the repeated work; the speedup claim comes from the repeated
benchmark trials, not from that profile alone.

## Additional run options

Useful build overrides:

```sh
HERMES_BUILD_JOBS=12 ./run.sh /tmp/hermes-v1
HERMES_BUILD_TYPE=Debug \
  HERMES_BUILD_DIR="$PWD/.build-debug" \
  ./run.sh /tmp/hermes-v1 4000 8
```

The benchmark binary can compare compilation modes directly:

```sh
.build/bin/hermes-lazy-bench \
  eager bundle-24000x8.js 287988000
.build/bin/hermes-lazy-bench \
  lazy bundle-24000x8.js 287988000
```

`run.sh` validates numeric inputs, builds with all detected cores unless
overridden, uses `ccache` when available, and verifies the expected checksum.

## Scope and non-claims

- This benchmark isolates Hermes evaluation; it is not a complete Expo or
  React Native startup benchmark.
- The change does not address Metro bundling, bundle transfer, rendering, or
  the missing Expo development-client loading indicator.
- The patch is a Hermes source change, not a JavaScript patch or an OTA update.
- The exact patch is fully validated on `.16` and `static_h`, not on the SDK 56
  `.10` or SDK 57 `.14` revisions.
- The evidence strongly supports the repeated `VariableScope` scans as a real
  performance bug, but does not claim that this mechanism explains every
  Hermes or Expo startup regression.

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

Related reports and source:

- [Expo #48298](https://github.com/expo/expo/issues/48298)
- [Hermes #2048 profile discussion](https://github.com/facebook/hermes/issues/2048#issuecomment-4859788189)
- [Hermes lazy/eval compilation design](https://github.com/facebook/hermes/blob/static_h/doc/LazyEvalCompilation.md)
