#include "mpi/MpiMaster.hpp"

#include <algorithm>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <unordered_set>

#include <mpi.h>
#include <nlohmann/json.hpp>

#include "common/Visualizer.hpp"

namespace alchemy {
namespace {

void sendString(int dest, int tag, const std::string& value, double& commSeconds) {
    const double start = MPI_Wtime();
    MPI_Send(value.empty() ? nullptr : value.data(), static_cast<int>(value.size()), MPI_CHAR, dest, tag, MPI_COMM_WORLD);
    commSeconds += MPI_Wtime() - start;
}

std::string receiveString(const MPI_Status& status, double& commSeconds) {
    int count = 0;
    MPI_Get_count(&status, MPI_CHAR, &count);
    std::string payload(static_cast<std::size_t>(std::max(0, count)), '\0');
    const double start = MPI_Wtime();
    MPI_Recv(payload.empty() ? nullptr : payload.data(), count, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    commSeconds += MPI_Wtime() - start;
    return payload;
}

void receiveEmptyRequest(const MPI_Status& status, double& commSeconds) {
    const double start = MPI_Wtime();
    MPI_Recv(nullptr, 0, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    commSeconds += MPI_Wtime() - start;
}

bool isExpandableLeaf(const RecipeGraph& graph, const RecipeTree& node) {
    return node.children.empty() && graph.hasElement(node.name) && !graph.isTerminal(node.name) && graph.hasRecipes(node.name);
}

bool limitReached(std::size_t size, int limit) {
    return limit > 0 && static_cast<int>(size) >= limit;
}

std::vector<RecipeTree> expandForSplit(const RecipeGraph& graph,
                                       RecipeTree node,
                                       int remainingDepth,
                                       std::unordered_set<std::string>& active,
                                       int maxTasks) {
    if (!graph.hasElement(node.name)) {
        return {node};
    }

    node.name = graph.canonicalName(node.name);
    node.basic = graph.isTerminal(node.name);
    if (remainingDepth <= 0 || node.basic) {
        return {node};
    }

    const auto activeKey = RecipeGraph::normalize(node.name);
    if (node.children.empty()) {
        if (!graph.hasRecipes(node.name) || active.find(activeKey) != active.end()) {
            return {node};
        }

        std::vector<RecipeTree> tasks;
        active.insert(activeKey);
        for (const auto& recipe : graph.recipesFor(node.name)) {
            RecipeTree forced;
            forced.name = node.name;
            forced.children = {RecipeTree{recipe.first, graph.isTerminal(recipe.first), false, false, {}, {}},
                               RecipeTree{recipe.second, graph.isTerminal(recipe.second), false, false, {}, {}}};
            auto expanded = expandForSplit(graph, forced, remainingDepth - 1, active, maxTasks);
            for (auto& task : expanded) {
                tasks.push_back(task);
                if (limitReached(tasks.size(), maxTasks)) {
                    active.erase(activeKey);
                    return tasks;
                }
            }
        }
        active.erase(activeKey);
        return tasks.empty() ? std::vector<RecipeTree>{node} : tasks;
    }

    active.insert(activeKey);
    for (std::size_t i = 0; i < node.children.size(); ++i) {
        if (!isExpandableLeaf(graph, node.children[i])) {
            continue;
        }
        auto variants = expandForSplit(graph, node.children[i], remainingDepth, active, maxTasks);
        std::vector<RecipeTree> tasks;
        for (auto& variant : variants) {
            RecipeTree copy = node;
            copy.children[i] = variant;
            tasks.push_back(copy);
            if (limitReached(tasks.size(), maxTasks)) {
                active.erase(activeKey);
                return tasks;
            }
        }
        active.erase(activeKey);
        return tasks.empty() ? std::vector<RecipeTree>{node} : tasks;
    }
    active.erase(activeKey);
    return {node};
}

std::vector<RecipeTree> buildDirectRecipeTasks(const RecipeGraph& graph, const std::string& target) {
    if (!graph.hasElement(target)) {
        throw std::runtime_error("Target is not available in recipe data: " + target);
    }

    RecipeTree root;
    root.name = graph.canonicalName(target);
    root.basic = graph.isTerminal(root.name);
    if (root.basic) {
        return {root};
    }

    std::vector<RecipeTree> tasks;
    for (const auto& recipe : graph.recipesFor(root.name)) {
        RecipeTree task;
        task.name = root.name;
        task.children = {RecipeTree{recipe.first, graph.isTerminal(recipe.first), false, false, {}, {}},
                         RecipeTree{recipe.second, graph.isTerminal(recipe.second), false, false, {}, {}}};
        tasks.push_back(task);
    }
    return tasks;
}

std::string taskToPayload(int id, const RecipeTree& partial) {
    nlohmann::json json;
    json["id"] = id;
    json["partial"] = treeToJson(partial);
    return json.dump();
}

void mergeWorkerResult(const nlohmann::json& payload,
                       int limit,
                       std::vector<RecipeTree>& recipes,
                       std::vector<int>* taskIds,
                       std::unordered_set<std::string>& seen,
                       SearchStats& stats) {
    const int rank = payload.value("rank", 0);
    const int taskId = payload.value("task_id", -1);
    const auto partStats = statsFromJson(payload.at("stats"));

    if (rank >= 0 && rank < static_cast<int>(stats.nodesVisitedByRank.size())) {
        stats.nodesVisitedByRank[rank] += partStats.nodesVisited;
        stats.cacheHitsByRank[rank] += partStats.cacheHits;
        stats.tasksProcessedByRank[rank] += partStats.tasksProcessed;
        stats.cacheEntriesByRank[rank] = std::max(stats.cacheEntriesByRank[rank], partStats.cacheEntries);
    }
    stats.communicationMs += partStats.communicationMs;

    if (payload.contains("recipes")) {
        for (const auto& recipeJson : payload.at("recipes")) {
            RecipeTree tree = treeFromJson(recipeJson);
            const auto signature = treeSignature(tree);
            if (seen.insert(signature).second) {
                recipes.push_back(tree);
                if (taskIds != nullptr) {
                    taskIds->push_back(taskId);
                }
                if (limitReached(recipes.size(), limit)) {
                    break;
                }
            }
        }
    }
}

void sortFinalRecipes(std::vector<RecipeTree>& recipes,
                      Algorithm algorithm,
                      SearchMode mode,
                      const std::vector<int>* taskIds) {
    if (mode == SearchMode::All && taskIds != nullptr && taskIds->size() == recipes.size()) {
        std::vector<std::size_t> order(recipes.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t leftIndex, std::size_t rightIndex) {
            if (algorithm == Algorithm::Bfs) {
                const int leftDepth = treeDepth(recipes[leftIndex]);
                const int rightDepth = treeDepth(recipes[rightIndex]);
                if (leftDepth != rightDepth) {
                    return leftDepth < rightDepth;
                }
            }
            if ((*taskIds)[leftIndex] != (*taskIds)[rightIndex]) {
                return (*taskIds)[leftIndex] < (*taskIds)[rightIndex];
            }
            return treeSignature(recipes[leftIndex]) < treeSignature(recipes[rightIndex]);
        });

        std::vector<RecipeTree> sorted;
        sorted.reserve(recipes.size());
        for (const auto index : order) {
            sorted.push_back(recipes[index]);
        }
        recipes = std::move(sorted);
        return;
    }

    if (algorithm == Algorithm::Bfs) {
        std::stable_sort(recipes.begin(), recipes.end(), [](const RecipeTree& left, const RecipeTree& right) {
            const int leftDepth = treeDepth(left);
            const int rightDepth = treeDepth(right);
            if (leftDepth != rightDepth) {
                return leftDepth < rightDepth;
            }
            return treeSignature(left) < treeSignature(right);
        });
    } else {
        std::stable_sort(recipes.begin(), recipes.end(), [](const RecipeTree& left, const RecipeTree& right) {
            return treeSignature(left) < treeSignature(right);
        });
    }
}

}  // namespace

std::vector<RecipeTree> buildMpiTasks(const RecipeGraph& graph,
                                      const std::string& target,
                                      int splitDepth,
                                      int maxTasks) {
    if (!graph.hasElement(target)) {
        throw std::runtime_error("Target is not available in recipe data: " + target);
    }
    RecipeTree root;
    root.name = graph.canonicalName(target);
    root.basic = graph.isTerminal(root.name);
    std::unordered_set<std::string> active;
    auto tasks = expandForSplit(graph, root, splitDepth, active, maxTasks);
    return tasks.empty() ? std::vector<RecipeTree>{root} : tasks;
}

MpiRunResult runMpiMaster(AppOptions options,
                          const RecipeGraph& graph,
                          int worldSize,
                          const std::vector<std::string>& rankHostnames) {
    if (options.mode == SearchMode::All) {
        options.algorithm = Algorithm::Bfs;
    }
    if (worldSize <= 1) {
        SearchEngine engine(graph, options);
        auto result = engine.search(options.target);
        result.stats.processes = 1;
        result.stats.rankHostnames = rankHostnames;
        auto outputs = Visualizer::writeOutputs(result.target, result.recipes, result.stats, options);
        return {result, outputs};
    }

    const int limit = options.mode == SearchMode::Single ? 1 : (options.mode == SearchMode::All ? 0 : std::max(1, options.limit));
    const int maxTasks = options.mode == SearchMode::All ? 0 : std::max(64, limit * worldSize * std::max(2, options.splitDepth + 1));
    auto tasks = options.mode == SearchMode::All
        ? buildDirectRecipeTasks(graph, options.target)
        : buildMpiTasks(graph, options.target, options.splitDepth, maxTasks);

    std::vector<std::string> payloads;
    payloads.reserve(tasks.size());
    for (std::size_t i = 0; i < tasks.size(); ++i) {
        payloads.push_back(taskToPayload(static_cast<int>(i), tasks[i]));
    }

    SearchStats stats;
    stats.processes = worldSize;
    stats.rankHostnames = rankHostnames;
    stats.nodesVisitedByRank.assign(static_cast<std::size_t>(worldSize), 0);
    stats.cacheHitsByRank.assign(static_cast<std::size_t>(worldSize), 0);
    stats.cacheEntriesByRank.assign(static_cast<std::size_t>(worldSize), 0);
    stats.tasksProcessedByRank.assign(static_cast<std::size_t>(worldSize), 0);

    std::vector<RecipeTree> recipes;
    std::vector<int> recipeTaskIds;
    std::unordered_set<std::string> seen;
    std::size_t nextTask = 0;
    int activeWorkers = worldSize - 1;
    double masterCommSeconds = 0.0;
    const double start = MPI_Wtime();
    int resultMessages = 0;

    while (activeWorkers > 0) {
        MPI_Status status;
        const double probeStart = MPI_Wtime();
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        masterCommSeconds += MPI_Wtime() - probeStart;

        if (status.MPI_TAG == MPI_TAG_REQUEST) {
            receiveEmptyRequest(status, masterCommSeconds);
            if (nextTask < payloads.size() && !limitReached(recipes.size(), limit)) {
                sendString(status.MPI_SOURCE, MPI_TAG_TASK, payloads[nextTask], masterCommSeconds);
                ++nextTask;
            } else {
                sendString(status.MPI_SOURCE, MPI_TAG_STOP, "", masterCommSeconds);
                --activeWorkers;
            }
        } else if (status.MPI_TAG == MPI_TAG_RESULT) {
            const auto payloadText = receiveString(status, masterCommSeconds);
            const auto payload = nlohmann::json::parse(payloadText);
            mergeWorkerResult(payload, limit, recipes, options.mode == SearchMode::All ? &recipeTaskIds : nullptr, seen, stats);
            ++resultMessages;
            if (options.mode == SearchMode::All && (resultMessages <= 10 || resultMessages % 10 == 0)) {
                std::cerr << "[progress] mpi_results=" << resultMessages
                          << " tasks_sent=" << nextTask
                          << " total_tasks=" << payloads.size()
                          << " recipes_found=" << recipes.size()
                          << " elapsed_ms=" << ((MPI_Wtime() - start) * 1000.0)
                          << "\n";
            }
        } else {
            throw std::runtime_error("MPI master received unexpected message tag");
        }
    }

    sortFinalRecipes(recipes, options.algorithm, options.mode, options.mode == SearchMode::All ? &recipeTaskIds : nullptr);
    if (limit > 0 && static_cast<int>(recipes.size()) > limit) {
        recipes.resize(static_cast<std::size_t>(limit));
    }

    stats.timeMs = (MPI_Wtime() - start) * 1000.0;
    stats.communicationMs += masterCommSeconds * 1000.0;
    stats.nodesVisited = std::accumulate(stats.nodesVisitedByRank.begin(), stats.nodesVisitedByRank.end(), 0LL);
    stats.cacheHits = std::accumulate(stats.cacheHitsByRank.begin(), stats.cacheHitsByRank.end(), 0LL);
    stats.cacheEntries = std::accumulate(stats.cacheEntriesByRank.begin(), stats.cacheEntriesByRank.end(), 0LL);
    stats.tasksProcessed = std::accumulate(stats.tasksProcessedByRank.begin(), stats.tasksProcessedByRank.end(), 0LL);
    if (options.mode == SearchMode::All) {
        stats.directRecipesAvailable = static_cast<std::int64_t>(graph.recipesFor(options.target).size());
    }
    if (options.baselineMs > 0.0 && stats.timeMs > 0.0) {
        stats.speedup = options.baselineMs / stats.timeMs;
        stats.efficiency = stats.speedup / static_cast<double>(worldSize);
    }

    const auto canonicalTarget = graph.canonicalName(options.target);
    auto outputs = Visualizer::writeOutputs(canonicalTarget, recipes, stats, options);
    return {SearchResult{canonicalTarget, recipes, stats}, outputs};
}

}  // namespace alchemy
