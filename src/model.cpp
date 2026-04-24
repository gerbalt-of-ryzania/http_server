#include "model.h"
#include "collision_detector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace model {
using namespace std::literals;

namespace {

constexpr double kRoadHalfWidth = 0.4;
constexpr double kPlayerWidth = 0.3;
constexpr double kLostObjectWidth = 0.0;
constexpr double kOfficeWidth = 0.25;
constexpr double kEps = 1e-9;

void MergeIntervals(std::vector<std::pair<double, double>>& intervals) {
    if (intervals.empty()) {
        return;
    }
    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<double, double>> merged;
    double cur_l = intervals[0].first;
    double cur_r = intervals[0].second;
    for (size_t i = 1; i < intervals.size(); ++i) {
        const auto [l, r] = intervals[i];
        if (l <= cur_r + kEps) {
            cur_r = std::max(cur_r, r);
        } else {
            merged.emplace_back(cur_l, cur_r);
            cur_l = l;
            cur_r = r;
        }
    }
    merged.emplace_back(cur_l, cur_r);
    intervals = std::move(merged);
}

template <typename PrimaryCoordFn, typename SecondaryCoordFn>
std::vector<std::pair<double, double>> CollectIntervals(const Map& map,
                                                        double fixed_coord,
                                                        PrimaryCoordFn get_primary_coord,
                                                        SecondaryCoordFn get_secondary_coord) {
    std::vector<std::pair<double, double>> intervals;
    for (const Road& road : map.GetRoads()) {
        const double fixed0 = get_secondary_coord(road.GetStart());
        if (get_secondary_coord(road.GetStart()) == get_secondary_coord(road.GetEnd())) {
            if (std::fabs(fixed_coord - fixed0) > kRoadHalfWidth + kEps) {
                continue;
            }
            const double primary0 = get_primary_coord(road.GetStart());
            const double primary1 = get_primary_coord(road.GetEnd());
            intervals.emplace_back(std::min(primary0, primary1) - kRoadHalfWidth,
                                   std::max(primary0, primary1) + kRoadHalfWidth);
        } else {
            const double primary = get_primary_coord(road.GetStart());
            const double fixed1 = get_secondary_coord(road.GetEnd());
            const double fixed_min = std::min(fixed0, fixed1);
            const double fixed_max = std::max(fixed0, fixed1);
            if (fixed_coord + kEps < fixed_min - kRoadHalfWidth || fixed_coord - kEps > fixed_max + kRoadHalfWidth) {
                continue;
            }
            intervals.emplace_back(primary - kRoadHalfWidth, primary + kRoadHalfWidth);
        }
    }
    MergeIntervals(intervals);
    return intervals;
}

std::vector<std::pair<double, double>> CollectXIntervals(const Map& map, double y) {
    return CollectIntervals(
        map, y,
        [](const Point& point) { return static_cast<double>(point.x); },
        [](const Point& point) { return static_cast<double>(point.y); });
}

std::vector<std::pair<double, double>> CollectYIntervals(const Map& map, double x) {
    return CollectIntervals(
        map, x,
        [](const Point& point) { return static_cast<double>(point.y); },
        [](const Point& point) { return static_cast<double>(point.x); });
}

std::optional<std::pair<double, double>> FindIntervalContaining(const std::vector<std::pair<double, double>>& intervals,
                                                                double coord) {
    for (const auto& [a, b] : intervals) {
        if (coord + kEps >= a && coord - kEps <= b) {
            return std::pair{a, b};
        }
    }
    return std::nullopt;
}

void ApplyAxisMove(double& coord_along_motion,
                   double vel,
                   double dt,
                   const std::vector<std::pair<double, double>>& intervals_along_axis,
                   Vec2& speed_out) {
    if (vel == 0.0) {
        return;
    }
    const double target = coord_along_motion + vel * dt;
    auto iv = FindIntervalContaining(intervals_along_axis, coord_along_motion);
    if (!iv) {
        speed_out.x = 0.0;
        speed_out.y = 0.0;
        return;
    }
    const auto [lo, hi] = *iv;
    const double clamped = std::clamp(target, lo, hi);
    if (std::fabs(clamped - target) > kEps) {
        speed_out.x = 0.0;
        speed_out.y = 0.0;
    }
    coord_along_motion = clamped;
}

geom::Point2D ToPoint2D(Vec2 point) {
    return {point.x, point.y};
}

class GatheringProvider : public collision_detector::ItemGathererProvider {
public:
    struct ItemInfo {
        enum class Kind {
            LostObject,
            Office,
        };

