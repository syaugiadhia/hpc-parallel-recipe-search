#include "common/Cli.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace alchemy {
namespace {

std::string requireValue(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for " + option);
    }
    ++index;
    return argv[index];
}

int parsePositiveInt(const std::string& value, const std::string& option) {
    try {
        const int parsed = std::stoi(value);
        if (parsed < 0) {
            throw std::runtime_error("");
        }
        return parsed;
    } catch (...) {
        throw std::runtime_error("Invalid " + option + " value '" + value + "': expected a non-negative integer");
    }
}

double parseDouble(const std::string& value, const std::string& option) {
    try {
        return std::stod(value);
    } catch (...) {
        throw std::runtime_error("Invalid " + option + " value '" + value + "': expected a number");
    }
}

std::string trim(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

}  // namespace

AppOptions parseArgs(int argc, char** argv, bool mpiMode) {
    AppOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--data") {
            options.dataPath = requireValue(i, argc, argv, arg);
        } else if (arg == "--tiers") {
            options.tiersPath = requireValue(i, argc, argv, arg);
        } else if (arg == "--target") {
            options.target = requireValue(i, argc, argv, arg);
        } else if (arg == "--benchmark") {
            options.benchmarkPath = requireValue(i, argc, argv, arg);
        } else if (arg == "--list-elements") {
            options.listElements = true;
        } else if (arg == "--tier") {
            options.tierFilter = requireValue(i, argc, argv, arg);
        } else if (arg == "--filter") {
            options.nameFilter = requireValue(i, argc, argv, arg);
        } else if (arg == "--algorithm") {
            options.algorithm = parseAlgorithm(requireValue(i, argc, argv, arg));
        } else if (arg == "--mode") {
            options.mode = parseSearchMode(requireValue(i, argc, argv, arg));
        } else if (arg == "--limit") {
            options.limit = std::max(1, parsePositiveInt(requireValue(i, argc, argv, arg), arg));
        } else if (arg == "--trace-mode") {
            options.traceMode = parseTraceMode(requireValue(i, argc, argv, arg));
        } else if (arg == "--visual-mode") {
            options.visualMode = parseVisualMode(requireValue(i, argc, argv, arg));
        } else if (arg == "--output") {
            options.outputPrefix = requireValue(i, argc, argv, arg);
        } else if (arg == "--max-visual-depth") {
            options.maxVisualDepth = parsePositiveInt(requireValue(i, argc, argv, arg), arg);
        } else if (arg == "--format") {
            options.imageFormat = requireValue(i, argc, argv, arg);
            if (options.imageFormat != "png" && options.imageFormat != "svg") {
                throw std::runtime_error("Invalid --format value '" + options.imageFormat + "'. Expected png or svg.");
            }
        } else if (arg == "--split-depth") {
            if (!mpiMode) {
                throw std::runtime_error("--split-depth is only supported by alchemy_mpi");
            }
            options.splitDepth = parsePositiveInt(requireValue(i, argc, argv, arg), arg);
        } else if (arg == "--baseline-ms") {
            if (!mpiMode) {
                throw std::runtime_error("--baseline-ms is only supported by alchemy_mpi");
            }
            options.baselineMs = parseDouble(requireValue(i, argc, argv, arg), arg);
        } else {
            throw std::runtime_error("Unknown argument: " + arg);
        }
    }

    if (options.help) {
        return options;
    }
    if (!options.listElements && options.target.empty() && options.benchmarkPath.empty()) {
        throw std::runtime_error("Either --target or --benchmark is required");
    }
    if (options.mode == SearchMode::All) {
        options.progress = true;
    }
    return options;
}

std::string usageText(bool mpiMode) {
    std::ostringstream out;
    out << "Usage: " << (mpiMode ? "mpirun -np N ./alchemy_mpi" : "./alchemy_serial") << " [options]\n\n"
        << "Required for normal run:\n"
        << "  --data PATH --target NAME --algorithm bfs|dfs --mode single|multiple|all\n"
        << "  --limit N --trace-mode full|memo --visual-mode full|shared --output PREFIX\n\n"
        << "Benchmark:\n"
        << "  --benchmark benchmarks/targets.txt --data data/recipes.json --output results/bench\n\n"
        << "Element listing:\n"
        << "  --list-elements --tier all|starter|special|tier1..tier15 [--filter TEXT]\n\n"
        << "Common options:\n"
        << "  --tiers PATH             Tier catalog JSON, default data/tiers.json.\n"
        << "  --max-visual-depth N     Optional visualization depth cap.\n"
        << "  --format png|svg         Rendered image format, default png.\n";
    if (mpiMode) {
        out << "MPI options:\n"
            << "  --split-depth N          Task expansion depth, default 1.\n"
            << "  --baseline-ms X          Serial baseline time for speedup/efficiency.\n";
    }
    return out.str();
}

std::vector<std::string> readBenchmarkTargets(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open benchmark targets file: " + path);
    }

    std::vector<std::string> targets;
    std::string line;
    while (std::getline(input, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (!line.empty()) {
            targets.push_back(line);
        }
    }
    return targets;
}

std::string sanitizeForPath(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        } else if (ch == '-' || ch == '_') {
            out.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "target";
    }
    return out;
}

void ensureParentDirectory(const std::string& filePrefix) {
    const std::filesystem::path path(filePrefix);
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

}  // namespace alchemy
