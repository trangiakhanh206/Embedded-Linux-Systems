#!/usr/bin/env python3

import csv
import math
from pathlib import Path

BASE = Path("logs/task4")

FILES = {
    "SCHED_OTHER": BASE / "jitter_other_1000.csv",
    "SCHED_FIFO": BASE / "jitter_fifo_1000.csv",
}


def load_jitter(path):
    values = []

    with path.open("r", newline="") as file:
        reader = csv.DictReader(file)

        for row in reader:
            value = float(row["jitter_us"])

            if not math.isfinite(value):
                raise ValueError(f"Invalid jitter value in {path}")

            values.append(value)

    if len(values) < 1000:
        raise ValueError(
            f"{path}: only {len(values)} samples, need >= 1000"
        )

    return values


def percentile(values, percent):
    sorted_values = sorted(values)

    position = (len(sorted_values) - 1) * percent / 100.0

    lower = math.floor(position)
    upper = math.ceil(position)

    if lower == upper:
        return sorted_values[lower]

    fraction = position - lower

    return (
        sorted_values[lower] * (1 - fraction)
        + sorted_values[upper] * fraction
    )


def summarize(values):
    return {
        "count": len(values),
        "min": min(values),
        "avg": sum(values) / len(values),
        "max": max(values),
        "p99": percentile(values, 99),
    }


def histogram(values):
    bins = [
        (0, 25),
        (25, 50),
        (50, 100),
        (100, 250),
        (250, 500),
        (500, 1000),
    ]

    lines = []

    for low, high in bins:
        count = sum(low <= value < high for value in values)
        percentage = 100 * count / len(values)

        lines.append(
            f"{low:4d}-{high:<4d} us : "
            f"{count:4d} ({percentage:6.2f}%)"
        )

    negative = sum(value < 0 for value in values)
    over = sum(value >= 1000 for value in values)

    lines.append(f"Negative jitter : {negative}")
    lines.append(f">= 1000 us      : {over}")

    return "\n".join(lines)


def main():
    data = {
        name: load_jitter(path)
        for name, path in FILES.items()
    }

    stats = {
        name: summarize(values)
        for name, values in data.items()
    }

    other = stats["SCHED_OTHER"]
    fifo = stats["SCHED_FIFO"]

    report = []

    report.append("ASSIGNMENT 02 - TASK 4")
    report.append("REAL-TIME JITTER ANALYSIS")
    report.append("=" * 65)

    report.append(
        f"{'Metric':<20}"
        f"{'SCHED_OTHER':>18}"
        f"{'SCHED_FIFO':>18}"
    )

    for key in ["count", "min", "avg", "max", "p99"]:
        report.append(
            f"{key:<20}"
            f"{other[key]:>18.3f}"
            f"{fifo[key]:>18.3f}"
        )

    report.append("")
    report.append("Units: microseconds (us)")
    report.append("P99 method: linear interpolation (N-1 method)")
    report.append("")

    if other["p99"] > 0:
        improvement = (
            (other["p99"] - fifo["p99"])
            / other["p99"]
            * 100.0
        )

        report.append(
            f"P99 improvement (FIFO vs OTHER): "
            f"{improvement:.2f}%"
        )

        if improvement > 0:
            report.append("Result: FIFO has lower P99 jitter.")
        elif improvement < 0:
            report.append("Result: FIFO has higher P99 jitter.")
        else:
            report.append("Result: Equal P99 jitter.")

    for name, values in data.items():
        report.append("")
        report.append(f"HISTOGRAM - {name}")
        report.append("-" * 45)
        report.append(histogram(values))

    text = "\n".join(report) + "\n"

    print(text)

    output = BASE / "jitter_comparison.txt"
    output.write_text(text)

    print(f"Report saved: {output}")


if __name__ == "__main__":
    main()