        Kind kind;
        size_t index;
        collision_detector::Item item;
    };

    GatheringProvider(const std::vector<Game::LostObject>& lost_objects,
                      const Map::Offices& offices,
                      const std::vector<Game::Player*>& players,
                      const std::vector<Vec2>& start_positions)
        : players_(players) {
        items_.reserve(lost_objects.size() + offices.size());
        gatherers_.reserve(players.size());

        for (size_t idx = 0; idx < lost_objects.size(); ++idx) {
            items_.push_back(ItemInfo{
                ItemInfo::Kind::LostObject,
                idx,
                {{lost_objects[idx].position.x, lost_objects[idx].position.y}, kLostObjectWidth},
            });
        }
        for (size_t idx = 0; idx < offices.size(); ++idx) {
            const auto position = offices[idx].GetPosition();
            items_.push_back(ItemInfo{
                ItemInfo::Kind::Office,
                idx,
                {{static_cast<double>(position.x), static_cast<double>(position.y)}, kOfficeWidth},
            });
        }
        for (size_t idx = 0; idx < players.size(); ++idx) {
            gatherers_.push_back({
                ToPoint2D(start_positions[idx]),
                ToPoint2D(players[idx]->position),
                kPlayerWidth,
            });
        }
    }

    size_t ItemsCount() const override {
        return items_.size();
    }

    collision_detector::Item GetItem(size_t idx) const override {
        return items_.at(idx).item;
    }

    size_t GatherersCount() const override {
        return gatherers_.size();
    }

    collision_detector::Gatherer GetGatherer(size_t idx) const override {
        return gatherers_.at(idx);
    }

    const ItemInfo& GetItemInfo(size_t idx) const {
        return items_.at(idx);
    }

    Game::Player& GetPlayer(size_t idx) const {
        return *players_.at(idx);
    }

private:
    std::vector<ItemInfo> items_;
    std::vector<collision_detector::Gatherer> gatherers_;
    std::vector<Game::Player*> players_;
};

void ProcessGatheringEvents(std::vector<Game::LostObject>& lost_objects,
                            const Map& map,
                            const std::vector<Game::Player*>& players,
                            const std::vector<Vec2>& start_positions) {
    if (players.empty() || (lost_objects.empty() && map.GetOffices().empty())) {
        return;
    }

    GatheringProvider provider{lost_objects, map.GetOffices(), players, start_positions};
    const auto events = collision_detector::FindGatherEvents(provider);
    if (events.empty()) {
        return;
    }

    std::unordered_set<Game::LostObjectId> collected_lost_object_ids;
    for (const auto& event : events) {
        Game::Player& player = provider.GetPlayer(event.gatherer_id);
        const auto& item_info = provider.GetItemInfo(event.item_id);
        if (item_info.kind == GatheringProvider::ItemInfo::Kind::Office) {
            player.ReturnToOffice();
            continue;
        }

        const Game::LostObject& lost_object = lost_objects.at(item_info.index);
        if (collected_lost_object_ids.contains(lost_object.id)) {
            continue;
        }
        if (!player.TryCollect(FoundObject{lost_object.id, lost_object.type, map.GetLootValue(lost_object.type)})) {
            continue;
        }
        collected_lost_object_ids.insert(lost_object.id);
    }

    if (collected_lost_object_ids.empty()) {
        return;
    }

    lost_objects.erase(
        std::remove_if(
            lost_objects.begin(),
            lost_objects.end(),
            [&collected_lost_object_ids](const Game::LostObject& lost_object) {
                return collected_lost_object_ids.contains(lost_object.id);
            }),
        lost_objects.end());
}

}  // namespace

void Map::AddOffice(Office office) {
    if (warehouse_id_to_index_.contains(office.GetId())) {
        throw std::invalid_argument("Duplicate warehouse");
    }

    const size_t index = offices_.size();
    Office& o = offices_.emplace_back(std::move(office));
    try {
        warehouse_id_to_index_.emplace(o.GetId(), index);
    } catch (...) {
        offices_.pop_back();
        throw;
    }
}

