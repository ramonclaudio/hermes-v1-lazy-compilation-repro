#include <hermes/Public/RuntimeConfig.h>
#include <hermes/hermes.h>
#include <jsi/jsi.h>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>

using facebook::hermes::makeHermesRuntime;
using facebook::jsi::StringBuffer;

int main(int argc, char **argv) {
  try {
    if (argc != 3 && argc != 4) {
      std::cerr << "usage: hermes-lazy-bench <smart|lazy|eager> <bundle.js> "
                   "[expected-checksum]\n";
      return 2;
    }

    const std::string modeName = argv[1];
    hermes::vm::CompilationMode mode;
    if (modeName == "smart") {
      mode = hermes::vm::SmartCompilation;
    } else if (modeName == "lazy") {
      mode = hermes::vm::ForceLazyCompilation;
    } else if (modeName == "eager") {
      mode = hermes::vm::ForceEagerCompilation;
    } else {
      std::cerr << "unknown compilation mode: " << modeName << "\n";
      return 2;
    }

    std::optional<double> expectedChecksum;
    if (argc == 4) {
      char *end = nullptr;
      errno = 0;
      const double parsed = std::strtod(argv[3], &end);
      if (errno != 0 || end == argv[3] || *end != '\0' ||
          !std::isfinite(parsed)) {
        std::cerr << "invalid expected checksum: " << argv[3] << "\n";
        return 2;
      }
      expectedChecksum = parsed;
    }

    std::ifstream input(argv[2], std::ios::binary);
    if (!input) {
      std::cerr << "unable to open " << argv[2] << "\n";
      return 2;
    }
    std::string source{std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>()};
    if (input.bad()) {
      std::cerr << "unable to read " << argv[2] << "\n";
      return 2;
    }

    const auto runtimeStart = std::chrono::steady_clock::now();
    auto runtime = makeHermesRuntime(
        hermes::vm::RuntimeConfig::Builder().withCompilationMode(mode).build());
    const auto runtimeReady = std::chrono::steady_clock::now();

    auto result = runtime->evaluateJavaScript(
        std::make_shared<StringBuffer>(std::move(source)), argv[2]);
    const auto evaluationDone = std::chrono::steady_clock::now();

    if (!result.isNumber()) {
      std::cerr << "bundle result is not a number\n";
      return 1;
    }
    const double checksum = result.asNumber();
    if (expectedChecksum && checksum != *expectedChecksum) {
      std::cerr << std::fixed << std::setprecision(3)
                << "checksum mismatch: expected " << *expectedChecksum
                << ", got " << checksum << "\n";
      return 1;
    }

    const auto runtimeMs =
        std::chrono::duration<double, std::milli>(runtimeReady - runtimeStart)
            .count();
    const auto evaluationMs =
        std::chrono::duration<double, std::milli>(evaluationDone - runtimeReady)
            .count();

    std::cout << std::fixed << std::setprecision(3) << "mode=" << modeName
              << " runtime_ms=" << runtimeMs
              << " evaluation_ms=" << evaluationMs << " checksum=" << checksum
              << "\n";

    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Hermes evaluation failed: " << error.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "Hermes evaluation failed with an unknown exception\n";
    return 1;
  }
}
