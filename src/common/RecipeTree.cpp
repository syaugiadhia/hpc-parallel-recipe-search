#include "common/RecipeTree.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace alchemy {

int treeDepth(const RecipeTree& tree) {
    int bestChild = 0;
    for (const auto& child : tree.children) {
        bestChild = std::max(bestChild, treeDepth(child));
    }
    return tree.children.empty() ? 0 : 1 + bestChild;
}

std::string treeSignature(const RecipeTree& tree) {
    std::vector<std::string> childSignatures;
    childSignatures.reserve(tree.children.size());
    for (const auto& child : tree.children) {
        childSignatures.push_back(treeSignature(child));
    }
    std::sort(childSignatures.begin(), childSignatures.end());

    std::ostringstream out;
    out << tree.name << "(";
    for (std::size_t i = 0; i < childSignatures.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << childSignatures[i];
    }
    out << ")";
    return out.str();
}

std::string treeSignatureOrdered(const RecipeTree& tree) {
    std::ostringstream out;
    out << tree.name << "(";
    for (std::size_t i = 0; i < tree.children.size(); ++i) {
        if (i != 0) {
            out << ",";
        }
        out << treeSignatureOrdered(tree.children[i]);
    }
    out << ")";
    return out.str();
}

nlohmann::json treeToJson(const RecipeTree& tree) {
    nlohmann::json json;
    json["name"] = tree.name;
    if (tree.basic) {
        json["basic"] = true;
    }
    if (tree.sharedRef) {
        json["shared_ref"] = true;
        json["ref_id"] = tree.refId;
    }
    if (tree.truncated) {
        json["truncated"] = true;
    }
    if (!tree.children.empty()) {
        json["children"] = nlohmann::json::array();
        for (const auto& child : tree.children) {
            json["children"].push_back(treeToJson(child));
        }
    }
    return json;
}

RecipeTree treeFromJson(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("name") || !json.at("name").is_string()) {
        throw std::runtime_error("Invalid recipe tree JSON: object with string field 'name' is required");
    }

    RecipeTree tree;
    tree.name = json.at("name").get<std::string>();
    tree.basic = json.value("basic", false);
    tree.sharedRef = json.value("shared_ref", false);
    tree.truncated = json.value("truncated", false);
    tree.refId = json.value("ref_id", std::string{});

    if (json.contains("children")) {
        if (!json.at("children").is_array()) {
            throw std::runtime_error("Invalid recipe tree JSON: 'children' must be an array");
        }
        for (const auto& child : json.at("children")) {
            tree.children.push_back(treeFromJson(child));
        }
    }

    return tree;
}

std::string treeToAscii(const RecipeTree& tree) {
    std::ostringstream out;
    out << tree.name;
    if (tree.sharedRef) {
        out << " [shared";
        if (!tree.refId.empty()) {
            out << ", " << tree.refId;
        }
        out << "]";
    }
    if (tree.truncated) {
        out << " [truncated]";
    }
    out << "\n";

    std::function<void(const RecipeTree&, const std::string&, bool)> walk =
        [&](const RecipeTree& node, const std::string& prefix, bool last) {
            out << prefix << (last ? "`-- " : "|-- ") << node.name;
            if (node.sharedRef) {
                out << " [shared";
                if (!node.refId.empty()) {
                    out << ", " << node.refId;
                }
                out << "]";
            }
            if (node.truncated) {
                out << " [truncated]";
            }
            out << "\n";
            const std::string nextPrefix = prefix + (last ? "    " : "|   ");
            for (std::size_t i = 0; i < node.children.size(); ++i) {
                walk(node.children[i], nextPrefix, i + 1 == node.children.size());
            }
        };

    for (std::size_t i = 0; i < tree.children.size(); ++i) {
        walk(tree.children[i], "", i + 1 == tree.children.size());
    }
    return out.str();
}

std::vector<RecipeTree> deduplicateTrees(const std::vector<RecipeTree>& trees, std::size_t limit) {
    std::vector<RecipeTree> unique;
    std::unordered_set<std::string> seen;
    for (const auto& tree : trees) {
        const auto signature = treeSignature(tree);
        if (seen.insert(signature).second) {
            unique.push_back(tree);
            if (limit > 0 && unique.size() >= limit) {
                break;
            }
        }
    }
    return unique;
}

}  // namespace alchemy
