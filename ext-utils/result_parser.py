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


@dataclass
class PacketTraceTable:
    path: Path
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


def parse_packet_trace(results_dir: Path) -> Optional[PacketTraceTable]:
    path = results_dir / "PacketTrace.log"
    if not path.exists() or not path.is_file():
        return None

    levels = ("ND", "LLC", "MAC", "PHY", "CH")
    level_set = set(levels)
    device_stats: Dict[Tuple[str, str, str], Dict[str, int]] = {}

    with path.open("r", encoding="utf-8") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            parts = line.split()
            if len(parts) < 7:
                continue

            # Data lines start with:
            # time event node_type node_id mac log_level link_dir ...
            try:
                float(parts[0])
            except ValueError:
                continue

            event = parts[1]
            node_type = parts[2]
            node_id = parts[3]
            mac_addr = parts[4]
            log_level = parts[5]

            if log_level not in level_set:
                continue

            key = (node_type, node_id, mac_addr)
            if key not in device_stats:
                row = {}
                for lvl in levels:
                    row[f"{lvl}_TX"] = 0
                    row[f"{lvl}_RX"] = 0
                device_stats[key] = row

            if event == "SND":
                device_stats[key][f"{log_level}_TX"] += 1
            elif event == "RCV":
                device_stats[key][f"{log_level}_RX"] += 1

    if not device_stats:
        return None

    header = [
        "node_type",
        "node_id",
        "mac",
        "ND_TX",
        "ND_RX",
        "LLC_TX",
        "LLC_RX",
        "MAC_TX",
        "MAC_RX",
        "PHY_TX",
        "PHY_RX",
        "CH_TX",
        "CH_RX",
    ]

    active_rows: List[List[str]] = []
    inactive_count = 0
    for (node_type, node_id, mac_addr), counters in sorted(device_stats.items()):
        activity = sum(counters.values())
        if activity == 0:
            inactive_count += 1
            continue

        active_rows.append(
            [
                node_type,
                node_id,
                mac_addr,
                str(counters["ND_TX"]),
                str(counters["ND_RX"]),
                str(counters["LLC_TX"]),
                str(counters["LLC_RX"]),
                str(counters["MAC_TX"]),
                str(counters["MAC_RX"]),
                str(counters["PHY_TX"]),
                str(counters["PHY_RX"]),
                str(counters["CH_TX"]),
                str(counters["CH_RX"]),
            ]
        )

    if inactive_count > 0:
        active_rows.append(
            [
                "INACTIVE_GROUP",
                "-",
                f"{inactive_count} devices",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
            ]
        )

    if not active_rows:
        # All devices are inactive: still show one aggregate line as requested.
        active_rows.append(
            [
                "INACTIVE_GROUP",
                "-",
                f"{len(device_stats)} devices",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
                "0",
            ]
        )

    return PacketTraceTable(path=path, header=header, rows=active_rows)


