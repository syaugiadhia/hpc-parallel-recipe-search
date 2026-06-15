#include "common/RecipeGraph.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace alchemy {
namespace {

std::string unorderedPairKey(const std::string& first, const std::string& second) {
    auto left = RecipeGraph::normalize(first);
    auto right = RecipeGraph::normalize(second);
    if (right < left) {
        std::swap(left, right);
    }
    return left + "\n" + right;
}

}  // namespace

RecipeGraph::RecipeGraph() {
    for (const std::string basic : {"Air", "Earth", "Fire", "Water"}) {
        const auto lower = normalize(basic);
        basicLower_.insert(lower);
        terminalLower_.insert(lower);
        addElement(basic);
    }
}

std::string RecipeGraph::normalize(const std::string& name) {
    std::string key;
    key.reserve(name.size());
    for (unsigned char ch : name) {
        key.push_back(static_cast<char>(std::tolower(ch)));
    }
    return key;
}

std::string RecipeGraph::ensureElement(const std::string& name) {
    if (name.empty()) {
        throw std::runtime_error("Element name cannot be empty");
    }
    const auto lower = normalize(name);
    const auto found = canonicalByLower_.find(lower);
    if (found != canonicalByLower_.end()) {
        return found->second;
    }
    canonicalByLower_[lower] = name;
    insertionOrder_.push_back(name);
    return name;
}

void RecipeGraph::addElement(const std::string& name) {
    ensureElement(name);
}

void RecipeGraph::addRecipe(const std::string& result, const std::string& first, const std::string& second) {
    const auto canonicalResult = ensureElement(result);
    const auto canonicalFirst = ensureElement(first);
    const auto canonicalSecond = ensureElement(second);
    const auto pairKey = unorderedPairKey(canonicalFirst, canonicalSecond);
    if (!recipePairKeysByResult_[canonicalResult].insert(pairKey).second) {
        return;
    }
    recipesByResult_[canonicalResult].push_back({canonicalFirst, canonicalSecond});
    ++recipeCount_;
}

bool RecipeGraph::hasElement(const std::string& name) const {
    return canonicalByLower_.find(normalize(name)) != canonicalByLower_.end();
}

bool RecipeGraph::hasRecipes(const std::string& name) const {
    if (!hasElement(name)) {
        return false;
    }
    const auto canonical = canonicalName(name);
    const auto found = recipesByResult_.find(canonical);
    return found != recipesByResult_.end() && !found->second.empty();
}

bool RecipeGraph::isBasic(const std::string& name) const {
    return basicLower_.find(normalize(name)) != basicLower_.end();
}

bool RecipeGraph::isTerminal(const std::string& name) const {
    return terminalLower_.find(normalize(name)) != terminalLower_.end();
}

void RecipeGraph::markTerminal(const std::string& name) {
    terminalLower_.insert(normalize(ensureElement(name)));
}

std::string RecipeGraph::canonicalName(const std::string& name) const {
    const auto found = canonicalByLower_.find(normalize(name));
    if (found == canonicalByLower_.end()) {
        throw std::runtime_error("Unknown element: " + name);
    }
    return found->second;
}

const std::vector<RecipePair>& RecipeGraph::recipesFor(const std::string& name) const {
    if (!hasElement(name)) {
        return emptyRecipes_;
    }
    const auto canonical = canonicalName(name);
    const auto found = recipesByResult_.find(canonical);
    if (found == recipesByResult_.end() || isTerminal(canonical)) {
        return emptyRecipes_;
    }
    return found->second;
}

std::vector<std::string> RecipeGraph::allElements() const {
    return insertionOrder_;
}

std::size_t RecipeGraph::elementCount() const {
    return insertionOrder_.size();
}

std::size_t RecipeGraph::recipeCount() const {
    return recipeCount_;
}

}  // namespace alchemy
