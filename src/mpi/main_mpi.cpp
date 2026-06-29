#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include <mpi.h>
#ifdef ALCHEMY_HAS_OPENMP
#include <omp.h>
#endif

#include "common/Cli.hpp"
#include "common/JsonLoader.hpp"
#include "common/Search.hpp"
#include "common/Statistics.hpp"
#include "common/TierCatalog.hpp"
#include "mpi/MpiMaster.hpp"
#include "mpi/MpiWorker.hpp"

namespace {

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\"\"";
        } else {
            out.push_back(ch);
        }
    }
    out += "\"";
    return out;
}

std::string renderSkipReason(const alchemy::OutputFiles& outputs) {
    return outputs.renderWarning.empty() ? "not rendered" : outputs.renderWarning;
}

int parsePositiveToken(const std::string& value, const std::string& context) {
    try {
        const int parsed = std::stoi(value);
        if (parsed < 1) {
            throw std::runtime_error("");
        }
        return parsed;
    } catch (...) {
        throw std::runtime_error("Invalid " + context + " value '" + value + "': expected a positive integer");
    }
}

std::vector<int> expandThreadProfile(const std::string& profile, int worldSize) {
    std::vector<int> threadsByRank;
    std::stringstream stream(profile);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }), token.end());
        if (token.empty()) {
            continue;
        }

        const auto marker = token.find_first_of("xX");
        if (marker == std::string::npos) {
            throw std::runtime_error("Invalid --thread-profile token '" + token + "'. Expected processesxthreads, e.g. 2x4.");
        }
        const int processes = parsePositiveToken(token.substr(0, marker), "--thread-profile process count");
        const int threads = parsePositiveToken(token.substr(marker + 1), "--thread-profile thread count");
        threadsByRank.insert(threadsByRank.end(), static_cast<std::size_t>(processes), threads);
    }

    if (static_cast<int>(threadsByRank.size()) != worldSize) {
        throw std::runtime_error(
            "--thread-profile expands to " + std::to_string(threadsByRank.size()) +
            " MPI ranks, but mpiexec launched " + std::to_string(worldSize) + " ranks");
    }
    return threadsByRank;
}

void configureRankThreads(alchemy::AppOptions& options, int rank, int worldSize) {
    if (!options.threadProfile.empty()) {
        options.threadsByRank = expandThreadProfile(options.threadProfile, worldSize);
        options.threads = options.threadsByRank[static_cast<std::size_t>(rank)];
    } else {
        options.threads = std::max(1, options.threads);
        options.threadsByRank.assign(static_cast<std::size_t>(worldSize), options.threads);
    }

    if (options.threads > 1 && !alchemy::openmpAvailable()) {
        throw std::runtime_error("This alchemy_mpi build does not include OpenMP support, so --threads > 1 is unavailable");
    }
    options.useOpenmp = options.threads > 1;
#ifdef ALCHEMY_HAS_OPENMP
    if (options.useOpenmp) {
        omp_set_num_threads(options.threads);
    }
#endif
}

std::string intVectorToString(const std::vector<int>& values) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << values[i];
    }
    out << "]";
    return out.str();
}

void broadcastString(std::string& value, int rank) {
    int length = rank == 0 ? static_cast<int>(value.size()) : 0;
    MPI_Bcast(&length, 1, MPI_INT, 0, MPI_COMM_WORLD);
    value.resize(static_cast<std::size_t>(length));
    MPI_Bcast(value.empty() ? nullptr : value.data(), length, MPI_CHAR, 0, MPI_COMM_WORLD);
}

std::vector<std::string> collectRankHostnames(int rank, int worldSize) {
    std::array<char, MPI_MAX_PROCESSOR_NAME> localName{};
    int nameLength = 0;
    MPI_Get_processor_name(localName.data(), &nameLength);

    std::vector<char> gathered;
    if (rank == 0) {
        gathered.resize(static_cast<std::size_t>(worldSize) * MPI_MAX_PROCESSOR_NAME, '\0');
    }
    MPI_Gather(localName.data(),
               MPI_MAX_PROCESSOR_NAME,
               MPI_CHAR,
               rank == 0 ? gathered.data() : nullptr,
               MPI_MAX_PROCESSOR_NAME,
               MPI_CHAR,
               0,
               MPI_COMM_WORLD);

    std::vector<std::string> hostnames;
    if (rank == 0) {
        hostnames.reserve(static_cast<std::size_t>(worldSize));
        for (int i = 0; i < worldSize; ++i) {
            const char* start = gathered.data() + (static_cast<std::size_t>(i) * MPI_MAX_PROCESSOR_NAME);
            hostnames.emplace_back(start);
        }
    }
    return hostnames;
}

