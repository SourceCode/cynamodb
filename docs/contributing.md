# Contributing Guide

Thank you for your interest in contributing to cynamoDB! We welcome contributions that improve performance, compatibility, or documentation.

## Engineering Standards

- **Language Style**: We follow modern C++23 standards. Use `std::jthread`, `std::expected`, and `auto` where appropriate.
- **Code Quality**: No `any` or `unknown` types (if interfacing with other languages). Maintain strict type safety.
- **Performance**: High-performance paths (JSON parsing, LSM tree, Expressions) should be non-blocking and memory-efficient.

## Development Workflow

1. **Fork and Clone**:
   ```bash
   git clone https://github.com/cynamodb/cynamodb.git
   ```
2. **Create a Feature Branch**:
   ```bash
   git checkout -b feature/your-feature-name
   ```
3. **Build and Test**:
   Always ensure that the project builds and all tests pass before submitting.
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ctest
   ```
4. **Commits**:
   Follow conventional commit messages (e.g., `feat: Add support for BatchExecuteStatement`).
5. **Submit a PR**:
   Open a Pull Request against the `main` branch. Provide a clear description of the change and any relevant Issue numbers.

## Pull Request Checklist

- [ ] Code builds without warnings on GCC/Clang/MSVC.
- [ ] New functionality is covered by unit and/or integration tests.
- [ ] Documentation is updated in the `docs/` folder.
- [ ] `AGENTS.md` is updated if there are new operational capabilities.
- [ ] `CHANGELOG.md` is updated.

## Code Style

We use a variation of the LLVM coding style. Run `clang-format` before committing:
```bash
find . -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
```

## Community Help
If you have questions or need help, please open an Issue with the `question` label or join our developer forum.
