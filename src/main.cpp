#include "sdk.h"
//
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "json_loader.h"
#include "request_handler.h"
#include "state_serialization.h"

using namespace std::literals;
namespace net = boost::asio;

namespace {

struct CommandLineArgs {
    bool show_help = false;
    std::optional<unsigned> tick_period_ms;
    std::string config_path;
    std::string www_root;
    bool randomize_spawn_points = false;
    std::optional<std::string> state_file;
    std::optional<unsigned> save_state_period_ms;
};

class StateSaveScheduler {
public:
    StateSaveScheduler(std::optional<std::filesystem::path> state_path,
                    std::optional<unsigned> save_period_ms)
        : state_path_(std::move(state_path))
        , save_period_ms_(save_period_ms) {
    }

    void OnTick(const model::Game& game, uint64_t delta_ms) {
        if (!state_path_ || !save_period_ms_) {
            return;
        }

        elapsed_since_save_ms_ += delta_ms;

        if (elapsed_since_save_ms_ >= *save_period_ms_) {
            serialization::SaveGameState(game, *state_path_);
            elapsed_since_save_ms_ = 0;
        }
    }

    void SaveNow(const model::Game& game) const {
        if (!state_path_) {
            return;
        }

        serialization::SaveGameState(game, *state_path_);
    }
    
private:
    std::optional<std::filesystem::path> state_path_;
    std::optional<unsigned> save_period_ms_;
    uint64_t elapsed_since_save_ms_ = 0;
};

template <typename Fn>
void RunWorkers(unsigned n, const Fn& fn) {
    n = std::max(1u, n);
    std::vector<std::jthread> workers;
    workers.reserve(n - 1);
    while (--n) {
        workers.emplace_back(fn);
    }
    fn();
}

std::string_view HelpText() {
    return
        "Allowed options:\n"
        "  -h [ --help ]                    produce help message\n"
        "  -t [ --tick-period ] arg         set tick period\n"
        "  -c [ --config-file ] arg         set config file path\n"
        "  -w [ --www-root ] arg            set static files root\n"
        "  --state-file arg                 set state file path\n"
        "  --save-state-period arg          set state save period\n"
        "  --randomize-spawn-points         spawn dogs at random positions\n";
}

std::string RequireOptionValue(int& index, int argc, const char* argv[], std::string_view option_name) {
    if (index + 1 >= argc) {
        throw std::runtime_error("Missing value for option " + std::string(option_name));
    }
    return argv[++index];
}

unsigned ParseUnsigned(const std::string& value, std::string_view option_name) {
    size_t pos = 0;
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value, &pos);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid value for option " + std::string(option_name));
    }
    if (pos != value.size() || parsed > std::numeric_limits<unsigned>::max()) {
        throw std::runtime_error("Invalid value for option " + std::string(option_name));
    }
    return static_cast<unsigned>(parsed);
}

CommandLineArgs ParseCommandLine(int argc, const char* argv[]) {
    CommandLineArgs args;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            args.show_help = true;
        } else if (arg == "-t" || arg == "--tick-period") {
            args.tick_period_ms = ParseUnsigned(RequireOptionValue(i, argc, argv, arg), arg);
        } else if (arg == "-c" || arg == "--config-file") {
            args.config_path = RequireOptionValue(i, argc, argv, arg);
        } else if (arg == "-w" || arg == "--www-root") {
            args.www_root = RequireOptionValue(i, argc, argv, arg);
        } else if (arg == "--state-file") {
            args.state_file = RequireOptionValue(i, argc, argv, arg);
        } else if (arg == "--save-state-period") {
            args.save_state_period_ms = ParseUnsigned(RequireOptionValue(i, argc, argv, arg), arg);
        } else if (arg == "--randomize-spawn-points") {
            args.randomize_spawn_points = true;
        } else {
            throw std::runtime_error("Unknown option " + std::string(arg));
        }
    }
    return args;
}


}  // namespace

