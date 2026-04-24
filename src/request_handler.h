#pragma once
#include "extra_data.h"
#include "http_server.h"
#include "model.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <functional>

#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/filesystem.hpp>
#include <boost/json.hpp>

namespace http_handler {
namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace fs = boost::filesystem;
namespace net = boost::asio;

using ApiStrand = net::strand<net::io_context::executor_type>;

struct Endpoints {
    static constexpr std::string_view Api = "/api/";
    static constexpr std::string_view Maps = "/api/v1/maps";
    static constexpr std::string_view MapsPrefix = "/api/v1/maps/";
    static constexpr std::string_view JoinGame = "/api/v1/game/join";
    static constexpr std::string_view Players = "/api/v1/game/players";
    static constexpr std::string_view GameState = "/api/v1/game/state";
    static constexpr std::string_view PlayerAction = "/api/v1/game/player/action";
    static constexpr std::string_view GameTick = "/api/v1/game/tick";
};

class RequestHandler {
public:
    using TickFn = std::function<void(int64_t)>;

    explicit RequestHandler(model::Game& game,
                            const extra_data::MapExtraData& map_extra_data,
                            fs::path static_root,
                            ApiStrand api_strand,
                            TickFn tick_fn,
                            bool enable_http_tick = true)
        : game_{game}
        , map_extra_data_{map_extra_data}
        , static_root_{fs::canonical(std::move(static_root))}
        , api_strand_{std::move(api_strand)}
        , tick_fn_{std::move(tick_fn)}
        , http_tick_enabled_{enable_http_tick} {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        const std::string path_str{ExtractPath(req)};

        if (path_str.starts_with(Endpoints::Api)) {
            net::post(api_strand_, [this, path_str, req = std::move(req), send = std::forward<Send>(send)]() mutable {
                ProcessApi(std::string_view{path_str}, std::move(req), std::move(send));
            });
            return;
        }

        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return SendPlainText(req, http::status::method_not_allowed, "Method not allowed", std::forward<Send>(send));
        }

        auto file_path = ResolveStaticPath(std::string_view{path_str});
        if (!file_path) {
            return SendPlainText(req, http::status::bad_request, "Bad request", std::forward<Send>(send));
        }

        return SendFile(req, *file_path, std::forward<Send>(send));
    }

private:
    model::Game& game_;
    const extra_data::MapExtraData& map_extra_data_;
    fs::path static_root_;
    ApiStrand api_strand_;
    TickFn tick_fn_;
    bool http_tick_enabled_;

    template <typename Body, typename Allocator>
    static std::string_view ExtractPath(const http::request<Body, http::basic_fields<Allocator>>& req) {
        std::string_view target = req.target();
        std::string_view path = target.substr(0, target.find('?'));
        if (!path.starts_with('/')) {
            if (const auto scheme_pos = path.find("://"); scheme_pos != std::string_view::npos) {
                const auto path_pos = path.find('/', scheme_pos + 3);
                path = (path_pos == std::string_view::npos) ? std::string_view{"/"} : path.substr(path_pos);
            }
        }
        return path;
    }

    template <typename Body, typename Allocator, typename Send>
    void ProcessApi(std::string_view path, http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (path == Endpoints::JoinGame) {
            return HandleJoinGame(std::move(req), std::forward<Send>(send));
        }
        if (path == Endpoints::Players) {
            return HandlePlayers(std::move(req), std::forward<Send>(send));
        }
        if (path == Endpoints::GameState) {
            return HandleGameState(std::move(req), std::forward<Send>(send));
        }
        if (path == Endpoints::PlayerAction) {
            return HandlePlayerAction(std::move(req), std::forward<Send>(send));
        }
        if (path == Endpoints::GameTick) {
            if (!http_tick_enabled_) {
                return send(MakeApiError(req, http::status::bad_request, "badRequest", "Invalid endpoint"));
            }
            return HandleGameTick(std::move(req), std::forward<Send>(send));
        }
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Invalid method", "GET, HEAD"));
        }

        if (path == Endpoints::Maps) {
            json::array arr;
            for (const auto& map : game_.GetMaps()) {
                json::object obj;
                obj["id"] = *map.GetId();
                obj["name"] = map.GetName();
                arr.push_back(obj);
            }
            return send(MakeApiJsonResponse(req, http::status::ok, json::serialize(arr)));
        }

