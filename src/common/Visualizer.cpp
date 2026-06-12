#include "common/Visualizer.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace alchemy {
namespace {

std::string dotEscape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        if (ch == '\n') {
            out += "\\n";
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

std::string shellQuote(const std::string& value) {
#ifdef _WIN32
    std::string out = "\"";
    for (char ch : value) {
        if (ch == '"') {
            out += "\\\"";
        } else {
            out.push_back(ch);
        }
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out += "'";
    return out;
#endif
}

std::string nodeStyle(const RecipeTree& tree, bool shared, bool truncated) {
    if (shared) {
        return "shape=box, style=\"dashed,filled\", fillcolor=\"#fff3cd\", color=\"#b8860b\"";
    }
    if (truncated) {
        return "shape=box, style=\"rounded,filled\", fillcolor=\"#f8d7da\", color=\"#b02a37\"";
    }
    if (tree.basic) {
        return "shape=ellipse, style=filled, fillcolor=\"#d1e7dd\", color=\"#0f5132\"";
    }
    return "shape=box, style=\"rounded,filled\", fillcolor=\"#e7f1ff\", color=\"#084298\"";
}

struct DotBuilder {
    const AppOptions& options;
    std::ostringstream out;
    int nextId = 0;
    std::unordered_map<std::string, std::pair<std::string, int>> sharedSeen;

    struct AddedNode {
        std::string id;
        bool shared = false;
    };

    AddedNode addNode(const RecipeTree& tree, int recipeIndex, int depth) {
        const bool depthLimited = options.maxVisualDepth >= 0 && depth >= options.maxVisualDepth && !tree.children.empty();
        const auto signature = treeSignature(tree);
        const bool canShare = options.visualMode == VisualMode::Shared && !tree.basic && !depthLimited && depth > 0;
        if (canShare) {
            const auto found = sharedSeen.find(signature);
            if (found != sharedSeen.end()) {
                const std::string id = "n" + std::to_string(nextId++);
                std::ostringstream label;
                label << tree.name << "\n(shared, see Recipe " << found->second.second << ")";
                out << "    " << id << " [label=\"" << dotEscape(label.str()) << "\", "
                    << nodeStyle(tree, true, false) << "];\n";
                return {id, true};
            }
        }

        const std::string id = "n" + std::to_string(nextId++);
        std::string label = tree.name;
        if (depthLimited) {
            label += "\n(truncated)";
        }
        out << "    " << id << " [label=\"" << dotEscape(label) << "\", "
            << nodeStyle(tree, false, depthLimited) << "];\n";

        if (canShare) {
            sharedSeen[signature] = {id, recipeIndex};
        }
        if (!depthLimited) {
            for (const auto& child : tree.children) {
                const auto childNode = addNode(child, recipeIndex, depth + 1);
                out << "    " << id << " -> " << childNode.id;
                if (childNode.shared) {
                    out << " [style=dashed]";
                }
                out << ";\n";
            }
        }
        return {id, false};
    }
};

nlohmann::json recipesToJson(const std::string& target,
                             const std::vector<RecipeTree>& recipes,
                             const SearchStats& stats,
                             const AppOptions& options) {
    nlohmann::json json;
    json["target"] = target;
    json["algorithm"] = toString(options.algorithm);
    json["mode"] = toString(options.mode);
    json["trace_mode"] = toString(options.traceMode);
    json["visual_mode"] = toString(options.visualMode);
    if (options.mode == SearchMode::All) {
        json["limit"] = nullptr;
        json["direct_recipes_available"] = stats.directRecipesAvailable;
    } else {
        json["limit"] = options.limit;
    }
    json["recipes_found"] = recipes.size();
    json["statistics"] = statsToJson(stats);
    json["recipes"] = nlohmann::json::array();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        json["recipes"].push_back({{"id", i + 1}, {"tree", treeToJson(recipes[i])}});
    }
    return json;
}

}  // namespace

std::string Visualizer::buildDot(const std::vector<RecipeTree>& recipes, const AppOptions& options) {
    DotBuilder builder{options};
    builder.out << "digraph RecipeResults {\n";
    builder.out << "  graph [rankdir=TB, labelloc=t, fontsize=20, fontname=\"Arial\"];\n";
    builder.out << "  node [fontname=\"Arial\"];\n";
    builder.out << "  edge [fontname=\"Arial\"];\n";
    builder.out << "  label=\"Little Alchemy Recipe Search\";\n\n";

    for (std::size_t i = 0; i < recipes.size(); ++i) {
        builder.out << "  subgraph cluster_recipe_" << (i + 1) << " {\n";
        builder.out << "    label=\"Recipe " << (i + 1) << "\";\n";
        builder.out << "    color=\"#adb5bd\";\n";
        builder.addNode(recipes[i], static_cast<int>(i + 1), 0);
        builder.out << "  }\n\n";
    }

    if (recipes.empty()) {
        builder.out << "  empty [label=\"No valid recipe found\", shape=box, style=filled, fillcolor=\"#f8d7da\"];\n";
    }
    builder.out << "}\n";
    return builder.out.str();
}

OutputFiles Visualizer::writeOutputs(const std::string& target,
                                     const std::vector<RecipeTree>& recipes,
                                     const SearchStats& stats,
                                     const AppOptions& options) {
    ensureParentDirectory(options.outputPrefix);
    OutputFiles files;
    files.dotPath = options.outputPrefix + ".dot";
    files.jsonPath = options.outputPrefix + ".json";
    files.imagePath = options.outputPrefix + "." + options.imageFormat;

    {
        std::ofstream dot(files.dotPath);
        if (!dot) {
            throw std::runtime_error("Cannot write DOT file: " + files.dotPath);
        }
        dot << buildDot(recipes, options);
    }

    {
        std::ofstream json(files.jsonPath);
        if (!json) {
            throw std::runtime_error("Cannot write JSON file: " + files.jsonPath);
        }
        json << recipesToJson(target, recipes, stats, options).dump(2) << "\n";
    }

    const std::string command = "dot -T" + options.imageFormat + " " + shellQuote(files.dotPath) + " -o " + shellQuote(files.imagePath);
    const int rc = std::system(command.c_str());
    if (rc == 0 && std::filesystem::exists(files.imagePath)) {
        files.imageRendered = true;
    } else {
        files.renderWarning = "Graphviz 'dot' is not available or failed; DOT was still generated at " + files.dotPath;
        files.imagePath.clear();
    }

    return files;
}

}  // namespace alchemy