int main(int argc, const char* argv[]) {
    CommandLineArgs args;
    try {
        args = ParseCommandLine(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }

    if (args.show_help) {
        std::cout << HelpText();
        return EXIT_SUCCESS;
    }

    if (args.config_path.empty() || args.www_root.empty()) {
        std::cerr << "Options --config-file and --www-root are required" << std::endl;
        return EXIT_FAILURE;
    }

    const std::string& config_path = args.config_path;
    const std::string& www_root = args.www_root;
    const bool randomize_spawn = args.randomize_spawn_points;
    const bool tick_timer_enabled = args.tick_period_ms.has_value();
    unsigned tick_period_ms = 0;
    if (tick_timer_enabled) {
        tick_period_ms = *args.tick_period_ms;
        if (tick_period_ms == 0) {
            std::cerr << "tick-period must be greater than zero" << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (args.state_file && args.save_state_period_ms && *args.save_state_period_ms == 0) {
        std::cerr << "save-state-period must be greater than zero" << std::endl;
        return EXIT_FAILURE;
    }

    try {
        auto loaded_game = json_loader::LoadGame(config_path);
        model::Game& game = loaded_game.game;
        game.SetRandomizeSpawnPoints(randomize_spawn);

        std::optional<std::filesystem::path> state_path;
        if (args.state_file) {
            state_path = std::filesystem::path{*args.state_file};

            if (std::filesystem::exists(*state_path)) {
                try {
                    serialization::LoadGameState(game, *state_path);
                } catch (const std::exception& ex) {
                    std::cerr << "Failed to load state: " << ex.what() << std::endl;
                    return EXIT_FAILURE;
                }
            }
        }

        StateSaveScheduler save_scheduler {
            state_path,
            state_path ? args.save_state_period_ms : std::nullopt
        };

        auto tick_fn = [&game, &save_scheduler](int64_t time_delta_ms) {
            game.Tick(time_delta_ms);
            save_scheduler.OnTick(game, static_cast<uint64_t>(time_delta_ms));
        };

        const unsigned num_threads = std::thread::hardware_concurrency();
        net::io_context ioc(num_threads);

        net::signal_set signals(ioc, SIGINT, SIGTERM);
        signals.async_wait([&ioc](const boost::system::error_code& ec, int signal_number) {
            if (!ec) {
                std::cout << "Signal " << signal_number << " received, stopping..." << std::endl;
                ioc.stop();
            }
        });

        auto api_strand = net::make_strand(ioc);
        const bool enable_http_tick = !tick_timer_enabled;
        http_handler::RequestHandler handler{
            game,
            loaded_game.map_extra_data,
            www_root,
            api_strand,
            tick_fn,
            enable_http_tick
        };

        const std::string address = "0.0.0.0";
        const unsigned short port = 8080;
        http_server::ServeHttp(ioc, {net::ip::make_address(address), port}, [&handler](auto&& req, auto&& send) {
            handler(std::forward<decltype(req)>(req), std::forward<decltype(send)>(send));
        });

        if (tick_timer_enabled) {
            auto timer = std::make_shared<net::steady_timer>(api_strand);
            std::function<void()> schedule_tick;
            schedule_tick = [timer, tick_fn, tick_period_ms, api_strand, &schedule_tick]() {
                timer->expires_after(std::chrono::milliseconds(tick_period_ms));
                timer->async_wait([timer, tick_fn, tick_period_ms, api_strand, &schedule_tick](
                                        boost::system::error_code ec) {
                    if (ec) {
                        return;
                    }
                    net::dispatch(api_strand, [tick_fn, tick_period_ms]() {
                        tick_fn(static_cast<int64_t>(tick_period_ms));
                    });
                    schedule_tick();
                });
            };
            schedule_tick();
        }

        std::cout << "Server has started..."sv << std::endl;

        RunWorkers(std::max(1u, num_threads), [&ioc] {
            ioc.run();
        });

        if (state_path) {
            try {
                save_scheduler.SaveNow(game);
            } catch (const std::exception& ex) {
                std::cerr << "Failed to save state: " << ex.what() << std::endl;
                return EXIT_FAILURE;
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
