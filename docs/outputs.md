# Output examples and field reference

The examples below come from actual executions of this repository's examples on
macOS, using a Release Kineto CPU build. HTA output was generated with
HolisticTraceAnalysis 0.5.0. Timings, ordering, process/thread IDs, and memory
figures vary by workload and machine; these numbers are not benchmark claims.
The native and Kineto examples were separate captures.

[User guide](profiler.md) · [HTA guide](hta.md) · [Downloadable samples](samples/README.md)

## Reproduce the files

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPROFILER_ENABLE_EXAMPLES=ON
cmake --build build --config Release --parallel
./build/bin/example_reports build/reports
./build/bin/example_hta build/hta-traces
python examples/analyze_hta.py build/hta-traces --output build/hta-results
```

Install the optional [HTA environment](hta.md#generate-and-analyze-a-trace) before
the final command. Visual Studio binaries are in `build/bin/Release/` with `.exe`.

| File | Writer | Contents / consumer |
| --- | --- | --- |
| `native_trace.json` | `session.write_chrome_trace()` | Native timeline; Perfetto / Chrome Trace viewer |
| `report.txt` | `report->export_console_report()` | Human-readable session sections |
| `report.json` | `report->export_json_report()` | Structured summary and nested scopes |
| `report.csv` | `report->export_csv_report()` | Scope rows for spreadsheets/dataframes |
| `report.xml` | `report->export_xml_report()` | XML document containing formatted report sections |
| `hotspots.txt` | `session.generate_hotspot_report()->table()` | Aggregated self/inclusive CPU time |
| `rank0.json` | `ProfilerResult::save()` | Kineto activities for HTA or Perfetto |
| `rank0_cpu.csv` | `examples/analyze_hta.py` | HTA/Pandas aggregation by CPU event name |

## Console report

Actual header excerpt from [report.txt](samples/native/report.txt):

```text
=== Profiler Profiler Report ===
Session active: no
Duration: 0.172 ms
Total scopes: 10
Max depth: 3
```

`scope_count` includes the synthetic `ROOT` hierarchy node. The example records
nine actual scopes: one `workload`, four `pass` scopes, and four `compute` scopes.
The synthetic root can show zero duration; inspect the session duration or the
`workload` scope for measured elapsed time.

The full report includes timing, memory, hierarchy, statistics, hotspot, and
thread sections when the corresponding data/options are available. Memory
values cover the tracker, not every process allocation.

## Hotspot table

Actual [hotspots.txt](samples/native/hotspots.txt):

```text
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
Name                    Self CPU %      Self CPU   CPU total %     CPU total  CPU time avg    # of Calls
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
compute                     88.83%     151.832us        88.83%     151.832us      37.958us             4
pass                         8.90%      15.209us        97.73%     167.041us      41.760us             4
workload                     2.27%       3.876us       100.00%     170.917us     170.917us             1
--------------------  ------------  ------------  ------------  ------------  ------------  ------------
Self CPU time total: 170.917us
```

| Column | Meaning |
| --- | --- |
| `Name` | Scope name; repeated names are aggregated |
| `Self CPU %` | Share of aggregate self CPU duration |
| `Self CPU` | Scope duration excluding nested children |
| `CPU total %` | Inclusive duration relative to aggregate self CPU duration |
| `CPU total` | Inclusive duration, including descendants |
| `CPU time avg` | Inclusive duration divided by call count |
| `# of Calls` | Recorded occurrences |

`compute` dominates self time in this capture. `workload` includes all passes,
so its inclusive duration is larger than its self duration. Inclusive percentages
across rows need not sum to 100%. They are not CPU utilization percentages.

## Native timeline JSON

One actual duration event, extracted from
[native_trace.json](samples/native/native_trace.json):

```json
{
  "traceEvents": [
    {
      "name": "compute",
      "ph": "X",
      "pid": 1,
      "tid": 189992,
      "ts": 4.375,
      "dur": 39.667
    }
  ],
  "displayTimeUnit": "ns"
}
```

