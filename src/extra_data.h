#pragma once

#include <unordered_map>

#include <boost/json.hpp>

#include "model.h"

namespace extra_data {

namespace json = boost::json;

class MapExtraData {
public:
    void AddLootTypes(const model::Map::Id& map_id, json::array loot_types);
    const json::array* FindLootTypes(const model::Map::Id& map_id) const noexcept;

private:
    using MapIdHasher = util::TaggedHasher<model::Map::Id>;
    std::unordered_map<model::Map::Id, json::array, MapIdHasher> loot_types_by_map_;
};

}  // namespace extra_data
