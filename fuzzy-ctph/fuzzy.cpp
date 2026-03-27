// fuzzy.cpp 
// Independent implementation based on the algorithm described in:
//   Kornbluth (2006) and Tridgell (2002) "Efficient Algorithms for Sorting and Synchronization"
// Not derived from ssdeep or any GPL-licensed source.
//
// MIT License — see fuzzy.h for full license text.

#include "fuzzy.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace ctph {

// Rolling hash

void RollingState::reset() {
    h1 = h2 = h3 = n = 0;
    std::memset(window, 0, sizeof(window));
}

void RollingState::update(uint8_t byte) {
    // Remove the contribution of the byte that is leaving the window
    uint32_t drop = window[n % ROLLING_WINDOW];
    h2 -= h1;
    h2 += ROLLING_WINDOW * static_cast<uint32_t>(byte);

    h1 += static_cast<uint32_t>(byte);
    h1 -= static_cast<uint32_t>(drop);

    window[n % ROLLING_WINDOW] = byte;
    n++;

    // Shift-register component (adds sensitivity to ordering)
    h3 = (h3 << 5) ^ static_cast<uint32_t>(byte);
}

uint32_t RollingState::digest() const {
    return h1 + h2 + h3;
}

// FNV-1a running hash (used to build the output digest characters)


static inline uint32_t fnv1a_update(uint32_t hash, uint8_t byte) {
    return (hash ^ static_cast<uint32_t>(byte)) * FNV_PRIME;
}

// Determine the starting block size for a given input length.
// The block size is chosen so that we produce roughly SPAMSUM_LENGTH
// output characters in the primary digest.

static uint32_t choose_block_size(size_t length) {
    uint32_t block_size = MIN_BLOCK_SIZE;
    while (block_size * SPAMSUM_LENGTH < static_cast<uint32_t>(length)) {
        block_size *= 2;
    }
    return block_size;
}

// Core hashing engine
// Produces two digests (at block_size and block_size*2) by making two
// passes over the data with different trigger thresholds.

struct DigestResult {
    uint32_t    block_size;
    std::string digest1; // primary digest  (block_size)
    std::string digest2; // secondary digest (block_size * 2)
};

static DigestResult compute_digests(const uint8_t* data, size_t length,
                                    uint32_t block_size) {
    DigestResult result;
    result.block_size = block_size;

    RollingState roll;
    uint32_t fnv1  = FNV_INIT;
    uint32_t fnv2  = FNV_INIT;

    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = data[i];
        roll.update(byte);
        fnv1 = fnv1a_update(fnv1, byte);
        fnv2 = fnv1a_update(fnv2, byte);

        // Primary block boundary: rolling hash mod block_size == (block_size-1)
        if (roll.digest() % block_size == (block_size - 1)) {
            if (result.digest1.size() < SPAMSUM_LENGTH - 1) {
                result.digest1 += BASE64_ALPHABET[fnv1 % 64];
            }
            fnv1 = FNV_INIT;
        }

        // Secondary block boundary: rolling hash mod (block_size*2) == (block_size*2-1)
        if (roll.digest() % (block_size * 2) == (block_size * 2 - 1)) {
            if (result.digest2.size() < (SPAMSUM_LENGTH / 2) - 1) {
                result.digest2 += BASE64_ALPHABET[fnv2 % 64];
            }
            fnv2 = FNV_INIT;
        }
    }

    // Append a final character representing any remaining bytes
    result.digest1 += BASE64_ALPHABET[fnv1 % 64];
    result.digest2 += BASE64_ALPHABET[fnv2 % 64];

    return result;
}

// Edit-distance helpers for compare()
// Uses a stripped-down Wagner-Fischer dynamic programming approach.

