// fuzzy.h
// Independent implementation based on the algorithm described in:
//   Kornbluth (2006) and Tridgell (2002) "Efficient Algorithms for Sorting and Synchronization"
// Not derived from ssdeep or any GPL-licensed source.
//
// MIT License
// Copyright (c) 2026 Andrew J. Greenwald, Jr.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace ctph {

// Minimum and maximum block sizes used in the piecewise hashing
static constexpr uint32_t MIN_BLOCK_SIZE = 3;
static constexpr uint32_t MAX_BLOCK_SIZE = MIN_BLOCK_SIZE * (1 << 16);

// The target number of hash characters per block
static constexpr uint32_t SPAMSUM_LENGTH = 64;

// Rolling hash window size (Adler/Rabin-style)
static constexpr uint32_t ROLLING_WINDOW = 7;

// Max hash size (for buffer allocation) — should be enough for any reasonable input size
static constexpr uint32_t FUZZY_MAX_RESULT = 1024;

// Base64 alphabet used to encode digest bytes
static constexpr char BASE64_ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// FNV prime and offset basis for 32-bit FNV-1a
static constexpr uint32_t FNV_PRIME  = 0x01000193u;
static constexpr uint32_t FNV_OFFSET = 0x28021967u;
static constexpr uint32_t FNV_INIT   = 0x28021967u;

/**
 * Rolling hash state used to detect block boundaries.
 * Uses a window-based Adler-style accumulator identical in spirit to
 * the one described in Tridgell (2002).
 */
struct RollingState {
    uint32_t h1 = 0;  // sum of bytes in window
    uint32_t h2 = 0;  // weighted positional sum
    uint32_t h3 = 0;  // shift register (LFSR-style)
    uint8_t  window[ROLLING_WINDOW] = {};
    uint32_t n = 0;   // position counter (mod window size)

    void reset();
    void update(uint8_t byte);
    uint32_t digest() const;
};

/**
 * Compute a CTPH fuzzy hash of the given data buffer.
 *
 * Returns a string of the form:
 *   <block_size>:<hash1>:<hash2>
 *
 * where hash1 is the digest at block_size and hash2 is the digest
 * at block_size*2, both Base64-encoded.
 *
 * Returns an empty string on failure.
 */
std::string hash_buffer(const uint8_t* data, size_t length);

/**
 * Compute a CTPH fuzzy hash of the file at the given path.
 * Returns an empty string on failure (file not found, unreadable, etc.).
 */
std::string hash_file(const std::filesystem::path& path);

/**
 * Compare two fuzzy hashes and return a similarity score in [0, 100].
 * 0 means completely dissimilar, 100 means identical.
 * Returns -1 if the hashes are malformed or incomparable.
 */
int compare(const std::string& hash1, const std::string& hash2);

} // namespace ctph