void Game::AddMap(Map map) {
    const size_t index = maps_.size();
    if (auto [it, inserted] = map_id_to_index_.emplace(map.GetId(), index); !inserted) {
        throw std::invalid_argument("Map with id "s + *map.GetId() + " already exists"s);
    } else {
        try {
            maps_.emplace_back(std::move(map));
            map_runtime_states_.push_back(MapRuntimeState{
                loot_gen::LootGenerator{loot_generator_config_.period, loot_generator_config_.probability}});
        } catch (...) {
            if (!maps_.empty() && maps_.size() > index) {
                maps_.pop_back();
            }
            if (!map_runtime_states_.empty() && map_runtime_states_.size() > index) {
                map_runtime_states_.pop_back();
            }
            map_id_to_index_.erase(it);
            throw;
        }
    }
}

void Game::SetLootGeneratorConfig(LootGeneratorConfig config) noexcept {
    loot_generator_config_ = config;
    for (auto& runtime_state : map_runtime_states_) {
        runtime_state.generator =
            loot_gen::LootGenerator{loot_generator_config_.period, loot_generator_config_.probability};
    }
}

Vec2 Game::SpawnAtFirstRoadStart(const Map& map) {
    const auto& roads = map.GetRoads();
    if (roads.empty()) {
        throw std::invalid_argument("Map has no roads");
    }
    const Road& road = roads[0];
    return {static_cast<double>(road.GetStart().x), static_cast<double>(road.GetStart().y)};
}

Vec2 Game::SpawnAtRandomRoadPoint(const Map& map) {
    const auto& roads = map.GetRoads();
    if (roads.empty()) {
        throw std::invalid_argument("Map has no roads");
    }
    std::uniform_int_distribution<size_t> road_index_dist(0, roads.size() - 1);
    const Road& road = roads[road_index_dist(random_generator_)];

    if (road.IsHorizontal()) {
        const int x0 = road.GetStart().x;
        const int x1 = road.GetEnd().x;
        const int lo = std::min(x0, x1);
        const int hi = std::max(x0, x1);
        std::uniform_int_distribution<int> along(lo, hi);
        return {static_cast<double>(along(random_generator_)), static_cast<double>(road.GetStart().y)};
    }

    const int y0 = road.GetStart().y;
    const int y1 = road.GetEnd().y;
    const int lo = std::min(y0, y1);
    const int hi = std::max(y0, y1);
    std::uniform_int_distribution<int> along(lo, hi);
    return {static_cast<double>(road.GetStart().x), static_cast<double>(along(random_generator_))};
}

Vec2 Game::GenerateRandomRoadPosition(const Map& map) {
    const auto& roads = map.GetRoads();
    if (roads.empty()) {
        throw std::invalid_argument("Map has no roads");
    }
    std::uniform_int_distribution<size_t> road_index_dist(0, roads.size() - 1);
    const Road& road = roads[road_index_dist(random_generator_)];

    if (road.IsHorizontal()) {
        const double x0 = static_cast<double>(road.GetStart().x);
        const double x1 = static_cast<double>(road.GetEnd().x);
        const double lo = std::min(x0, x1);
        const double hi = std::max(x0, x1);
        std::uniform_real_distribution<double> along(lo, hi);
        return {along(random_generator_), static_cast<double>(road.GetStart().y)};
    }

    const double y0 = static_cast<double>(road.GetStart().y);
    const double y1 = static_cast<double>(road.GetEnd().y);
    const double lo = std::min(y0, y1);
    const double hi = std::max(y0, y1);
    std::uniform_real_distribution<double> along(lo, hi);
    return {static_cast<double>(road.GetStart().x), along(random_generator_)};
}

