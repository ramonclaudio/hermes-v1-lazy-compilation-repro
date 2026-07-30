# Hermes V1 lazy compilation repro
Minimal repro for a Hermes V1 development-mode lazy compilation bug where repeated module-wide scope scans make large eagerly loaded module graphs extremely slow.
