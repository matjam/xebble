# Xebble Project Instructions

## Git Workflow
- Commit directly to main. No feature branches.
- vcpkg is at `~/vcpkg`

## Build
```bash
cmake --preset debug
cmake --build build/debug
ctest --preset debug
```

## Code Style
- C++23 (C++20 baseline), RAII everywhere, value semantics
- No raw pointers in public API
- `std::expected<T, Error>` for fallible construction
- Move-only types for GPU resource owners
