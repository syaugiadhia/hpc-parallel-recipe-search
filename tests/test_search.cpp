#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/Cli.hpp"
#include "common/JsonLoader.hpp"
#include "common/Search.hpp"
#include "common/TierCatalog.hpp"
#include "common/Visualizer.hpp"

namespace {

namespace fs = std::filesystem;

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int count = 0;
    std::size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

alchemy::SearchResult searchTarget(const alchemy::RecipeGraph& graph,
                                   const std::string& target,
                                   alchemy::Algorithm algorithm,
                                   alchemy::TraceMode traceMode,
                                   int limit,
                                   alchemy::SearchMode mode = alchemy::SearchMode::Multiple) {
    alchemy::SearchOptions options;
    options.algorithm = algorithm;
    options.mode = mode;
    options.traceMode = traceMode;
    options.limit = limit;
    alchemy::SearchEngine engine(graph, options);
    return engine.search(target);
}

std::string testOutputPrefix(const std::string& name) {
    fs::path outputDir = fs::path("build-codex") / "test_outputs";
    fs::create_directories(outputDir);
    return (outputDir / name).string();
}

void removeOutputSet(const std::string& prefix, const std::string& imageFormat) {
    fs::remove(prefix + ".json");
    fs::remove(prefix + ".dot");
    fs::remove(prefix + "." + imageFormat);
}

std::vector<std::string> signatures(const std::vector<alchemy::RecipeTree>& recipes) {
    std::vector<std::string> out;
    out.reserve(recipes.size());
    for (const auto& recipe : recipes) {
        out.push_back(alchemy::treeSignature(recipe));
    }
    return out;
}

}  // namespace

