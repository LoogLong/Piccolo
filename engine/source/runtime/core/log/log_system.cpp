#include "runtime/core/log/log_system.h"

#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace Piccolo
{
    LogSystem::LogSystem()
    {
        spdlog::init_thread_pool(8192, 1);

#ifdef PICCOLO_WIN32_GUI
        // GUI build: no console — log to file only
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/piccolo.log", true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");

        const spdlog::sinks_init_list sink_list = {file_sink};

        m_logger = std::make_shared<spdlog::async_logger>("muggle_logger",
                                                          sink_list.begin(),
                                                          sink_list.end(),
                                                          spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block);
#else
        // Console build: log to console only
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::trace);
        console_sink->set_pattern("[%^%l%$] %v");

        const spdlog::sinks_init_list sink_list = {console_sink};

        m_logger = std::make_shared<spdlog::async_logger>("muggle_logger",
                                                          sink_list.begin(),
                                                          sink_list.end(),
                                                          spdlog::thread_pool(),
                                                          spdlog::async_overflow_policy::block);
#endif

        m_logger->set_level(spdlog::level::trace);
        spdlog::register_logger(m_logger);
    }

    LogSystem::~LogSystem()
    {
        m_logger->flush();
        spdlog::drop_all();
    }

} // namespace Piccolo
