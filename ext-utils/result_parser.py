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

# A4 landscape margins:
# - left: 20 mm
# - top/right/bottom: 5 mm
PAGE_LEFT = 20.0 / 297.0
PAGE_RIGHT = 1.0 - (5.0 / 297.0)
PAGE_TOP = 1.0 - (5.0 / 210.0)
PAGE_BOTTOM = 5.0 / 210.0


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


@dataclass
class SummaryTable:
    header: List[str]
    rows: List[List[str]]


@dataclass
class TocLink:
    from_page: int  # 1-based page number in final PDF
    target_page: int  # 1-based page number in final PDF
    rect_norm: Tuple[float, float, float, float]  # x0, y0, x1, y1 in [0..1]


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


def build_summary_table(stat_files: List[StatFile]) -> Optional[SummaryTable]:
    if not stat_files:
        return None

    header = [
        "No.",
        "source_file",
        "scope",
        "dir",
        "point",
        "metric",
        "fmt",
        "id",
        "samples",
        "min",
        "avg",
        "max",
        "sum",
        "last",
    ]
    rows: List[List[str]] = []

    for i, stat in enumerate(stat_files, start=1):
        y_vals = [y for _, y in stat.rows]
        sample_count = len(y_vals)
        y_min = min(y_vals)
        y_max = max(y_vals)
        y_sum = sum(y_vals)
        y_avg = y_sum / sample_count if sample_count else 0.0
        y_last = y_vals[-1] if sample_count else 0.0

        rows.append(
            [
                f"{i}.",
                stat.path.name,
                stat.scope,
                stat.direction,
                stat.measurement,
                stat.metric,
                stat.fmt,
                str(stat.entity_id),
                str(sample_count),
                f"{y_min:.6g}",
                f"{y_avg:.6g}",
                f"{y_max:.6g}",
                f"{y_sum:.6g}",
                f"{y_last:.6g}",
            ]
        )

    return SummaryTable(header=header, rows=rows)