int main() {
    {
        auto fullGraph = alchemy::JsonLoader::loadFromFile("data/recipes.json");
        const auto tiers = alchemy::TierCatalog::loadFromFile("data/tiers.json");
        tiers.validateAgainstGraph(fullGraph);
        tiers.applyTerminals(fullGraph);
        assert(tiers.uniqueElementCount() == 720);
        assert(tiers.filterElements("starter", "").size() == 4);
        assert(tiers.filterElements("special", "").size() == 1);
        assert(tiers.filterElements("special", "")[0] == "Time");
        assert(tiers.filterElements("tier1", "mud").size() == 1);
        assert(tiers.filterElements("tier1", "mud")[0] == "Mud");
        assert(!tiers.filterElements("tier6", "light").empty());
        assert(!tiers.filterElements("tier6", "LIGHT").empty());
        for (int i = 1; i <= 6; ++i) {
            assert(!tiers.filterElements("tier" + std::to_string(i), "").empty());
        }

        auto mudAll = searchTarget(fullGraph, "Mud", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        assert(mudAll.stats.directRecipesAvailable == 2);
        assert(mudAll.recipes.size() == 2);
    }

    auto graphWithSpecial = alchemy::JsonLoader::loadFromFile("tests/fixture_recipes.json");
    graphWithSpecial.markTerminal("Time");

    {
        assert(alchemy::parseSearchMode("all") == alchemy::SearchMode::All);
        assert(alchemy::toString(alchemy::SearchMode::All) == "all");
        assert(alchemy::parseRenderMode("json") == alchemy::RenderMode::Json);
        assert(alchemy::parseRenderMode("full") == alchemy::RenderMode::Full);
        assert(alchemy::toString(alchemy::RenderMode::Json) == "json");
        assert(alchemy::toString(alchemy::RenderMode::Full) == "full");

        const char* rawArgs[] = {
            "alchemy_openmp",
            "--data", "tests/fixture_recipes.json",
            "--target", "Brick",
            "--algorithm", "bfs",
            "--mode", "multiple",
            "--limit", "3",
            "--trace-mode", "memo",
            "--visual-mode", "shared",
            "--threads", "3",
        };
        std::vector<char*> args;
        for (const char* arg : rawArgs) {
            args.push_back(const_cast<char*>(arg));
        }
        auto parsed = alchemy::parseArgs(static_cast<int>(args.size()), args.data(), false);
        assert(parsed.threads == 3);

        alchemy::SearchStats stats;
        stats.processes = 2;
        stats.threadsPerProcess = 4;
        stats.totalWorkers = 8;
        stats.threadsByRank = {4, 4};
        auto restored = alchemy::statsFromJson(alchemy::statsToJson(stats));
        assert(restored.threadsPerProcess == 4);
        assert(restored.totalWorkers == 8);
        assert(restored.threadsByRank.size() == 2);
    }

    {
        auto result = searchTarget(graphWithSpecial, "Fire", alchemy::Algorithm::Dfs, alchemy::TraceMode::Full, 1);
        assert(result.recipes.size() == 1);
        assert(result.recipes[0].name == "Fire");
        assert(result.recipes[0].basic);
        assert(result.recipes[0].children.empty());
    }

    {
        auto result = searchTarget(graphWithSpecial, "mud", alchemy::Algorithm::Dfs, alchemy::TraceMode::Full, 1);
        assert(result.target == "Mud");
        assert(result.recipes.size() == 1);
        assert(result.recipes[0].children.size() == 2);
        assert(result.recipes[0].children[0].name == "Water");
        assert(result.recipes[0].children[1].name == "Earth");
    }

    {
        auto result = searchTarget(graphWithSpecial, "Brick", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 4);
        assert(result.recipes.size() >= 2);
    }

    {
        auto time = searchTarget(graphWithSpecial, "Time", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        auto old = searchTarget(graphWithSpecial, "Old", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        assert(time.recipes.size() == 1);
        assert(time.recipes[0].name == "Time");
        assert(time.recipes[0].basic);
        assert(old.recipes.size() == 1);
        assert(old.recipes[0].children[0].name == "Time");
    }

    {
        auto direct = searchTarget(graphWithSpecial, "DirectAll", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 2);
        assert(direct.recipes.size() == 2);
        assert(direct.recipes[0].children[0].name == "C");
        assert(direct.recipes[1].children[0].name == "Branch");
    }

    {
        auto brick = searchTarget(graphWithSpecial, "Brick", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 50);
        assert(brick.recipes.size() == 2);
    }

    {
        auto one = searchTarget(graphWithSpecial, "FlipDirect", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        auto two = searchTarget(graphWithSpecial, "FlipDirect", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 2);
        auto high = searchTarget(graphWithSpecial, "FlipDirect", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 50);
        assert(one.recipes.size() == 1);
        assert(two.recipes.size() == 2);
        assert(high.recipes.size() == 2);
    }

    {
        auto one = searchTarget(graphWithSpecial, "DeepOrder", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        auto two = searchTarget(graphWithSpecial, "DeepOrder", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 2);
        auto high = searchTarget(graphWithSpecial, "DeepOrder", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 50);
        assert(one.recipes.size() == 1);
        assert(two.recipes.size() == 2);
        assert(high.recipes.size() == 5);
        assert(treeDepth(one.recipes[0]) <= treeDepth(two.recipes[1]));
        for (std::size_t i = 1; i < high.recipes.size(); ++i) {
            assert(treeDepth(high.recipes[i - 1]) <= treeDepth(high.recipes[i]));
        }
    }

    if (alchemy::openmpAvailable()) {
        alchemy::SearchOptions serialOptions;
        serialOptions.algorithm = alchemy::Algorithm::Bfs;
        serialOptions.mode = alchemy::SearchMode::Multiple;
        serialOptions.traceMode = alchemy::TraceMode::Memo;
        serialOptions.limit = 5;

        alchemy::SearchOptions openmpOptions = serialOptions;
        openmpOptions.useOpenmp = true;
        openmpOptions.threads = 2;

        alchemy::SearchEngine serialEngine(graphWithSpecial, serialOptions);
        alchemy::SearchEngine openmpEngine(graphWithSpecial, openmpOptions);
        auto serial = serialEngine.search("DeepOrder");
        auto openmp = openmpEngine.search("DeepOrder");
        assert(signatures(openmp.recipes) == signatures(serial.recipes));
        assert(openmp.stats.processes == 1);
        assert(openmp.stats.threadsPerProcess == 2);
        assert(openmp.stats.totalWorkers == 2);

        alchemy::RecipeTree partial{"DirectAll", false, false, false, {}, {
            alchemy::RecipeTree{"Branch", false, false, false, {}, {}},
            alchemy::RecipeTree{"Fire", true, false, false, {}, {}},
        }};
        alchemy::SearchEngine serialPartial(graphWithSpecial, serialOptions);
        alchemy::SearchEngine openmpPartial(graphWithSpecial, openmpOptions);
        auto serialCompleted = serialPartial.completePartial(partial, 5, true);
        auto openmpCompleted = openmpPartial.completePartial(partial, 5, true);
        assert(signatures(openmpCompleted.recipes) == signatures(serialCompleted.recipes));
    }

    {
        auto one = searchTarget(graphWithSpecial, "Brick", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 1);
        auto allDfs = searchTarget(graphWithSpecial, "Brick", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        auto allBfs = searchTarget(graphWithSpecial, "Brick", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        assert(one.recipes.size() == 1);
        assert(allDfs.recipes.size() == 2);
        assert(allBfs.recipes.size() == 2);
        assert(allDfs.stats.directRecipesAvailable == 2);
    }

    {
        auto allDfs = searchTarget(graphWithSpecial, "DirectAll", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        auto allBfs = searchTarget(graphWithSpecial, "DirectAll", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        assert(allDfs.stats.directRecipesAvailable == 2);
        assert(allDfs.recipes.size() == 2);
        assert(allBfs.recipes.size() == 2);
        assert(allDfs.recipes[0].children[0].name == "C");
        assert(allBfs.recipes[0].children[0].name == "C");
    }

    {
        assert(graphWithSpecial.recipesFor("FlipDirect").size() == 2);
        auto all = searchTarget(graphWithSpecial, "FlipDirect", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        assert(all.stats.directRecipesAvailable == 2);
        assert(all.recipes.size() == 2);
    }

    {
        auto cycleDirect = searchTarget(graphWithSpecial, "CycleDirect", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 1, alchemy::SearchMode::All);
        assert(cycleDirect.stats.directRecipesAvailable == 2);
        assert(cycleDirect.recipes.size() == 1);
        assert(cycleDirect.recipes[0].children[0].name == "Mud");
    }

    {
        auto full = searchTarget(graphWithSpecial, "A", alchemy::Algorithm::Dfs, alchemy::TraceMode::Full, 2);
        auto memo = searchTarget(graphWithSpecial, "A", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 2);
        assert(full.recipes.size() == 2);
        assert(memo.recipes.size() == 2);
        assert(memo.stats.cacheHits > 0);
        assert(full.stats.cacheHits == 0);
        assert(full.stats.nodesVisited > memo.stats.nodesVisited);

        alchemy::AppOptions visualOptions;
        visualOptions.algorithm = alchemy::Algorithm::Dfs;
        visualOptions.mode = alchemy::SearchMode::Multiple;
        visualOptions.traceMode = alchemy::TraceMode::Memo;
        visualOptions.limit = 2;

        visualOptions.visualMode = alchemy::VisualMode::Full;
        const auto fullDot = alchemy::Visualizer::buildDot(memo.recipes, visualOptions);
        assert(fullDot.find("shared") == std::string::npos);
        assert(countOccurrences(fullDot, "C") >= 2);

        visualOptions.visualMode = alchemy::VisualMode::Shared;
        const auto sharedDot = alchemy::Visualizer::buildDot(memo.recipes, visualOptions);
        assert(sharedDot.find("shared") != std::string::npos);
    }

    {
        auto result = searchTarget(graphWithSpecial, "Fire", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        assert(result.recipes.size() == 1);

        alchemy::AppOptions options;
        options.algorithm = alchemy::Algorithm::Bfs;
        options.mode = alchemy::SearchMode::Multiple;
        options.traceMode = alchemy::TraceMode::Memo;
        options.limit = 1;
        options.imageFormat = "svg";
        options.renderMode = alchemy::RenderMode::Json;
        options.outputPrefix = testOutputPrefix("visualizer_json_only");
        removeOutputSet(options.outputPrefix, options.imageFormat);

        const auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
        assert(!outputs.jsonPath.empty());
        assert(fs::exists(outputs.jsonPath));
        assert(outputs.dotPath.empty());
        assert(outputs.imagePath.empty());
        assert(!outputs.imageRendered);
        assert(!outputs.renderWarning.empty());
        assert(!fs::exists(options.outputPrefix + ".dot"));
        assert(!fs::exists(options.outputPrefix + ".svg"));
    }

    {
        auto result = searchTarget(graphWithSpecial, "Fire", alchemy::Algorithm::Bfs, alchemy::TraceMode::Memo, 1);
        assert(result.recipes.size() == 1);

        alchemy::AppOptions options;
        options.algorithm = alchemy::Algorithm::Bfs;
        options.mode = alchemy::SearchMode::Multiple;
        options.traceMode = alchemy::TraceMode::Memo;
        options.limit = 1;
        options.imageFormat = "png";
        options.renderMode = alchemy::RenderMode::Full;
        options.outputPrefix = testOutputPrefix("visualizer_full_render");
        removeOutputSet(options.outputPrefix, options.imageFormat);

        const auto outputs = alchemy::Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
        assert(!outputs.jsonPath.empty());
        assert(fs::exists(outputs.jsonPath));
        assert(!outputs.dotPath.empty());
        assert(fs::exists(outputs.dotPath));
        if (outputs.imageRendered) {
            assert(!outputs.imagePath.empty());
            assert(fs::exists(outputs.imagePath));
        } else {
            assert(outputs.imagePath.empty());
            assert(!outputs.renderWarning.empty());
        }
    }

    {
        alchemy::RecipeTree left{"Root", false, false, false, {}, {
            alchemy::RecipeTree{"B", false, false, false, {}, {}},
            alchemy::RecipeTree{"C", false, false, false, {}, {}},
        }};
        alchemy::RecipeTree right{"Root", false, false, false, {}, {
            alchemy::RecipeTree{"C", false, false, false, {}, {}},
            alchemy::RecipeTree{"B", false, false, false, {}, {}},
        }};
        auto unique = alchemy::deduplicateTrees({left, right}, 0);
        assert(unique.size() == 1);
    }

    {
        alchemy::SearchOptions options;
        options.algorithm = alchemy::Algorithm::Bfs;
        options.mode = alchemy::SearchMode::Multiple;
        options.traceMode = alchemy::TraceMode::Memo;
        options.limit = 5;
        alchemy::SearchEngine engine(graphWithSpecial, options);
        alchemy::RecipeTree partial{"DirectAll", false, false, false, {}, {
            alchemy::RecipeTree{"Branch", false, false, false, {}, {}},
            alchemy::RecipeTree{"Fire", true, false, false, {}, {}},
        }};
        auto completed = engine.completePartial(partial, 5, true);
        assert(!completed.recipes.empty());
        assert(completed.recipes[0].name == "DirectAll");
        assert(completed.recipes[0].children[0].name == "Branch");
    }

    {
        bool missingThrown = false;
        try {
            (void)searchTarget(graphWithSpecial, "Not An Element", alchemy::Algorithm::Dfs, alchemy::TraceMode::Full, 1);
        } catch (const std::exception&) {
            missingThrown = true;
        }
        assert(missingThrown);
    }

    {
        auto result = searchTarget(graphWithSpecial, "X", alchemy::Algorithm::Dfs, alchemy::TraceMode::Memo, 2, alchemy::SearchMode::All);
        assert(result.recipes.empty());
    }

    std::cout << "alchemy tests passed\n";
    return 0;
}
