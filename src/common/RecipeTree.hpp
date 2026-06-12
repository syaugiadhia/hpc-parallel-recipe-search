#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace alchemy {

struct RecipeTree {
    std::string name;
    bool basic = false;
    bool sharedRef = false;
    bool truncated = false;
    std::string refId;
    std::vector<RecipeTree> children;
};

int treeDepth(const RecipeTree& tree);
std::string treeSignature(const RecipeTree& tree);
std::string treeSignatureOrdered(const RecipeTree& tree);
nlohmann::json treeToJson(const RecipeTree& tree);
RecipeTree treeFromJson(const nlohmann::json& json);
std::string treeToAscii(const RecipeTree& tree);
std::vector<RecipeTree> deduplicateTrees(const std::vector<RecipeTree>& trees, std::size_t limit);

}  // namespace alchemy
