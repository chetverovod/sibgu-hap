#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Parse ns-3 statistics files and build a PDF report with plots.
"""

from __future__ import annotations

import argparse
import datetime as dt
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Optional, Tuple


FILE_NAME_RE = re.compile(
    r"^stat-(global|per-gw|per-ut)-([^-]+)-(.+)-([^-]+)-(scatter|scalar)-(-?\d+)\.txt$"
)


@dataclass
class StatFile:
    path: Path
    scope: str
    direction: str
    measurement: str
    metric: str
    fmt: str
    entity_id: int
    metadata: Dict[str, str]
    rows: List[Tuple[float, float]]


@dataclass
class DevicesTable:
    path: Path
    metadata: Dict[str, str]
    header: List[str]
    rows: List[List[str]]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Parse simulation statistics (*.txt) and generate a PDF report with graphs."
        ),
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "results_dir",
        type=Path,
        help="Path to directory with simulation result files",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Output PDF path; by default <results_dir>/results_report.pdf",
    )
    return parser.parse_args()


def parse_file_name(path: Path) -> Optional[Tuple[str, str, str, str, str, int]]:
    m = FILE_NAME_RE.match(path.name)
    if not m:
        return None
    scope, direction, measurement, metric, fmt, entity_id = m.groups()
    return scope, direction, measurement, metric, fmt, int(entity_id)


def parse_metadata_line(line: str) -> Optional[Tuple[str, str]]:
    payload = line[1:].strip()  # remove leading '%'
    if ":" not in payload:
        return None
    key, value = payload.split(":", 1)
    return key.strip(), value.strip().strip("'\"")


def parse_rows(path: Path) -> Tuple[Dict[str, str], List[Tuple[float, float]]]:
    metadata: Dict[str, str] = {}
    rows: List[Tuple[float, float]] = []

    with path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            if line.startswith("%"):
                parsed = parse_metadata_line(line)
                if parsed:
                    key, value = parsed
                    metadata[key] = value
                continue

            parts = line.split()
            if len(parts) < 2:
                continue

            try:
                x_val = float(parts[0])
                y_val = float(parts[1])
            except ValueError:
                continue
            rows.append((x_val, y_val))

    return metadata, rows


def collect_stat_files(results_dir: Path) -> List[StatFile]:
    files: List[StatFile] = []
    for path in sorted(results_dir.glob("stat-*.txt")):
        parsed_name = parse_file_name(path)
        if parsed_name is None:
            continue

        metadata, rows = parse_rows(path)
        if not rows:
            # User requirement: empty files must not be included in report.
            continue

        scope, direction, measurement, metric, fmt, entity_id = parsed_name
        files.append(
            StatFile(
                path=path,
                scope=scope,
                direction=direction,
                measurement=measurement,
                metric=metric,
                fmt=fmt,
                entity_id=entity_id,
                metadata=metadata,
                rows=rows,
            )
        )
    return files


def find_xml_files(results_dir: Path) -> List[Path]:
    return sorted(results_dir.glob("*.xml"))


def parse_devices_table(results_dir: Path) -> Optional[DevicesTable]:
    path = results_dir / "DevicesTable.txt"
    if not path.exists() or not path.is_file():
        return None

    metadata: Dict[str, str] = {}
    header: List[str] = []
    rows: List[List[str]] = []

    with path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            if line.startswith("%"):
                parsed = parse_metadata_line(line)
                if parsed:
                    key, value = parsed
                    metadata[key] = value
                else:
                    # Header line like: % node_id role dev_id device_type ip_address
                    payload = line[1:].strip()
                    if payload and ":" not in payload:
                        header = payload.split()
                continue

            parts = line.split()
            if parts:
                rows.append(parts)

    if not rows:
        return None

    if not header:
        header = ["node_id", "role", "dev_id", "device_type", "ip_address"]

    return DevicesTable(path=path, metadata=metadata, header=header, rows=rows)


def render_report(
    output_pdf: Path,
    results_dir: Path,
    stat_files: List[StatFile],
    xml_files: List[Path],
    devices_table: Optional[DevicesTable],
) -> None:
    try:
        import matplotlib.pyplot as plt
        from matplotlib.backends.backend_pdf import PdfPages
    except ImportError as exc:
        raise SystemExit(
            "matplotlib is required. Install with: pip install matplotlib"
        ) from exc

    with PdfPages(output_pdf) as pdf:
        page_number = 0

        def save_page(fig) -> None:
            nonlocal page_number
            page_number += 1
            fig.text(
                0.985,
                0.012,
                f"Page {page_number}",
                ha="right",
                va="bottom",
                fontsize=9,
                color="dimgray",
            )
            pdf.savefig(fig)
            plt.close(fig)

        # Cover page
        fig = plt.figure(figsize=(11.69, 8.27))  # A4 landscape
        fig.suptitle("Simulation Results Report", fontsize=18, fontweight="bold", y=0.95)
        now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        xml_text = ", ".join(p.name for p in xml_files) if xml_files else "Not found"
        fig.text(0.05, 0.82, f"Generated at: {now}", fontsize=11)
        fig.text(0.05, 0.77, f"Results directory: {results_dir}", fontsize=11)
        fig.text(0.05, 0.72, f"Statistics files included: {len(stat_files)}", fontsize=11)
        fig.text(0.05, 0.67, f"XML attributes file(s): {xml_text}", fontsize=11)
        fig.text(
            0.05,
            0.62,
            f"Devices table file: {devices_table.path.name if devices_table else 'Not found'}",
            fontsize=11,
        )
        fig.text(
            0.05,
            0.58,
            "Each following page contains one graph and the source filename.",
            fontsize=10,
        )
        fig.text(
            0.05,
            0.54,
            "Files without data rows are skipped automatically.",
            fontsize=10,
        )
        fig.subplots_adjust(left=0, right=1, top=1, bottom=0)
        save_page(fig)

        if devices_table is not None:
            rows_per_page = 28
            for chunk_start in range(0, len(devices_table.rows), rows_per_page):
                chunk = devices_table.rows[chunk_start:chunk_start + rows_per_page]
                fig, ax = plt.subplots(figsize=(11.69, 8.27))
                ax.axis("off")
                title_suffix = ""
                if len(devices_table.rows) > rows_per_page:
                    page_idx = (chunk_start // rows_per_page) + 1
                    page_cnt = (len(devices_table.rows) + rows_per_page - 1) // rows_per_page
                    title_suffix = f" (page {page_idx}/{page_cnt})"
                ax.set_title(
                    f"Devices IP Table from {devices_table.path.name}{title_suffix}",
                    fontsize=13,
                    pad=12,
                )

                table = ax.table(
                    cellText=chunk,
                    colLabels=devices_table.header,
                    bbox=[0.0, 0.08, 1.0, 0.78],
                    cellLoc="left",
                )
                table.auto_set_font_size(False)
                table.set_fontsize(8)
                table.scale(1, 1.4)
                for col_idx in range(len(devices_table.header)):
                    table[(0, col_idx)].set_text_props(weight="bold")

                meta_lines = [f"Source: {devices_table.path.name}"]
                if devices_table.metadata:
                    meta_lines.extend(
                        [f"{k}: {v}" for k, v in sorted(devices_table.metadata.items())]
                    )
                fig.text(
                    0.01,
                    0.01,
                    "\n".join(meta_lines),
                    fontsize=8.5,
                    va="bottom",
                    family="monospace",
                )
                fig.tight_layout(rect=(0, 0.06, 1, 0.96))
                save_page(fig)

        for stat in stat_files:
            x_vals = [x for x, _ in stat.rows]
            y_vals = [y for _, y in stat.rows]

            fig, ax = plt.subplots(figsize=(11.69, 8.27))
            ax.plot(x_vals, y_vals, marker="o", markersize=2.5, linewidth=1.1)
            ax.grid(True, alpha=0.3)
            ax.set_xlabel("Time (s)")
            ax.set_ylabel(stat.metric)
            ax.set_title(
                f"{stat.scope} | {stat.direction} | {stat.measurement} | "
                f"{stat.metric} | {stat.fmt} | id={stat.entity_id}"
            )

            meta_lines = [f"Source: {stat.path.name}"]
            if stat.metadata:
                meta_lines.extend([f"{k}: {v}" for k, v in sorted(stat.metadata.items())])

            fig.text(
                0.01,
                0.01,
                "\n".join(meta_lines),
                fontsize=9,
                va="bottom",
                family="monospace",
            )
            fig.tight_layout(rect=(0, 0.08, 1, 0.96))
            save_page(fig)


def main() -> None:
    args = parse_args()
    results_dir = args.results_dir.resolve()

    if not results_dir.exists() or not results_dir.is_dir():
        raise SystemExit(f"Directory does not exist: {results_dir}")

    output_pdf = args.output.resolve() if args.output else results_dir / "results_report.pdf"

    stat_files = collect_stat_files(results_dir)
    xml_files = find_xml_files(results_dir)
    devices_table = parse_devices_table(results_dir)

    if not stat_files and devices_table is None:
        raise SystemExit(
            "No non-empty stat-*.txt files found and DevicesTable.txt is missing/empty."
        )

    render_report(output_pdf, results_dir, stat_files, xml_files, devices_table)
    print(f"Report generated: {output_pdf}")


if __name__ == "__main__":
    main()
