#include "common/Search.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

#ifdef ALCHEMY_HAS_OPENMP
#include <omp.h>
#endif

namespace alchemy {
namespace {

constexpr int kUnreachableDepth = std::numeric_limits<int>::max() / 4;

struct CompactNode {
    std::string name;
    int left = -1;
    int right = -1;
    int parent = -1;
    bool basic = false;
    int estimate = kUnreachableDepth;
};

struct FrontierNode {
    std::vector<CompactNode> nodes;
    std::vector<int> openLeaves;
    int estimate = kUnreachableDepth;
    std::size_t sequence = 0;
};

struct FrontierNodeCompare {
    bool operator()(const FrontierNode& left, const FrontierNode& right) const {
        if (left.estimate != right.estimate) {
            return left.estimate > right.estimate;
        }
        return left.sequence > right.sequence;
    }
};

struct FrontierExpansion {
    std::vector<FrontierNode> states;
    std::int64_t nodesVisited = 0;
    bool cycleBlocked = false;
};

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

int leafEstimate(const RecipeGraph& graph,
                 const std::string& canonical,
                 bool basic,
                 const std::unordered_map<std::string, int>& shortestDepths) {
    if (basic || graph.isTerminal(canonical)) {
        return 0;
    }
    const auto found = shortestDepths.find(canonical);
    return found == shortestDepths.end() ? kUnreachableDepth : found->second;
}

bool isExpandableCompactLeaf(const RecipeGraph& graph, const CompactNode& node) {
    return node.left < 0 && node.right < 0 && !node.basic && graph.hasElement(node.name) &&
           !graph.isTerminal(node.name) && graph.hasRecipes(node.name);
}

int appendCompactTree(const RecipeGraph& graph,
                      const RecipeTree& tree,
                      int parent,
                      const std::unordered_map<std::string, int>& shortestDepths,
                      std::vector<CompactNode>& nodes,
                      std::vector<int>& openLeaves) {
    if (!graph.hasElement(tree.name)) {
        return -1;
    }

    const auto canonical = graph.canonicalName(tree.name);
    CompactNode node;
    node.name = canonical;
    node.parent = parent;
    node.basic = tree.basic || (tree.children.empty() && graph.isTerminal(canonical));
    const int index = static_cast<int>(nodes.size());
    nodes.push_back(std::move(node));

    if (tree.children.empty()) {
        nodes[index].estimate = leafEstimate(graph, canonical, nodes[index].basic, shortestDepths);
        if (nodes[index].estimate >= kUnreachableDepth) {
            return -1;
        }
        if (isExpandableCompactLeaf(graph, nodes[index])) {
            openLeaves.push_back(index);
        }
        return index;
    }

    if (tree.children.size() != 2) {
        return -1;
    }

    const int left = appendCompactTree(graph, tree.children[0], index, shortestDepths, nodes, openLeaves);
    const int right = appendCompactTree(graph, tree.children[1], index, shortestDepths, nodes, openLeaves);
    if (left < 0 || right < 0) {
        return -1;
    }
    nodes[index].left = left;
    nodes[index].right = right;
    nodes[index].basic = false;
    nodes[index].estimate = 1 + std::max(nodes[left].estimate, nodes[right].estimate);
    return index;
}

FrontierNode makeFrontierNode(const RecipeGraph& graph,
                              const RecipeTree& tree,
                              const std::unordered_map<std::string, int>& shortestDepths,
                              std::size_t sequence) {
    FrontierNode state;
    state.sequence = sequence;
    const int root = appendCompactTree(graph, tree, -1, shortestDepths, state.nodes, state.openLeaves);
    if (root == 0) {
        state.estimate = state.nodes[0].estimate;
    }
    return state;
}

void refreshAncestorEstimates(std::vector<CompactNode>& nodes, int index) {
    while (index >= 0) {
        auto& node = nodes[index];
        if (node.left >= 0 && node.right >= 0) {
            node.estimate = 1 + std::max(nodes[node.left].estimate, nodes[node.right].estimate);
        }
        index = node.parent;
    }
}

bool pathContainsName(const std::vector<CompactNode>& nodes, int leafIndex, const std::string& name) {
    const auto key = RecipeGraph::normalize(name);
    for (int index = leafIndex; index >= 0; index = nodes[index].parent) {
        if (RecipeGraph::normalize(nodes[index].name) == key) {
            return true;
        }
    }
    return false;
}

CompactNode makeCompactLeaf(const RecipeGraph& graph,
                            const std::string& name,
                            int parent,
                            const std::unordered_map<std::string, int>& shortestDepths) {
    const auto canonical = graph.canonicalName(name);
    CompactNode node;
    node.name = canonical;
    node.parent = parent;
    node.basic = graph.isTerminal(canonical);
    node.estimate = leafEstimate(graph, canonical, node.basic, shortestDepths);
    return node;
}

RecipeTree compactToRecipeTree(const std::vector<CompactNode>& nodes, int index) {
    const auto& node = nodes[index];
    RecipeTree tree;
    tree.name = node.name;
    tree.basic = node.basic;
    if (node.left >= 0 && node.right >= 0) {
        tree.children.push_back(compactToRecipeTree(nodes, node.left));
        tree.children.push_back(compactToRecipeTree(nodes, node.right));
    }
    return tree;
}

std::string compactSignature(const std::vector<CompactNode>& nodes, int index) {
    const auto& node = nodes[index];
    std::vector<std::string> childSignatures;
    if (node.left >= 0 && node.right >= 0) {
        childSignatures.push_back(compactSignature(nodes, node.left));
        childSignatures.push_back(compactSignature(nodes, node.right));
        std::sort(childSignatures.begin(), childSignatures.end());
    }

    std::string signature = node.name + "(";
    for (std::size_t i = 0; i < childSignatures.size(); ++i) {
        if (i != 0) {
            signature += ",";
        }
        signature += childSignatures[i];
    }
    signature += ")";
    return signature;
}

FrontierExpansion expandFrontierState(const RecipeGraph& graph,
                                      const std::unordered_map<std::string, int>& shortestDepths,
                                      FrontierNode current) {
    FrontierExpansion output;
    if (current.openLeaves.empty()) {
        return output;
    }

    const int leafIndex = current.openLeaves.front();
    current.openLeaves.erase(current.openLeaves.begin());
    if (leafIndex < 0 || leafIndex >= static_cast<int>(current.nodes.size())) {
        return output;
    }

    const auto canonicalLeaf = current.nodes[leafIndex].name;
    output.nodesVisited = 1;
    for (const auto& recipe : graph.recipesFor(canonicalLeaf)) {
        if (pathContainsName(current.nodes, leafIndex, recipe.first) ||
            pathContainsName(current.nodes, leafIndex, recipe.second)) {
            output.cycleBlocked = true;
            continue;
        }

        FrontierNode expanded = current;
        auto left = makeCompactLeaf(graph, recipe.first, leafIndex, shortestDepths);
        auto right = makeCompactLeaf(graph, recipe.second, leafIndex, shortestDepths);
        if (left.estimate >= kUnreachableDepth || right.estimate >= kUnreachableDepth) {
            continue;
        }

        const int leftIndex = static_cast<int>(expanded.nodes.size());
        expanded.nodes.push_back(std::move(left));
        const int rightIndex = static_cast<int>(expanded.nodes.size());
        expanded.nodes.push_back(std::move(right));

        auto& expandedLeaf = expanded.nodes[leafIndex];
        expandedLeaf.left = leftIndex;
        expandedLeaf.right = rightIndex;
        expandedLeaf.basic = false;
        expandedLeaf.estimate = 1 + std::max(expanded.nodes[leftIndex].estimate, expanded.nodes[rightIndex].estimate);
        refreshAncestorEstimates(expanded.nodes, leafIndex);
        expanded.estimate = expanded.nodes[0].estimate;

        std::vector<int> newOpenLeaves;
        if (isExpandableCompactLeaf(graph, expanded.nodes[leftIndex])) {
            newOpenLeaves.push_back(leftIndex);
        }
        if (isExpandableCompactLeaf(graph, expanded.nodes[rightIndex])) {
            newOpenLeaves.push_back(rightIndex);
        }
        expanded.openLeaves.insert(expanded.openLeaves.begin(), newOpenLeaves.begin(), newOpenLeaves.end());
        output.states.push_back(std::move(expanded));
    }

    return output;
}

int normalizedThreadCount(const SearchOptions& options) {
    if (!options.useOpenmp) {
        return 1;
    }
    return std::max(1, options.threads);
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

bool openmpAvailable() {
#ifdef ALCHEMY_HAS_OPENMP
    return true;
#else
    return false;
#endif
}

int openmpMaxThreads() {
#ifdef ALCHEMY_HAS_OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
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
    auto recipes = (options_.algorithm == Algorithm::Bfs || options_.mode == SearchMode::All)
        ? expanded.trees
        : dedupeAndClip(expanded.trees, effectiveLimit(options_.limit));
    const auto end = std::chrono::steady_clock::now();

    stats_.timeMs = std::chrono::duration<double, std::milli>(end - start).count();
    stats_.cacheEntries = memoEnabled() ? static_cast<std::int64_t>(memo_.size()) : 0;
    stats_.processes = 1;
    stats_.threadsPerProcess = normalizedThreadCount(options_);
    stats_.totalWorkers = stats_.threadsPerProcess;
    stats_.threadsByRank = {stats_.threadsPerProcess};

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
    auto expanded = (options_.algorithm == Algorithm::Bfs || options_.mode == SearchMode::All)
        ? expandPartialBfsLazy(partial, partialLimit)
        : completePartialNode(partial, partialLimit, active);
    auto recipes = (options_.algorithm == Algorithm::Bfs || options_.mode == SearchMode::All)
        ? expanded.trees
        : dedupeAndClip(expanded.trees, partialLimit);
    const auto end = std::chrono::steady_clock::now();

    stats_.timeMs = std::chrono::duration<double, std::milli>(end - start).count();
    stats_.cacheEntries = memoEnabled() ? static_cast<std::int64_t>(memo_.size()) : 0;
    stats_.processes = 1;
    stats_.threadsPerProcess = normalizedThreadCount(options_);
    stats_.totalWorkers = stats_.threadsPerProcess;
    stats_.threadsByRank = {stats_.threadsPerProcess};
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

const std::unordered_map<std::string, int>& SearchEngine::shortestDepths() {
    if (shortestDepthsReady_) {
        return shortestDepths_;
    }

    shortestDepths_.clear();
    const auto elements = graph_.allElements();
    for (const auto& element : elements) {
        const auto canonical = graph_.canonicalName(element);
        if (graph_.isTerminal(canonical)) {
            shortestDepths_[canonical] = 0;
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& element : elements) {
            const auto canonical = graph_.canonicalName(element);
            if (graph_.isTerminal(canonical)) {
                continue;
            }

            int best = kUnreachableDepth;
            const auto existing = shortestDepths_.find(canonical);
            if (existing != shortestDepths_.end()) {
                best = existing->second;
            }

            for (const auto& recipe : graph_.recipesFor(canonical)) {
                const auto left = shortestDepths_.find(recipe.first);
                const auto right = shortestDepths_.find(recipe.second);
                if (left == shortestDepths_.end() || right == shortestDepths_.end()) {
                    continue;
                }
                const int candidate = 1 + std::max(left->second, right->second);
                if (candidate < best) {
                    best = candidate;
                    shortestDepths_[canonical] = candidate;
                    changed = true;
                }
            }
        }
    }

    shortestDepthsReady_ = true;
    return shortestDepths_;
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
    if (options_.mode != SearchMode::All && options_.algorithm == Algorithm::Dfs) {
        return expandElementDfs(name, limit, active);
    }
    (void)active;
    return expandElementBfsLazy(name, limit);
}

SearchEngine::ExpandResult SearchEngine::expandElementBfsLazy(const std::string& name, int limit) {
    RecipeTree root;
    root.name = graph_.canonicalName(name);
    root.basic = graph_.isTerminal(root.name);
    return expandPartialBfsLazy(root, limit);
}

SearchEngine::ExpandResult SearchEngine::expandPartialBfsLazy(const RecipeTree& partial, int limit) {
    if (options_.useOpenmp && options_.threads > 1 && openmpAvailable() &&
        options_.mode != SearchMode::All && limit > 1) {
        return expandPartialBfsLazyOpenmp(partial, limit);
    }

    ExpandResult result;
    const auto& depthMap = shortestDepths();
    std::priority_queue<FrontierNode, std::vector<FrontierNode>, FrontierNodeCompare> frontier;
    std::unordered_set<std::string> seenFinalSignatures;
    std::size_t nextSequence = 0;

    auto pushState = [&](FrontierNode state) {
        if (state.estimate >= kUnreachableDepth) {
            return;
        }
        state.sequence = nextSequence++;
        frontier.push(std::move(state));
    };

    pushState(makeFrontierNode(graph_, partial, depthMap, nextSequence));

    while (!frontier.empty() && !limitReached(result.trees.size(), limit)) {
        auto current = frontier.top();
        frontier.pop();

        if (current.openLeaves.empty()) {
            const auto signature = compactSignature(current.nodes, 0);
            if (seenFinalSignatures.insert(signature).second) {
                result.trees.push_back(compactToRecipeTree(current.nodes, 0));
                printBfsProgress(current.estimate, result.trees.size());
            }
            continue;
        }

        const int leafIndex = current.openLeaves.front();
        current.openLeaves.erase(current.openLeaves.begin());
        if (leafIndex < 0 || leafIndex >= static_cast<int>(current.nodes.size())) {
            continue;
        }

        const auto canonicalLeaf = current.nodes[leafIndex].name;
        ++stats_.nodesVisited;
        maybePrintNodeProgress();

        for (const auto& recipe : graph_.recipesFor(canonicalLeaf)) {
            if (pathContainsName(current.nodes, leafIndex, recipe.first) ||
                pathContainsName(current.nodes, leafIndex, recipe.second)) {
                result.cycleBlocked = true;
                continue;
            }

            FrontierNode expanded = current;
            auto left = makeCompactLeaf(graph_, recipe.first, leafIndex, depthMap);
            auto right = makeCompactLeaf(graph_, recipe.second, leafIndex, depthMap);
            if (left.estimate >= kUnreachableDepth || right.estimate >= kUnreachableDepth) {
                continue;
            }

            const int leftIndex = static_cast<int>(expanded.nodes.size());
            expanded.nodes.push_back(std::move(left));
            const int rightIndex = static_cast<int>(expanded.nodes.size());
            expanded.nodes.push_back(std::move(right));

            auto& expandedLeaf = expanded.nodes[leafIndex];
            expandedLeaf.left = leftIndex;
            expandedLeaf.right = rightIndex;
            expandedLeaf.basic = false;
            expandedLeaf.estimate = 1 + std::max(expanded.nodes[leftIndex].estimate, expanded.nodes[rightIndex].estimate);
            refreshAncestorEstimates(expanded.nodes, leafIndex);
            expanded.estimate = expanded.nodes[0].estimate;

            std::vector<int> newOpenLeaves;
            if (isExpandableCompactLeaf(graph_, expanded.nodes[leftIndex])) {
                newOpenLeaves.push_back(leftIndex);
            }
            if (isExpandableCompactLeaf(graph_, expanded.nodes[rightIndex])) {
                newOpenLeaves.push_back(rightIndex);
            }
            expanded.openLeaves.insert(expanded.openLeaves.begin(), newOpenLeaves.begin(), newOpenLeaves.end());
            pushState(std::move(expanded));
        }
    }

    return result;
}

SearchEngine::ExpandResult SearchEngine::expandPartialBfsLazyOpenmp(const RecipeTree& partial, int limit) {
#ifndef ALCHEMY_HAS_OPENMP
    return expandPartialBfsLazy(partial, limit);
#else
    ExpandResult result;
    const auto& depthMap = shortestDepths();
    std::priority_queue<FrontierNode, std::vector<FrontierNode>, FrontierNodeCompare> frontier;
    std::unordered_set<std::string> seenFinalSignatures;
    std::size_t nextSequence = 0;

    auto pushState = [&](FrontierNode state) {
        if (state.estimate >= kUnreachableDepth) {
            return;
        }
        state.sequence = nextSequence++;
        frontier.push(std::move(state));
    };

    pushState(makeFrontierNode(graph_, partial, depthMap, nextSequence));

    const int threadCount = std::max(1, options_.threads);
    const std::size_t batchLimit = static_cast<std::size_t>(std::max(8, threadCount * 8));
    while (!frontier.empty() && !limitReached(result.trees.size(), limit)) {
        auto current = frontier.top();
        frontier.pop();

        if (current.openLeaves.empty()) {
            const auto signature = compactSignature(current.nodes, 0);
            if (seenFinalSignatures.insert(signature).second) {
                result.trees.push_back(compactToRecipeTree(current.nodes, 0));
                printBfsProgress(current.estimate, result.trees.size());
            }
            continue;
        }

        std::vector<FrontierNode> batch;
        batch.reserve(batchLimit);
        const int batchEstimate = current.estimate;
        batch.push_back(std::move(current));
        while (batch.size() < batchLimit && !frontier.empty()) {
            const auto& next = frontier.top();
            if (next.estimate != batchEstimate || next.openLeaves.empty()) {
                break;
            }
            batch.push_back(next);
            frontier.pop();
        }

        std::vector<FrontierExpansion> expansions(batch.size());
#pragma omp parallel for schedule(dynamic) num_threads(threadCount)
        for (int i = 0; i < static_cast<int>(batch.size()); ++i) {
            expansions[static_cast<std::size_t>(i)] =
                expandFrontierState(graph_, depthMap, std::move(batch[static_cast<std::size_t>(i)]));
        }

        for (auto& expansion : expansions) {
            stats_.nodesVisited += expansion.nodesVisited;
            result.cycleBlocked = result.cycleBlocked || expansion.cycleBlocked;
            maybePrintNodeProgress();
            for (auto& state : expansion.states) {
                pushState(std::move(state));
            }
        }
    }

    return result;
#endif
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
    std::stable_sort(result.trees.begin(), result.trees.end(), [](const RecipeTree& left, const RecipeTree& right) {
        const int leftDepth = treeDepth(left);
        const int rightDepth = treeDepth(right);
        if (leftDepth != rightDepth) {
            return leftDepth < rightDepth;
        }
        return treeSignature(left) < treeSignature(right);
    });
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