void printSummary(const alchemy::AppOptions& options, const alchemy::MpiRunResult& run) {
    const auto& result = run.search;
    const auto& outputs = run.outputs;
    std::cout << "Target: " << result.target << "\n";
    std::cout << "Algorithm: " << alchemy::toString(options.algorithm) << "\n";
    std::cout << "Mode: " << alchemy::toString(options.mode) << "\n";
    std::cout << "Trace mode: " << alchemy::toString(options.traceMode) << "\n";
    std::cout << "Visual mode: " << alchemy::toString(options.visualMode) << "\n";
    std::cout << "Render mode: " << alchemy::toString(options.renderMode) << "\n";
    std::cout << "Processes: " << result.stats.processes << "\n";
    if (!result.stats.threadsByRank.empty()) {
        std::cout << "Threads by rank: " << intVectorToString(result.stats.threadsByRank) << "\n";
    } else {
        std::cout << "Threads per rank: " << result.stats.threadsPerProcess << "\n";
    }
    std::cout << "Total workers: " << result.stats.totalWorkers << "\n";
    if (!result.stats.rankHostnames.empty()) {
        std::cout << "Rank hostnames: " << alchemy::stringVectorToString(result.stats.rankHostnames) << "\n";
    }
    std::cout << "Recipes requested: "
              << (options.mode == alchemy::SearchMode::All ? std::string("all unique direct recipes with shortest subtrees") : std::to_string(options.limit))
              << "\n";
    if (options.mode == alchemy::SearchMode::All) {
        std::cout << "Unique direct recipes available: " << result.stats.directRecipesAvailable << "\n";
    }
    std::cout << "Recipes found: " << result.recipes.size() << "\n";
    std::cout << "Total time: " << std::fixed << std::setprecision(3) << result.stats.timeMs << " ms\n";
    std::cout << "Communication time: " << std::fixed << std::setprecision(3) << result.stats.communicationMs << " ms\n";
    std::cout << "Nodes visited total: " << result.stats.nodesVisited << "\n";
    std::cout << "Nodes visited per rank: " << alchemy::rankVectorToString(result.stats.nodesVisitedByRank) << "\n";
    std::cout << "Tasks processed total: " << result.stats.tasksProcessed << "\n";
    std::cout << "Tasks processed per rank: " << alchemy::rankVectorToString(result.stats.tasksProcessedByRank) << "\n";
    std::cout << "Cache hits total: " << result.stats.cacheHits << "\n";
    std::cout << "Cache hits per rank: " << alchemy::rankVectorToString(result.stats.cacheHitsByRank) << "\n";
    std::cout << "Cache entries total: " << result.stats.cacheEntries << "\n";
    if (result.stats.speedup > 0.0) {
        std::cout << "Speedup: " << std::fixed << std::setprecision(3) << result.stats.speedup << "\n";
        std::cout << "Efficiency: " << std::fixed << std::setprecision(3) << result.stats.efficiency << "\n";
    }
    std::cout << "Output JSON: " << outputs.jsonPath << "\n";
    if (!outputs.dotPath.empty()) {
        std::cout << "Output DOT: " << outputs.dotPath << "\n";
    } else {
        std::cout << "Output DOT: skipped (" << renderSkipReason(outputs) << ")\n";
    }
    if (outputs.imageRendered) {
        std::cout << "Output " << options.imageFormat << ": " << outputs.imagePath << "\n";
    } else {
        std::cout << "Output image: skipped (" << renderSkipReason(outputs) << ")\n";
    }

    for (std::size_t i = 0; i < result.recipes.size(); ++i) {
        std::cout << "\nRecipe " << (i + 1) << ":\n";
        std::cout << alchemy::treeToAscii(result.recipes[i]);
    }
}

void writeBenchmarkRow(std::ofstream& csv,
                       const alchemy::AppOptions& options,
                       const alchemy::MpiRunResult& run) {
    const auto& result = run.search;
    const auto& outputs = run.outputs;
    csv << csvEscape(result.target) << ","
        << alchemy::toString(options.algorithm) << ","
        << alchemy::toString(options.mode) << ","
        << alchemy::toString(options.traceMode) << ","
        << alchemy::toString(options.visualMode) << ","
        << result.stats.processes << ","
        << result.recipes.size() << ","
        << result.stats.nodesVisited << ","
        << result.stats.cacheHits << ","
        << result.stats.cacheEntries << ","
        << std::fixed << std::setprecision(3) << result.stats.timeMs << ","
        << result.stats.tasksProcessed << ","
        << std::fixed << std::setprecision(3) << result.stats.communicationMs << ","
        << csvEscape(outputs.dotPath) << ","
        << csvEscape(outputs.imagePath) << "\n";
}

