#pragma once

#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/serialization/utility.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>

#include "model.h"

namespace model {


template <typename Archive>
void serialize(Archive& ar, Vec2& vec, [[maybe_unused]] const unsigned version) {
    ar& vec.x;
    ar& vec.y;
}

template <typename Archive>
void serialize(Archive& ar, Direction& dir, [[maybe_unused]] const unsigned version) {
    using Repr = std::underlying_type_t<model::Direction>;
    Repr value = static_cast<Repr>(dir);
    ar& value;
    if constexpr (Archive::is_loading::value) {
        dir = static_cast<Direction>(value);
    }
}

template <typename Archive>
void serialize(Archive& ar, FoundObject& obj, [[maybe_unused]] const unsigned version) {
    ar& obj.id;
    ar& obj.type;
    ar& obj.value;
}

} // namespace model

namespace serialization {

class PlayerRepr {
public:
    PlayerRepr() = default;
    
    explicit PlayerRepr(const model::Game::Player& player)
        : id_(player.id)
        , name_(player.name)
        , map_id_(*player.map_id)
        , position_(player.position)
        , speed_(player.speed)
        , direction_(player.direction)
        , bag_capacity_(player.bag_capacity)
        , bag_(player.bag)
        , score_(player.score) {
    }

    model::Game::Player Restore() const {
        model::Game::Player player {
            id_,
            name_,
            model::Map::Id{map_id_},
            position_,
            speed_,
            direction_,
            bag_capacity_,
            bag_,
            score_,
        };

        return player;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& id_;
        ar& name_;
        ar& map_id_;
        ar& position_;
        ar& speed_;
        ar& direction_;
        ar& bag_capacity_;
        ar& bag_;
        ar& score_;
    }
private:
    model::Game::PlayerId id_ = 0;
    std::string name_;
    std::string map_id_;
    model::Vec2 position_;
    model::Vec2 speed_;
    model::Direction direction_ = model::Direction::NORTH;
    size_t bag_capacity_ = 0;
    std::vector<model::FoundObject> bag_;
    size_t score_ = 0;
};

class LostObjectRepr {
public:
    LostObjectRepr() = default;

    explicit LostObjectRepr(const model::Game::LostObject& obj)
        : id_(obj.id)
        , map_id_(*obj.map_id)
        , type_(obj.type)
        , position_(obj.position) {
    }

    [[nodiscard]] model::Game::LostObject Restore() const {
        return {
            id_,
            model::Map::Id{map_id_},
            type_,
            position_
        };
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar& id_;
        ar& map_id_;
        ar& type_;
        ar& position_;
    }
private:
    model::Game::LostObjectId id_ = 0;
    std::string map_id_;
    size_t type_ = 0;
    model::Vec2 position_;
};

class GameStateRepr {
public:
    GameStateRepr() = default;

    explicit GameStateRepr(const model::Game& game, 
            model::Game::PlayerId next_player_id,
            model::Game::LostObjectId next_lost_object_id)
        : token_to_player_id_(game.GetTokenAndPlyerIds())
        , next_player_id_(next_player_id)
        , next_lost_object_id_(next_lost_object_id) {
            players_.reserve(game.GetPlayers().size());
            for (const auto& player : game.GetPlayers()) {
                players_.emplace_back(player);
            }

            lost_objects_.reserve(game.GetLostObjects().size());
            for (const auto& obj : game.GetLostObjects()) {
                lost_objects_.emplace_back(obj);
            }
    }
    
    [[nodiscard]] std::vector<model::Game::Player> RestorePlayers() const {
        std::vector<model::Game::Player> players;
        players.reserve(players_.size());

        for (const auto& player_repr : players_) {
            players.push_back(player_repr.Restore());
        }
    
        return players;
    }

    [[nodiscard]] std::vector<model::Game::LostObject> RestoreLostObjects() const {
        std::vector<model::Game::LostObject> lost_objects;
        lost_objects.reserve(lost_objects_.size());

        for (const auto& object_repr : lost_objects_) {
            lost_objects.push_back(object_repr.Restore());
        }

        return lost_objects;
    }

    [[nodiscard]] const std::vector<std::pair<std::string, model::Game::PlayerId>>& 
    GetTokenToPlayerIds() const noexcept {
        return token_to_player_id_;
    }

    [[nodiscard]] model::Game::PlayerId GetNextPlayerId() const noexcept {
        return next_player_id_;
    }

    [[nodiscard]] model::Game::LostObjectId GetNextLostObjectId() const noexcept {
        return next_lost_object_id_;
    }

    template <typename Archive>
    void serialize(Archive& ar, [[maybe_unused]] const unsigned version) {
        ar & players_;
        ar & lost_objects_;
        ar & token_to_player_id_;
        ar & next_player_id_;
        ar & next_lost_object_id_;
    }
private:
    std::vector<PlayerRepr> players_;
    std::vector<LostObjectRepr> lost_objects_;
    std::vector<std::pair<std::string, model::Game::PlayerId>> token_to_player_id_;
    model::Game::PlayerId next_player_id_ = 0;
    model::Game::LostObjectId next_lost_object_id_ = 0;
};

inline void SaveGameState(const model::Game& game, const std::filesystem::path& path) {
    GameStateRepr state{game, game.GetNextPlayerId(), game.GetNextLostObjectId()};

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    try {
        {
            std::ofstream output{temp_path, std::ios::binary};
            if (!output.is_open()) {
                throw std::runtime_error("Failed to open temporary state file for writing");
            }

            boost::archive::text_oarchive archive{output};
            archive << state;
        }

        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }

        std::filesystem::rename(temp_path, path);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
        throw;
    }
}

inline GameStateRepr LoadGameStateRepr(const std::filesystem::path& path) {
    GameStateRepr state;

    std::ifstream input{path, std::ios::binary};
    if (!input.is_open()) {
        throw std::runtime_error{"Failed to open state file for reading"};
    }

    boost::archive::text_iarchive archive{input};
    archive >> state;

    return state;
}

inline void LoadGameState(model::Game& game, const std::filesystem::path& path) {
    const GameStateRepr state = LoadGameStateRepr(path);

    game.RestoreState(
        state.RestorePlayers(),
        state.RestoreLostObjects(),
        state.GetTokenToPlayerIds(),
        state.GetNextPlayerId(),
        state.GetNextLostObjectId()
    );
}

} // namespace serialization