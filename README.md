# ctph-fuzzy

A clean-room C++ implementation of **Context-Triggered Piecewise Hashing (CTPH)**,
also known as fuzzy hashing.

## Background

CTPH was described by Jesse Kornbluth (2006) and builds on rolling-checksum
techniques from Andrew Tridgell's 2002 PhD thesis
*"Efficient Algorithms for Sorting and Synchronization"*.

This implementation is written **independently from scratch** based on the
published algorithm descriptions. It is **not** derived from ssdeep or any
other GPL-licensed codebase.

## Files

| File | Description |
|------|-------------|
| `fuzzy.h` | Public API header |
| `fuzzy.cpp` | Library implementation |
| `fuzzy-ctph.cpp` | CLI application |
| `CMakeLists.txt` | Build system |

## 🛠 Installation

### Prerequisites
* C++20 compatible compiler (MSVC 2022+, GCC 11+, or Clang 13+)
* [CMake](https://cmake.org/) (3.20+)
* [vcpkg](https://github.com/microsoft/vcpkg) for dependency management

### Building from Source
```bash
# Install dependencies
vcpkg install

# Configure and Build
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

## Usage

### Command-line tool

**Hash a single file:**
```bash
./fuzzyhash <file>
# 3072:rApHEPXDqCbwwXXv6lTSLBmFH/YkrGBx8WJMQ5b8Nj:rApHEPXqCwXv6lSLBmH/Ykr8WJ5b
```

**Compare two files:**
```bash
./fuzzyhash <file1> <file2>
# 87% similar
#   file1: /path/to/original.bin
#   file2: /path/to/modified.bin
#   hash1: 3072:abc...
#   hash2: 3072:abd...
```

**Cluster all files in a directory by similarity:**
```bash
./fuzzyhash <directory> [--threshold N] [--csv output.csv]
```
- `--threshold N` — only group files with ≥ N% similarity (default: 50)
- `--csv output.csv` — also write results to a CSV with columns `file,cluster_id,hash`

Linux Users: If you get a "shared library" error, install the TBB library using:
sudo apt update && sudo apt install libtbb-dev

### Library API

```cpp
#include <filesystem>
#include "fuzzy.h"

namespace fs = std::filesystem;

// Hash a file using a path object
std::string hash = ctph::hash_file(fs::path("/path/to/file"));

// Note: You can also pass a string directly if the implicit 
// constructor for fs::path is available:
// std::string hash = ctph::hash_file("/path/to/file");
```

## Algorithm Overview

1. A **rolling hash** (Adler-style window accumulator + shift register)
   scans the input byte by byte.
2. Whenever `rolling_hash % block_size == block_size - 1`, a block
   boundary is triggered.
3. The bytes within each block are accumulated with an **FNV-1a** hash;
   its value is Base64-encoded into one output character.
4. Two digests are produced simultaneously: one at `block_size` and one
   at `block_size * 2`.
5. The `block_size` is chosen so the primary digest is approximately
   64 characters long.

The output format is: `block_size:digest1:digest2`

## Similarity Comparison

`ctph::compare()` computes a normalised edit-distance score between two
digests whose block sizes are equal (or adjacent powers of two). Scores
range from **0** (completely different) to **100** (identical).

## License

MIT License. See `fuzzy.h` for the full license text.

## ⚖️ Clean-Room Implementation & Legal

This project is a **clean-room implementation** of the Context-Triggered Piecewise Hashing (CTPH) algorithm. 

* **Source Material**: Developed exclusively from the technical specifications provided in *Kornbluth (2006)* and *Tridgell (2002)*.
* **Non-Derivative**: This codebase was authored from scratch in modern C++20. It does not contain, reference, or derive from the source code of `ssdeep`, `libfuzzy`, or any other GPL-licensed implementations.
* **Compatibility**: While the underlying math is identical to standard CTPH, this library is intended as a modern, MIT-licensed alternative for environments where GPL-linkage is restricted.
