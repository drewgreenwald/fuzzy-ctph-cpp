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

#include <argparse/argparse.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <shared_mutex>
#include <execution>
#include <algorithm>
#include <fstream>
#include <iomanip>

#include "fuzzy.h" 

namespace fs = std::filesystem;

struct FileEntry {
    fs::path path;
    std::string fuzzy_hash;
};

struct FileClusterer {
    std::vector<int> parent;
    std::vector<int> rank;
    mutable std::shared_mutex mtx;

    explicit FileClusterer(int n) : parent(n), rank(n, 0) {
        for (int i = 0; i < n; ++i) parent[i] = i;
    }

    int find(int x) {
        if (parent[x] == x) return x;
        return parent[x] = find(parent[x]);
    }

    void join(int a, int b) {
        std::unique_lock lock(mtx);
        a = find(a);
        b = find(b);
        if (a != b) {
            if (rank[a] < rank[b]) parent[a] = b;
            else if (rank[a] > rank[b]) parent[b] = a;
            else { parent[b] = a; rank[a]++; }
        }
    }

    int getGroupId(int x) {
        std::shared_lock lock(mtx);
        return find(x);
    }
};

struct Args {
    enum class Mode { Single, Compare, Cluster };
    Mode mode = Mode::Single;
    std::string path1;
    std::string path2;
    int threshold = 50;
    std::string csv_path;
};


static Args parse_args(int argc, char* argv[]) {
    argparse::ArgumentParser program("fuzzy-ctph", "2.1");

    program.add_argument("-t", "--threshold")
        .help("Similarity threshold (0-100)")
        .default_value(50).scan<'i', int>();

    program.add_argument("-c", "--csv")
        .help("Output cluster results to CSV file")
        .default_value(std::string(""));

    program.add_argument("paths")
        .help("Input: [file] for hash, [file1 file2] for compare, [dir] for cluster")
        .remaining();

    try {
        program.parse_args(argc, argv);
    }
    catch (const std::exception& err) {
        std::cerr << "Error: " << err.what() << "\n" << program;
        std::exit(1);
    }

    Args args;
    args.threshold = program.get<int>("--threshold");
    args.csv_path = program.get<std::string>("--csv");

    std::vector<std::string> paths;
    try {
        paths = program.get<std::vector<std::string>>("paths");
    }
    catch (const std::logic_error&) {
        std::cerr << "Error: No paths provided.\n" << program;
        std::exit(1);
    }

    args.path1 = paths[0];
    if (paths.size() == 1) {
        args.mode = fs::is_directory(args.path1) ? Args::Mode::Cluster : Args::Mode::Single;
    }
    else {
        args.path2 = paths[1];
        args.mode = Args::Mode::Compare;
    }
    return args;
}


void mode_single(const fs::path& filepath) {
    std::string hash = ctph::hash_file(filepath);
    if (!hash.empty()) {
        std::cout << hash << "  \"" << filepath.filename().string() << "\"\n";
    }
    else {
        std::cerr << "Error hashing file: " << filepath.string() << "\n";
    }
}

void mode_compare(const fs::path& p1, const fs::path& p2) {
    std::string h1 = ctph::hash_file(p1);
    std::string h2 = ctph::hash_file(p2);

    if (h1.empty() || h2.empty()) {
        std::cerr << "Error processing files for comparison.\n";
        return;
    }

    int score = ctph::compare(h1, h2);
    std::cout << "Similarity: " << score << "% (" << p1.filename().string()
        << " <-> " << p2.filename().string() << ")\n";
}

void mode_cluster(const fs::path& dir, int threshold, const std::string& csv_path) {
    std::vector<FileEntry> files;
    std::cout << "[1/4] Scanning and hashing files in: " << dir.string() << "...\n";

    for (auto const& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string hash = ctph::hash_file(entry.path());
            if (!hash.empty()) {
                files.push_back({ entry.path(), hash });
            }
        }
    }

    if (files.empty()) {
        std::cout << "No files found to process.\n";
        return;
    }

    std::cout << "[2/4] Building Inverted Index (Blocking)...\n";

    const size_t SHINGLE_SIZE = ctph::ROLLING_WINDOW;
    std::unordered_map<std::string, std::vector<int>> index;

    for (int i = 0; i < (int)files.size(); ++i) {
        const std::string& h = files[i].fuzzy_hash;
        if (h.length() < SHINGLE_SIZE) continue;
        for (size_t j = 0; j + SHINGLE_SIZE <= h.length(); ++j) {
            index[h.substr(j, SHINGLE_SIZE)].push_back(i);
        }
    }

    FileClusterer clusterer((int)files.size());
    std::vector<std::string> shingles;
    for (auto const& [key, _] : index) shingles.push_back(key);

    std::cout << "[3/4] Parallel clustering across " << shingles.size() << " buckets...\n";
    std::for_each(std::execution::par, shingles.begin(), shingles.end(), [&](const std::string& s) {
        const auto& bucket = index[s];
        for (size_t i = 0; i < bucket.size(); ++i) {
            for (size_t j = i + 1; j < bucket.size(); ++j) {
                int u = bucket[i];
                int v = bucket[j];

                // Fast length filter
                size_t len1 = files[u].fuzzy_hash.length();
                size_t len2 = files[v].fuzzy_hash.length();
                if (len1 > len2 * 3 || len2 > len1 * 3) continue;

                if (clusterer.getGroupId(u) != clusterer.getGroupId(v)) {
                    int score = ctph::compare(files[u].fuzzy_hash, files[v].fuzzy_hash);
                    if (score >= threshold) {
                        clusterer.join(u, v);
                    }
                }
            }
        }
        });

    std::cout << "[4/4] Grouping results...\n";
    std::map<int, std::vector<int>> clusters;
    for (int i = 0; i < (int)files.size(); ++i) {
        clusters[clusterer.getGroupId(i)].push_back(i);
    }

    int clusterCount = 0;
    int loneFiles = 0;

    std::cout << "\n" << std::string(60, '=') << "\n";
    std::cout << std::left << std::setw(30) << "CLUSTER SUMMARY" << "\n";
    std::cout << std::string(60, '=') << "\n";

    for (auto const& [rootId, members] : clusters) {
        if (members.size() > 1) {
            clusterCount++;
            std::cout << "\n[" << clusterCount << "] Cluster (Root: "
                << files[rootId].path.filename().string() << ")\n";
            std::cout << "    Size: " << members.size() << " files\n";

            for (int id : members) {
                std::cout << "    - " << files[id].path.string() << "\n";
            }
        }
        else {
            loneFiles++;
        }
    }

    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Total Clusters Found: " << clusterCount << "\n";
    std::cout << "Unique Files (Unclustered): " << loneFiles << "\n";
    std::cout << std::string(60, '=') << "\n";

    if (!csv_path.empty()) {
        std::ofstream csv(csv_path);
        csv << "ClusterID,FilePath,FuzzyHash\n";
        for (auto const& [rootId, members] : clusters) {
            for (int id : members) {
                csv << rootId << ",\"" << files[id].path.string() << "\",\"" << files[id].fuzzy_hash << "\"\n";
            }
        }
        std::cout << "Results exported to: " << csv_path << "\n";
    }
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);

    try {
        switch (args.mode) {
        case Args::Mode::Single:
            mode_single(args.path1);
            break;
        case Args::Mode::Compare:
            mode_compare(args.path1, args.path2);
            break;
        case Args::Mode::Cluster:
            mode_cluster(args.path1, args.threshold, args.csv_path);
            break;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Runtime Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}