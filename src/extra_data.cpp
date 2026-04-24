#include "extra_data.h"

namespace extra_data {

void MapExtraData::AddLootTypes(const model::Map::Id& map_id, json::array loot_types) {
    loot_types_by_map_[map_id] = std::move(loot_types);
}

const json::array* MapExtraData::FindLootTypes(const model::Map::Id& map_id) const noexcept {
    if (auto it = loot_types_by_map_.find(map_id); it != loot_types_by_map_.end()) {
        return &it->second;
    }
    return nullptr;
}

}  // namespace extra_data
