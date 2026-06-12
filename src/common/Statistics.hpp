#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace alchemy {

struct SearchStats {
    double timeMs = 0.0;
    double communicationMs = 0.0;
    std::int64_t nodesVisited = 0;
    std::int64_t cacheHits = 0;
    std::int64_t cacheEntries = 0;
    std::int64_t tasksProcessed = 0;
    std::int64_t directRecipesAvailable = 0;
    int processes = 1;
    double speedup = 0.0;
    double efficiency = 0.0;
    std::vector<std::int64_t> nodesVisitedByRank;
    std::vector<std::int64_t> cacheHitsByRank;
    std::vector<std::int64_t> cacheEntriesByRank;
    std::vector<std::int64_t> tasksProcessedByRank;
    std::vector<std::string> rankHostnames;
};

nlohmann::json statsToJson(const SearchStats& stats);
SearchStats statsFromJson(const nlohmann::json& json);
void addStats(SearchStats& total, const SearchStats& part);
std::string rankVectorToString(const std::vector<std::int64_t>& values);
std::string stringVectorToString(const std::vector<std::string>& values);

}  // namespace alchemy
