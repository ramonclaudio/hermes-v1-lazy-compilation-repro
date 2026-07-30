import { writeFileSync } from 'node:fs';

const moduleCount = Number.parseInt(process.argv[2] ?? '', 10);
const functionsPerModule = Number.parseInt(process.argv[3] ?? '', 10);
const outputPath = process.argv[4];

if (
  !Number.isSafeInteger(moduleCount) ||
  moduleCount < 1 ||
  !Number.isSafeInteger(functionsPerModule) ||
  functionsPerModule < 0 ||
  !outputPath
) {
  console.error(
    'usage: node generate-bundle.mjs <module-count> <functions-per-module> <output.js>'
  );
  process.exit(2);
}

const lines = ['var __modules = [];'];
for (let moduleIndex = 0; moduleIndex < moduleCount; moduleIndex++) {
  lines.push(`__modules[${moduleIndex}] = function module_${moduleIndex}() {`);
  for (
    let functionIndex = 0;
    functionIndex < functionsPerModule;
    functionIndex++
  ) {
    lines.push(
      `  function fn_${moduleIndex}_${functionIndex}(value) { return value + ${
        functionIndex + 1
      }; }`
    );
  }
  lines.push(`  return ${moduleIndex};`, '};');
}
lines.push(
  'var __checksum = 0;',
  'for (var __index = 0; __index < __modules.length; __index++) {',
  '  __checksum += __modules[__index]();',
  '}',
  '__checksum;'
);

writeFileSync(outputPath, `${lines.join('\n')}\n`);
