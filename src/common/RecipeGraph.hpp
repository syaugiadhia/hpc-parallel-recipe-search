#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace alchemy {

struct RecipePair {
    std::string first;
    std::string second;
};

class RecipeGraph {
public:
    RecipeGraph();

    void addElement(const std::string& name);
    void addRecipe(const std::string& result, const std::string& first, const std::string& second);

    bool hasElement(const std::string& name) const;
    bool hasRecipes(const std::string& name) const;
    bool isBasic(const std::string& name) const;
    bool isTerminal(const std::string& name) const;
    void markTerminal(const std::string& name);
    std::string canonicalName(const std::string& name) const;
    const std::vector<RecipePair>& recipesFor(const std::string& name) const;
    std::vector<std::string> allElements() const;
    std::size_t elementCount() const;
    std::size_t recipeCount() const;

    static std::string normalize(const std::string& name);

private:
    std::string ensureElement(const std::string& name);

    std::unordered_map<std::string, std::string> canonicalByLower_;
    std::unordered_map<std::string, std::vector<RecipePair>> recipesByResult_;
    std::unordered_set<std::string> basicLower_;
    std::unordered_set<std::string> terminalLower_;
    std::vector<std::string> insertionOrder_;
    std::vector<RecipePair> emptyRecipes_;
    std::size_t recipeCount_ = 0;
};

}  // namespace alchemy
