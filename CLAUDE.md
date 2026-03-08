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
- `[[nodiscard]]` on all public getters, query functions, and factory methods

## Formatting & Linting
After every code change, run clang-format and clang-tidy before committing:

```bash
# Format all source files
find include src examples tests -name '*.hpp' -o -name '*.cpp' | xargs clang-format -i

# Rebuild (required for clang-tidy to use the compilation database)
cmake --build build/debug

# Run clang-tidy on changed files (substitute the files you touched)
/opt/homebrew/opt/llvm/bin/clang-tidy -p build/debug <files...>
```

- `.clang-format` and `.clang-tidy` are in the project root.
- clang-tidy is at `/opt/homebrew/opt/llvm/bin/clang-tidy`
- clangd is at `/opt/homebrew/opt/llvm/bin/clangd`
- Fix all actionable warnings. Known false positives to ignore:
  - `bugprone-chained-comparison` on template angle brackets (e.g. `world.each<T>(...)`)
  - `bugprone-infinite-loop` when loop condition is affected by called functions with side effects
  - `bugprone-invalid-enum-default-initialization` on Vulkan C-style enum zero-init (`VkFoo foo{}`)
