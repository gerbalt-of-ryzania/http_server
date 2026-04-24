#include "json_loader.h"

#include <boost/json.hpp>

#include <chrono>
#include <fstream>
#include <stdexcept>

namespace json_loader {

namespace json = boost::json;

namespace {

struct LoadedMapData {
    model::Map map;
    json::array loot_types;
};

LoadedMapData LoadMap(const json::object& map_obj, double default_dog_speed, size_t default_bag_capacity);
std::vector<size_t> LoadLootValues(const json::array& loot_types);
void LoadRoads(model::Map& map, const json::object& map_obj);
void LoadBuildings(model::Map& map, const json::object& map_obj);
void LoadOffices(model::Map& map, const json::object& map_obj);
model::Game::LootGeneratorConfig LoadLootGeneratorConfig(const json::object& root_obj);

}  // namespace

LoadedGame LoadGame(const std::filesystem::path& json_path) {
    std::ifstream file(json_path);
    if (!file) {
        throw std::runtime_error("Failed to open config file: " + json_path.string());
    }
    std::string json_str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    json::value json_val;
    try {
        json_val = json::parse(json_str);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse JSON file: " + json_path.string() + ": " + e.what());
    }
    json::object json_obj = json_val.as_object();

    double default_dog_speed = 1.0;
    if (auto it = json_obj.find("defaultDogSpeed"); it != json_obj.end()) {
        default_dog_speed = json::value_to<double>(it->value());
    }

    size_t default_bag_capacity = 3;
    if (auto it = json_obj.find("defaultBagCapacity"); it != json_obj.end()) {
        default_bag_capacity = json::value_to<size_t>(it->value());
    }

    LoadedGame loaded_game;
    loaded_game.game.SetLootGeneratorConfig(LoadLootGeneratorConfig(json_obj));

    if (auto it_maps = json_obj.find("maps"); it_maps != json_obj.end()) {
        for (const auto& map_val : it_maps->value().as_array()) {
            auto loaded_map = LoadMap(map_val.as_object(), default_dog_speed, default_bag_capacity);
            loaded_game.map_extra_data.AddLootTypes(loaded_map.map.GetId(), loaded_map.loot_types);
            loaded_game.game.AddMap(std::move(loaded_map.map));
        }
    }

    return loaded_game;
}

namespace {

model::Game::LootGeneratorConfig LoadLootGeneratorConfig(const json::object& root_obj) {
    const auto& config = root_obj.at("lootGeneratorConfig").as_object();
    const double period_seconds = json::value_to<double>(config.at("period"));
    const double probability = json::value_to<double>(config.at("probability"));
    if (period_seconds <= 0.0) {
        throw std::runtime_error("lootGeneratorConfig.period must be greater than zero");
    }
    return {
        std::chrono::duration_cast<loot_gen::LootGenerator::TimeInterval>(
            std::chrono::duration<double>(period_seconds)),
        probability};
}

LoadedMapData LoadMap(const json::object& map_obj, double default_dog_speed, size_t default_bag_capacity) {
    std::string id = map_obj.at("id").as_string().c_str();
    std::string name = map_obj.at("name").as_string().c_str();
    double dog_speed = default_dog_speed;
    if (auto it = map_obj.find("dogSpeed"); it != map_obj.end()) {
        dog_speed = json::value_to<double>(it->value());
    }
    size_t bag_capacity = default_bag_capacity;
    if (auto it = map_obj.find("bagCapacity"); it != map_obj.end()) {
        bag_capacity = json::value_to<size_t>(it->value());
    }

    const json::array loot_types = map_obj.at("lootTypes").as_array();
    if (loot_types.empty()) {
        throw std::runtime_error("Map " + id + " must contain at least one loot type");
    }

    model::Map map(
        model::Map::Id{id},
        name,
        dog_speed,
        loot_types.size(),
        bag_capacity,
        LoadLootValues(loot_types));
    LoadRoads(map, map_obj);
    LoadBuildings(map, map_obj);
    LoadOffices(map, map_obj);
    return {std::move(map), loot_types};
}

std::vector<size_t> LoadLootValues(const json::array& loot_types) {
    std::vector<size_t> values;
    values.reserve(loot_types.size());
    for (const auto& loot_type : loot_types) {
        values.push_back(json::value_to<size_t>(loot_type.as_object().at("value")));
    }
    return values;
}

void LoadRoads(model::Map& map, const json::object& map_obj) {
    if (auto it_roads = map_obj.find("roads"); it_roads != map_obj.end()) {
        for (const auto& road_val : it_roads->value().as_array()) {
            json::object road_obj = road_val.as_object();
            int x0 = static_cast<int>(road_obj["x0"].as_int64());
            int y0 = static_cast<int>(road_obj["y0"].as_int64());
            model::Point start{x0, y0};
            if (auto it_x1 = road_obj.find("x1"); it_x1 != road_obj.end()) {
                int x1 = static_cast<int>(it_x1->value().as_int64());
                map.AddRoad(model::Road{model::Road::HORIZONTAL, start, x1});
            } else if (auto it_y1 = road_obj.find("y1"); it_y1 != road_obj.end()) {
                int y1 = static_cast<int>(it_y1->value().as_int64());
                map.AddRoad(model::Road{model::Road::VERTICAL, start, y1});
            }
        }
    }
}

void LoadBuildings(model::Map& map, const json::object& map_obj) {
    if (auto it_buildings = map_obj.find("buildings"); it_buildings != map_obj.end()) {
        for (const auto& build_val : it_buildings->value().as_array()) {
            json::object build_obj = build_val.as_object();
            int x = static_cast<int>(build_obj["x"].as_int64());
            int y = static_cast<int>(build_obj["y"].as_int64());
            int w = static_cast<int>(build_obj["w"].as_int64());
            int h = static_cast<int>(build_obj["h"].as_int64());
            model::Rectangle rect{model::Point{x, y}, model::Size{w, h}};
            map.AddBuilding(model::Building{rect});
        }
    }
}

void LoadOffices(model::Map& map, const json::object& map_obj) {
    if (auto it_offices = map_obj.find("offices"); it_offices != map_obj.end()) {
        for (const auto& office_val : it_offices->value().as_array()) {
            json::object office_obj = office_val.as_object();
            std::string office_id = office_obj["id"].as_string().c_str();
            int x = static_cast<int>(office_obj["x"].as_int64());
            int y = static_cast<int>(office_obj["y"].as_int64());
            int offsetX = static_cast<int>(office_obj["offsetX"].as_int64());
            int offsetY = static_cast<int>(office_obj["offsetY"].as_int64());
            model::Office office{model::Office::Id{office_id}, model::Point{x, y}, model::Offset{offsetX, offsetY}};
            map.AddOffice(std::move(office));
        }
    }
}

}  // namespace

}  // namespace json_loader
