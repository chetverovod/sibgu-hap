#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Generate GPS points for a circular trajectory.
"""

import argparse
import math
import sys


EARTH_RADIUS = 6371009.0  # meters


def strictly_positive_float(value):
    value = float(value)
    if value <= 0:
        raise argparse.ArgumentTypeError("Value must be > 0")
    return value


def latitude(value):
    value = float(value)
    if value < -90.0 or value > 90.0:
        raise argparse.ArgumentTypeError("Latitude must be in [-90, 90]")
    return value


def longitude(value):
    value = float(value)
    if value < -180.0 or value > 180.0:
        raise argparse.ArgumentTypeError("Longitude must be in [-180, 180]")
    return value


def generate_circle_points(center_lat_deg,
                           center_lon_deg,
                           altitude_m,
                           radius_m,
                           speed_mps,
                           time_step_s,
                           start_angle_deg=0.0):
    lat0_rad = math.radians(center_lat_deg)
    omega = speed_mps / radius_m  # rad/s
    total_time = (2.0 * math.pi * radius_m) / speed_mps
    steps = int(math.ceil(total_time / time_step_s))
    start_angle_rad = math.radians(start_angle_deg)

    points = []
    for i in range(steps + 1):
        t = min(i * time_step_s, total_time)
        theta = start_angle_rad + omega * t

        # Local tangent-plane approximation (suitable for kilometer-scale circles).
        east_m = radius_m * math.cos(theta)
        north_m = radius_m * math.sin(theta)

        dlat_rad = north_m / EARTH_RADIUS
        dlon_rad = east_m / (EARTH_RADIUS * math.cos(lat0_rad))

        lat = center_lat_deg + math.degrees(dlat_rad)
        lon = center_lon_deg + math.degrees(dlon_rad)
        points.append((t, lat, lon, altitude_m))

    return points


def main():
    parser = argparse.ArgumentParser(
        description="Generate '# time lat lon alt' points for a circular trajectory.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--center-lat", type=latitude, default=56.0119444444,
                        help="Center latitude in decimal degrees")
    parser.add_argument("--center-lon", type=longitude, default=92.8713888889,
                        help="Center longitude in decimal degrees")
    parser.add_argument("--altitude", type=strictly_positive_float, default=20000.0,
                        help="Altitude above ground/sea level (m)")
    parser.add_argument("--radius", type=strictly_positive_float, default=6000.0,
                        help="Circle radius (m)")
    parser.add_argument("--speed", type=strictly_positive_float, default=10.0,
                        help="Speed (m/s)")
    parser.add_argument("--time-step", type=strictly_positive_float, default=1.0,
                        help="Time step between generated points (s)")
    parser.add_argument("--start-angle", type=float, default=0.0,
                        help="Start angle in degrees (0 = east direction)")
    parser.add_argument("--output", default="-",
                        help="Output file path; '-' means stdout")

    args = parser.parse_args()

    points = generate_circle_points(
        center_lat_deg=args.center_lat,
        center_lon_deg=args.center_lon,
        altitude_m=args.altitude,
        radius_m=args.radius,
        speed_mps=args.speed,
        time_step_s=args.time_step,
        start_angle_deg=args.start_angle,
    )

    out = sys.stdout if args.output == "-" else open(args.output, "w", encoding="utf-8")
    try:
        out.write("# time lat lon alt\n")
        for t, lat, lon, alt in points:
            out.write(f"{t:.3f} {lat:.9f} {lon:.9f} {alt:.3f}\n")
    finally:
        if out is not sys.stdout:
            out.close()


if __name__ == "__main__":
    main()
