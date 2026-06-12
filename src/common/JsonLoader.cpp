#include "common/JsonLoader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace alchemy {
namespace {

std::string recipePath(const std::string& prefix, std::size_t index) {
    std::ostringstream out;
    out << prefix << "[" << index << "]";
    return out.str();
}

std::pair<std::string, std::string> parseIngredientPair(const nlohmann::json& value, const std::string& path) {
    if (!value.is_array() || value.size() != 2 || !value[0].is_string() || !value[1].is_string()) {
        throw std::runtime_error("Invalid recipe at " + path + ": expected exactly two string ingredients");
    }
    return {value[0].get<std::string>(), value[1].get<std::string>()};
}

void loadFormatA(const nlohmann::json& json, RecipeGraph& graph) {
    for (auto it = json.begin(); it != json.end(); ++it) {
        if (!it.value().is_array()) {
            throw std::runtime_error("Invalid Format A recipe for result '" + it.key() + "': expected array of pairs");
        }
        graph.addElement(it.key());
        std::size_t index = 0;
        for (const auto& pairJson : it.value()) {
            const auto pair = parseIngredientPair(pairJson, it.key() + ".recipes[" + std::to_string(index) + "]");
            graph.addRecipe(it.key(), pair.first, pair.second);
            ++index;
        }
    }
}

void loadFormatBOrC(const nlohmann::json& json, RecipeGraph& graph) {
    for (std::size_t i = 0; i < json.size(); ++i) {
        const auto& entry = json[i];
        if (!entry.is_object()) {
            throw std::runtime_error("Invalid recipe entry at [" + std::to_string(i) + "]: expected object");
        }

        if (entry.contains("result") && entry.contains("ingredients")) {
            if (!entry.at("result").is_string()) {
                throw std::runtime_error("Invalid Format B entry at [" + std::to_string(i) + "]: 'result' must be a string");
            }
            const auto pair = parseIngredientPair(entry.at("ingredients"), recipePath("ingredients", i));
            graph.addRecipe(entry.at("result").get<std::string>(), pair.first, pair.second);
            continue;
        }

        if (entry.contains("name") && entry.contains("recipes")) {
            if (!entry.at("name").is_string()) {
                throw std::runtime_error("Invalid recipe entry at [" + std::to_string(i) + "]: 'name' must be a string");
            }
            if (!entry.at("recipes").is_array()) {
                throw std::runtime_error("Invalid recipe entry for '" + entry.at("name").get<std::string>() + "': 'recipes' must be an array");
            }

            const std::string result = entry.at("name").get<std::string>();
            graph.addElement(result);
            for (std::size_t j = 0; j < entry.at("recipes").size(); ++j) {
                const auto& recipe = entry.at("recipes")[j];
                if (!recipe.is_object() || !recipe.contains("elements")) {
                    throw std::runtime_error("Invalid recipe for '" + result + "' at recipes[" + std::to_string(j) + "]: expected object with 'elements'");
                }
                const auto pair = parseIngredientPair(recipe.at("elements"), result + ".recipes[" + std::to_string(j) + "].elements");
                graph.addRecipe(result, pair.first, pair.second);
            }
            continue;
        }

        throw std::runtime_error("Unsupported recipe entry at [" + std::to_string(i) + "]: expected Format B or name/recipes/elements format");
    }
}

}  // namespace

RecipeGraph JsonLoader::loadFromFile(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open recipe JSON file: " + path);
    }

    nlohmann::json json;
    try {
        input >> json;
    } catch (const std::exception& error) {
        throw std::runtime_error("Failed to parse JSON file '" + path + "': " + error.what());
    }

    RecipeGraph graph;
    if (json.is_object()) {
        loadFormatA(json, graph);
    } else if (json.is_array()) {
        loadFormatBOrC(json, graph);
    } else {
        throw std::runtime_error("Unsupported JSON root in '" + path + "': expected object or array");
    }

    return graph;
}

}  // namespace alchemy
