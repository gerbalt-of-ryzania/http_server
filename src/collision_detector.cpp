#include "collision_detector.h"

#include <limits>

namespace collision_detector {

CollectionResult TryCollectPoint(geom::Point2D a, geom::Point2D b, geom::Point2D c) {
    const double u_x = c.x - a.x;
    const double u_y = c.y - a.y;
    const double v_x = b.x - a.x;
    const double v_y = b.y - a.y;
    const double u_dot_v = u_x * v_x + u_y * v_y;
    const double u_len2 = u_x * u_x + u_y * u_y;
    const double v_len2 = v_x * v_x + v_y * v_y;
    if (v_len2 == 0.0) {
        return CollectionResult{u_len2, std::numeric_limits<double>::infinity()};
    }
    const double proj_ratio = u_dot_v / v_len2;
    const double sq_distance = u_len2 - (u_dot_v * u_dot_v) / v_len2;

    return CollectionResult{sq_distance, proj_ratio};
}

std::vector<GatheringEvent> FindGatherEvents(const ItemGathererProvider& provider) {
    std::vector<GatheringEvent> events;
    events.reserve(provider.GatherersCount() * provider.ItemsCount());

    for (size_t gatherer_idx = 0; gatherer_idx < provider.GatherersCount(); ++gatherer_idx) {
        const auto gatherer = provider.GetGatherer(gatherer_idx);
        if (gatherer.start_pos == gatherer.end_pos) {
            continue;
        }

        for (size_t item_idx = 0; item_idx < provider.ItemsCount(); ++item_idx) {
            const auto item = provider.GetItem(item_idx);
            const auto collect_result = TryCollectPoint(gatherer.start_pos, gatherer.end_pos, item.position);
            if (collect_result.IsCollected(gatherer.width + item.width)) {
                events.push_back({
                    .item_id = item_idx,
                    .gatherer_id = gatherer_idx,
                    .sq_distance = collect_result.sq_distance,
                    .time = collect_result.proj_ratio,
                });
            }
        }
    }

    std::sort(events.begin(), events.end(), [](const GatheringEvent& lhs, const GatheringEvent& rhs) {
        if (lhs.time != rhs.time) {
            return lhs.time < rhs.time;
        }
        if (lhs.gatherer_id != rhs.gatherer_id) {
            return lhs.gatherer_id < rhs.gatherer_id;
        }
        return lhs.item_id < rhs.item_id;
    });

    return events;
}

}  // namespace collision_detector
