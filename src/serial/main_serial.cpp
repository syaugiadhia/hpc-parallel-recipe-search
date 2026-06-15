#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

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

void printSummary(const alchemy::AppOptions& options,
                  const alchemy::SearchResult& result,
                  const alchemy::OutputFiles& outputs) {
    std::cout << "Target: " << result.target << "\n";
    std::cout << "Algorithm: " << alchemy::toString(options.algorithm) << "\n";
    std::cout << "Mode: " << alchemy::toString(options.mode) << "\n";
    std::cout << "Trace mode: " << alchemy::toString(options.traceMode) << "\n";
    std::cout << "Visual mode: " << alchemy::toString(options.visualMode) << "\n";
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
                       const alchemy::SearchResult& result,
                       const alchemy::OutputFiles& outputs) {
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
    csv << "target,algorithm,mode,trace_mode,visual_mode,processes,recipes_found,nodes_visited,"
        << "cache_hits,cache_entries,time_ms,tasks_processed,communication_ms,output_dot,output_image\n";

    for (const auto& target : targets) {
        options.target = target;
        options.outputPrefix = baseOutput + "_" + alchemy::sanitizeForPath(target);
        alchemy::SearchEngine engine(graph, options);
        auto result = engine.search(target);
        auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
        writeBenchmarkRow(csv, options, result, outputs);
        std::cout << "Bench " << target << ": " << result.recipes.size() << " recipes, "
                  << std::fixed << std::setprecision(3) << result.stats.timeMs << " ms\n";
    }

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