Game::JoinResult Game::JoinGame(const std::string& user_name, const Map::Id& map_id) {
    const Map* map = FindMap(map_id);
    if (!map) {
        throw std::invalid_argument("Map not found");
    }

    const PlayerId player_id = next_player_id_++;
    const Vec2 spawn =
        randomize_spawn_points_ ? SpawnAtRandomRoadPoint(*map) : SpawnAtFirstRoadStart(*map);
    players_.push_back(Player{player_id, user_name, map_id, spawn, Vec2{}, Direction::NORTH, map->GetBagCapacity()});

    std::uniform_int_distribution<unsigned> distribution(0, 15);
    static constexpr std::array<char, 16> hex_digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

    std::string token(32, '0');
    do {
        for (char& ch : token) {
            ch = hex_digits[distribution(random_generator_)];
        }
    } while (token_to_player_index_.contains(token));

    token_to_player_index_.emplace(token, players_.size() - 1);
    return {player_id, token};
}

void Game::ApplyPlayerMove(const std::string& token, std::string_view move) {
    auto it = token_to_player_index_.find(token);
    if (it == token_to_player_index_.end()) {
        throw std::logic_error("ApplyPlayerMove: unknown token");
    }
    Player& player = players_[it->second];
    const Map* map = FindMap(player.map_id);
    if (!map) {
        throw std::logic_error("ApplyPlayerMove: map not found");
    }
    const double s = map->GetDogSpeed();

    if (move == "L") {
        player.speed = {-s, 0.0};
        player.direction = Direction::WEST;
    } else if (move == "R") {
        player.speed = {s, 0.0};
        player.direction = Direction::EAST;
    } else if (move == "U") {
        player.speed = {0.0, -s};
        player.direction = Direction::NORTH;
    } else if (move == "D") {
        player.speed = {0.0, s};
        player.direction = Direction::SOUTH;
    } else if (move.empty()) {
        player.speed = {0.0, 0.0};
    } else {
        throw std::invalid_argument("invalid move");
    }
}

void Game::Tick(int64_t time_delta_ms) {
    const double dt = static_cast<double>(time_delta_ms) / 1000.0;
    if (dt == 0.0) {
        return;
    }

    std::vector<Vec2> start_positions;
    start_positions.reserve(players_.size());
    for (const Player& player : players_) {
        start_positions.push_back(player.position);
    }

    for (size_t player_idx = 0; player_idx < players_.size(); ++player_idx) {
        Player& player = players_[player_idx];
        const Map* map = FindMap(player.map_id);
        if (!map) {
            continue;
        }

        if (player.speed.x != 0.0) {
            auto x_intervals = CollectXIntervals(*map, player.position.y);
            ApplyAxisMove(player.position.x, player.speed.x, dt, x_intervals, player.speed);
        } else if (player.speed.y != 0.0) {
            auto y_intervals = CollectYIntervals(*map, player.position.x);
            ApplyAxisMove(player.position.y, player.speed.y, dt, y_intervals, player.speed);
        }
    }

    for (const Map& map : maps_) {
        std::vector<Player*> map_players;
        std::vector<Vec2> map_start_positions;
        for (size_t player_idx = 0; player_idx < players_.size(); ++player_idx) {
            if (players_[player_idx].map_id == map.GetId()) {
                map_players.push_back(&players_[player_idx]);
                map_start_positions.push_back(start_positions[player_idx]);
            }
        }
        if (map_players.empty()) {
            continue;
        }

        std::vector<LostObject> map_lost_objects;
        map_lost_objects.reserve(lost_objects_.size());
        for (const LostObject& lost_object : lost_objects_) {
            if (lost_object.map_id == map.GetId()) {
                map_lost_objects.push_back(lost_object);
            }
        }

        ProcessGatheringEvents(map_lost_objects, map, map_players, map_start_positions);

        lost_objects_.erase(
            std::remove_if(
                lost_objects_.begin(),
                lost_objects_.end(),
                [&map](const LostObject& lost_object) {
                    return lost_object.map_id == map.GetId();
                }),
            lost_objects_.end());
        lost_objects_.insert(lost_objects_.end(), map_lost_objects.begin(), map_lost_objects.end());
    }

    const auto time_delta = loot_gen::LootGenerator::TimeInterval{time_delta_ms};
    for (size_t map_index = 0; map_index < maps_.size(); ++map_index) {
        const Map& map = maps_[map_index];
        if (map.GetLootTypesCount() == 0) {
            continue;
        }

        unsigned looter_count = 0;
        for (const Player& player : players_) {
            if (player.map_id == map.GetId()) {
                ++looter_count;
            }
        }

        unsigned loot_count = 0;
        for (const LostObject& lost_object : lost_objects_) {
            if (lost_object.map_id == map.GetId()) {
                ++loot_count;
            }
        }

        const unsigned generated_count =
            map_runtime_states_[map_index].generator.Generate(time_delta, loot_count, looter_count);
        if (generated_count == 0) {
            continue;
        }

        std::uniform_int_distribution<size_t> loot_type_dist(0, map.GetLootTypesCount() - 1);
        for (unsigned i = 0; i < generated_count; ++i) {
            lost_objects_.push_back(LostObject{
                next_lost_object_id_++,
                map.GetId(),
                loot_type_dist(random_generator_),
                GenerateRandomRoadPosition(map),
            });
        }
    }
}

