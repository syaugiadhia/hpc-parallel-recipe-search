#include "common/Search.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace alchemy {
namespace {

std::vector<RecipeTree> clipTrees(const std::vector<RecipeTree>& trees, int limit) {
    if (limit <= 0 || static_cast<int>(trees.size()) <= limit) {
        return trees;
    }
    return {trees.begin(), trees.begin() + limit};
}

std::vector<RecipeTree> dedupeAndClip(const std::vector<RecipeTree>& trees, int limit) {
    return deduplicateTrees(trees, static_cast<std::size_t>(std::max(0, limit)));
}

bool limitReached(std::size_t size, int limit) {
    return limit > 0 && static_cast<int>(size) >= limit;
}

void combineChoices(const std::string& name,
                    const std::vector<std::vector<RecipeTree>>& choices,
                    std::size_t index,
                    std::vector<RecipeTree>& current,
                    std::vector<RecipeTree>& out,
                    int limit) {
    if (limitReached(out.size(), limit)) {
        return;
    }
    if (index == choices.size()) {
        RecipeTree tree;
        tree.name = name;
        tree.children = current;
        out.push_back(tree);
        return;
    }
    for (const auto& candidate : choices[index]) {
        current.push_back(candidate);
        combineChoices(name, choices, index + 1, current, out, limit);
        current.pop_back();
        if (limitReached(out.size(), limit)) {
            break;
        }
    }
}

}  // namespace

std::string toString(Algorithm algorithm) {
    return algorithm == Algorithm::Bfs ? "bfs" : "dfs";
}

std::string toString(SearchMode mode) {
    if (mode == SearchMode::Single) {
        return "single";
    }
    if (mode == SearchMode::All) {
        return "all";
    }
    return "multiple";
}

std::string toString(TraceMode mode) {
    return mode == TraceMode::Full ? "full" : "memo";
}

std::string toString(VisualMode mode) {
    return mode == VisualMode::Full ? "full" : "shared";
}

Algorithm parseAlgorithm(const std::string& value) {
    if (value == "bfs") {
        return Algorithm::Bfs;
    }
    if (value == "dfs") {
        return Algorithm::Dfs;
    }
    throw std::runtime_error("Invalid --algorithm value '" + value + "'. Expected bfs or dfs.");
}

SearchMode parseSearchMode(const std::string& value) {
    if (value == "single") {
        return SearchMode::Single;
    }
    if (value == "multiple") {
        return SearchMode::Multiple;
    }
    if (value == "all") {
        return SearchMode::All;
    }
    throw std::runtime_error("Invalid --mode value '" + value + "'. Expected single, multiple, or all.");
}

TraceMode parseTraceMode(const std::string& value) {
    if (value == "full") {
        return TraceMode::Full;
    }
    if (value == "memo") {
        return TraceMode::Memo;
    }
    throw std::runtime_error("Invalid --trace-mode value '" + value + "'. Expected full or memo.");
}

VisualMode parseVisualMode(const std::string& value) {
    if (value == "full") {
        return VisualMode::Full;
    }
    if (value == "shared") {
        return VisualMode::Shared;
    }
    throw std::runtime_error("Invalid --visual-mode value '" + value + "'. Expected full or shared.");
}

SearchEngine::SearchEngine(const RecipeGraph& graph, SearchOptions options)
    : graph_(graph), options_(options) {}

SearchResult SearchEngine::search(const std::string& target, bool resetMemo) {
    if (!graph_.hasElement(target)) {
        throw std::runtime_error("Target is not available in recipe data: " + target);
    }
    if (resetMemo) {
        clearMemo();
    }
    stats_ = {};

    const auto start = std::chrono::steady_clock::now();
    startedAt_ = start;
    nextProgressNode_ = 1000;
    std::unordered_set<std::string> active;
    const auto canonicalTarget = graph_.canonicalName(target);
    auto expanded = options_.mode == SearchMode::All
        ? expandDirectRecipes(canonicalTarget, active)
        : expandElementAny(canonicalTarget, effectiveLimit(options_.limit), active);
    auto recipes = dedupeAndClip(expanded.trees, effectiveLimit(options_.limit));
    const auto end = std::chrono::steady_clock::now();

    stats_.timeMs = std::chrono::duration<double, std::milli>(end - start).count();
    stats_.cacheEntries = memoEnabled() ? static_cast<std::int64_t>(memo_.size()) : 0;
    stats_.processes = 1;

    return {canonicalTarget, recipes, stats_};
}

