import { writeFileSync } from 'node:fs';

const moduleCountText = process.argv[2] ?? '';
const functionsPerModuleText = process.argv[3] ?? '';
const moduleCount = Number(moduleCountText);
const functionsPerModule = Number(functionsPerModuleText);
const outputPath = process.argv[4];
const expectedChecksum = (moduleCount * (moduleCount - 1)) / 2;

if (
  !/^[1-9]\d*$/.test(moduleCountText) ||
  !Number.isSafeInteger(moduleCount) ||
  !/^(0|[1-9]\d*)$/.test(functionsPerModuleText) ||
  !Number.isSafeInteger(functionsPerModule) ||
  !Number.isSafeInteger(expectedChecksum) ||
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
