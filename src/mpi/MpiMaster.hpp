#pragma once

#include <vector>

#include "common/Cli.hpp"
#include "common/RecipeGraph.hpp"
#include "common/RecipeTree.hpp"
#include "common/Search.hpp"
#include "common/Visualizer.hpp"

namespace alchemy {

constexpr int MPI_TAG_REQUEST = 100;
constexpr int MPI_TAG_TASK = 101;
constexpr int MPI_TAG_RESULT = 102;
constexpr int MPI_TAG_STOP = 103;

struct MpiRunResult {
    SearchResult search;
    OutputFiles outputs;
};

std::vector<RecipeTree> buildMpiTasks(const RecipeGraph& graph,
                                      const std::string& target,
                                      int splitDepth,
                                      int maxTasks);

MpiRunResult runMpiMaster(AppOptions options,
                          const RecipeGraph& graph,
                          int worldSize,
                          const std::vector<std::string>& rankHostnames = {});

}  // namespace alchemy