SearchResult SearchEngine::completePartial(const RecipeTree& partial, int limit, bool resetMemo) {
    if (resetMemo) {
        clearMemo();
    }
    stats_ = {};

    const auto start = std::chrono::steady_clock::now();
    startedAt_ = start;
    nextProgressNode_ = 1000;
    std::unordered_set<std::string> active;
    const int partialLimit = options_.mode == SearchMode::All ? 1 : effectiveLimit(limit);
    auto expanded = completePartialNode(partial, partialLimit, active);
    auto recipes = dedupeAndClip(expanded.trees, partialLimit);
    const auto end = std::chrono::steady_clock::now();

    stats_.timeMs = std::chrono::duration<double, std::milli>(end - start).count();
    stats_.cacheEntries = memoEnabled() ? static_cast<std::int64_t>(memo_.size()) : 0;
    stats_.processes = 1;
    stats_.tasksProcessed = 1;

    return {partial.name, recipes, stats_};
}

void SearchEngine::clearMemo() {
    memo_.clear();
}

std::size_t SearchEngine::memoSize() const {
    return memo_.size();
}

bool SearchEngine::memoEnabled() const {
    return options_.traceMode == TraceMode::Memo;
}

int SearchEngine::effectiveLimit(int requested) const {
    if (options_.mode == SearchMode::Single) {
        return 1;
    }
    if (options_.mode == SearchMode::All) {
        return 0;
    }
    return std::max(1, requested);
}

int SearchEngine::maxSearchDepth() const {
    return static_cast<int>(graph_.elementCount()) + 4;
}

SearchEngine::ExpandResult SearchEngine::memoHit(const std::string& key, int limit) const {
    ExpandResult result;
    if (!memoEnabled()) {
        result.cycleBlocked = true;
        return result;
    }
    const auto found = memo_.find(key);
    if (found == memo_.end()) {
        result.cycleBlocked = true;
        return result;
    }
    ++stats_.cacheHits;
    result.trees = clipTrees(found->second, limit);
    result.cycleBlocked = false;
    return result;
}

void SearchEngine::memoStore(const std::string& key, const ExpandResult& result) {
    if (memoEnabled() && !result.cycleBlocked) {
        memo_[key] = result.trees;
    }
}

void SearchEngine::maybePrintNodeProgress() {
    if (!options_.progress || stats_.nodesVisited < nextProgressNode_) {
        return;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt_).count();
    std::cerr << "[progress] nodes_visited=" << stats_.nodesVisited
              << " cache_hits=" << stats_.cacheHits
              << " elapsed_ms=" << elapsed << "\n";
    nextProgressNode_ *= 2;
}

void SearchEngine::printBfsProgress(int depth, std::size_t recipesFound) const {
    if (!options_.progress) {
        return;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt_).count();
    std::cerr << "[progress] bfs_depth=" << depth
              << " recipes_found=" << recipesFound
              << " nodes_visited=" << stats_.nodesVisited
              << " cache_hits=" << stats_.cacheHits
              << " elapsed_ms=" << elapsed << "\n";
}

SearchEngine::ExpandResult SearchEngine::expandElementAny(const std::string& name, int limit, std::unordered_set<std::string>& active) {
    if (options_.algorithm == Algorithm::Dfs) {
        return expandElementDfs(name, limit, active);
    }
    if (limit == 0) {
        auto result = expandElementDfs(name, limit, active);
        std::stable_sort(result.trees.begin(), result.trees.end(), [](const RecipeTree& left, const RecipeTree& right) {
            const int leftDepth = treeDepth(left);
            const int rightDepth = treeDepth(right);
            if (leftDepth != rightDepth) {
                return leftDepth < rightDepth;
            }
            return treeSignature(left) < treeSignature(right);
        });
        printBfsProgress(treeDepth(result.trees.empty() ? RecipeTree{} : result.trees.back()), result.trees.size());
        return result;
    }

    ExpandResult aggregate;
    std::unordered_set<std::string> seen;
    for (int depth = 0; depth <= maxSearchDepth() && !limitReached(aggregate.trees.size(), limit); ++depth) {
        auto bounded = expandElementBfsBounded(name, depth, limit, active);
        aggregate.cycleBlocked = aggregate.cycleBlocked || bounded.cycleBlocked;
        for (const auto& tree : bounded.trees) {
            const auto signature = treeSignature(tree);
            if (seen.insert(signature).second) {
                aggregate.trees.push_back(tree);
                if (limitReached(aggregate.trees.size(), limit)) {
                    break;
                }
            }
        }
        printBfsProgress(depth, aggregate.trees.size());
    }
    return aggregate;
}

