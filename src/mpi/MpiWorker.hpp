#pragma once

#include "common/Cli.hpp"
#include "common/RecipeGraph.hpp"

namespace alchemy {

void runMpiWorker(const AppOptions& options, const RecipeGraph& graph, int rank);

}  // namespace alchemy
