#include "common/TierCatalog.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace alchemy {
namespace {

std::string lower(const std::string& value) {
    return RecipeGraph::normalize(value);
}

bool containsCaseInsensitive(const std::string& value, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return lower(value).find(lower(needle)) != std::string::npos;
}

std::vector<std::string> requiredTierOrder() {
    std::vector<std::string> order{"starter", "special"};
    for (int i = 1; i <= 15; ++i) {
        order.push_back("tier" + std::to_string(i));
    }
    return order;
}

}  // namespace

TierCatalog TierCatalog::loadFromFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open tier catalog JSON file: " + path);
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to parse tier catalog '" + path + "': " + error.what());
    }
    if (!json.is_object()) {
        throw std::runtime_error("Invalid tier catalog: root must be an object");
    }

    TierCatalog catalog;
    catalog.tierOrder_ = requiredTierOrder();
    std::unordered_set<std::string> seen;
    for (const auto& tier : catalog.tierOrder_) {
        if (!json.contains(tier) || !json.at(tier).is_array()) {
            throw std::runtime_error("Invalid tier catalog: missing array '" + tier + "'");
        }
        for (const auto& item : json.at(tier)) {
            if (!item.is_string()) {
                throw std::runtime_error("Invalid tier catalog: tier '" + tier + "' contains non-string item");
            }
            const std::string name = item.get<std::string>();
            const std::string key = lower(name);
            if (!seen.insert(key).second) {
                throw std::runtime_error("Invalid tier catalog: duplicate element '" + name + "'");
            }
            catalog.elementsByTier_[tier].push_back(name);
        }
    }

    if (seen.size() != 720) {
        std::ostringstream error;
        error << "Invalid tier catalog: expected 720 unique elements, got " << seen.size();
        throw std::runtime_error(error.str());
    }
    return catalog;
}

void TierCatalog::validateAgainstGraph(const RecipeGraph& graph) const {
    std::vector<std::string> missing;
    for (const auto& tier : tierOrder_) {
        const auto found = elementsByTier_.find(tier);
        if (found == elementsByTier_.end()) {
            continue;
        }
        for (const auto& name : found->second) {
            if (!graph.hasElement(name)) {
                missing.push_back(name);
            }
        }
    }
    if (!missing.empty()) {
        std::ostringstream error;
        error << "Tier catalog contains elements not found in recipe data:";
        for (std::size_t i = 0; i < missing.size() && i < 20; ++i) {
            error << (i == 0 ? " " : ", ") << missing[i];
        }
        if (missing.size() > 20) {
            error << ", ...";
        }
        throw std::runtime_error(error.str());
    }
}

void TierCatalog::applyTerminals(RecipeGraph& graph) const {
    for (const auto& tier : {"starter", "special"}) {
        const auto found = elementsByTier_.find(tier);
        if (found == elementsByTier_.end()) {
            continue;
        }
        for (const auto& name : found->second) {
            if (graph.hasElement(name)) {
                graph.markTerminal(name);
            }
        }
    }
}

std::vector<std::string> TierCatalog::tiers() const {
    return tierOrder_;
}

std::vector<std::string> TierCatalog::elementsForTier(const std::string& tier) const {
    if (tier == "all") {
        std::vector<std::string> all;
        for (const auto& name : tierOrder_) {
            const auto found = elementsByTier_.find(name);
            if (found != elementsByTier_.end()) {
                all.insert(all.end(), found->second.begin(), found->second.end());
            }
        }
        return all;
    }

    const auto found = elementsByTier_.find(tier);
    if (found == elementsByTier_.end()) {
        throw std::runtime_error("Unknown tier '" + tier + "'. Expected all, starter, special, or tier1..tier15.");
    }
    return found->second;
}

std::vector<std::string> TierCatalog::filterElements(const std::string& tier, const std::string& filter) const {
    auto elements = elementsForTier(tier);
    std::vector<std::string> filtered;
    for (const auto& element : elements) {
        if (containsCaseInsensitive(element, filter)) {
            filtered.push_back(element);
        }
    }
    return filtered;
}

std::size_t TierCatalog::uniqueElementCount() const {
    std::unordered_set<std::string> seen;
    for (const auto& tier : tierOrder_) {
        const auto found = elementsByTier_.find(tier);
        if (found != elementsByTier_.end()) {
            for (const auto& name : found->second) {
                seen.insert(lower(name));
            }
        }
    }
    return seen.size();
}

}  // namespace alchemy
