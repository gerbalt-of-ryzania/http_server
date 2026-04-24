#pragma once
#include <cstdint>
#include <chrono>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "loot_generator.h"
#include "tagged.h"

namespace model {

using Dimension = int;
using Coord = Dimension;

struct Point {
    Coord x, y;
};

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

enum class Direction {
    NORTH,
    SOUTH,
    WEST,
    EAST,
};

struct FoundObject {
    std::uint32_t id;
    size_t type;
    size_t value;
};

struct Size {
    Dimension width, height;
};

struct Rectangle {
    Point position;
    Size size;
};

struct Offset {
    Dimension dx, dy;
};

class Road {
    struct HorizontalTag {
        explicit HorizontalTag() = default;
    };

    struct VerticalTag {
        explicit VerticalTag() = default;
    };

public:
    constexpr static HorizontalTag HORIZONTAL{};
    constexpr static VerticalTag VERTICAL{};

    Road(HorizontalTag, Point start, Coord end_x) noexcept
        : start_{start}
        , end_{end_x, start.y} {
    }

    Road(VerticalTag, Point start, Coord end_y) noexcept
        : start_{start}
        , end_{start.x, end_y} {
    }

    bool IsHorizontal() const noexcept {
        return start_.y == end_.y;
    }

    bool IsVertical() const noexcept {
        return start_.x == end_.x;
    }

    Point GetStart() const noexcept {
        return start_;
    }

    Point GetEnd() const noexcept {
        return end_;
    }

private:
    Point start_;
    Point end_;
};

class Building {
public:
    explicit Building(Rectangle bounds) noexcept
        : bounds_{bounds} {
    }

    const Rectangle& GetBounds() const noexcept {
        return bounds_;
    }

private:
    Rectangle bounds_;
};

class Office {
public:
    using Id = util::Tagged<std::string, Office>;

    Office(Id id, Point position, Offset offset) noexcept
        : id_{std::move(id)}
        , position_{position}
        , offset_{offset} {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    Point GetPosition() const noexcept {
        return position_;
    }

    Offset GetOffset() const noexcept {
        return offset_;
    }

private:
    Id id_;
    Point position_;
    Offset offset_;
};

class Map {
public:
    using Id = util::Tagged<std::string, Map>;
    using Roads = std::vector<Road>;
    using Buildings = std::vector<Building>;
    using Offices = std::vector<Office>;

    Map(Id id,
        std::string name,
        double dog_speed,
        size_t loot_types_count = 0,
        size_t bag_capacity = 3,
        std::vector<size_t> loot_values = {}) noexcept
        : id_(std::move(id))
        , name_(std::move(name))
        , dog_speed_{dog_speed}
        , loot_types_count_{loot_types_count}
        , bag_capacity_{bag_capacity}
        , loot_values_(std::move(loot_values)) {
    }

    const Id& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

    double GetDogSpeed() const noexcept {
        return dog_speed_;
    }

    size_t GetLootTypesCount() const noexcept {
        return loot_types_count_;
    }

    size_t GetBagCapacity() const noexcept {
        return bag_capacity_;
    }

    size_t GetLootValue(size_t type) const noexcept {
        return loot_values_.at(type);
    }

    const Buildings& GetBuildings() const noexcept {
        return buildings_;
    }

    const Roads& GetRoads() const noexcept {
        return roads_;
    }

    const Offices& GetOffices() const noexcept {
        return offices_;
    }

    void AddRoad(const Road& road) {
        roads_.emplace_back(road);
    }

    void AddBuilding(const Building& building) {
        buildings_.emplace_back(building);
    }

    void AddOffice(Office office);

private:
    using OfficeIdToIndex = std::unordered_map<Office::Id, size_t, util::TaggedHasher<Office::Id>>;

    Id id_;
    std::string name_;
    double dog_speed_;
    size_t loot_types_count_ = 0;
    size_t bag_capacity_ = 3;
    std::vector<size_t> loot_values_;
    Roads roads_;
    Buildings buildings_;

    OfficeIdToIndex warehouse_id_to_index_;
    Offices offices_;
};

class Game {
public:
    using Maps = std::vector<Map>;
    using PlayerId = std::uint32_t;
    using LostObjectId = std::uint32_t;

