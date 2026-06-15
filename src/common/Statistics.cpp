#include "common/Statistics.hpp"

#include <algorithm>
#include <sstream>

namespace alchemy {

nlohmann::json statsToJson(const SearchStats& stats) {
    return {
        {"time_ms", stats.timeMs},
        {"communication_ms", stats.communicationMs},
        {"nodes_visited", stats.nodesVisited},
        {"cache_hits", stats.cacheHits},
        {"cache_entries", stats.cacheEntries},
        {"tasks_processed", stats.tasksProcessed},
        {"direct_recipes_available", stats.directRecipesAvailable},
        {"processes", stats.processes},
        {"threads_per_process", stats.threadsPerProcess},
        {"total_workers", stats.totalWorkers},
        {"speedup", stats.speedup},
        {"efficiency", stats.efficiency},
        {"nodes_visited_by_rank", stats.nodesVisitedByRank},
        {"cache_hits_by_rank", stats.cacheHitsByRank},
        {"cache_entries_by_rank", stats.cacheEntriesByRank},
        {"tasks_processed_by_rank", stats.tasksProcessedByRank},
        {"threads_by_rank", stats.threadsByRank},
        {"rank_hostnames", stats.rankHostnames},
    };
}

SearchStats statsFromJson(const nlohmann::json& json) {
    SearchStats stats;
    stats.timeMs = json.value("time_ms", 0.0);
    stats.communicationMs = json.value("communication_ms", 0.0);
    stats.nodesVisited = json.value("nodes_visited", 0LL);
    stats.cacheHits = json.value("cache_hits", 0LL);
    stats.cacheEntries = json.value("cache_entries", 0LL);
    stats.tasksProcessed = json.value("tasks_processed", 0LL);
    stats.directRecipesAvailable = json.value("direct_recipes_available", 0LL);
    stats.processes = json.value("processes", 1);
    stats.threadsPerProcess = json.value("threads_per_process", 1);
    stats.totalWorkers = json.value("total_workers", std::max(1, stats.processes * stats.threadsPerProcess));
    stats.speedup = json.value("speedup", 0.0);
    stats.efficiency = json.value("efficiency", 0.0);
    stats.nodesVisitedByRank = json.value("nodes_visited_by_rank", std::vector<std::int64_t>{});
    stats.cacheHitsByRank = json.value("cache_hits_by_rank", std::vector<std::int64_t>{});
    stats.cacheEntriesByRank = json.value("cache_entries_by_rank", std::vector<std::int64_t>{});
    stats.tasksProcessedByRank = json.value("tasks_processed_by_rank", std::vector<std::int64_t>{});
    stats.threadsByRank = json.value("threads_by_rank", std::vector<int>{});
    stats.rankHostnames = json.value("rank_hostnames", std::vector<std::string>{});
    return stats;
}

void addStats(SearchStats& total, const SearchStats& part) {
    total.timeMs += part.timeMs;
    total.communicationMs += part.communicationMs;
    total.nodesVisited += part.nodesVisited;
    total.cacheHits += part.cacheHits;
    total.tasksProcessed += part.tasksProcessed;
    total.directRecipesAvailable += part.directRecipesAvailable;
    total.cacheEntries += part.cacheEntries;
}

std::string rankVectorToString(const std::vector<std::int64_t>& values) {
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

std::string stringVectorToString(const std::vector<std::string>& values) {
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

}  // namespace alchemy
