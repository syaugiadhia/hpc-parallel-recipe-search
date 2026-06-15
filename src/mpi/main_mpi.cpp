#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <stdexcept>

#include <mpi.h>

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
    std::cout << "Processes: " << result.stats.processes << "\n";
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
    std::cout << "Output DOT: " << outputs.dotPath << "\n";
    if (outputs.imageRendered) {
        std::cout << "Output " << options.imageFormat << ": " << outputs.imagePath << "\n";
    } else {
        std::cout << "Output image: skipped (" << outputs.renderWarning << ")\n";
    }
    std::cout << "Output JSON: " << outputs.jsonPath << "\n";

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

int runBenchmark(alchemy::AppOptions options,
                 const alchemy::RecipeGraph& graph,
                 int rank,
                 int worldSize,
                 const std::vector<std::string>& rankHostnames) {
    std::vector<std::string> targets;
    if (rank == 0) {
        targets = alchemy::readBenchmarkTargets(options.benchmarkPath);
    }

    int targetCount = rank == 0 ? static_cast<int>(targets.size()) : 0;
    MPI_Bcast(&targetCount, 1, MPI_INT, 0, MPI_COMM_WORLD);

    std::ofstream csv;
    const std::string baseOutput = options.outputPrefix;
    if (rank == 0) {
        alchemy::ensureParentDirectory(baseOutput);
        csv.open(baseOutput + ".csv");
        if (!csv) {
            throw std::runtime_error("Cannot write benchmark CSV: " + baseOutput + ".csv");
        }
        csv << "target,algorithm,mode,trace_mode,visual_mode,processes,recipes_found,nodes_visited,"
            << "cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,output_image\n";
    }

    for (int i = 0; i < targetCount; ++i) {
        std::string target = rank == 0 ? targets[static_cast<std::size_t>(i)] : "";
        broadcastString(target, rank);

        auto runOptions = options;
        runOptions.target = target;
        runOptions.outputPrefix = baseOutput + "_" + alchemy::sanitizeForPath(target) + "_np" + std::to_string(worldSize);

        if (rank == 0) {
            auto run = alchemy::runMpiMaster(runOptions, graph, worldSize, rankHostnames);
            writeBenchmarkRow(csv, runOptions, run);
            std::cout << "MPI bench " << target << ": " << run.search.recipes.size()
                      << " recipes, " << std::fixed << std::setprecision(3)
                      << run.search.stats.timeMs << " ms\n";
        } else {
            alchemy::runMpiWorker(runOptions, graph, rank);
        }
    }

    if (rank == 0) {
        std::cout << "Benchmark CSV: " << baseOutput << ".csv\n";
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
