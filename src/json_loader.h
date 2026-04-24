#pragma once

#include <filesystem>

#include "extra_data.h"
#include "model.h"

namespace json_loader {

struct LoadedGame {
    model::Game game;
    extra_data::MapExtraData map_extra_data;
};

LoadedGame LoadGame(const std::filesystem::path& json_path);

}  // namespace json_loader
