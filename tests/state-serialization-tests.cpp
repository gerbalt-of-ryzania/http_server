#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

#include "../src/model.h"
#include "../src/state_serialization.h"

using namespace std::literals;

namespace {

using InputArchive = boost::archive::text_iarchive;
using OutputArchive = boost::archive::text_oarchive;

struct Fixture {
    std::stringstream strm;
};

}  // namespace

SCENARIO_METHOD(Fixture, "Vec2 serialization") {
    GIVEN("A Vec2") {
        const model::Vec2 point{10.5, 20.25};

        WHEN("Vec2 is serialized") {
            {
                OutputArchive output_archive{strm};
                output_archive << point;
            }

            THEN("it can be deserialized") {
                InputArchive input_archive{strm};
                model::Vec2 restored_point;
                input_archive >> restored_point;

                CHECK(restored_point.x == point.x);
                CHECK(restored_point.y == point.y);
            }
        }
    }
}

SCENARIO_METHOD(Fixture, "Player serialization") {
    GIVEN("A player") {
        model::Game::Player player;
        player.id = 42;
        player.name = "Bob"s;
        player.map_id = model::Map::Id{"map1"s};
        player.position = {10.5, 20.25};
        player.speed = {1.0, 0.0};
        player.direction = model::Direction::EAST;
        player.bag_capacity = 3;
        player.bag = {
            model::FoundObject{7, 1, 100},
            model::FoundObject{8, 2, 200},
        };
        player.score = 500;

        WHEN("PlayerRepr is serialized") {
            {
                OutputArchive output_archive{strm};
                serialization::PlayerRepr repr{player};
                output_archive << repr;
            }

            THEN("it can be deserialized") {
                InputArchive input_archive{strm};
                serialization::PlayerRepr repr;
                input_archive >> repr;

                const auto restored = repr.Restore();

                CHECK(restored.id == player.id);
                CHECK(restored.name == player.name);
                CHECK(restored.map_id == player.map_id);
                CHECK(restored.position.x == player.position.x);
                CHECK(restored.position.y == player.position.y);
                CHECK(restored.speed.x == player.speed.x);
                CHECK(restored.speed.y == player.speed.y);
                CHECK(restored.direction == player.direction);
                CHECK(restored.bag_capacity == player.bag_capacity);
                CHECK(restored.bag.size() == player.bag.size());
                CHECK(restored.bag[0].id == player.bag[0].id);
                CHECK(restored.bag[0].type == player.bag[0].type);
                CHECK(restored.bag[0].value == player.bag[0].value);
                CHECK(restored.score == player.score);
            }
        }
    }
}

SCENARIO_METHOD(Fixture, "Lost object serialization") {
    GIVEN("A lost object") {
        model::Game::LostObject object{
            17,
            model::Map::Id{"map1"s},
            2,
            model::Vec2{3.5, 4.5}
        };

        WHEN("LostObjectRepr is serialized") {
            {
                OutputArchive output_archive{strm};
                serialization::LostObjectRepr repr{object};
                output_archive << repr;
            }

            THEN("it can be deserialized") {
                InputArchive input_archive{strm};
                serialization::LostObjectRepr repr;
                input_archive >> repr;

                const auto restored = repr.Restore();

                CHECK(restored.id == object.id);
                CHECK(restored.map_id == object.map_id);
                CHECK(restored.type == object.type);
                CHECK(restored.position.x == object.position.x);
                CHECK(restored.position.y == object.position.y);
            }
        }
    }
}

SCENARIO("Game state serialization") {
    GIVEN("A game with one map and one player") {
        model::Game game;

        model::Map map{
            model::Map::Id{"map1"s},
            "Map 1"s,
            1.0,
            2,
            3,
            std::vector<size_t>{100, 200}
        };
        map.AddRoad(model::Road{model::Road::HORIZONTAL, {0, 0}, 10});
        game.AddMap(std::move(map));

        const auto join = game.JoinGame("Bob"s, model::Map::Id{"map1"s});
        game.ApplyPlayerMove(join.auth_token, "R");
        game.Tick(1000);

        serialization::GameStateRepr original_state{
            game,
            game.GetNextPlayerId(),
            game.GetNextLostObjectId()
        };

        std::stringstream strm;

        WHEN("GameStateRepr is serialized") {
            {
                OutputArchive output_archive{strm};
                output_archive << original_state;
            }

            THEN("it can be deserialized and restored") {
                InputArchive input_archive{strm};
                serialization::GameStateRepr restored_state;
                input_archive >> restored_state;

                auto players = restored_state.RestorePlayers();
                auto lost_objects = restored_state.RestoreLostObjects();

                CHECK(players.size() == 1);
                CHECK(players[0].id == join.player_id);
                CHECK(players[0].name == "Bob"s);
                CHECK(players[0].map_id == model::Map::Id{"map1"s});
                CHECK(restored_state.GetTokenToPlayerIds().size() == 1);
                CHECK(restored_state.GetNextPlayerId() == game.GetNextPlayerId());
                CHECK(restored_state.GetNextLostObjectId() == game.GetNextLostObjectId());
            }
        }
    }
}

