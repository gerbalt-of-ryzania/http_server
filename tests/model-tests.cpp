#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "../src/json_loader.h"
#include "../src/model.h"

namespace {

constexpr double kEps = 1e-9;

bool IsPointOnRoad(const model::Road& road, model::Vec2 point) {
    if (road.IsHorizontal()) {
        if (std::fabs(point.y - static_cast<double>(road.GetStart().y)) > kEps) {
            return false;
        }
        const double x0 = static_cast<double>(road.GetStart().x);
        const double x1 = static_cast<double>(road.GetEnd().x);
        return point.x + kEps >= std::min(x0, x1) && point.x - kEps <= std::max(x0, x1);
    }

    if (std::fabs(point.x - static_cast<double>(road.GetStart().x)) > kEps) {
        return false;
    }
    const double y0 = static_cast<double>(road.GetStart().y);
    const double y1 = static_cast<double>(road.GetEnd().y);
    return point.y + kEps >= std::min(y0, y1) && point.y - kEps <= std::max(y0, y1);
}

bool IsPointOnAnyRoad(const model::Map& map, model::Vec2 point) {
    for (const auto& road : map.GetRoads()) {
        if (IsPointOnRoad(road, point)) {
            return true;
        }
    }
    return false;
}

model::Map MakeMap(size_t loot_types_count = 2) {
    model::Map map(model::Map::Id{"map1"}, "Map 1", 1.0, loot_types_count, 3, std::vector<size_t>(loot_types_count, 10));
    map.AddRoad(model::Road(model::Road::HORIZONTAL, {0, 0}, 10));
    map.AddRoad(model::Road(model::Road::VERTICAL, {10, 0}, 10));
    return map;
}

}  // namespace

SCENARIO("Game generates lost objects on roads") {
    model::Game game;
    game.SetLootGeneratorConfig({std::chrono::milliseconds{1000}, 1.0});
    game.SeedRandomGenerator(42);
    game.AddMap(MakeMap());

    const auto join = game.JoinGame("Tuzik", model::Map::Id{"map1"});
    REQUIRE(join.player_id == 0);

    game.Tick(1000);

    const auto* map = game.FindMap(model::Map::Id{"map1"});
    REQUIRE(map != nullptr);

    const auto lost_objects = game.GetLostObjectsByMap(model::Map::Id{"map1"});
    REQUIRE(lost_objects.size() == 1);
    CHECK(lost_objects.front()->type < map->GetLootTypesCount());
    CHECK(IsPointOnAnyRoad(*map, lost_objects.front()->position));
}

SCENARIO("Game does not generate more lost objects than players on map") {
    model::Game game;
    game.SetLootGeneratorConfig({std::chrono::milliseconds{1000}, 1.0});
    game.SeedRandomGenerator(7);
    game.AddMap(MakeMap());

    game.JoinGame("Tuzik", model::Map::Id{"map1"});
    game.Tick(1000);
    REQUIRE(game.GetLostObjectsByMap(model::Map::Id{"map1"}).size() == 1);

    game.Tick(1000);
    CHECK(game.GetLostObjectsByMap(model::Map::Id{"map1"}).size() == 1);
}

SCENARIO("Game generates loot independently on different maps") {
    model::Game game;
    game.SetLootGeneratorConfig({std::chrono::milliseconds{1000}, 1.0});
    game.SeedRandomGenerator(11);

    game.AddMap(MakeMap(2));
    model::Map second_map(model::Map::Id{"map2"}, "Map 2", 1.0, 3, 3, {10, 20, 30});
    second_map.AddRoad(model::Road(model::Road::VERTICAL, {5, 5}, 15));
    game.AddMap(std::move(second_map));

    game.JoinGame("Alpha", model::Map::Id{"map1"});
    game.JoinGame("Beta", model::Map::Id{"map2"});

    game.Tick(1000);

    CHECK(game.GetLostObjectsByMap(model::Map::Id{"map1"}).size() == 1);
    CHECK(game.GetLostObjectsByMap(model::Map::Id{"map2"}).size() == 1);
}

SCENARIO("Json loader applies default and per-map bag capacity") {
    const auto config_path = std::filesystem::temp_directory_path() / "find_return_bag_capacity_config.json";
    std::ofstream config(config_path);
    config << R"({
  "defaultBagCapacity": 4,
  "lootGeneratorConfig": { "period": 1.0, "probability": 0.0 },
  "maps": [
    {
      "id": "map1",
      "name": "Map 1",
      "lootTypes": [{ "name": "key", "file": "assets/key.obj", "type": "obj", "scale": 0.03, "value": 15 }],
      "roads": [{ "x0": 0, "y0": 0, "x1": 10 }],
      "buildings": [],
      "offices": []
    },
    {
      "id": "map2",
      "name": "Map 2",
      "bagCapacity": 1,
      "lootTypes": [{ "name": "wallet", "file": "assets/wallet.obj", "type": "obj", "scale": 0.01, "value": 20 }],
      "roads": [{ "x0": 0, "y0": 0, "x1": 10 }],
      "buildings": [],
      "offices": []
    }
  ]
})";
    config.close();

    const auto loaded = json_loader::LoadGame(config_path);

    REQUIRE(loaded.game.FindMap(model::Map::Id{"map1"}) != nullptr);
    REQUIRE(loaded.game.FindMap(model::Map::Id{"map2"}) != nullptr);
    CHECK(loaded.game.FindMap(model::Map::Id{"map1"})->GetBagCapacity() == 4);
    CHECK(loaded.game.FindMap(model::Map::Id{"map2"})->GetBagCapacity() == 1);

    std::filesystem::remove(config_path);
}

SCENARIO("Player collects loot and returns it to the office during movement") {
    model::Game game;
    game.SetLootGeneratorConfig({std::chrono::milliseconds{1000}, 1.0});
    game.SeedRandomGenerator(17);

    model::Map map(model::Map::Id{"map1"}, "Map 1", 10.0, 1, 3, {10});
    map.AddRoad(model::Road(model::Road::HORIZONTAL, {0, 0}, 10));
    map.AddOffice(model::Office(model::Office::Id{"office"}, {10, 0}, {0, 0}));
    game.AddMap(std::move(map));

    const auto join = game.JoinGame("Tuzik", model::Map::Id{"map1"});
    game.Tick(1000);
    REQUIRE(game.GetLostObjectsByMap(model::Map::Id{"map1"}).size() == 1);

    game.SetLootGeneratorConfig({std::chrono::milliseconds{1000}, 0.0});
    game.ApplyPlayerMove(join.auth_token, "R");
    game.Tick(1000);

    const auto* player = game.FindPlayerByToken(join.auth_token);
    REQUIRE(player != nullptr);
    CHECK(player->position.x == 10.0);
    CHECK(player->position.y == 0.0);
    CHECK(player->bag.empty());
    CHECK(player->score == 10);
    CHECK(game.GetLostObjectsByMap(model::Map::Id{"map1"}).empty());
}
