#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "common/RecipeGraph.hpp"

namespace alchemy {

class TierCatalog {
public:
    static TierCatalog loadFromFile(const std::string& path);

    void validateAgainstGraph(const RecipeGraph& graph) const;
    void applyTerminals(RecipeGraph& graph) const;

    std::vector<std::string> tiers() const;
    std::vector<std::string> elementsForTier(const std::string& tier) const;
    std::vector<std::string> filterElements(const std::string& tier, const std::string& filter) const;
    std::size_t uniqueElementCount() const;

private:
    std::unordered_map<std::string, std::vector<std::string>> elementsByTier_;
    std::vector<std::string> tierOrder_;
};

}  // namespace alchemy
