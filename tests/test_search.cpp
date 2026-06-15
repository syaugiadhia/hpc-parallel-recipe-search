#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

#include "common/Cli.hpp"
#include "common/JsonLoader.hpp"
#include "common/Search.hpp"
#include "common/TierCatalog.hpp"
#include "common/Visualizer.hpp"

namespace {

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