`ph: "X"` identifies a complete interval. `pid` selects a plane/process track;
`tid` selects its thread. `ts` and `dur` are in **microseconds**, including
fractional microseconds. `displayTimeUnit` is only a viewer presentation hint.
Other events with `ph: "M"` name the process and thread tracks. See
[Perfetto's Chrome JSON description](https://perfetto.dev/docs/getting-started/other-formats#chrome-json-format).

Earlier Profiler revisions wrote native `ts`/`dur` as nanoseconds, making
standard viewers show durations 1,000 times too large. Regenerate those traces
with the corrected exporter before comparing absolute durations.

To view the file:

1. Open [Perfetto](https://ui.perfetto.dev/) and choose **Open trace file**.
2. Select `native_trace.json` or a Kineto `rank0.json` file.
3. Expand the process/thread tracks and locate `workload`, `pass`, or `compute`.
4. Select a slice to inspect its duration; zoom into nested scopes to compare
   where elapsed time is spent.

The annotated nesting in the native example is:

```text
workload
  pass
    compute
  pass
    compute
  pass
    compute
  pass
    compute
```

## Session JSON report

Reduced excerpt from [report.json](samples/native/report.json), keeping the
header and the first leaf scope (the complete file preserves its parent chain):

```json
{
  "header": {
    "active": false,
    "scope_count": 10,
    "max_depth": 3,
    "duration_ms": 0.172
  },
  "scopes": [
    {
      "name": "compute",
      "duration_ms": 0.039,
      "thread": "thread 189992",
      "memory": {
        "delta_mean_bytes": 0.0,
        "delta_max_bytes": 0.0
      }
    }
  ]
}
```

The full schema includes `header`, `scopes`, `top_durations`, `memory`, and
`threads`. `duration_ms` is in milliseconds; memory fields ending in `_bytes`
are bytes. `null` means a statistic is unavailable. This schema is for reports;
it has no `traceEvents` array and is not an HTA input.

## CSV report

First rows of [report.csv](samples/native/report.csv):

```csv
Scope,Depth,Thread,Duration(ms),Memory Delta Mean,Memory Delta Max
ROOT,0,n/a,0.000,n/a,n/a
  workload,1,thread 189992,0.170,0.000 MB,0.000 MB
    pass,2,thread 189992,0.053,0.000 MB,0.000 MB
      compute,3,thread 189992,0.039,0.000 MB,0.000 MB
    pass,2,thread 189992,0.038,0.000 MB,0.000 MB
```

Each row is one scope occurrence, not an aggregate. `Depth` and indentation
encode the reconstructed hierarchy. `Duration(ms)` is inclusive elapsed time.
Memory columns contain formatted values with units, or `n/a`; parse the JSON
report when numeric byte fields are needed. Nested durations overlap.

## XML report

Header excerpt from [report.xml](samples/native/report.xml):

```xml
<?xml version="1.0" encoding="UTF-8"?>
<profiler_report>
  <header>
=== Profiler Profiler Report ===
Session active: no
Duration: 0.172 ms
Total scopes: 10
Max depth: 3

  </header>
  <!-- Additional sections omitted from this excerpt. -->
</profiler_report>
```

XML stores formatted report sections such as `header`, `summary`, and timing
output. It is selected by `export_xml_report()` or the `STRUCTURED` output enum.
Use the JSON report for structured per-scope numeric analysis.

## Kineto trace JSON

An actual CPU event extracted from [rank0.json](samples/hta/rank0.json):

```json
{
  "ph": "X",
  "cat": "cpu_op",
  "name": "compute",
  "pid": 47034,
  "tid": 189994,
  "ts": 6360605645266.826,
  "dur": 46.417,
  "args": {
    "External id": 2,
    "Record function id": 0,
    "Ev Idx": 1
  }
}
```

The complete trace also includes `schemaVersion`, `distributedInfo`, clock
metadata, thread metadata, and four `ProfilerStep#N` annotations.
Kineto `ts` and `dur` are microseconds. This CPU capture has an empty
`deviceProperties` array and no CUDA activities. `External id` belongs to the
operation correlation machinery; it does not itself prove that a GPU kernel
was captured.

## HTA analysis

Actual output from the included Python script (destination path shortened):

```text
Loaded ranks: [0]
Profiler steps: [0, 1, 2, 3]

Rank 0: CPU inclusive durations (microseconds)
                calls  total_us  mean_us
name
compute             4   183.000   45.750
ProfilerStep#0      1    46.000   46.000
ProfilerStep#1      1    46.000   46.000
ProfilerStep#3      1    46.000   46.000
ProfilerStep#2      1    45.000   45.000

GPU analyses not requested. Use --gpu with a CUDA/CUPTI capture.
Wrote CSV files to build/hta-results
```

HTA rounded fractional microseconds while parsing this trace. `total_us` sums
inclusive duration per event name; `mean_us` divides it by `calls`. Step durations
include the `compute` durations, so summing all rows would double count work.

The corresponding [rank0_cpu.csv](samples/hta/rank0_cpu.csv) is ready to load
into a spreadsheet or dataframe. Run the same analysis on the checked-in sample:

```bash
python examples/analyze_hta.py docs/samples/hta --output build/sample-hta-results
```

No GPU output is claimed for these CPU samples. The [HTA GPU workflow](hta.md#gpu-analysis-and-outputs)
explains how to generate temporal breakdowns, kernel tables, and launch statistics
from an actual CUDA/CUPTI capture.