def render_report(
    output_pdf: Path,
    results_dir: Path,
    stat_files: List[StatFile],
    xml_files: List[Path],
    summary_table: Optional[SummaryTable],
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

    toc_links: List[TocLink] = []

    with PdfPages(output_pdf) as pdf:
        page_number = 0
        rows_per_devices_page = 28
        rows_per_packet_page = 20
        rows_per_summary_page = 22
        toc_rows_per_page = 32

        def save_page(fig) -> None:
            nonlocal page_number
            page_number += 1
            fig.text(
                PAGE_RIGHT,
                PAGE_BOTTOM,
                f"Page {page_number}",
                ha="right",
                va="bottom",
                fontsize=9,
                color="dimgray",
            )
            pdf.savefig(fig)
            plt.close(fig)

        def add_rubric(fig, y: float, label: str, value: str, label_x: float = PAGE_LEFT) -> None:
            fig.text(label_x, y, label, fontsize=11, fontweight="bold")
            fig.text(label_x + 0.195, y, value, fontsize=11)

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
            col_widths: Optional[List[float]] = None,
            force_left_cols: Optional[List[int]] = None,
            link_targets: Optional[List[Optional[int]]] = None,
            link_col_idx: Optional[int] = None,
            fixed_body_row_height: Optional[float] = None,
            link_text_color: str = "#0b57d0",
        ) -> None:
            ax.axis("off")
            ax.set_title(title, fontsize=13, pad=12)

            header_h = 0.038
            base_h = 0.030
            row_heights: List[float] = []
            if fixed_body_row_height is not None:
                row_heights = [fixed_body_row_height for _ in rows]
            else:
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

            table_top = PAGE_TOP - 0.055
            table_bottom_limit = PAGE_BOTTOM + 0.05
            max_by_margins = max(0.2, table_top - table_bottom_limit)
            if table_height > max_by_margins:
                scale = max_by_margins / table_height
                header_h *= scale
                row_heights = [h * scale for h in row_heights]
                table_height = max_by_margins

            table_y = table_top - table_height
            table = ax.table(
                cellText=rows,
                colLabels=header,
                bbox=[PAGE_LEFT, table_y, PAGE_RIGHT - PAGE_LEFT, table_height],
                colWidths=col_widths,
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

            # Optional per-column hard left alignment.
            if force_left_cols:
                for col_idx in force_left_cols:
                    if col_idx < 0 or col_idx >= n_cols:
                        continue
                    table[(0, col_idx)].set_text_props(ha="left")
                    table[(0, col_idx)]._text.set_x(0.02)
                    for row_idx in range(1, len(rows) + 1):
                        cell = table[(row_idx, col_idx)]
                        cell.set_text_props(ha="left")
                        cell._text.set_x(0.02)

            # Optional clickable links for a specific column (per row).
            links_to_add: List[TocLink] = []
            if link_targets is not None and link_col_idx is not None and 0 <= link_col_idx < n_cols:
                current_page = page_number + 1
                for row_idx, _ in enumerate(row_heights):
                    target_page = link_targets[row_idx] if row_idx < len(link_targets) else None
                    if target_page is None:
                        continue
                    # Visual highlight for clickable links in table cell.
                    cell = table[(row_idx + 1, link_col_idx)]
                    cell.set_text_props(color=link_text_color)
                    try:
                        cell.get_text().set_underline(True)
                    except Exception:
                        pass
                    links_to_add.append(
                        TocLink(
                            from_page=current_page,
                            target_page=target_page,
                            rect_norm=(0.0, 0.0, 0.0, 0.0),  # filled after final layout
                        )
                    )

            fig.text(
                PAGE_LEFT,
                PAGE_BOTTOM + 0.003,
                source_text,
                fontsize=8.5,
                va="bottom",
                family="monospace",
            )
            fig.tight_layout(rect=(PAGE_LEFT, PAGE_BOTTOM + 0.035, PAGE_RIGHT, PAGE_TOP))

            # Compute link rectangles after final layout from actual cell geometry.
            if links_to_add and link_col_idx is not None:
                fig.canvas.draw()
                renderer = fig.canvas.get_renderer()
                for row_idx, link in enumerate(links_to_add):
                    cell = table[(row_idx + 1, link_col_idx)]
                    bbox = cell.get_window_extent(renderer=renderer)
                    (x0, y0) = fig.transFigure.inverted().transform((bbox.x0, bbox.y0))
                    (x1, y1) = fig.transFigure.inverted().transform((bbox.x1, bbox.y1))
                    # Small inset to avoid touching borders
                    pad_x = 0.0015
                    pad_y = 0.001
                    x0 = max(0.0, min(1.0, x0 + pad_x))
                    y0 = max(0.0, min(1.0, y0 + pad_y))
                    x1 = max(0.0, min(1.0, x1 - pad_x))
                    y1 = max(0.0, min(1.0, y1 - pad_y))
                    if x1 > x0 and y1 > y0:
                        link.rect_norm = (x0, y0, x1, y1)
                        toc_links.append(link)
            save_page(fig)

        # Compute page map before rendering (for TOC and links).
        toc_entries: List[Tuple[str, int]] = []
        toc_entries.append(("Preamble", 1))

        devices_pages = 0
        summary_pages = 0
        if summary_table is not None:
            summary_pages = (len(summary_table.rows) + rows_per_summary_page - 1) // rows_per_summary_page
        if devices_table is not None:
            devices_pages = (len(devices_table.rows) + rows_per_devices_page - 1) // rows_per_devices_page
        packet_pages = 0
        if packet_trace_table is not None:
            packet_pages = (len(packet_trace_table.rows) + rows_per_packet_page - 1) // rows_per_packet_page
        stat_pages = len(stat_files)

        dynamic_entries_count = 0
        if devices_pages > 0:
            dynamic_entries_count += 1
        if packet_pages > 0:
            dynamic_entries_count += 1
        if summary_pages > 0:
            dynamic_entries_count += 1
        dynamic_entries_count += stat_pages
        toc_pages = max(1, (dynamic_entries_count + toc_rows_per_page - 1) // toc_rows_per_page)
        first_content_page = 2 + toc_pages

        page_cursor = first_content_page
        stat_page_map: Dict[str, int] = {}
        if summary_pages > 0:
            toc_entries.append(("Key metrics summary table", page_cursor))
            page_cursor += summary_pages
        if devices_pages > 0:
            toc_entries.append(("Devices table", page_cursor))
            page_cursor += devices_pages
        if packet_pages > 0:
            toc_entries.append(("PacketTrace summary table", page_cursor))
            page_cursor += packet_pages
        for stat in stat_files:
            toc_entries.append((f"{stat.path.name}", page_cursor))
            stat_page_map[stat.path.name] = page_cursor
            page_cursor += 1

        # Cover page (preamble)
        fig = plt.figure(figsize=(11.69, 8.27))  # A4 landscape
        fig.suptitle("Simulation Results Report", fontsize=18, fontweight="bold", y=PAGE_TOP - 0.01)
        now = dt.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        xml_text = ", ".join(p.name for p in xml_files) if xml_files else "Not found"
        add_rubric(fig, PAGE_TOP - 0.13, "Generated at:", now)
        add_rubric(fig, PAGE_TOP - 0.18, "Results directory:", str(results_dir))
        add_rubric(fig, PAGE_TOP - 0.23, "Statistics files included:", str(len(stat_files)))
        add_rubric(fig, PAGE_TOP - 0.28, "XML attributes file(s):", xml_text)
        add_rubric(
            fig,
            PAGE_TOP - 0.33,
            "Devices table file:",
            devices_table.path.name if devices_table else "Not found",
        )
        add_rubric(
            fig,
            PAGE_TOP - 0.38,
            "Packet trace file:",
            packet_trace_table.path.name if packet_trace_table else "Not found",
        )
        fig.text(
            PAGE_LEFT,
            PAGE_TOP - 0.45,
            "Each following page contains one graph and the source filename.",
            fontsize=10,
        )
        fig.text(
            PAGE_LEFT,
            PAGE_TOP - 0.49,
            "Files without data rows are skipped automatically.",
            fontsize=10,
        )
        fig.subplots_adjust(left=PAGE_LEFT, right=PAGE_RIGHT, top=PAGE_TOP, bottom=PAGE_BOTTOM)
        save_page(fig)

        # Table of contents (after preamble)
        toc_items = toc_entries[1:]  # skip preamble self-link
        for toc_start in range(0, len(toc_items), toc_rows_per_page):
            toc_chunk = toc_items[toc_start:toc_start + toc_rows_per_page]
            fig = plt.figure(figsize=(11.69, 8.27))
            fig.suptitle("Table of Contents", fontsize=18, fontweight="bold", y=PAGE_TOP - 0.01)

            y = PAGE_TOP - 0.08
            line_h = 0.024
            if len(toc_items) > toc_rows_per_page:
                idx = (toc_start // toc_rows_per_page) + 1
                cnt = (len(toc_items) + toc_rows_per_page - 1) // toc_rows_per_page
                fig.text(PAGE_LEFT, y, f"Part {idx}/{cnt}", fontsize=10, color="dimgray")
                y -= line_h * 1.2

            for title, target_page in toc_chunk:
                x0 = PAGE_LEFT
                y0 = y - (line_h * 0.35)
                x1 = PAGE_RIGHT - 0.06
                y1 = y + (line_h * 0.55)
                toc_page_number = page_number + 1
                toc_links.append(
                    TocLink(
                        from_page=toc_page_number,
                        target_page=target_page,
                        rect_norm=(x0, y0, x1, y1),
                    )
                )
                fig.text(
                    PAGE_LEFT,
                    y,
                    title,
                    fontsize=10.5,
                    color="#0b57d0",
                )
                fig.text(PAGE_RIGHT, y, f"{target_page}", fontsize=10.5, ha="right")
                y -= line_h

            fig.text(
                PAGE_LEFT,
                PAGE_BOTTOM + 0.003,
                "Click item title to jump to page (viewer support may vary).",
                fontsize=8.5,
                color="dimgray",
            )
            fig.subplots_adjust(left=PAGE_LEFT, right=PAGE_RIGHT, top=PAGE_TOP, bottom=PAGE_BOTTOM)
            save_page(fig)

        if summary_table is not None:
            for chunk_start in range(0, len(summary_table.rows), rows_per_summary_page):
                chunk = summary_table.rows[chunk_start:chunk_start + rows_per_summary_page]
                chunk_targets: List[Optional[int]] = []
                for row in chunk:
                    src_file = row[1] if len(row) > 1 else ""
                    chunk_targets.append(stat_page_map.get(src_file))
                fig, ax = plt.subplots(figsize=(11.69, 8.27))
                title_suffix = ""
                if len(summary_table.rows) > rows_per_summary_page:
                    page_idx = (chunk_start // rows_per_summary_page) + 1
                    page_cnt = (len(summary_table.rows) + rows_per_summary_page - 1) // rows_per_summary_page
                    title_suffix = f" (page {page_idx}/{page_cnt})"
                draw_table_page(
                    fig=fig,
                    ax=ax,
                    title=f"Key Metrics Summary{title_suffix}",
                    header=summary_table.header,
                    rows=chunk,
                    source_text="Source: aggregated from non-empty stat-*.txt files",
                    body_fontsize=7.3,
                    header_fontsize=7.6,
                    chars_per_col=[4, 27, 7, 5, 10, 8, 6, 4, 8, 10, 10, 10, 10, 10],
                    col_widths=[0.03, 0.336, 0.06, 0.028, 0.081, 0.07, 0.05, 0.03, 0.06, 0.051, 0.051, 0.051, 0.051, 0.051],
                    force_left_cols=[1],
                    link_targets=chunk_targets,
                    link_col_idx=1,
                    fixed_body_row_height=0.03,
                )

        if devices_table is not None:
            for chunk_start in range(0, len(devices_table.rows), rows_per_devices_page):
                chunk = devices_table.rows[chunk_start:chunk_start + rows_per_devices_page]
                fig, ax = plt.subplots(figsize=(11.69, 8.27))
                title_suffix = ""
                if len(devices_table.rows) > rows_per_devices_page:
                    page_idx = (chunk_start // rows_per_devices_page) + 1
                    page_cnt = (len(devices_table.rows) + rows_per_devices_page - 1) // rows_per_devices_page
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
                    col_widths=[0.045, 0.075, 0.0525, 0.0525, 0.195, 0.1925],
                )

        if packet_trace_table is not None:
            for chunk_start in range(0, len(packet_trace_table.rows), rows_per_packet_page):
                chunk = packet_trace_table.rows[chunk_start:chunk_start + rows_per_packet_page]
                fig, ax = plt.subplots(figsize=(11.69, 8.27))
                title_suffix = ""
                if len(packet_trace_table.rows) > rows_per_packet_page:
                    page_idx = (chunk_start // rows_per_packet_page) + 1
                    page_cnt = (len(packet_trace_table.rows) + rows_per_packet_page - 1) // rows_per_packet_page
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
                    col_widths=[0.03, 0.066, 0.0675, 0.126, 0.055, 0.055, 0.065, 0.065, 0.065, 0.065, 0.065, 0.065, 0.055, 0.055],
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
                PAGE_LEFT,
                PAGE_BOTTOM + 0.003,
                "\n".join(meta_lines),
                fontsize=9,
                va="bottom",
                family="monospace",
            )
            fig.tight_layout(rect=(PAGE_LEFT, PAGE_BOTTOM + 0.04, PAGE_RIGHT, PAGE_TOP))
            save_page(fig)

    links_ok, links_msg = add_internal_toc_links(output_pdf, toc_links)
    if links_ok:
        print(f"[INFO] {links_msg}")
    else:
        print(f"[WARNING] {links_msg}")


def add_internal_toc_links(pdf_path: Path, links: List[TocLink]) -> Tuple[bool, str]:
    if not links:
        return True, "TOC is empty: no links to embed."

    try:
        from pypdf import PdfReader, PdfWriter
        from pypdf.annotations import Link
        from pypdf.generic import Fit
    except Exception:
        # If pypdf is unavailable, keep report usable without links.
        return (
            False,
            "Internal TOC links were NOT embedded: package 'pypdf' is not installed. "
            "Install it with: pip install pypdf",
        )

    reader = PdfReader(str(pdf_path))
    writer = PdfWriter()
    for page in reader.pages:
        writer.add_page(page)

    embedded = 0
    for link in links:
        toc_idx = link.from_page - 1
        dst_idx = link.target_page - 1
        if toc_idx < 0 or toc_idx >= len(writer.pages):
            continue
        if dst_idx < 0 or dst_idx >= len(writer.pages):
            continue

        page = writer.pages[toc_idx]
        width = float(page.mediabox.width)
        height = float(page.mediabox.height)
        x0n, y0n, x1n, y1n = link.rect_norm
        rect = (
            x0n * width,
            y0n * height,
            x1n * width,
            y1n * height,
        )

        if hasattr(writer, "add_link"):
            # Legacy API (older pypdf/PyPDF2 versions)
            writer.add_link(toc_idx, dst_idx, rect, fit="/Fit")
        else:
            # Current API (newer pypdf versions)
            writer.add_annotation(
                toc_idx,
                Link(
                    rect=rect,
                    target_page_index=dst_idx,
                    fit=Fit.fit(),
                ),
            )
        embedded += 1

    with pdf_path.open("wb") as out:
        writer.write(out)
    return True, f"Embedded {embedded} internal TOC link(s)."


def main() -> None:
    args = parse_args()
    results_dir = args.results_dir.resolve()

    if not results_dir.exists() or not results_dir.is_dir():
        raise SystemExit(f"Directory does not exist: {results_dir}")

    output_pdf = args.output.resolve() if args.output else results_dir / "results_report.pdf"

    stat_files = collect_stat_files(results_dir)
    xml_files = find_xml_files(results_dir)
    summary_table = build_summary_table(stat_files)
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
        summary_table,
        devices_table,
        packet_trace_table,
    )
    print(f"Report generated: {output_pdf}")


if __name__ == "__main__":
    main()
