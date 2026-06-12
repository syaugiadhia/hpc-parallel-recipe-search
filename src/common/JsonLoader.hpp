#pragma once

#include <string>

#include "common/RecipeGraph.hpp"

namespace alchemy {

class JsonLoader {
public:
    static RecipeGraph loadFromFile(const std::string& path);
};

}  // namespace alchemy
