/*
 * Copyright (C) 2016-2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Jan Kundrát <jan.kundrat@cesnet.cz>
 *
*/

#include <cstdio>
#include <cstdlib>
#include <inttypes.h>

#include "configure.cmake.h" /* Expose HAVE_SYSTEMD */

#include <boost/asio.hpp>
#ifdef HAVE_SYSTEMD
   #include <spdlog/sinks/systemd_sink.h>
#endif
#include <spdlog/sinks/syslog_sink.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <docopt.h>
#include <spdlog/spdlog.h>
#include <sysrepo-cpp/Session.hpp>
#include "restconf/Server.h"
static const char usage[] =
  R"(Rousette - RESTCONF server
Usage:
  rousette [--syslog] [--timeout <SECONDS>] [--log-level <LEVEL>] [--help]
Options:
  -h --help                         Show this screen.
  -t --timeout <SECONDS>            Change default timeout in sysrepo (if not set, use sysrepo internal).
  --syslog                          Log to syslog.
  --log-level <LEVEL>               Set log level (trace, debug, info, warn, error, critical, off) [default: info].
)";
#ifdef HAVE_SYSTEMD

namespace {
/** @short Is stderr connected to journald? Not thread safe. */
bool is_journald_active()
{
    const auto stream = ::getenv("JOURNAL_STREAM");
    if (!stream) {
        return false;
    }
    uintmax_t dev;
    uintmax_t inode;
    if (::sscanf(stream, "%" SCNuMAX ":%" SCNuMAX, &dev, &inode) != 2) {
        return false;
    }
    struct stat buf;
    if (fstat(STDERR_FILENO, &buf)) {
        return false;
    }
    return static_cast<uintmax_t>(buf.st_dev) == dev && static_cast<uintmax_t>(buf.st_ino) == inode;
}

/** @short Provide better levels, see https://github.com/gabime/spdlog/pull/1292#discussion_r340777258 */
template<typename Mutex>
class journald_sink : public spdlog::sinks::systemd_sink<Mutex> {
public:
    journald_sink()
    {
        this->syslog_levels_ = {/* spdlog::level::trace      */ LOG_DEBUG,
              /* spdlog::level::debug      */ LOG_INFO,
              /* spdlog::level::info       */ LOG_NOTICE,
              /* spdlog::level::warn       */ LOG_WARNING,
              /* spdlog::level::err        */ LOG_ERR,
              /* spdlog::level::critical   */ LOG_CRIT,
              /* spdlog::level::off        */ LOG_ALERT};
    }
};
}
#endif
int main(int argc, char* argv [])
{
    auto args = docopt::docopt(usage, {argv + 1, argv + argc}, true,""/* version */, true);
    auto timeout = std::chrono::milliseconds{0};
    auto logLevel = spdlog::level::info;

    if (args["--timeout"]) {
        timeout = std::chrono::milliseconds{args["--timeout"].asLong() * 1000};
    }

    if (args["--log-level"]) {
        auto levelStr = args["--log-level"].asString();
        if (levelStr == "trace") {
            logLevel = spdlog::level::trace;
        } else if (levelStr == "debug") {
            logLevel = spdlog::level::debug;
        } else if (levelStr == "info") {
            logLevel = spdlog::level::info;
        } else if (levelStr == "warn" || levelStr == "warning") {
            logLevel = spdlog::level::warn;
        } else if (levelStr == "error" || levelStr == "err") {
            logLevel = spdlog::level::err;
        } else if (levelStr == "critical") {
            logLevel = spdlog::level::critical;
        } else if (levelStr == "off") {
            logLevel = spdlog::level::off;
        } else {
            throw std::runtime_error("Invalid log level: " + levelStr + ". Valid levels: trace, debug, info, warn, error, critical, off");
        }
    }
    if (args["--syslog"].asBool()) {
        auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>("rousette", LOG_PID, LOG_USER, true);
        auto logger = std::make_shared<spdlog::logger>("rousette", syslog_sink);
        spdlog::set_default_logger(logger);
#ifdef HAVE_SYSTEMD
    } else if (is_journald_active()) {
        auto sink = std::make_shared<journald_sink<std::mutex>>();
        auto logger = std::make_shared<spdlog::logger>("rousette", sink);
        spdlog::set_default_logger(logger);
#endif
    } else {
        auto stdout_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
        auto logger = std::make_shared<spdlog::logger>("rousette", stdout_sink);
        spdlog::set_default_logger(logger);
    }
    spdlog::set_level(logLevel);

    /* We will parse URIs using boost::spirit's alnum/alpha/... matchers which are locale-dependent.
     * Let's use something stable no matter what the system is using
     */
    if (!std::setlocale(LC_CTYPE, "C.UTF-8")) {
        throw std::runtime_error("Could not set locale C.UTF-8");
    }

    auto conn = sysrepo::Connection{};
    auto server = rousette::restconf::Server{conn, "::1", "10080", timeout};

    // allow graceful shutdown
    boost::asio::signal_set signals(*server.io_services()[0], SIGTERM, SIGINT);
    signals.async_wait([&](const boost::system::error_code& ec, int) {
        if (!ec) {
            server.stop();
        }
    });
    server.join();

    return 0;
}