SearchEngine::ExpandResult SearchEngine::expandDirectRecipes(const std::string& name, std::unordered_set<std::string>& active) {
    const auto canonical = graph_.canonicalName(name);
    const auto& recipes = graph_.recipesFor(canonical);
    stats_.directRecipesAvailable = static_cast<std::int64_t>(recipes.size());

    if (options_.progress) {
        std::cerr << "[progress] mode=all-direct target=\"" << canonical
                  << "\" direct_recipes=" << recipes.size()
                  << " status=started\n";
    }

    if (graph_.isTerminal(canonical)) {
        ++stats_.nodesVisited;
        maybePrintNodeProgress();
        return {{{canonical, true, false, false, {}, {}}}, false};
    }
    if (recipes.empty()) {
        return {{}, false};
    }

    const auto activeKey = RecipeGraph::normalize(canonical);
    if (active.find(activeKey) != active.end()) {
        return {{}, true};
    }

    const bool progressEnabled = options_.progress;
    options_.progress = false;
    active.insert(activeKey);
    ExpandResult result;
    for (std::size_t index = 0; index < recipes.size(); ++index) {
        const auto& recipe = recipes[index];
        auto left = expandElementAny(recipe.first, 1, active);
        auto right = expandElementAny(recipe.second, 1, active);
        result.cycleBlocked = result.cycleBlocked || left.cycleBlocked || right.cycleBlocked;
        if (left.trees.empty() || right.trees.empty()) {
            continue;
        }

        RecipeTree tree;
        tree.name = canonical;
        tree.children = {left.trees.front(), right.trees.front()};
        result.trees.push_back(tree);

        if (progressEnabled) {
            std::cerr << "[progress] direct_recipe=" << (index + 1)
                      << "/" << recipes.size()
                      << " recipes_found=" << result.trees.size()
                      << " nodes_visited=" << stats_.nodesVisited
                      << " cache_hits=" << stats_.cacheHits
                      << "\n";
        }
    }
    active.erase(activeKey);
    options_.progress = progressEnabled;

    result.trees = dedupeAndClip(result.trees, 0);
    if (options_.algorithm == Algorithm::Bfs) {
        std::stable_sort(result.trees.begin(), result.trees.end(), [](const RecipeTree& left, const RecipeTree& right) {
            const int leftDepth = treeDepth(left);
            const int rightDepth = treeDepth(right);
            if (leftDepth != rightDepth) {
                return leftDepth < rightDepth;
            }
            return treeSignature(left) < treeSignature(right);
        });
    }
    return result;
}

SearchEngine::ExpandResult SearchEngine::expandElementDfs(const std::string& name, int limit, std::unordered_set<std::string>& active) {
    const auto canonical = graph_.canonicalName(name);
    const auto key = "D:" + canonical;
    if (memoEnabled()) {
        auto cached = memoHit(key, limit);
        if (!cached.cycleBlocked) {
            return cached;
        }
    }

    const auto activeKey = RecipeGraph::normalize(canonical);
    if (active.find(activeKey) != active.end()) {
        return {{}, true};
    }

    ++stats_.nodesVisited;
    maybePrintNodeProgress();
    if (graph_.isTerminal(canonical)) {
        ExpandResult result{{RecipeTree{canonical, true, false, false, {}, {}}}, false};
        memoStore(key, result);
        return result;
    }

    const auto& recipes = graph_.recipesFor(canonical);
    if (recipes.empty()) {
        ExpandResult result{{}, false};
        memoStore(key, result);
        return result;
    }

    active.insert(activeKey);
    ExpandResult result;
    for (const auto& recipe : recipes) {
        auto left = expandElementDfs(recipe.first, limit, active);
        auto right = expandElementDfs(recipe.second, limit, active);
        result.cycleBlocked = result.cycleBlocked || left.cycleBlocked || right.cycleBlocked;
        if (left.trees.empty() || right.trees.empty()) {
            continue;
        }
        for (const auto& leftTree : left.trees) {
            for (const auto& rightTree : right.trees) {
                RecipeTree tree;
                tree.name = canonical;
                tree.children = {leftTree, rightTree};
                result.trees.push_back(tree);
                if (limitReached(result.trees.size(), limit)) {
                    break;
                }
            }
            if (limitReached(result.trees.size(), limit)) {
                break;
            }
        }
        if (limitReached(result.trees.size(), limit)) {
            break;
        }
    }
    active.erase(activeKey);

    result.trees = dedupeAndClip(result.trees, limit);
    memoStore(key, result);
    return result;
}

