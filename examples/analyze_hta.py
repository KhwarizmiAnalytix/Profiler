"""Analyze Profiler's Kineto exports with HolisticTraceAnalysis (optional dependency)."""

import argparse
import gzip
import json
from pathlib import Path


def rank_files(trace_dir):
    """Map one Kineto trace per rank, with errors before HTA starts parsing."""
    files = sorted(trace_dir.glob("*.json")) + sorted(trace_dir.glob("*.json.gz"))
    if not files:
        raise ValueError(f"No .json or .json.gz traces in {trace_dir}")
    ranks = {}
    for path in files:
        opener = gzip.open if path.suffix == ".gz" else open
        with opener(path, "rt", encoding="utf-8") as source:
            trace = json.load(source)
        if not isinstance(trace, dict) or "schemaVersion" not in trace or not any(
            event.get("cat") in ("cpu_op", "user_annotation", "kernel", "cuda_runtime")
            for event in trace.get("traceEvents", [])
        ):
            raise ValueError(f"{path}: expected a Kineto trace from ProfilerResult::save()")
        rank = trace.get("distributedInfo", {}).get("rank", 0)
        if type(rank) is not int or rank < 0:
            raise ValueError(f"{path}: distributedInfo.rank must be a nonnegative integer")
        if rank in ranks:
            raise ValueError(f"Duplicate rank {rank}; keep one capture per rank in each directory")
        ranks[rank] = path.name
    return ranks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace_dir", type=Path)
    parser.add_argument("--output", type=Path, default=Path("hta-results"))
    parser.add_argument("--gpu", action="store_true", help="Also export CUDA GPU analyses")
    args = parser.parse_args()
    try:
        files = rank_files(args.trace_dir)
    except (ValueError, OSError) as error:
        parser.error(str(error))

    from hta.trace_analysis import TraceAnalysis

    analyzer = TraceAnalysis(
        trace_dir=str(args.trace_dir),
        trace_files=files,
        include_last_profiler_step=True,
    )
    analyzer.t.decode_symbol_ids(use_shorten_name=False)
    ranks = sorted(analyzer.t.get_ranks())
    if args.gpu and any(
        not (analyzer.t.get_trace(rank)["stream"] >= 0).any() for rank in ranks
    ):
        parser.error(
            "GPU analysis needs CUDA device activities on every rank; "
            "this capture is CPU-only or incomplete"
        )

    args.output.mkdir(parents=True, exist_ok=True)
    print(f"Loaded ranks: {ranks}")
    print(f"Profiler steps: {[int(step) for step in analyzer.get_profiler_steps()]}")
    for rank in ranks:
        events = analyzer.t.get_trace(rank)
        cpu = events[events["s_cat"].isin(["cpu_op", "user_annotation"])]
        summary = (
            cpu.groupby("s_name")["dur"]
            .agg(calls="count", total_us="sum", mean_us="mean")
            .sort_values("total_us", ascending=False)
            .rename_axis("name")
        )
        summary.to_csv(args.output / f"rank{rank}_cpu.csv")
        print(f"\nRank {rank}: CPU inclusive durations (microseconds)")
        print(summary.to_string(float_format=lambda value: f"{value:.3f}"))

    if args.gpu:
        analyzer.get_temporal_breakdown(visualize=False).to_csv(
            args.output / "temporal_breakdown.csv", index=False
        )
        types, kernels = analyzer.get_gpu_kernel_breakdown(visualize=False)
        types.to_csv(args.output / "kernel_types.csv", index=False)
        kernels.to_csv(args.output / "kernels.csv", index=False)
        for rank, frame in analyzer.get_cuda_kernel_launch_stats(
            ranks=ranks, visualize=False
        ).items():
            frame.to_csv(args.output / f"rank{rank}_launch_stats.csv", index=False)
    else:
        print("\nGPU analyses not requested. Use --gpu with a CUDA/CUPTI capture.")
    print(f"Wrote CSV files to {args.output}")


if __name__ == "__main__":
    main()