const Game::Player* Game::FindPlayerByToken(const std::string& token) const noexcept {
    if (auto it = token_to_player_index_.find(token); it != token_to_player_index_.end()) {
        return &players_[it->second];
    }
    return nullptr;
}

std::vector<const Game::Player*> Game::GetPlayersByMap(const Map::Id& map_id) const {
    std::vector<const Player*> result;
    for (const auto& player : players_) {
        if (player.map_id == map_id) {
            result.push_back(&player);
        }
    }
    return result;
}

std::vector<const Game::LostObject*> Game::GetLostObjectsByMap(const Map::Id& map_id) const {
    std::vector<const LostObject*> result;
    for (const auto& lost_object : lost_objects_) {
        if (lost_object.map_id == map_id) {
            result.push_back(&lost_object);
        }
    }
    return result;
}

std::vector<std::pair<std::string, Game::PlayerId>> Game::GetTokenAndPlyerIds() const {
    std::vector<std::pair<std::string, PlayerId>> result;
    result.reserve(token_to_player_index_.size());

    for (const auto& [token, player_index] : token_to_player_index_) {
        if (player_index >= players_.size()) {
            throw std::logic_error("GetTokenAndPlyerIds: invalid player index");
        }
        
        result.emplace_back(token, players_[player_index].id);
    }

    return result;
}

void Game::RestoreState(std::vector<Player> players,
                      std::vector<LostObject> lost_objects,
                      std::vector<std::pair<std::string, PlayerId>> token_and_player_ids,
                      PlayerId next_player_id,
                      LostObjectId next_lost_object_id) {
    std::unordered_map<PlayerId, size_t> player_id_to_index;
    player_id_to_index.reserve(players.size());

    for (size_t i = 0; i < players.size(); i++) {
        const Player& player = players[i];

        if (!FindMap(player.map_id)) {
            throw std::logic_error("RestoreState: player refers to unknown map");
        }

        if (player.id >= next_player_id) {
            throw std::logic_error("RestoreState: invalid next player id");
        }

        if (player.bag.size() > player.bag_capacity) {
            throw std::logic_error("RestoreState: player bag exceeds capacity");
        }

        const auto [_, inserted] = player_id_to_index.emplace(player.id, i);
        if (!inserted) {
            throw std::logic_error("RestoreState: duplicate player id");
        }
    }

    for (const LostObject& obj : lost_objects) {
        if (!FindMap(obj.map_id)) {
            throw std::logic_error("RestoreState: lost object refers to unknown map");
        }

        if (obj.id >= next_lost_object_id) {
            throw std::logic_error("RestoreState: invalid next lost object id");
        }
    }

    TokenToPlayerIndex restored_token_to_player_index;
    restored_token_to_player_index.reserve(token_and_player_ids.size());

    for (const auto& [token, player_id] : token_and_player_ids) {
        const auto player_it = player_id_to_index.find(player_id);
        if (player_it == player_id_to_index.end()) {
            throw std::logic_error("RestoreState: token refers to unknown player id");
        }

        const auto [_, inserted] = restored_token_to_player_index.emplace(token, player_it->second);
        if (!inserted) {
            throw std::logic_error("RestoreState: duplicate token");
        }
    }

    players_ = std::move(players);
    lost_objects_ = std::move(lost_objects);
    token_to_player_index_ = std::move(restored_token_to_player_index);
    next_player_id_ = next_player_id;
    next_lost_object_id_ = next_lost_object_id;
}

}  // namespace model
