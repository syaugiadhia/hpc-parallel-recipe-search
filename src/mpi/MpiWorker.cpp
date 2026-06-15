#include "mpi/MpiWorker.hpp"

#include <algorithm>
#include <stdexcept>

#include <mpi.h>
#include <nlohmann/json.hpp>

#include "common/RecipeTree.hpp"
#include "common/Search.hpp"
#include "common/Statistics.hpp"
#include "mpi/MpiMaster.hpp"

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

std::string resultToPayload(int rank, int taskId, const SearchResult& result, double communicationMs) {
    auto stats = result.stats;
    stats.communicationMs = communicationMs;
    stats.processes = 1;

    nlohmann::json payload;
    payload["rank"] = rank;
    payload["task_id"] = taskId;
    payload["stats"] = statsToJson(stats);
    payload["recipes"] = nlohmann::json::array();
    for (const auto& recipe : result.recipes) {
        payload["recipes"].push_back(treeToJson(recipe));
    }
    return payload.dump();
}

}  // namespace

void runMpiWorker(const AppOptions& options, const RecipeGraph& graph, int rank) {
    auto workerOptions = options;
    if (workerOptions.mode == SearchMode::All) {
        workerOptions.progress = false;
        workerOptions.algorithm = Algorithm::Bfs;
    }
    SearchEngine engine(graph, workerOptions);
    double communicationSinceLastResult = 0.0;

    while (true) {
        sendString(0, MPI_TAG_REQUEST, "", communicationSinceLastResult);

        MPI_Status status;
        const double probeStart = MPI_Wtime();
        MPI_Probe(0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
        communicationSinceLastResult += MPI_Wtime() - probeStart;

        if (status.MPI_TAG == MPI_TAG_STOP) {
            receiveString(status, communicationSinceLastResult);
            break;
        }
        if (status.MPI_TAG != MPI_TAG_TASK) {
            throw std::runtime_error("MPI worker received unexpected message tag");
        }

        const auto taskText = receiveString(status, communicationSinceLastResult);
        const auto task = nlohmann::json::parse(taskText);
        const int taskId = task.value("id", -1);
        const RecipeTree partial = treeFromJson(task.at("partial"));

        const int taskLimit = options.mode == SearchMode::All ? 1 : options.limit;
        auto result = engine.completePartial(partial, taskLimit, false);
        const std::string payload = resultToPayload(rank, taskId, result, communicationSinceLastResult * 1000.0);
        communicationSinceLastResult = 0.0;
        sendString(0, MPI_TAG_RESULT, payload, communicationSinceLastResult);
    }
}

}  // namespace alchemy
