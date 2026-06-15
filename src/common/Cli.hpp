#pragma once

#include <string>
#include <vector>

#include "common/Search.hpp"

namespace alchemy {

enum class RenderMode {
    Json,
    Full,
};

struct AppOptions : SearchOptions {
    std::string dataPath = "data/recipes.json";
    std::string tiersPath = "data/tiers.json";
    std::string target;
    std::string benchmarkPath;
    std::string tierFilter = "all";
    std::string nameFilter;
    VisualMode visualMode = VisualMode::Full;
    RenderMode renderMode = RenderMode::Json;
    std::string outputPrefix = "results/alchemy";
    int maxVisualDepth = -1;
    std::string imageFormat = "png";
    int splitDepth = 1;
    std::string threadProfile;
    std::vector<int> threadsByRank;
    double baselineMs = 0.0;
    bool listElements = false;
    bool help = false;
};

std::string toString(RenderMode mode);
RenderMode parseRenderMode(const std::string& value);
AppOptions parseArgs(int argc, char** argv, bool mpiMode);
std::string usageText(bool mpiMode);
std::vector<std::string> readBenchmarkTargets(const std::string& path);
std::string sanitizeForPath(const std::string& value);
void ensureParentDirectory(const std::string& filePrefix);

}  // namespace alchemy