def render_report(
    output_pdf: Path,
    results_dir: Path,
    stat_files: List[StatFile],
    xml_files: List[Path],
    devices_table: Optional[DevicesTable],
    packet_trace_table: Optional[PacketTraceTable],
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

        def estimate_lines(text: str, chars_per_line: int) -> int:
            if chars_per_line <= 0:
                return 1
            chunks = text.splitlines() or [""]
            total = 0
            for chunk in chunks:
                total += max(1, (len(chunk) + chars_per_line - 1) // chars_per_line)
            return max(1, total)

        def draw_table_page(
            fig,
            ax,
            title: str,
            header: List[str],
            rows: List[List[str]],
            source_text: str,
            body_fontsize: float,
            header_fontsize: float,
            chars_per_col: List[int],
        ) -> None:
            ax.axis("off")
            ax.set_title(title, fontsize=13, pad=12)

            header_h = 0.038
            base_h = 0.030
            row_heights: List[float] = []
            for row in rows:
                max_lines = 1
                for idx, value in enumerate(row):
                    limit = chars_per_col[idx] if idx < len(chars_per_col) else 14
                    max_lines = max(max_lines, estimate_lines(str(value), limit))
                row_heights.append(base_h * max_lines)

            table_height = header_h + sum(row_heights)
            max_table_height = 0.78
            if table_height > max_table_height:
                # Keep row heights proportional if content is large.
                scale = max_table_height / table_height
                header_h *= scale
                row_heights = [h * scale for h in row_heights]
                table_height = max_table_height

            table_y = 0.86 - table_height
            table = ax.table(
                cellText=rows,
                colLabels=header,
                bbox=[0.0, table_y, 1.0, table_height],
                cellLoc="left",
            )
            table.auto_set_font_size(False)
            table.set_fontsize(body_fontsize)

            n_cols = len(header)
            for col_idx in range(n_cols):
                cell = table[(0, col_idx)]
                cell.set_text_props(weight="bold", fontsize=header_fontsize)
                cell.set_height(header_h)

            for row_idx in range(1, len(rows) + 1):
                row_h = row_heights[row_idx - 1]
                for col_idx in range(n_cols):
                    table[(row_idx, col_idx)].set_height(row_h)

            fig.text(
                0.01,
                0.01,
                source_text,
                fontsize=8.5,
                va="bottom",
                family="monospace",
            )
            fig.tight_layout(rect=(0, 0.06, 1, 0.96))
            save_page(fig)

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
            f"Packet trace file: {packet_trace_table.path.name if packet_trace_table else 'Not found'}",
            fontsize=11,
        )
        fig.text(
            0.05,
            0.53,
            "Each following page contains one graph and the source filename.",
            fontsize=10,
        )
        fig.text(
            0.05,
            0.49,
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
                title_suffix = ""
                if len(devices_table.rows) > rows_per_page:
                    page_idx = (chunk_start // rows_per_page) + 1
                    page_cnt = (len(devices_table.rows) + rows_per_page - 1) // rows_per_page
                    title_suffix = f" (page {page_idx}/{page_cnt})"
                chunk_with_idx = [
                    [f"{chunk_start + i + 1}."] + row
                    for i, row in enumerate(chunk)
                ]
                header = ["No."] + devices_table.header

                meta_lines = [f"Source: {devices_table.path.name}"]
                if devices_table.metadata:
                    meta_lines.extend(
                        [f"{k}: {v}" for k, v in sorted(devices_table.metadata.items())]
                    )
                draw_table_page(
                    fig=fig,
                    ax=ax,
                    title=f"Devices IP Table from {devices_table.path.name}{title_suffix}",
                    header=header,
                    rows=chunk_with_idx,
                    source_text="\n".join(meta_lines),
                    body_fontsize=8.0,
                    header_fontsize=8.3,
                    chars_per_col=[5, 8, 6, 7, 24, 20],
                )

        if packet_trace_table is not None:
            rows_per_page = 20
            for chunk_start in range(0, len(packet_trace_table.rows), rows_per_page):
                chunk = packet_trace_table.rows[chunk_start:chunk_start + rows_per_page]
                fig, ax = plt.subplots(figsize=(11.69, 8.27))
                title_suffix = ""
                if len(packet_trace_table.rows) > rows_per_page:
                    page_idx = (chunk_start // rows_per_page) + 1
                    page_cnt = (len(packet_trace_table.rows) + rows_per_page - 1) // rows_per_page
                    title_suffix = f" (page {page_idx}/{page_cnt})"
                chunk_with_idx = [
                    [f"{chunk_start + i + 1}."] + row
                    for i, row in enumerate(chunk)
                ]
                header = ["No."] + packet_trace_table.header
                draw_table_page(
                    fig=fig,
                    ax=ax,
                    title=f"PacketTrace Device Counters from {packet_trace_table.path.name}{title_suffix}",
                    header=header,
                    rows=chunk_with_idx,
                    source_text=f"Source: {packet_trace_table.path.name}",
                    body_fontsize=7.0,
                    header_fontsize=7.3,
                    chars_per_col=[5, 10, 8, 17, 6, 6, 7, 7, 7, 7, 7, 7, 6, 6],
                )

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
    packet_trace_table = parse_packet_trace(results_dir)

    if not stat_files and devices_table is None and packet_trace_table is None:
        raise SystemExit(
            "No non-empty stat-*.txt files found, DevicesTable.txt missing/empty, and "
            "PacketTrace.log missing/empty."
        )

    render_report(
        output_pdf,
        results_dir,
        stat_files,
        xml_files,
        devices_table,
        packet_trace_table,
    )
    print(f"Report generated: {output_pdf}")


if __name__ == "__main__":
    main()
