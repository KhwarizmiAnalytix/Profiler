/*
 * XSigma: High-Performance Computational Library
 *
 * SPDX-License-Identifier: GPL-3.0-or-later OR Commercial
 *
 * This file is part of XSigma and is licensed under a dual-license model:
 *
 *   - Open-source License (GPLv3):
 *       Free for personal, academic, and research use under the terms of
 *       the GNU General Public License v3.0 or later.
 *
 *   - Commercial License:
 *       A commercial license is required for proprietary, closed-source,
 *       or SaaS usage. Contact us to obtain a commercial agreement.
 *
 * Contact: licensing@xsigma.co.uk
 * Website: https://www.xsigma.co.uk
 */

#include "native/exporters/chrome_trace_exporter.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

////#include "logger/logger.h"
#include "native/exporters/xplane/xplane.h"

namespace profiler::profiler_impl
{

namespace
{

/**
 * @brief Escape a string for JSON output.
 *
 * Escapes special characters: ", \, /, \b, \f, \n, \r, \t, plus any other
 * control byte (< 0x20) as \uXXXX so the output is always valid JSON even
 * for names containing stray control characters.
 */
std::string escape_json_string(std::string_view str)
{
    std::string result;
    result.reserve(str.size());

    for (char const c : str)
    {
        switch (c)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '/':
            result += "\\/";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
                result += buffer;
            }
            else
            {
                result += c;
            }
            break;
        }
    }

    return result;
}

/**
 * @brief Convert xstat value to JSON string.
 */
std::string xstat_value_to_json(const xstat& stat)
{
    switch (stat.value_case())
    {
    case xstat::value_case_type::kInt64Value:
        return std::to_string(stat.int64_value());
    case xstat::value_case_type::kUint64Value:
        return std::to_string(stat.uint64_value());
    case xstat::value_case_type::kDoubleValue:
        return std::to_string(stat.double_value());
    case xstat::value_case_type::kStrValue:
        return "\"" + escape_json_string(stat.str_value()) + "\"";
    case xstat::value_case_type::kRefValue:
        return std::to_string(stat.ref_value());
    default:
        return "null";
    }
}

}  // namespace

std::string export_to_chrome_trace_json(const x_space& space, bool pretty_print)
{
    std::ostringstream json;
    std::string const  indent  = pretty_print ? "  " : "";
    std::string const  newline = pretty_print ? "\n" : "";

    json << "{" << newline;
    json << indent << "\"traceEvents\": [" << newline;

    bool first_event = true;

    // Iterate through all planes
    const auto& planes = space.planes();
    for (size_t plane_idx = 0; plane_idx < planes.size(); ++plane_idx)
    {
        const auto&   plane = planes[plane_idx];
        int64_t const pid   = plane.id() > 0 ? plane.id() : static_cast<int64_t>(plane_idx + 1);

        // Add process name metadata event
        if (!first_event)
        {
            json << "," << newline;
        }
        first_event = false;

        json << indent << indent << "{";
        json << R"("name":"process_name",)";
        json << R"("ph":"M",)";
        json << "\"pid\":" << pid << ",";
        json << R"("args":{"name":")" << escape_json_string(plane.name()) << "\"}";
        json << "}";

        // Iterate through all lines (threads) in the plane
        for (size_t line_idx = 0; line_idx < plane.lines_size(); ++line_idx)
        {
            const auto&   line = plane.lines(line_idx);
            int64_t const tid  = line.id() > 0 ? line.id() : static_cast<int64_t>(line_idx + 1);

            // Add thread name metadata event
            json << "," << newline;
            json << indent << indent << "{";
            json << R"("name":"thread_name",)";
            json << R"("ph":"M",)";
            json << "\"pid\":" << pid << ",";
            json << "\"tid\":" << tid << ",";
            json << R"("args":{"name":")" << escape_json_string(line.name()) << "\"}";
            json << "}";

            // Get event metadata map for name lookup
            const auto& event_metadata_map = plane.event_metadata();
            const auto& stat_metadata_map  = plane.stat_metadata();

            // Iterate through all events in the line
            for (const auto& event : line.events())
            {
                json << "," << newline;
                json << indent << indent << "{";

                // Get event name from metadata
                std::string event_name = "unknown";
                if (auto it = event_metadata_map.find(event.metadata_id());
                    it != event_metadata_map.end())
                {
                    event_name = it->second.name();
                }

                json << R"("name":")" << escape_json_string(event_name) << "\",";
                json << R"("ph":"X",)";  // Duration event (complete)
                json << "\"pid\":" << pid << ",";
                json << "\"tid\":" << tid << ",";

                // Calculate timestamp in nanoseconds (displayTimeUnit: ns)
                // XPlane stores: timestamp_ns (line base) + offset_ps (event offset)
                const auto timestamp_ns = static_cast<double>(line.timestamp_ns()) +
                                          (static_cast<double>(event.offset_ps()) / 1000.0);
                json << "\"ts\":" << timestamp_ns << ",";

                // Duration in nanoseconds
                const auto duration_ns = static_cast<double>(event.duration_ps()) / 1000.0;
                json << "\"dur\":" << duration_ns;

                // Add event stats as args
                if (!event.stats().empty())
                {
                    json << ",\"args\":{";
                    bool first_arg = true;
                    for (const auto& stat : event.stats())
                    {
                        if (!first_arg)
                        {
                            json << ",";
                        }
                        first_arg = false;

                        // Get stat name from metadata
                        std::string stat_name = "stat_" + std::to_string(stat.metadata_id());
                        if (auto it = stat_metadata_map.find(stat.metadata_id());
                            it != stat_metadata_map.end())
                        {
                            stat_name = it->second.name();
                        }

                        json << "\"" << escape_json_string(stat_name)
                             << "\":" << xstat_value_to_json(stat);
                    }
                    json << "}";
                }

                json << "}";
            }
        }
    }

    json << newline << indent << "]," << newline;
    json << indent << R"("displayTimeUnit": "ns")" << newline;
    json << "}" << newline;

    return json.str();
}

bool export_to_chrome_trace_json_file(
    const x_space& space, const std::string& filename, bool pretty_print)
{
    try
    {
        std::string const json = export_to_chrome_trace_json(space, pretty_print);

        std::ofstream file(filename);
        if (!file.is_open())
        {
            PROFILER_LOG_ERROR("Failed to open file for writing: {}", filename);
            return false;
        }

        file << json;
        bool const write_ok = file.good();
        file.close();
        if (!write_ok || !file.good())
        {
            PROFILER_LOG_ERROR("Failed to write Chrome Trace JSON to: {}", filename);
            return false;
        }

        //PROFILER_LOG_INFO("Exported Chrome Trace JSON to: {}", filename);
        return true;
    }
    catch (const std::exception& e)
    {
        PROFILER_LOG_ERROR("Failed to export Chrome Trace JSON: {}", e.what());
        return false;
    }
}

}  // namespace profiler::profiler_impl
