#pragma once

#pragma warning(push)
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// basic_file_sink and null_sink are both needed in every configuration:
// SetupLog picks between them at runtime from the INI, not at compile time.
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#ifndef NDEBUG
#	include <spdlog/sinks/msvc_sink.h>
#endif

// ClibUtil headers resolve through ClibUtil/include, added in xmake.lua.
// simpleINI.hpp bundles SimpleIni, so there is no separate checkout to manage.
#include <CLIBUtil/editorID.hpp>
#include <CLIBUtil/simpleINI.hpp>
#include <CLIBUtil/string.hpp>
#pragma warning(pop)

using namespace std::literals;

namespace logger = SKSE::log;

namespace util
{
	using SKSE::stl::report_and_fail;
}

#define DLLEXPORT __declspec(dllexport)

#define RELOCATION_OFFSET(SE, AE) REL::VariantOffset(SE, AE, 0).offset()

#include "plugin.h"