SearchEngine::ExpandResult SearchEngine::expandElementBfsBounded(const std::string& name,
                                                                 int depthRemaining,
                                                                 int limit,
                                                                 std::unordered_set<std::string>& active) {
    const auto canonical = graph_.canonicalName(name);
    const auto key = "B:" + canonical + ":" + std::to_string(depthRemaining);
    if (memoEnabled()) {
        auto cached = memoHit(key, limit);
        if (!cached.cycleBlocked) {
            return cached;
        }
    }

    const auto activeKey = RecipeGraph::normalize(canonical);
    if (active.find(activeKey) != active.end()) {
        return {{}, true};
    }

    ++stats_.nodesVisited;
    maybePrintNodeProgress();
    if (graph_.isTerminal(canonical)) {
        ExpandResult result{{RecipeTree{canonical, true, false, false, {}, {}}}, false};
        memoStore(key, result);
        return result;
    }
    if (depthRemaining <= 0) {
        ExpandResult result{{}, false};
        memoStore(key, result);
        return result;
    }

    const auto& recipes = graph_.recipesFor(canonical);
    if (recipes.empty()) {
        ExpandResult result{{}, false};
        memoStore(key, result);
        return result;
    }

    active.insert(activeKey);
    ExpandResult result;
    for (const auto& recipe : recipes) {
        auto left = expandElementBfsBounded(recipe.first, depthRemaining - 1, limit, active);
        auto right = expandElementBfsBounded(recipe.second, depthRemaining - 1, limit, active);
        result.cycleBlocked = result.cycleBlocked || left.cycleBlocked || right.cycleBlocked;
        if (left.trees.empty() || right.trees.empty()) {
            continue;
        }
        for (const auto& leftTree : left.trees) {
            for (const auto& rightTree : right.trees) {
                RecipeTree tree;
                tree.name = canonical;
                tree.children = {leftTree, rightTree};
                result.trees.push_back(tree);
                if (limitReached(result.trees.size(), limit)) {
                    break;
                }
            }
            if (limitReached(result.trees.size(), limit)) {
                break;
            }
        }
        if (limitReached(result.trees.size(), limit)) {
            break;
        }
    }
    active.erase(activeKey);

    result.trees = dedupeAndClip(result.trees, limit);
    memoStore(key, result);
    return result;
}

SearchEngine::ExpandResult SearchEngine::completePartialNode(const RecipeTree& partial,
                                                             int limit,
                                                             std::unordered_set<std::string>& active) {
    if (!graph_.hasElement(partial.name)) {
        return {{}, false};
    }

    const auto canonical = graph_.canonicalName(partial.name);
    if (partial.children.empty()) {
        if (partial.basic || graph_.isTerminal(canonical)) {
            return {{RecipeTree{canonical, true, false, false, {}, {}}}, false};
        }
        return expandElementAny(canonical, limit, active);
    }

    const auto activeKey = RecipeGraph::normalize(canonical);
    if (active.find(activeKey) != active.end()) {
        return {{}, true};
    }

    ++stats_.nodesVisited;
    maybePrintNodeProgress();
    active.insert(activeKey);
    ExpandResult result;
    std::vector<std::vector<RecipeTree>> childChoices;
    childChoices.reserve(partial.children.size());
    for (const auto& child : partial.children) {
        auto expandedChild = completePartialNode(child, limit, active);
        result.cycleBlocked = result.cycleBlocked || expandedChild.cycleBlocked;
        if (expandedChild.trees.empty()) {
            active.erase(activeKey);
            return result;
        }
        childChoices.push_back(expandedChild.trees);
    }
    active.erase(activeKey);

    std::vector<RecipeTree> current;
    combineChoices(canonical, childChoices, 0, current, result.trees, limit);
    result.trees = dedupeAndClip(result.trees, limit);
    return result;
}

}  // namespace alchemy
