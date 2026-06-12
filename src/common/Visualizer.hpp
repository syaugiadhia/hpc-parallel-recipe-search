#pragma once

#include <string>
#include <vector>

#include "common/Cli.hpp"
#include "common/RecipeTree.hpp"
#include "common/Statistics.hpp"

namespace alchemy {

struct OutputFiles {
    std::string dotPath;
    std::string imagePath;
    std::string jsonPath;
    bool imageRendered = false;
    std::string renderWarning;
};

class Visualizer {
public:
    static std::string buildDot(const std::vector<RecipeTree>& recipes, const AppOptions& options);
    static OutputFiles writeOutputs(const std::string& target,
                                    const std::vector<RecipeTree>& recipes,
                                    const SearchStats& stats,
                                    const AppOptions& options);
};

}  // namespace alchemy
