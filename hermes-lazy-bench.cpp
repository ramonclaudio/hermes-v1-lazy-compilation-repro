#include <hermes/Public/RuntimeConfig.h>
#include <hermes/hermes.h>
#include <jsi/jsi.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

using facebook::hermes::makeHermesRuntime;
using facebook::jsi::StringBuffer;

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: hermes-lazy-bench <smart|lazy|eager> <bundle.js>\n";
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

  std::ifstream input(argv[2], std::ios::binary);
  if (!input) {
    std::cerr << "unable to open " << argv[2] << "\n";
    return 2;
  }
  std::string source{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};

  const auto runtimeStart = std::chrono::steady_clock::now();
  auto runtime = makeHermesRuntime(
      hermes::vm::RuntimeConfig::Builder().withCompilationMode(mode).build());
  const auto runtimeReady = std::chrono::steady_clock::now();

  auto result = runtime->evaluateJavaScript(
      std::make_shared<StringBuffer>(std::move(source)), argv[2]);
  const auto evaluationDone = std::chrono::steady_clock::now();

  const auto runtimeMs =
      std::chrono::duration<double, std::milli>(runtimeReady - runtimeStart)
          .count();
  const auto evaluationMs =
      std::chrono::duration<double, std::milli>(evaluationDone - runtimeReady)
          .count();

  std::cout << std::fixed << std::setprecision(3)
            << "mode=" << modeName << " runtime_ms=" << runtimeMs
            << " evaluation_ms=" << evaluationMs;
  if (result.isNumber()) {
    std::cout << " checksum=" << result.asNumber();
  }
  std::cout << "\n";
  return 0;
}