void sendStr(int dest, int tag, const std::string& s) {
    MPI_Send(s.empty() ? nullptr : s.data(), static_cast<int>(s.size()), MPI_CHAR, dest, tag, MPI_COMM_WORLD);
}

std::string recvStr(const MPI_Status& st) {
    int count = 0;
    MPI_Get_count(&st, MPI_CHAR, &count);
    std::string s(static_cast<std::size_t>(std::max(0, count)), '\0');
    MPI_Recv(s.empty() ? nullptr : s.data(), count, MPI_CHAR, st.MPI_SOURCE, st.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    return s;
}

std::string benchmarkRowString(const alchemy::AppOptions& options, const std::string& target,
                               const alchemy::SearchStats& stats, std::size_t recipeCount,
                               const std::string& dotPath, const std::string& imagePath) {
    std::ostringstream r;
    r << csvEscape(target) << "," << alchemy::toString(options.algorithm) << ","
      << alchemy::toString(options.mode) << "," << alchemy::toString(options.traceMode) << ","
      << alchemy::toString(options.visualMode) << "," << stats.processes << ","
      << recipeCount << "," << stats.nodesVisited << "," << stats.cacheHits << ","
      << stats.cacheEntries << "," << std::fixed << std::setprecision(3) << stats.timeMs << ","
      << stats.tasksProcessed << "," << std::fixed << std::setprecision(3) << stats.communicationMs << ","
      << csvEscape(dotPath) << "," << csvEscape(imagePath) << "\n";
    return r.str();
}

// Benchmark MPI: distribusi TARGET utuh antar-rank (data-parallel; tiap target independen ->
// embarrassingly parallel, comm minim = cuma string baris CSV). Ini menggantikan distribusi
// per-sub-task lama yang comm-bound. Master (rank 0) membagi target, worker mengerjakan
// pencarian SERIAL penuh per target & mengirim balik baris hasil.
int runBenchmark(alchemy::AppOptions options,
                 const alchemy::RecipeGraph& graph,
                 int rank,
                 int worldSize,
                 const std::vector<std::string>& /*rankHostnames*/) {
    std::vector<std::string> targets;
    if (rank == 0) {
        targets = alchemy::readBenchmarkTargets(options.benchmarkPath);
    }
    int targetCount = rank == 0 ? static_cast<int>(targets.size()) : 0;
    MPI_Bcast(&targetCount, 1, MPI_INT, 0, MPI_COMM_WORLD);

    const std::string baseOutput = options.outputPrefix;
    const std::string header =
        "target,algorithm,mode,trace_mode,visual_mode,processes,recipes_found,nodes_visited,"
        "cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,output_image\n";

    auto runOne = [&](const std::string& target) -> std::string {
        auto ro = options;
        ro.progress = false;
        ro.target = target;
        ro.outputPrefix = baseOutput + "_" + alchemy::sanitizeForPath(target) + "_np" + std::to_string(worldSize);
        alchemy::ensureParentDirectory(ro.outputPrefix);
        alchemy::SearchEngine engine(graph, ro);
        auto result = engine.search(target);
        result.stats.processes = worldSize;
        auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, ro);
        return benchmarkRowString(options, target, result.stats, result.recipes.size(), outputs.dotPath, outputs.imagePath);
    };

    // worldSize==1: tak ada worker -> rank 0 kerjakan semua serial.
    if (worldSize <= 1) {
        alchemy::ensureParentDirectory(baseOutput);
        std::ofstream csv(baseOutput + ".csv");
        if (!csv) throw std::runtime_error("Cannot write benchmark CSV: " + baseOutput + ".csv");
        csv << header;
        for (int i = 0; i < targetCount; ++i) csv << runOne(targets[static_cast<std::size_t>(i)]);
        std::cout << "Benchmark CSV: " << baseOutput << ".csv\n";
        return 0;
    }

    if (rank == 0) {
        alchemy::ensureParentDirectory(baseOutput);
        std::ofstream csv(baseOutput + ".csv");
        if (!csv) throw std::runtime_error("Cannot write benchmark CSV: " + baseOutput + ".csv");
        csv << header;
        std::vector<std::string> rows(static_cast<std::size_t>(targetCount));
        int nextTarget = 0;
        int activeWorkers = worldSize - 1;
        const double t0 = MPI_Wtime();
        while (activeWorkers > 0) {
            MPI_Status st;
            MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == alchemy::MPI_TAG_REQUEST) {
                recvStr(st);  // request kosong
                if (nextTarget < targetCount) {
                    sendStr(st.MPI_SOURCE, alchemy::MPI_TAG_TASK,
                            std::to_string(nextTarget) + " " + targets[static_cast<std::size_t>(nextTarget)]);
                    ++nextTarget;
                } else {
                    sendStr(st.MPI_SOURCE, alchemy::MPI_TAG_STOP, "");
                    --activeWorkers;
                }
            } else if (st.MPI_TAG == alchemy::MPI_TAG_RESULT) {
                const std::string payload = recvStr(st);  // "idx|baris"
                const auto bar = payload.find('|');
                const int idx = std::stoi(payload.substr(0, bar));
                rows[static_cast<std::size_t>(idx)] = payload.substr(bar + 1);
            }
        }
        const double wall = (MPI_Wtime() - t0) * 1000.0;
        for (const auto& row : rows) {
            if (!row.empty()) csv << row;
        }
        std::cout << "MPI benchmark total wall time: " << std::fixed << std::setprecision(3)
                  << wall << " ms (ranks=" << worldSize << ", targets=" << targetCount << ")\n";
        std::cout << "Benchmark CSV: " << baseOutput << ".csv\n";
    } else {
        while (true) {
            sendStr(0, alchemy::MPI_TAG_REQUEST, "");
            MPI_Status st;
            MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == alchemy::MPI_TAG_STOP) {
                recvStr(st);
                break;
            }
            const std::string taskMsg = recvStr(st);  // "idx target"
            const auto sp = taskMsg.find(' ');
            const int idx = std::stoi(taskMsg.substr(0, sp));
            const std::string target = taskMsg.substr(sp + 1);
            const std::string row = runOne(target);
            sendStr(0, alchemy::MPI_TAG_RESULT, std::to_string(idx) + "|" + row);
        }
    }
    return 0;
}

