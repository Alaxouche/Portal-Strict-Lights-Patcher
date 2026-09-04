#include "pch.h"

#include "logger.h"

namespace PortalLightsRuntimePatcher
{
	namespace
	{
		spdlog::level::level_enum ToLevel(int a_logLevel)
		{
			switch (a_logLevel) {
			case 1:
				return spdlog::level::err;
			case 2:
				return spdlog::level::warn;
			case 3:
				return spdlog::level::info;
			case 4:
				return spdlog::level::debug;
			case 5:
				return spdlog::level::trace;
			default:
				return spdlog::level::info;
			}
		}
	}

	void SetupLog(bool a_enableLogging, int a_logLevel)
	{
		std::vector<spdlog::sink_ptr> sinks;
		sinks.reserve(2);

		auto level = spdlog::level::off;

		if (a_enableLogging) {
			if (auto logDir = logger::log_directory()) {
				*logDir /= std::format("{}.log", Plugin::NAME);
				sinks.push_back(
					std::make_shared<spdlog::sinks::basic_file_sink_mt>(logDir->string(), true));
			} else {
				// No log directory: stay silent rather than fail to load.
				sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
			}

#ifndef NDEBUG
			sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

			level = ToLevel(a_logLevel);
		} else {
			sinks.push_back(std::make_shared<spdlog::sinks::null_sink_mt>());
		}

		auto log = std::make_shared<spdlog::logger>("global log", sinks.begin(), sinks.end());
		log->set_level(level);
		log->flush_on(spdlog::level::warn);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_level(level);
		spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] %v");
	}
}