    struct LootGeneratorConfig {
        loot_gen::LootGenerator::TimeInterval period{1};
        double probability = 0.0;
    };

    struct Player {
        PlayerId id;
        std::string name;
        Map::Id map_id;
        Vec2 position;
        Vec2 speed;
        Direction direction = Direction::NORTH;
        size_t bag_capacity = 3;
        std::vector<FoundObject> bag;
        size_t score = 0;

        bool IsBagFull() const noexcept {
            return bag.size() >= bag_capacity;
        }

        bool TryCollect(FoundObject item) {
            if (IsBagFull()) {
                return false;
            }
            bag.push_back(item);
            return true;
        }

        void ReturnToOffice() noexcept {
            for (const auto& item : bag) {
                score += item.value;
            }
            bag.clear();
        }
    };

    struct JoinResult {
        PlayerId player_id;
        std::string auth_token;
    };

    struct LostObject {
        LostObjectId id;
        Map::Id map_id;
        size_t type;
        Vec2 position;
    };

    Game() = default;

    void AddMap(Map map);
    void SetLootGeneratorConfig(LootGeneratorConfig config) noexcept;
    void SetRandomizeSpawnPoints(bool enable) noexcept {
        randomize_spawn_points_ = enable;
    }
    void SeedRandomGenerator(uint64_t seed) noexcept {
        random_generator_.seed(seed);
    }

    JoinResult JoinGame(const std::string& user_name, const Map::Id& map_id);

    void ApplyPlayerMove(const std::string& token, std::string_view move);
    void Tick(int64_t time_delta_ms);

    const Maps& GetMaps() const noexcept {
        return maps_;
    }

    const Map* FindMap(const Map::Id& id) const noexcept {
        if (auto it = map_id_to_index_.find(id); it != map_id_to_index_.end()) {
            return &maps_.at(it->second);
        }
        return nullptr;
    }

    const Player* FindPlayerByToken(const std::string& token) const noexcept;
    std::vector<const Player*> GetPlayersByMap(const Map::Id& map_id) const;
    std::vector<const LostObject*> GetLostObjectsByMap(const Map::Id& map_id) const;

    const std::vector<Player>& GetPlayers() const noexcept {
        return players_;
    }

    const std::vector<LostObject>& GetLostObjects() const noexcept {
        return lost_objects_;
    }
    
    std::vector<std::pair<std::string, PlayerId>> GetTokenAndPlyerIds() const;
    
    PlayerId GetNextPlayerId() const noexcept {
        return next_player_id_;
    }

    LostObjectId GetNextLostObjectId() const noexcept {
        return next_lost_object_id_;
    }
    
    void RestoreState(std::vector<Player> players,
                      std::vector<LostObject> lost_objects,
                      std::vector<std::pair<std::string, PlayerId>> token_and_player_ids,
                      PlayerId next_player_id,
                      LostObjectId next_lost_object_id);

private:
    static Vec2 SpawnAtFirstRoadStart(const Map& map);
    Vec2 SpawnAtRandomRoadPoint(const Map& map);
    Vec2 GenerateRandomRoadPosition(const Map& map);

    using MapIdHasher = util::TaggedHasher<Map::Id>;
    using MapIdToIndex = std::unordered_map<Map::Id, size_t, MapIdHasher>;
    using TokenToPlayerIndex = std::unordered_map<std::string, size_t>;

    struct MapRuntimeState {
        loot_gen::LootGenerator generator;
    };

    std::vector<Map> maps_;
    std::vector<MapRuntimeState> map_runtime_states_;
    MapIdToIndex map_id_to_index_;
    std::vector<Player> players_;
    std::vector<LostObject> lost_objects_;
    TokenToPlayerIndex token_to_player_index_;
    PlayerId next_player_id_ = 0;
    LostObjectId next_lost_object_id_ = 0;
    bool randomize_spawn_points_ = false;
    LootGeneratorConfig loot_generator_config_;
    std::mt19937_64 random_generator_{std::random_device{}()};
};

}  // namespace model
