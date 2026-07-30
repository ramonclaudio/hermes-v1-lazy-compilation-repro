# Hermes V1 lazy compilation repro

Minimal repro for a Hermes V1 development-mode lazy compilation bug where
repeated module-wide scope scans make large eagerly loaded module graphs
extremely slow.

## Run the repro

Requirements: Node.js, CMake, Ninja, and a C++ toolchain.

```sh
git clone --depth 1 \
  --branch hermes-v250829098.0.16 \
  https://github.com/facebook/hermes.git \
  /tmp/hermes-v1

./run.sh /tmp/hermes-v1
```

The default run builds a debugger-enabled Hermes V1 runtime and evaluates a
Metro-shaped graph containing 24,000 eagerly loaded module wrappers with eight
nested functions each. It is intentionally very slow on an unpatched build.

For a faster scaling check:

```sh
./run.sh /tmp/hermes-v1 4000 8
./run.sh /tmp/hermes-v1 8000 8
```

The final output looks like:

```text
mode=smart runtime_ms=1.247 evaluation_ms=255504.961 checksum=287988000.000
```

`smart` is the default compilation mode used by React Native. The generated
binary can also compare eager and fully lazy compilation:

```sh
.build/bin/hermes-lazy-bench eager bundle-24000x8.js
.build/bin/hermes-lazy-bench lazy bundle-24000x8.js
```

## Profile it

Build a small case first, generate the full input, and then attach a profiler
directly to the benchmark process:

```sh
./run.sh /tmp/hermes-v1 100 8
node generate-bundle.mjs 24000 8 bundle-24000x8.js
```

On macOS:

```sh
.build/bin/hermes-lazy-bench smart bundle-24000x8.js &
benchmark_pid=$!
sample "$benchmark_pid" 10 -file hermes-v1.sample.txt
wait "$benchmark_pid"
```

On Linux:

```sh
perf record -g -- \
  .build/bin/hermes-lazy-bench smart bundle-24000x8.js
perf report
```

The included
[`hermes-v1-16-24000x8.sample.txt`](./hermes-v1-16-24000x8.sample.txt)
is a macOS sample from the unpatched 255-second run.

## What it reproduces

In a debugger-enabled source build, Hermes `SmartCompilation` defers compiling
large functions until they execute. Metro registers application modules as
function wrappers and then eagerly requires this graph.

Hermes V1 reuses one IR `Module`. After each lazy function compilation:

1. `BytecodeModuleGenerator::generateAddedFunctions()` calls
   `Module::assignIndexToVariables()`, which traverses every accumulated
   `VariableScope`.
2. `Module::resetForMoreCompilation()` traverses the same full scope list to
   find unused scopes.

Compilation number `N` therefore revisits scopes created by earlier
compilations, making the total work quadratic for a large eagerly evaluated
graph. In the included sample, more than 94% of active lazy-compilation samples
are in those two traversals:

| Top frame | Samples | Share |
| --- | ---: | ---: |
| `Module::resetForMoreCompilation()` | 2,721 | 71.1% |
| `VariableScope::assignIndexToVariables()` | 888 | 23.2% |
| Other `generateAddedFunctions()` work | 189 | 4.9% |

This source path is unchanged across Hermes V1 `.10`, `.14`, and `.16`.

## Results

The full matrix is in [`results.csv`](./results.csv). The main 24,000×8
comparison was:

| Engine/configuration | Evaluation |
| --- | ---: |
| Hermes 0.14.1, Smart | 34.754 s |
| Hermes V1 `.16`, Eager | 1.550 s |
| Hermes V1 `.16`, Smart, unpatched | **255.505 s** |
| Hermes V1 `.16`, Smart, candidate patch | **1.869 s** |

All runs produced the expected checksum. The candidate change improved the
pathological case by 136.7×.

## Candidate patch

[`hermes-v1-variable-scope-worklists.patch`](./hermes-v1-variable-scope-worklists.patch)
replaces the two full-scope traversals with incremental worklists.

Apply and rerun:

```sh
git -C /tmp/hermes-v1 apply \
  "$PWD/hermes-v1-variable-scope-worklists.patch"
./run.sh /tmp/hermes-v1
```

This is a diagnostic prototype for Hermes maintainers to review, not a claimed
ready-to-land upstream patch. It preserves the benchmark checksum and passed:

- 35/35 Hermes IR tests
- 39/39 bytecode-generation tests
- 449/449 API, JSI, and debugger tests
- the full Hermes regression suite with zero unexpected failures

## Files

- [`hermes-lazy-bench.cpp`](./hermes-lazy-bench.cpp): minimal JSI host using
  the same `SmartCompilation` runtime mode as React Native.
- [`generate-bundle.mjs`](./generate-bundle.mjs): creates the Metro-shaped
  source graph.
- [`run.sh`](./run.sh): configures debugger-enabled Hermes, builds the host,
  generates the source, and runs it.
- [`results.csv`](./results.csv): SDK/Hermes version and compilation-mode
  benchmark matrix.
- [`hermes-v1-16-24000x8.sample.txt`](./hermes-v1-16-24000x8.sample.txt):
  unpatched macOS sample profile.
- [`hermes-v1-variable-scope-worklists.patch`](./hermes-v1-variable-scope-worklists.patch):
  candidate incremental-scope fix.

## Related reports and source

- [Expo #48298](https://github.com/expo/expo/issues/48298)
- [Independent Hermes profile](https://github.com/facebook/hermes/issues/2048#issuecomment-4859788189)
- [Hermes lazy/eval compilation design](https://github.com/facebook/hermes/blob/static_h/doc/LazyEvalCompilation.md)
- [Full-scope cleanup](https://github.com/facebook/hermes/blob/static_h/lib/IR/IR.cpp#L919-L975)
- [Full-scope index assignment](https://github.com/facebook/hermes/blob/static_h/include/hermes/IR/IR.h#L2324-L2328)
- [Lazy generation path](https://github.com/facebook/hermes/blob/static_h/lib/BCGen/HBC/BytecodeGenerator.cpp#L790-L854)