        if (path.starts_with(Endpoints::MapsPrefix)) {
            std::string_view id_str = path.substr(Endpoints::MapsPrefix.size());
            if (id_str.empty()) {
                return send(MakeApiError(req, http::status::bad_request, "badRequest", "Bad request"));
            }
            model::Map::Id id{std::string{id_str}};
            const model::Map* map = game_.FindMap(id);
            if (!map) {
                return send(MakeApiError(req, http::status::not_found, "mapNotFound", "Map not found"));
            }
            return send(MakeApiJsonResponse(req, http::status::ok, json::serialize(SerializeMap(*map, map_extra_data_))));
        }

        return send(MakeApiError(req, http::status::bad_request, "badRequest", "Invalid endpoint"));
    }

    static const char* DirectionToApiLetter(model::Direction d) noexcept {
        using model::Direction;
        switch (d) {
            case Direction::NORTH:
                return "U";
            case Direction::SOUTH:
                return "D";
            case Direction::WEST:
                return "L";
            case Direction::EAST:
                return "R";
        }
        return "U";
    }

    template <typename Body, typename Allocator>
    static http::response<http::string_body> MakeApiJsonResponse(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status,
        std::string body) {
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.body() = std::move(body);
        res.content_length(res.body().size());
        res.keep_alive(req.keep_alive());
        return res;
    }

    template <typename Body, typename Allocator>
    static http::response<http::empty_body> MakeApiJsonHeadResponse(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status,
        std::size_t content_length) {
        http::response<http::empty_body> res{status, req.version()};
        res.set(http::field::content_type, "application/json");
        res.set(http::field::cache_control, "no-cache");
        res.content_length(content_length);
        res.keep_alive(req.keep_alive());
        return res;
    }

    template <typename Body, typename Allocator>
    static http::response<http::string_body> MakeStringResponse(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status,
        std::string body,
        std::string_view content_type = "text/plain") {
        http::response<http::string_body> res{status, req.version()};
        res.set(http::field::content_type, content_type);
        res.body() = std::move(body);
        res.content_length(res.body().size());
        res.keep_alive(req.keep_alive());
        return res;
    }

    template <typename Body, typename Allocator>
    static auto MakeApiError(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status,
        std::string_view code,
        std::string_view message,
        std::optional<std::string_view> allow = std::nullopt) {
        json::object obj;
        obj["code"] = std::string(code);
        obj["message"] = std::string(message);
        auto res = MakeApiJsonResponse(req, status, json::serialize(obj));
        if (allow) {
            res.set(http::field::allow, *allow);
        }
        return res;
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleJoinGame(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::post) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Only POST method is expected", "POST"));
        }
        if (!IsJsonContentType(req)) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Join game request parse error"));
        }

        try {
            const json::value value = json::parse(req.body());
            const json::object& obj = value.as_object();
            const std::string user_name = json::value_to<std::string>(obj.at("userName"));
            const std::string map_id = json::value_to<std::string>(obj.at("mapId"));

            if (user_name.empty()) {
                return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Invalid name"));
            }

            if (!game_.FindMap(model::Map::Id{map_id})) {
                return send(MakeApiError(req, http::status::not_found, "mapNotFound", "Map not found"));
            }

            const auto join_result = game_.JoinGame(user_name, model::Map::Id{map_id});
            json::object response_body;
            response_body["authToken"] = join_result.auth_token;
            response_body["playerId"] = join_result.player_id;
            return send(MakeApiJsonResponse(req, http::status::ok, json::serialize(response_body)));
        } catch (const std::exception&) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Join game request parse error"));
        }
    }

    template <typename Body, typename Allocator, typename Send>
    void HandlePlayers(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Invalid method", "GET, HEAD"));
        }

        const auto token = ExtractBearerToken(req);
        if (!token) {
            return send(MakeApiError(req, http::status::unauthorized, "invalidToken", "Authorization header is missing"));
        }

        const model::Game::Player* current_player = game_.FindPlayerByToken(*token);
        if (!current_player) {
            return send(MakeApiError(req, http::status::unauthorized, "unknownToken", "Player token has not been found"));
        }

        json::object response_body;
        for (const model::Game::Player* player : game_.GetPlayersByMap(current_player->map_id)) {
            json::object player_data;
            player_data["name"] = player->name;
            response_body[std::to_string(player->id)] = player_data;
        }
        const std::string body = json::serialize(response_body);
        if (req.method() == http::verb::head) {
            return send(MakeApiJsonHeadResponse(req, http::status::ok, body.size()));
        }
        return send(MakeApiJsonResponse(req, http::status::ok, body));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleGameState(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::get && req.method() != http::verb::head) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Invalid method", "GET, HEAD"));
        }

        const auto token = ExtractBearerToken(req);
        if (!token) {
            return send(MakeApiError(req, http::status::unauthorized, "invalidToken", "Authorization header is required"));
        }

        const model::Game::Player* current_player = game_.FindPlayerByToken(*token);
        if (!current_player) {
            return send(MakeApiError(req, http::status::unauthorized, "unknownToken", "Player token has not been found"));
        }

        json::object response_body;
        json::object players_obj;
        for (const model::Game::Player* player : game_.GetPlayersByMap(current_player->map_id)) {
            json::object player_data;
            json::array pos;
            pos.push_back(player->position.x);
            pos.push_back(player->position.y);
            player_data["pos"] = pos;
            json::array speed;
            speed.push_back(player->speed.x);
            speed.push_back(player->speed.y);
            player_data["speed"] = speed;
            player_data["dir"] = DirectionToApiLetter(player->direction);
            json::array bag;
            for (const auto& item : player->bag) {
                json::object bag_item;
                bag_item["id"] = item.id;
                bag_item["type"] = static_cast<std::uint64_t>(item.type);
                bag.push_back(std::move(bag_item));
            }
            player_data["bag"] = std::move(bag);
            player_data["score"] = static_cast<std::uint64_t>(player->score);
            players_obj[std::to_string(player->id)] = player_data;
        }
        response_body["players"] = players_obj;

        json::object lost_objects_obj;
        for (const model::Game::LostObject* lost_object : game_.GetLostObjectsByMap(current_player->map_id)) {
            json::object lost_object_data;
            lost_object_data["type"] = static_cast<std::uint64_t>(lost_object->type);
            json::array pos;
            pos.push_back(lost_object->position.x);
            pos.push_back(lost_object->position.y);
            lost_object_data["pos"] = pos;
            lost_objects_obj[std::to_string(lost_object->id)] = lost_object_data;
        }
        response_body["lostObjects"] = lost_objects_obj;

        const std::string body = json::serialize(response_body);
        if (req.method() == http::verb::head) {
            return send(MakeApiJsonHeadResponse(req, http::status::ok, body.size()));
        }
        return send(MakeApiJsonResponse(req, http::status::ok, body));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandlePlayerAction(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::post) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Invalid method", "POST"));
        }
        if (!IsJsonContentType(req)) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Invalid content type"));
        }

        const auto token = ExtractBearerToken(req);
        if (!token) {
            return send(MakeApiError(req, http::status::unauthorized, "invalidToken", "Authorization header is required"));
        }

        if (!game_.FindPlayerByToken(*token)) {
            return send(MakeApiError(req, http::status::unauthorized, "unknownToken", "Player token has not been found"));
        }

        std::string move_str;
        try {
            const json::value value = json::parse(req.body());
            const json::object& obj = value.as_object();
            const auto& mv = obj.at("move");
            if (!mv.is_string()) {
                return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Failed to parse action"));
            }
            move_str = std::string{mv.as_string()};
        } catch (const std::exception&) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Failed to parse action"));
        }

        const std::string_view mv = move_str;
        if (!(mv == "L" || mv == "R" || mv == "U" || mv == "D" || mv.empty())) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Failed to parse action"));
        }

        game_.ApplyPlayerMove(*token, mv);
        return send(MakeApiJsonResponse(req, http::status::ok, "{}"));
    }

    template <typename Body, typename Allocator, typename Send>
    void HandleGameTick(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        if (req.method() != http::verb::post) {
            return send(MakeApiError(req, http::status::method_not_allowed, "invalidMethod", "Invalid method", "POST"));
        }
        if (!IsJsonContentType(req)) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON"));
        }

        int64_t time_delta_ms = 0;
        try {
            const json::value value = json::parse(req.body());
            const json::object& obj = value.as_object();
            const json::value& td = obj.at("timeDelta");
            if (td.is_int64()) {
                time_delta_ms = td.as_int64();
            } else if (td.is_uint64()) {
                const std::uint64_t u = td.as_uint64();
                if (u > static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
                    return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Invalid timeDelta value"));
                }
                time_delta_ms = static_cast<int64_t>(u);
            } else {
                return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Invalid timeDelta value"));
            }
        } catch (const std::exception&) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Failed to parse tick request JSON"));
        }

        if (time_delta_ms < 0) {
            return send(MakeApiError(req, http::status::bad_request, "invalidArgument", "Invalid timeDelta value"));
        }

        tick_fn_(time_delta_ms);
        return send(MakeApiJsonResponse(req, http::status::ok, "{}"));
    }

    template <typename Body, typename Allocator>
    static bool IsJsonContentType(const http::request<Body, http::basic_fields<Allocator>>& req) {
        const auto value = req[http::field::content_type];
        if (value.empty()) {
            return false;
        }
        std::string lower(value);
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return lower == "application/json" || lower.starts_with("application/json;");
    }

    template <typename Body, typename Allocator>
    static std::optional<std::string> ExtractBearerToken(const http::request<Body, http::basic_fields<Allocator>>& req) {
        const auto auth = req[http::field::authorization];
        if (auth.empty()) {
            return std::nullopt;
        }
        constexpr std::string_view prefix = "Bearer ";
        std::string auth_value(auth);
        if (!auth_value.starts_with(prefix)) {
            return std::nullopt;
        }
        std::string token = auth_value.substr(prefix.size());
        if (token.size() != 32) {
            return std::nullopt;
        }
        for (char ch : token) {
            const bool is_digit = (ch >= '0' && ch <= '9');
            const bool is_hex_alpha = (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
            if (!is_digit && !is_hex_alpha) {
                return std::nullopt;
            }
        }
        return token;
    }

    template <typename Body, typename Allocator, typename Send>
    static void SendPlainText(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        http::status status,
        std::string_view text,
        Send&& send) {
        if (req.method() == http::verb::head) {
            http::response<http::empty_body> res{status, req.version()};
            res.set(http::field::content_type, "text/plain");
            res.content_length(text.size());
            res.keep_alive(req.keep_alive());
            send(std::move(res));
            return;
        }

        send(MakeStringResponse(req, status, std::string(text)));
    }

    template <typename Body, typename Allocator, typename Send>
    static void SendFile(
        const http::request<Body, http::basic_fields<Allocator>>& req,
        const fs::path& path,
        Send&& send) {
        beast::error_code ec;
        http::file_body::value_type file;
        file.open(path.string().c_str(), beast::file_mode::scan, ec);
        if (ec) {
            SendPlainText(req, http::status::not_found, "File not found", std::forward<Send>(send));
            return;
        }

        const auto file_size = file.size();
        const auto content_type = GetMimeType(path.extension().string());

        if (req.method() == http::verb::head) {
            http::response<http::empty_body> res{http::status::ok, req.version()};
            res.set(http::field::content_type, content_type);
            res.content_length(file_size);
            res.keep_alive(req.keep_alive());
            send(std::move(res));
            return;
        }

        http::response<http::file_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, content_type);
        res.content_length(file_size);
        res.keep_alive(req.keep_alive());
        res.body() = std::move(file);
        send(std::move(res));
    }

    std::optional<fs::path> ResolveStaticPath(std::string_view path) const {
        if (path.empty() || path.front() != '/') {
            return std::nullopt;
        }

        auto decoded = UrlDecode(path);
        if (!decoded) {
            return std::nullopt;
        }

        decoded->erase(decoded->begin());

        fs::path relative_path{*decoded};
        if (relative_path.empty()) {
            relative_path = "index.html";
        }

        if (relative_path.is_absolute() || relative_path.has_root_name() || relative_path.has_root_directory()) {
            return std::nullopt;
        }

        relative_path = relative_path.lexically_normal();
        for (const auto& part : relative_path) {
            if (part == "..") {
                return std::nullopt;
            }
        }

        fs::path resolved = (static_root_ / relative_path).lexically_normal();

        boost::system::error_code ec;
        if (fs::is_directory(resolved, ec)) {
            resolved = (resolved / "index.html").lexically_normal();
        }

        return resolved;
    }

    static std::optional<std::string> UrlDecode(std::string_view value) {
        std::string result;
        result.reserve(value.size());

        for (size_t i = 0; i < value.size(); ++i) {
            const char ch = value[i];
            if (ch != '%') {
                result.push_back(ch);
                continue;
            }

            if (i + 2 >= value.size()) {
                return std::nullopt;
            }

            const int hi = HexDigitToInt(value[i + 1]);
            const int lo = HexDigitToInt(value[i + 2]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }

            result.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        }

        return result;
    }

    static int HexDigitToInt(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    }

    static std::string_view GetMimeType(std::string_view extension) {
        std::string lowercase_extension(extension);
        std::transform(lowercase_extension.begin(), lowercase_extension.end(), lowercase_extension.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (lowercase_extension == ".htm" || lowercase_extension == ".html") {
            return "text/html";
        }
        if (lowercase_extension == ".css") {
            return "text/css";
        }
        if (lowercase_extension == ".txt") {
            return "text/plain";
        }
        if (lowercase_extension == ".js") {
            return "text/javascript";
        }
        if (lowercase_extension == ".json") {
            return "application/json";
        }
        if (lowercase_extension == ".xml") {
            return "application/xml";
        }
        if (lowercase_extension == ".png") {
            return "image/png";
        }
        if (lowercase_extension == ".jpg" || lowercase_extension == ".jpe" || lowercase_extension == ".jpeg") {
            return "image/jpeg";
        }
        if (lowercase_extension == ".gif") {
            return "image/gif";
        }
        if (lowercase_extension == ".bmp") {
            return "image/bmp";
        }
        if (lowercase_extension == ".ico") {
            return "image/vnd.microsoft.icon";
        }
        if (lowercase_extension == ".tif" || lowercase_extension == ".tiff") {
            return "image/tiff";
        }
        if (lowercase_extension == ".svg" || lowercase_extension == ".svgz") {
            return "image/svg+xml";
        }
        if (lowercase_extension == ".mp3") {
            return "audio/mpeg";
        }

        return "application/octet-stream";
    }

    static json::object SerializeRoad(const model::Road& road) {
        json::object road_obj;
        road_obj["x0"] = road.GetStart().x;
        road_obj["y0"] = road.GetStart().y;
        if (road.IsHorizontal()) {
            road_obj["x1"] = road.GetEnd().x;
        } else {
            road_obj["y1"] = road.GetEnd().y;
        }
        return road_obj;
    }

    static json::object SerializeBuilding(const model::Building& building) {
        const auto& rect = building.GetBounds();
        json::object building_obj;
        building_obj["x"] = rect.position.x;
        building_obj["y"] = rect.position.y;
        building_obj["w"] = rect.size.width;
        building_obj["h"] = rect.size.height;
        return building_obj;
    }

    static json::object SerializeOffice(const model::Office& office) {
        json::object office_obj;
        office_obj["id"] = *office.GetId();
        office_obj["x"] = office.GetPosition().x;
        office_obj["y"] = office.GetPosition().y;
        office_obj["offsetX"] = office.GetOffset().dx;
        office_obj["offsetY"] = office.GetOffset().dy;
        return office_obj;
    }

    static json::object SerializeMap(const model::Map& map, const extra_data::MapExtraData& map_extra_data) {
        json::object obj;
        obj["id"] = *map.GetId();
        obj["name"] = map.GetName();

        json::array roads;
        for (const auto& road : map.GetRoads()) {
            roads.push_back(SerializeRoad(road));
        }
        obj["roads"] = roads;

        json::array buildings;
        for (const auto& building : map.GetBuildings()) {
            buildings.push_back(SerializeBuilding(building));
        }
        obj["buildings"] = buildings;

        json::array offices;
        for (const auto& office : map.GetOffices()) {
            offices.push_back(SerializeOffice(office));
        }
        obj["offices"] = offices;

        if (const json::array* loot_types = map_extra_data.FindLootTypes(map.GetId())) {
            obj["lootTypes"] = *loot_types;
        }

        return obj;
    }
};

}  // namespace http_handler
