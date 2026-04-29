#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Plot trajectory points produced in '% time lat lon alt' format.
"""

import argparse
import math
from pathlib import Path

EARTH_RADIUS = 6371009.0  # meters


def read_points(path):
    points = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            s = line.strip()
            if not s or s.startswith("%"):
                continue
            parts = s.split()
            if len(parts) < 4:
                continue
            t, lat, lon, alt = map(float, parts[:4])
            points.append((t, lat, lon, alt))
    if not points:
        raise ValueError(f"No points found in: {path}")
    return points


def draw_chevron_tick(ax, x, y, ux, uy, size=120.0, angle_deg=25.0, label=None):
    """
    Draw a V-shaped tick with apex at (x, y), pointing along (ux, uy).
    """
    backward_x = -ux
    backward_y = -uy
    a = math.radians(angle_deg)
    c = math.cos(a)
    s = math.sin(a)

    # Rotate backward vector by +/- angle to get the two "arms" of the chevron.
    lx = backward_x * c - backward_y * s
    ly = backward_x * s + backward_y * c
    rx = backward_x * c + backward_y * s
    ry = -backward_x * s + backward_y * c

    ax.plot([x, x + size * lx], [y, y + size * ly], color="black", linewidth=1.1, label=label)
    ax.plot([x, x + size * rx], [y, y + size * ry], color="black", linewidth=1.1)


def main():
    parser = argparse.ArgumentParser(
        description="Visualize trajectory from '% time lat lon alt' file."
    )
    parser.add_argument("input", help="Path to trajectory file")
    parser.add_argument(
        "--mode",
        choices=["2d", "3d", "both"],
        default="both",
        help="Which plot to build",
    )
    parser.add_argument(
        "--output-prefix",
        default="",
        help="If set, save image(s) as '<prefix>_2d.png' and/or '<prefix>_3d.png'",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Show interactive plot window",
    )
    parser.add_argument(
        "--tick-every",
        type=float,
        default=0.0,
        help="Trajectory tick interval in seconds (0 = use input time step)",
    )
    args = parser.parse_args()
    input_name = Path(args.input).name

    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit(
            "matplotlib is required. Install with: pip install matplotlib"
        ) from exc

    points = read_points(args.input)
    times = [p[0] for p in points]
    lats = [p[1] for p in points]
    lons = [p[2] for p in points]
    alts = [p[3] for p in points]

    # Convert to local ENU-like plane (meters) for true geometric shape in 2D.
    lat0 = math.radians(lats[0])
    lon0 = math.radians(lons[0])
    xs = []
    ys = []
    for lat_deg, lon_deg in zip(lats, lons):
        lat = math.radians(lat_deg)
        lon = math.radians(lon_deg)
        x = (lon - lon0) * math.cos(lat0) * EARTH_RADIUS  # East, m
        y = (lat - lat0) * EARTH_RADIUS  # North, m
        xs.append(x)
        ys.append(y)

    if len(times) >= 2:
        dt_default = times[1] - times[0]
    else:
        dt_default = 0.0
    tick_every = args.tick_every if args.tick_every > 0 else dt_default

    tick_idx = []
    if tick_every > 0 and len(times) > 0:
        next_t = times[0]
        eps = 1e-9
        for i, t in enumerate(times):
            if t + eps >= next_t:
                tick_idx.append(i)
                next_t += tick_every

    make_2d = args.mode in ("2d", "both")
    make_3d = args.mode in ("3d", "both")

    if make_2d:
        fig2d, ax2d = plt.subplots(figsize=(8, 6))
        ax2d.plot(xs, ys, "-", linewidth=1.6, label="Trajectory")
        if tick_idx:
            chevron_len = max(15.0, 0.015 * max(max(xs) - min(xs), max(ys) - min(ys), 1.0))
            labeled = False
            for i in tick_idx:
                if i == 0:
                    j_prev, j_next = 0, 1 if len(xs) > 1 else 0
                elif i == len(xs) - 1:
                    j_prev, j_next = len(xs) - 2, len(xs) - 1
                else:
                    j_prev, j_next = i - 1, i + 1
                dx = xs[j_next] - xs[j_prev]
                dy = ys[j_next] - ys[j_prev]
                norm = math.hypot(dx, dy)
                if norm <= 1e-12:
                    continue
                ux, uy = dx / norm, dy / norm
                draw_chevron_tick(
                    ax2d,
                    xs[i],
                    ys[i],
                    ux,
                    uy,
                    size=chevron_len,
                    angle_deg=25.0,
                    label=(f"Ticks every {tick_every:g}s" if not labeled else None),
                )
                labeled = True
        ax2d.scatter([xs[0]], [ys[0]], color="green", s=40, label="Start")
        ax2d.scatter([xs[-1]], [ys[-1]], color="red", s=40, label="End")
        ax2d.set_xlabel("East (m)")
        ax2d.set_ylabel("North (m)")
        ax2d.set_title(f"Trajectory (2D) - {input_name}")
        ax2d.grid(True, alpha=0.3)
        ax2d.legend()
        ax2d.text(
            0.02,
            0.98,
            f"Update interval: {dt_default:g} s",
            transform=ax2d.transAxes,
            va="top",
            fontsize=9,
            bbox={"boxstyle": "round,pad=0.2", "fc": "white", "alpha": 0.8},
        )
        ax2d.text(
            0.02,
            0.91,
            f"Source file: {input_name}",
            transform=ax2d.transAxes,
            va="top",
            fontsize=9,
            bbox={"boxstyle": "round,pad=0.2", "fc": "white", "alpha": 0.8},
        )
        ax2d.set_aspect("equal", adjustable="box")
        fig2d.tight_layout()

        if args.output_prefix:
            out_2d = Path(f"{args.output_prefix}_2d.png")
            fig2d.savefig(out_2d, dpi=140)
            print(f"Saved: {out_2d}")

    if make_3d:
        from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

        fig3d = plt.figure(figsize=(9, 6))
        ax3d = fig3d.add_subplot(111, projection="3d")
        ax3d.plot(xs, ys, alts, "-", linewidth=1.4, label="Trajectory")
        if tick_idx:
            ax3d.scatter([xs[i] for i in tick_idx], [ys[i] for i in tick_idx], [alts[i] for i in tick_idx],
                         color="black", s=10, marker="o", label=f"Ticks every {tick_every:g}s")
        ax3d.scatter([xs[0]], [ys[0]], [alts[0]], color="green", s=35, label="Start")
        ax3d.scatter([xs[-1]], [ys[-1]], [alts[-1]], color="red", s=35, label="End")
        ax3d.set_xlabel("East (m)")
        ax3d.set_ylabel("North (m)")
        ax3d.set_zlabel("Altitude (m)")
        ax3d.set_title(f"Trajectory (3D) - {input_name}")
        ax3d.legend()
        ax3d.text2D(
            0.02,
            0.98,
            f"Update interval: {dt_default:g} s",
            transform=ax3d.transAxes,
            va="top",
            fontsize=9,
            bbox={"boxstyle": "round,pad=0.2", "fc": "white", "alpha": 0.8},
        )
        ax3d.text2D(
            0.02,
            0.91,
            f"Source file: {input_name}",
            transform=ax3d.transAxes,
            va="top",
            fontsize=9,
            bbox={"boxstyle": "round,pad=0.2", "fc": "white", "alpha": 0.8},
        )
        fig3d.tight_layout()

        if args.output_prefix:
            out_3d = Path(f"{args.output_prefix}_3d.png")
            fig3d.savefig(out_3d, dpi=140)
            print(f"Saved: {out_3d}")

    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
