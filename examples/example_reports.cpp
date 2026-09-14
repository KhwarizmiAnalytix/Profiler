// Generate every native report format from one capture.
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "profiler.h"

namespace
{
double compute()
{
    PROFILER_FUNCTION();
    double sum = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        sum += std::sin(static_cast<double>(i) * 0.001);
    }
    return sum;
}
}  // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path output = argc > 1 ? argv[1] : "reports";
    std::filesystem::create_directories(output);
    profiler::session session;
    if (!session.start())
    {
        std::cerr << "Could not start the profiling session\n";
        return 1;
    }
    double checksum = 0.0;
    {
        PROFILER_SCOPE("workload");
        for (int i = 0; i < 4; ++i)
        {
            PROFILER_SCOPE("pass");
            checksum += compute();
        }
    }
    if (!session.stop())
    {
        std::cerr << "Could not stop the profiling session\n";
        return 1;
    }

    auto report = session.generate_report();
    if (!session.write_chrome_trace((output / "native_trace.json").string()) ||
        !report->export_console_report((output / "report.txt").string()) ||
        !report->export_json_report((output / "report.json").string()) ||
        !report->export_csv_report((output / "report.csv").string()) ||
        !report->export_xml_report((output / "report.xml").string()))
    {
        std::cerr << "Could not write a report to " << output << '\n';
        return 1;
    }
    auto          hotspots = session.generate_hotspot_report();
    std::ofstream hotspot_file(output / "hotspots.txt");
    hotspot_file << hotspots->table();
    hotspot_file.close();
    if (!hotspot_file)
    {
        std::cerr << "Could not write hotspots.txt\n";
        return 1;
    }
    std::cout << "checksum=" << checksum << '\n' << hotspots->table();
    std::cout << "Wrote native_trace.json, report.txt, report.json, report.csv, "
                 "report.xml and hotspots.txt to "
              << output.string() << '\n';
    return 0;
}
