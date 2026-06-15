#pragma once

#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/RecipeGraph.hpp"
#include "common/RecipeTree.hpp"
#include "common/Statistics.hpp"

namespace alchemy {

enum class Algorithm {
    Bfs,
    Dfs,
};

enum class SearchMode {
    Single,
    Multiple,
    All,
};

enum class TraceMode {
    Full,
    Memo,
};

enum class VisualMode {
    Full,
    Shared,
};

struct SearchOptions {
    Algorithm algorithm = Algorithm::Bfs;
    SearchMode mode = SearchMode::Multiple;
    TraceMode traceMode = TraceMode::Memo;
    int limit = 5;
    int threads = 1;
    bool useOpenmp = false;
    bool progress = false;
};

struct SearchResult {
    std::string target;
    std::vector<RecipeTree> recipes;
    SearchStats stats;
};

std::string toString(Algorithm algorithm);
std::string toString(SearchMode mode);
std::string toString(TraceMode mode);
std::string toString(VisualMode mode);
Algorithm parseAlgorithm(const std::string& value);
SearchMode parseSearchMode(const std::string& value);
TraceMode parseTraceMode(const std::string& value);
VisualMode parseVisualMode(const std::string& value);
bool openmpAvailable();
int openmpMaxThreads();

class SearchEngine {
public:
    SearchEngine(const RecipeGraph& graph, SearchOptions options);

    SearchResult search(const std::string& target, bool resetMemo = true);
    SearchResult completePartial(const RecipeTree& partial, int limit, bool resetMemo = false);
    void clearMemo();
    std::size_t memoSize() const;

private:
    struct ExpandResult {
        std::vector<RecipeTree> trees;
        bool cycleBlocked = false;
    };

    ExpandResult expandElementAny(const std::string& name, int limit, std::unordered_set<std::string>& active);
    ExpandResult expandDirectRecipes(const std::string& name, std::unordered_set<std::string>& active);
    ExpandResult expandElementBfsLazy(const std::string& name, int limit);
    ExpandResult expandPartialBfsLazy(const RecipeTree& partial, int limit);
    ExpandResult expandPartialBfsLazyOpenmp(const RecipeTree& partial, int limit);
    ExpandResult expandElementDfs(const std::string& name, int limit, std::unordered_set<std::string>& active);
    ExpandResult expandElementBfsBounded(const std::string& name, int depthRemaining, int limit, std::unordered_set<std::string>& active);
    ExpandResult completePartialNode(const RecipeTree& partial, int limit, std::unordered_set<std::string>& active);

    bool memoEnabled() const;
    int effectiveLimit(int requested) const;
    int maxSearchDepth() const;
    const std::unordered_map<std::string, int>& shortestDepths();
    ExpandResult memoHit(const std::string& key, int limit) const;
    void memoStore(const std::string& key, const ExpandResult& result);
    void maybePrintNodeProgress();
    void printBfsProgress(int depth, std::size_t recipesFound) const;

    const RecipeGraph& graph_;
    SearchOptions options_;
    std::unordered_map<std::string, std::vector<RecipeTree>> memo_;
    std::unordered_map<std::string, int> shortestDepths_;
    bool shortestDepthsReady_ = false;
    mutable SearchStats stats_;
    std::chrono::steady_clock::time_point startedAt_;
    std::int64_t nextProgressNode_ = 1000;
};

}  // namespace alchemy