static int edit_distance(const std::string& a, const std::string& b) {
    const size_t m = a.size();
    const size_t n = b.size();

    // Use two rows to keep memory O(n)
    std::vector<int> prev(n + 1), curr(n + 1);

    for (size_t j = 0; j <= n; ++j) prev[j] = static_cast<int>(j);

    for (size_t i = 1; i <= m; ++i) {
        curr[0] = static_cast<int>(i);
        for (size_t j = 1; j <= n; ++j) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            curr[j] = std::min({prev[j] + 1,
                                curr[j - 1] + 1,
                                prev[j - 1] + cost});
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Eliminate sequences of 4+ identical characters (run-length reduction)
// to avoid inflating similarity for poorly-randomised digests.
static std::string reduce_runs(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (out.size() >= 3 &&
            out[out.size()-1] == c &&
            out[out.size()-2] == c &&
            out[out.size()-3] == c) {
            continue;
        }
        out += c;
    }
    return out;
}

#pragma region Public API

// hash_buffer

std::string hash_buffer(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) {
        return {};
    }

    uint32_t block_size = choose_block_size(length);

    // Try progressively larger block sizes until we get a usable digest.
    // A digest is considered usable if the primary hash has at least a few
    // characters (otherwise comparisons would be meaningless).
    DigestResult best;
    for (;;) {
        best = compute_digests(data, length, block_size);
        if (best.digest1.size() >= SPAMSUM_LENGTH / 2 ||
            block_size >= MAX_BLOCK_SIZE) {
            break;
        }
        block_size *= 2;
    }

    std::ostringstream oss;
    oss << best.block_size << ':' << best.digest1 << ':' << best.digest2;
    return oss.str();
}

// hash_file

std::string hash_file(const std::filesystem::path& path) { // Use the path object
    // ifstream handles the Unicode path internally now
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        return {};
    }

    std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return {};
    }

    return hash_buffer(buffer.data(), buffer.size());
}

// compare

int compare(const std::string& hash1, const std::string& hash2) {
    // Parse "block_size:digest1:digest2"
    auto parse = [](const std::string& h,
                    uint32_t& bs, std::string& d1, std::string& d2) -> bool {
        auto c1 = h.find(':');
        if (c1 == std::string::npos) return false;
        auto c2 = h.find(':', c1 + 1);
        if (c2 == std::string::npos) return false;
        try {
            bs = static_cast<uint32_t>(std::stoul(h.substr(0, c1)));
        } catch (...) { return false; }
        d1 = h.substr(c1 + 1, c2 - c1 - 1);
        d2 = h.substr(c2 + 1);
        return true;
    };

    uint32_t bs1, bs2;
    std::string d1a, d1b, d2a, d2b;

    if (!parse(hash1, bs1, d1a, d1b)) return -1;
    if (!parse(hash2, bs2, d2a, d2b)) return -1;

    // Block sizes must match (or be adjacent powers of two)
    if (bs1 != bs2 && bs1 != bs2 * 2 && bs2 != bs1 * 2) return 0;

    // Apply run-length reduction before comparison
    std::string s1, s2;
    int score = 0;

    if (bs1 == bs2) {
        // Compare primary digests
        s1 = reduce_runs(d1a);
        s2 = reduce_runs(d2a);
        int ed = edit_distance(s1, s2);
        int maxLen = static_cast<int>(std::max(s1.size(), s2.size()));
        if (maxLen > 0) {
            score = std::max(score,
                100 - (100 * ed) / maxLen);
        }

        // Also compare secondary digests
        s1 = reduce_runs(d1b);
        s2 = reduce_runs(d2b);
        ed = edit_distance(s1, s2);
        maxLen = static_cast<int>(std::max(s1.size(), s2.size()));
        if (maxLen > 0) {
            score = std::max(score,
                100 - (100 * ed) / maxLen);
        }
    } else if (bs1 == bs2 * 2) {
        // hash1's block_size is double hash2's — compare cross digests
        s1 = reduce_runs(d1a);
        s2 = reduce_runs(d2b);
        int ed = edit_distance(s1, s2);
        int maxLen = static_cast<int>(std::max(s1.size(), s2.size()));
        if (maxLen > 0) {
            score = 100 - (100 * ed) / maxLen;
        }
    } else {
        // hash2's block_size is double hash1's
        s1 = reduce_runs(d1b);
        s2 = reduce_runs(d2a);
        int ed = edit_distance(s1, s2);
        int maxLen = static_cast<int>(std::max(s1.size(), s2.size()));
        if (maxLen > 0) {
            score = 100 - (100 * ed) / maxLen;
        }
    }

    return std::clamp(score, 0, 100);
}

#pragma endregion

} // namespace ctph
