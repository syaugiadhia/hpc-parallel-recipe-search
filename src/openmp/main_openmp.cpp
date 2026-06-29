#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifdef ALCHEMY_HAS_OPENMP
#include <omp.h>
#endif

#include "common/Cli.hpp"
#include "common/JsonLoader.hpp"
#include "common/Search.hpp"
#include "common/TierCatalog.hpp"
#include "common/Visualizer.hpp"

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

void configureOpenmp(alchemy::AppOptions& options) {
    if (!alchemy::openmpAvailable()) {
        throw std::runtime_error("This build does not include OpenMP support");
    }
    options.useOpenmp = true;
    options.threads = std::max(1, options.threads);
#ifdef ALCHEMY_HAS_OPENMP
    omp_set_num_threads(options.threads);
#endif
}

void printSummary(const alchemy::AppOptions& options,
                  const alchemy::SearchResult& result,
                  const alchemy::OutputFiles& outputs) {
    std::cout << "Target: " << result.target << "\n";
    std::cout << "Engine: openmp\n";
    std::cout << "Algorithm: " << alchemy::toString(options.algorithm) << "\n";
    std::cout << "Mode: " << alchemy::toString(options.mode) << "\n";
    std::cout << "Trace mode: " << alchemy::toString(options.traceMode) << "\n";
    std::cout << "Visual mode: " << alchemy::toString(options.visualMode) << "\n";
    std::cout << "Render mode: " << alchemy::toString(options.renderMode) << "\n";
    std::cout << "Threads per process: " << result.stats.threadsPerProcess << "\n";
    std::cout << "Total workers: " << result.stats.totalWorkers << "\n";
    std::cout << "Recipes requested: "
              << (options.mode == alchemy::SearchMode::All ? std::string("all unique direct recipes with shortest subtrees") : std::to_string(options.limit))
              << "\n";
    if (options.mode == alchemy::SearchMode::All) {
        std::cout << "Unique direct recipes available: " << result.stats.directRecipesAvailable << "\n";
    }
    std::cout << "Recipes found: " << result.recipes.size() << "\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(3) << result.stats.timeMs << " ms\n";
    std::cout << "Nodes visited: " << result.stats.nodesVisited << "\n";
    std::cout << "Cache hits: " << result.stats.cacheHits << "\n";
    std::cout << "Cache entries: " << result.stats.cacheEntries << "\n";
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
                       const alchemy::SearchResult& result,
                       const alchemy::OutputFiles& outputs) {
    csv << csvEscape(result.target) << ","
        << alchemy::toString(options.algorithm) << ","
        << alchemy::toString(options.mode) << ","
        << alchemy::toString(options.traceMode) << ","
        << alchemy::toString(options.visualMode) << ","
        << result.stats.processes << ","
        << result.stats.threadsPerProcess << ","
        << result.stats.totalWorkers << ","
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

int runSingle(const alchemy::AppOptions& options, const alchemy::RecipeGraph& graph) {
    alchemy::SearchEngine engine(graph, options);
    auto result = engine.search(options.target);
    auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
    printSummary(options, result, outputs);
    if (result.recipes.empty()) {
        std::cerr << "No valid recipe found for target: " << result.target << "\n";
        return 2;
    }
    return 0;
}

int runBenchmark(alchemy::AppOptions options, const alchemy::RecipeGraph& graph) {
    const auto targets = alchemy::readBenchmarkTargets(options.benchmarkPath);
    alchemy::ensureParentDirectory(options.outputPrefix);
    const std::string baseOutput = options.outputPrefix;
    const std::string csvPath = options.outputPrefix + ".csv";
    std::ofstream csv(csvPath);
    if (!csv) {
        throw std::runtime_error("Cannot write benchmark CSV: " + csvPath);
    }
    csv << "target,algorithm,mode,trace_mode,visual_mode,processes,threads_per_process,total_workers,"
        << "recipes_found,nodes_visited,cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,output_image\n";

    // PARALELISME ANTAR-TARGET: tiap target independen -> dibagi ke thread (data-parallel,
    // embarrassingly parallel -> speedup ~linear). Tiap pencarian dijalankan SERIAL per-thread
    // (useOpenmp=false) supaya tak ada nested-parallel; paralelismenya di loop target ini.
    const int threadCount = std::max(1, options.threads);
    struct BenchRow {
        alchemy::SearchResult result;
        alchemy::OutputFiles outputs;
    };
    std::vector<BenchRow> rows(targets.size());

    const auto t0 = std::chrono::steady_clock::now();
#pragma omp parallel for schedule(dynamic) num_threads(threadCount)
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        alchemy::AppOptions ro = options;
        ro.useOpenmp = false;        // tiap target serial; paralel di antar-target
        ro.threads = 1;
        ro.progress = false;
        ro.target = targets[static_cast<std::size_t>(i)];
        ro.outputPrefix = baseOutput + "_" + alchemy::sanitizeForPath(ro.target);
        alchemy::SearchEngine engine(graph, ro);  // engine per-thread (graph read-only -> aman)
        auto result = engine.search(ro.target);
        auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, ro);
        // Catat paralelisme benchmark (N thread antar-target) untuk laporan.
        result.stats.threadsPerProcess = threadCount;
        result.stats.totalWorkers = threadCount;
        rows[static_cast<std::size_t>(i)] = {std::move(result), std::move(outputs)};
    }
    const double totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

    for (const auto& row : rows) {
        writeBenchmarkRow(csv, options, row.result, row.outputs);
        std::cout << "OpenMP bench " << row.result.target << ": " << row.result.recipes.size()
                  << " recipes, " << std::fixed << std::setprecision(3) << row.result.stats.timeMs << " ms\n";
    }

    std::cout << "Benchmark total wall time: " << std::fixed << std::setprecision(3)
              << totalMs << " ms (threads=" << threadCount << ", targets=" << targets.size() << ")\n";
    std::cout << "Benchmark CSV: " << csvPath << "\n";
    return 0;
}

int runListElements(const alchemy::AppOptions& options, const alchemy::TierCatalog& tiers) {
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
    try {
        auto options = alchemy::parseArgs(argc, argv, false);
        if (options.help) {
            std::cout << alchemy::usageText(false);
            return 0;
        }
        configureOpenmp(options);

        auto graph = alchemy::JsonLoader::loadFromFile(options.dataPath);
        const auto tiers = alchemy::TierCatalog::loadFromFile(options.tiersPath);
        try {
            tiers.validateAgainstGraph(graph);
        } catch (const std::exception& error) {
            if (options.listElements) {
                throw;
            }
            std::cerr << "Warning: " << error.what()
                      << ". Continuing search without strict tier-data validation.\n";
        }
        tiers.applyTerminals(graph);

        if (options.listElements) {
            return runListElements(options, tiers);
        }
        if (!options.benchmarkPath.empty()) {
            return runBenchmark(options, graph);
        }
        return runSingle(options, graph);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n";
        std::cerr << alchemy::usageText(false);
        return 1;
    }
}
