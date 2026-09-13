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

#pragma once

#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "native/analysis/statistical_analyzer.h"
#include "native/memory/memory_tracker.h"
#include "native/session/profiler.h"

namespace profiler
{

/**
 * @brief Report generation and formatting for enhanced profiler
 *
 * Provides comprehensive report generation capabilities with multiple
 * output formats including console, JSON, CSV, and XML formats.
 */
class PROFILER_VISIBILITY profiler_report
{
public:
    /**
     * @brief Construct a new profiler report
     * @param session Reference to the profiler session to generate report from
     */
    PROFILER_API explicit profiler_report(const profiler::profiler_session& session);

    /**
     * @brief Default destructor
     */
    ~profiler_report() = default;

    /**
     * @brief Generate human-readable console report
     * @return String containing formatted console report
     */
    PROFILER_API std::string generate_console_report() const;

    /**
     * @brief Generate JSON format report
     * @return String containing JSON formatted report
     */
    PROFILER_API std::string generate_json_report() const;

    /**
     * @brief Generate CSV format report
     * @return String containing CSV formatted report
     */
    PROFILER_API std::string generate_csv_report() const;

    /**
     * @brief Generate XML format report
     * @return String containing XML formatted report
     */
    PROFILER_API std::string generate_xml_report() const;

    /**
     * @brief Export report to file in specified format
     * @param filename Path to output file
     * @param format Output format to use
     * @return true if export successful, false otherwise
     */
    PROFILER_API bool export_to_file(
        const std::string& filename, profiler::profiler_options::output_format_enum format) const;

    /**
     * @brief Export console report to file
     * @param filename Path to output file
     * @return true if export successful, false otherwise
     */
    PROFILER_API bool export_console_report(const std::string& filename) const;

    /**
     * @brief Export JSON report to file
     * @param filename Path to output file
     * @return true if export successful, false otherwise
     */
    PROFILER_API bool export_json_report(const std::string& filename) const;

    /**
     * @brief Export CSV report to file
     * @param filename Path to output file
     * @return true if export successful, false otherwise
     */
    PROFILER_API bool export_csv_report(const std::string& filename) const;

    /**
     * @brief Export XML report to file
     * @param filename Path to output file
     * @return true if export successful, false otherwise
     */
    PROFILER_API bool export_xml_report(const std::string& filename) const;

    // Print to console
    PROFILER_API static void print_summary();
    PROFILER_API void        print_detailed_report() const;
    PROFILER_API static void print_memory_report();
    PROFILER_API static void print_timing_report();
    PROFILER_API static void print_statistical_report();

    // Report customization
    void set_precision(int precision) { precision_ = precision; }
    void set_time_unit(const std::string& unit) { time_unit_ = unit; }
    void set_memory_unit(const std::string& unit) { memory_unit_ = unit; }
    void set_include_thread_info(bool include) { include_thread_info_ = include; }
    void set_include_hierarchical_data(bool include) { include_hierarchical_data_ = include; }

private:
    const profiler::profiler_session& session_;

    // Formatting options
    int         precision_                 = 3;
    std::string time_unit_                 = "ms";
    std::string memory_unit_               = "MB";
    bool        include_thread_info_       = true;
    bool        include_hierarchical_data_ = true;

    // Helper methods for report generation
    std::string        format_duration(double duration_ns) const;
    std::string        format_memory_size(size_t bytes) const;
    static std::string format_percentage(double value);
    static std::string format_thread_label(const std::string& thread_label);

    std::string format_double(double value) const;
    // Section generators
    std::string generate_header_section() const;
    std::string generate_summary_section() const;
    std::string generate_timing_section() const;
    std::string generate_memory_section() const;
    std::string generate_hierarchical_section() const;
    std::string generate_statistical_section() const;
    std::string generate_hotspot_section() const;
    std::string generate_thread_section() const;

    // JSON helpers
    static std::string escape_json_string(const std::string& str);

    // CSV helpers
    static std::string escape_csv_field(const std::string& field);
    static std::string generate_csv_header();
    static std::string generate_csv_row(const std::vector<std::string>& fields);

    // XML helpers
    static std::string escape_xml_string(const std::string& str);
    static std::string generate_xml_element(const std::string& tag, const std::string& content);
    static std::string generate_xml_attribute(const std::string& name, const std::string& value);

    // Hierarchical data processing
    void process_scope_data_recursive(
        const profiler::profiler_scope_data& scope, std::stringstream& ss, int indent = 0) const;
    void process_scope_data_json_recursive(
        const profiler::profiler_scope_data& scope, std::stringstream& ss, int indent = 0) const;
    void process_scope_data_csv_recursive(
        const profiler::profiler_scope_data& scope,
        std::vector<std::string>&            rows,
        int                                  depth = 0) const;
};

// Report builder with fluent interface
class PROFILER_VISIBILITY profiler_report_builder
{
public:
    PROFILER_API explicit profiler_report_builder(const profiler::profiler_session& session);

    profiler_report_builder& with_precision(int precision)
    {
        precision_ = precision;
        return *this;
    }

    profiler_report_builder& with_time_unit(const std::string& unit)
    {
        time_unit_ = unit;
        return *this;
    }

    profiler_report_builder& with_memory_unit(const std::string& unit)
    {
        memory_unit_ = unit;
        return *this;
    }

    profiler_report_builder& include_thread_info(bool include = true)
    {
        include_thread_info_ = include;
        return *this;
    }

    profiler_report_builder& include_hierarchical_data(bool include = true)
    {
        include_hierarchical_data_ = include;
        return *this;
    }

    profiler_report_builder& include_statistical_analysis(bool include = true)
    {
        include_statistical_analysis_ = include;
        return *this;
    }

    profiler_report_builder& include_memory_details(bool include = true)
    {
        include_memory_details_ = include;
        return *this;
    }

    PROFILER_API std::unique_ptr<profiler::profiler_report> build() const;

private:
    const profiler::profiler_session& session_;
    int                               precision_                    = 3;
    std::string                       time_unit_                    = "ms";
    std::string                       memory_unit_                  = "MB";
    bool                              include_thread_info_          = true;
    bool                              include_hierarchical_data_    = true;
    bool                              include_statistical_analysis_ = true;
    bool                              include_memory_details_       = true;
};

}  // namespace profiler
