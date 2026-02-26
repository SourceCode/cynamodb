# Installation Guide

This guide outlines the prerequisites and steps required to build and install cynamoDB on your system.

## Prerequisites

### Operating Systems
- **Linux**: Ubuntu 22.04+, Fedora 38+, or equivalent.
- **macOS**: 13.0+ (Ventura) or newer with Xcode Command Line Tools.
- **Windows**: WSL2 recommended; native MSVC support is available via CMake.

### Build Tools
- **CMake**: Version 3.25 or higher.
- **C++ Compiler**: A compiler with full **C++23** support:
  - GCC 13.1+
  - Clang 16.0+
  - MSVC 19.36+ (Visual Studio 2022 17.6+)
- **Build System**: `make`, `ninja`, or Visual Studio.

### Dependencies
The following dependencies are fetched automatically via CMake's `FetchContent` or found via `find_package`:
- **simdjson**: High-performance JSON parsing.
- **Catch2**: Unit testing framework.
- **Boost**: C++ utility libraries (v1.81+ recommended).
  - *Note*: Ensure Boost is installed on your system (e.g., `brew install boost` or `apt-get install libboost-all-dev`).

## Build Instructions

1. **Clone the Repository**:
   ```bash
   git clone https://github.com/cynamodb/cynamodb.git
   cd cynamodb
   ```

2. **Configure with CMake**:
   ```bash
   mkdir build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   ```
   *Optional Flags*:
   - `-DENABLE_FUZZING=ON`: Enable fuzzing targets.
   - `-DCMAKE_INSTALL_PREFIX=/usr/local`: Set installation destination.

3. **Build the Project**:
   ```bash
   make -j$(nproc)
   ```

4. **Run Unit Tests**:
   Verify the build by running the test suite:
   ```bash
   ctest --output-on-failure
   ```

## Installation
To install the `cynamodb` binary to your system:
```bash
sudo make install
```

## Potential Issues
- **Compiler Version**: If you encounter errors related to `std::jthread` or other C++23 features, verify your compiler version.
- **Boost Paths**: If CMake fails to find Boost, provide the path manually:
  ```bash
  cmake .. -DBOOST_ROOT=/path/to/boost
  ```
