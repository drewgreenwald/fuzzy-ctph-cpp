// fuzzyhash.cpp - Multi-mode CTPH fuzzy hash utility
//
// Modes:
//   fuzzyhash <file>                          Hash a single file
//   fuzzyhash <file1> <file2>                 Compare two files, print similarity %
//   fuzzyhash <directory> [--threshold N]     Cluster files by similarity
//                                             N is 0-100, default 50
//   fuzzyhash <directory> [--threshold N] [--csv <output.csv>]
//
// MIT License — see fuzzy.h for full license text.

#include "fuzzy.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

struct Args {
    enum class Mode { Single, Compare, Cluster };

    Mode        mode      = Mode::Single;
    std::string path1;
    std::string path2;
    int         threshold = 50;
    std::string csv_path;
};

static void print_usage(const char* prog) {
    std::cerr
        << "Usage:\n"
        << "  " << prog << " <file>                         Hash a single file\n"
        << "  " << prog << " <file1> <file2>                Compare two files (0-100 score)\n"
        << "  " << prog << " <directory> [options]          Cluster files by similarity\n"
        << "\nCluster options:\n"
        << "  --threshold N     Similarity threshold 0-100 (default: 50)\n"
        << "  --csv <file>      Also write results to a CSV file\n";
}

static Args parse_args(int argc, char* argv[]) {
    Args args;

    if (argc < 2) {
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        std::string a(argv[i]);
        if (a == "--threshold" && i + 1 < argc) {
            try {
                args.threshold = std::stoi(argv[++i]);
                if (args.threshold < 0 || args.threshold > 100)
                    throw std::out_of_range("threshold");
            } catch (...) {
                std::cerr << "Error: --threshold must be an integer 0-100\n";
                std::exit(EXIT_FAILURE);
            }
        } else if (a == "--csv" && i + 1 < argc) {
            args.csv_path = argv[++i];
        } else if (a.rfind("--", 0) == 0) {
            std::cerr << "Unknown option: " << a << '\n';
            print_usage(argv[0]);
            std::exit(EXIT_FAILURE);
        } else {
            positional.push_back(a);
        }
    }

    if (positional.empty()) {
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    args.path1 = positional[0];

    if (positional.size() == 1) {
        args.mode = fs::is_directory(args.path1)
                    ? Args::Mode::Cluster
                    : Args::Mode::Single;
    } else if (positional.size() == 2) {
        args.path2 = positional[1];
        args.mode  = Args::Mode::Compare;
    } else {
        std::cerr << "Error: too many positional arguments\n";
        print_usage(argv[0]);
        std::exit(EXIT_FAILURE);
    }

    return args;
}

// ---------------------------------------------------------------------------
// Mode: single file hash
// ---------------------------------------------------------------------------

static int mode_single(const std::string& path) {
    std::string hash = ctph::hash_file(path);
    if (hash.empty()) {
        std::cerr << "Error: could not hash file: " << path << '\n';
        return EXIT_FAILURE;
    }
    std::cout << hash << '\n';
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// Mode: compare two files
// ---------------------------------------------------------------------------

static int mode_compare(const std::string& path1, const std::string& path2) {
    std::string h1 = ctph::hash_file(path1);
    std::string h2 = ctph::hash_file(path2);

    if (h1.empty()) {
        std::cerr << "Error: could not hash file: " << path1 << '\n';
        return EXIT_FAILURE;
    }
    if (h2.empty()) {
        std::cerr << "Error: could not hash file: " << path2 << '\n';
        return EXIT_FAILURE;
    }

    int score = ctph::compare(h1, h2);
    if (score < 0) {
        std::cerr << "Error: could not compare hashes (incompatible format)\n";
        return EXIT_FAILURE;
    }

    std::cout << score << "% similar\n"
              << "  file1: " << path1 << '\n'
              << "  file2: " << path2 << '\n'
              << "  hash1: " << h1    << '\n'
              << "  hash2: " << h2    << '\n';
    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// Mode: cluster directory
// ---------------------------------------------------------------------------

struct FileEntry {
    std::string path;
    std::string hash;
};

// Simple union-find for single-linkage clustering
struct UnionFind {
    std::vector<int> parent;
    explicit UnionFind(int n) : parent(n) {
        for (int i = 0; i < n; ++i) parent[i] = i;
    }
    int find(int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    }
    void unite(int a, int b) {
        a = find(a); b = find(b);
        if (a != b) parent[b] = a;
    }
};

static int mode_cluster(const std::string& dir_path, int threshold,
                        const std::string& csv_path) {
    // Collect and hash all regular files
    std::vector<FileEntry> files;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
            if (!entry.is_regular_file()) continue;
            FileEntry fe;
            fe.path = entry.path().string();
            fe.hash = ctph::hash_file(fe.path);
            if (fe.hash.empty()) {
                std::cerr << "Warning: skipping unreadable/empty file: "
                          << fe.path << '\n';
                continue;
            }
            files.push_back(std::move(fe));
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error reading directory: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

    if (files.empty()) {
        std::cerr << "No hashable files found in: " << dir_path << '\n';
        return EXIT_FAILURE;
    }

    const int n = static_cast<int>(files.size());
    std::cout << "Hashed " << n << " file(s). Computing pairwise similarities...\n\n";

    // All-pairs comparison (O(n^2) — fine for hundreds of files)
    UnionFind uf(n);
    struct Pair { int i, j, score; };
    std::vector<Pair> above_threshold;

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            int score = ctph::compare(files[i].hash, files[j].hash);
            if (score >= threshold) {
                uf.unite(i, j);
                above_threshold.push_back({i, j, score});
            }
        }
    }

    // Sort pairs descending by score for nicer output
    std::sort(above_threshold.begin(), above_threshold.end(),
              [](const Pair& a, const Pair& b){ return a.score > b.score; });

    // Group files by cluster root
    std::map<int, std::vector<int>> cluster_map;
    for (int i = 0; i < n; ++i)
        cluster_map[uf.find(i)].push_back(i);

    std::vector<std::vector<int>> clusters, singletons;
    for (auto& [root, members] : cluster_map) {
        if (members.size() > 1) clusters.push_back(members);
        else                    singletons.push_back(members);
    }

    // Sort clusters largest-first
    std::sort(clusters.begin(), clusters.end(),
              [](const auto& a, const auto& b){ return a.size() > b.size(); });

    // --------------- stdout output ----------------------------------------
    const std::string bar(60, '-');
    std::cout << bar << '\n'
              << "SIMILARITY CLUSTERS  (threshold: " << threshold << "%)\n"
              << bar << '\n';

    if (clusters.empty()) {
        std::cout << "No files exceeded the " << threshold
                  << "% similarity threshold.\n\n";
    } else {
        int cid = 1;
        for (const auto& members : clusters) {
            int root = uf.find(members[0]);
            std::cout << "Cluster " << cid++ << "  (" << members.size() << " files):\n";
            for (int idx : members)
                std::cout << "  " << files[idx].path << '\n';

            std::cout << "  Pairs above threshold:\n";
            for (const auto& p : above_threshold) {
                if (uf.find(p.i) == root && uf.find(p.j) == root) {
                    std::cout << "    " << std::setw(3) << p.score << "%  "
                              << fs::path(files[p.i].path).filename().string()
                              << "  <->  "
                              << fs::path(files[p.j].path).filename().string()
                              << '\n';
                }
            }
            std::cout << '\n';
        }
    }

    std::cout << singletons.size()
              << " file(s) had no similar matches above the threshold.\n";

    // --------------- CSV output -------------------------------------------
    if (!csv_path.empty()) {
        std::ofstream csv(csv_path);
        if (!csv.is_open()) {
            std::cerr << "Warning: could not write CSV to: " << csv_path << '\n';
        } else {
            csv << "file,cluster_id,hash\n";
            int cid = 1;
            for (const auto& members : clusters) {
                for (int idx : members)
                    csv << '"' << files[idx].path << '"'
                        << ',' << cid
                        << ",\"" << files[idx].hash << "\"\n";
                ++cid;
            }
            for (const auto& members : singletons) {
                int idx = members[0];
                csv << '"' << files[idx].path << '"'
                    << ",0"
                    << ",\"" << files[idx].hash << "\"\n";
            }
            std::cout << "\nCSV written to: " << csv_path << '\n';
        }
    }

    return EXIT_SUCCESS;
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    const Args args = parse_args(argc, argv);
    switch (args.mode) {
        case Args::Mode::Single:  return mode_single(args.path1);
        case Args::Mode::Compare: return mode_compare(args.path1, args.path2);
        case Args::Mode::Cluster: return mode_cluster(args.path1, args.threshold,
                                                      args.csv_path);
    }
    return EXIT_SUCCESS;
}