int runListElements(const alchemy::AppOptions& options, const alchemy::TierCatalog& tiers, int rank) {
    if (rank != 0) {
        return 0;
    }
    const auto elements = tiers.filterElements(options.tierFilter, options.nameFilter);
    std::cout << "Tier: " << options.tierFilter << "\n";
    if (!options.nameFilter.empty()) {
        std::cout << "Filter: " << options.nameFilter << "\n";
    }
    std::cout << "Elements found: " << elements.size() << "\n";
    for (const auto& element : elements) {
        std::cout << element << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank = 0;
    int worldSize = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);
    const auto rankHostnames = collectRankHostnames(rank, worldSize);

    try {
        auto options = alchemy::parseArgs(argc, argv, true);
        if (options.help) {
            if (rank == 0) {
                std::cout << alchemy::usageText(true);
            }
            MPI_Finalize();
            return 0;
        }
        configureRankThreads(options, rank, worldSize);

        auto graph = alchemy::JsonLoader::loadFromFile(options.dataPath);
        const auto tiers = alchemy::TierCatalog::loadFromFile(options.tiersPath);
        try {
            tiers.validateAgainstGraph(graph);
        } catch (const std::exception& error) {
            if (options.listElements) {
                throw;
            }
            if (rank == 0) {
                std::cerr << "Warning: " << error.what()
                          << ". Continuing search without strict tier-data validation.\n";
            }
        }
        tiers.applyTerminals(graph);
        int rc = 0;
        if (options.listElements) {
            rc = runListElements(options, tiers, rank);
        } else if (!options.benchmarkPath.empty()) {
            rc = runBenchmark(options, graph, rank, worldSize, rankHostnames);
        } else if (rank == 0) {
            auto run = alchemy::runMpiMaster(options, graph, worldSize, rankHostnames);
            printSummary(options, run);
            rc = run.search.recipes.empty() ? 2 : 0;
        } else {
            alchemy::runMpiWorker(options, graph, rank);
        }

        MPI_Finalize();
        return rc;
    } catch (const std::exception& error) {
        std::cerr << "Rank " << rank << " error: " << error.what() << "\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
        return 1;
    }
}
