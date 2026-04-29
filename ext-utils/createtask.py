#!/usr/bin/env python3
"""
Create SibGu HAP task archives (*.tsk).

A .tsk file is a zip archive with predefined directory layout, baseline files,
and README.md descriptions for every folder.
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import sys
import tempfile
from pathlib import Path
from textwrap import dedent
import zipfile

TOOL_VERSION = "1.2.0"


def make_file_format_content() -> str:
    return (
        f"{EXPECTED_FILEFORMAT_LINE}\n"
        f"Creator-Version: createtask.py {TOOL_VERSION}\n"
    )


ROOT_FILES = {
    "README.md": dedent(
        """\
        # SibGu HAP Task Container

        This container stores simulation input, configuration, and output data.

        ## Purpose
        - `commandLine.txt` stores the ns-3 launch command line.
        - `fileFormat.txt` stores archive format and creator version.
        - Subdirectories store scenario data, parameters, and results.

        ## User Notes
        - ...
        """
    ),
    "commandLine.txt": "# Example: --scenario=your-scenario --sim-time=60\n",
    "fileFormat.txt": "",
}


DIR_READMES = {
    "globalparams": dedent(
        """\
        # globalparams

        Global simulation parameters shared by scenario subsystems.

        ## Typical Content
        - Experiment-level settings.
        - Constants used by multiple modules.

        ## User Notes
        - ...
        """
    ),
    "propagation": dedent(
        """\
        # propagation

        Channel and propagation environment settings.

        ## Typical Content
        - Path loss and fading model parameters.
        - Medium-related configuration.

        ## User Notes
        - ...
        """
    ),
    "phy": dedent(
        """\
        # phy

        PHY-layer radio settings for nodes and links.

        ## Typical Content
        - TX/RX configuration.
        - Modulation and coding parameters.

        ## User Notes
        - ...
        """
    ),
    "trafic": dedent(
        """\
        # trafic

        Traffic generation and protocol configuration.

        ## Typical Content
        - Traffic profiles/patterns.
        - Application flow and protocol options.

        ## User Notes
        - ...
        """
    ),
    "scenario": dedent(
        """\
        # scenario

        Physical scenario definition: node positions, beam configuration,
        and communication standard selection.

        ## Key Subdirectories
        - `antennapatterns`: antenna gain pattern files.
        - `beams`: beam/channel/gateway assignment.
        - `positions`: GW/UT/satellite positions and HAP traces.
        - `standard`: selected communication standard (`DVB` or `LORA`).
        - `waveforms`: allowed waveforms and default waveform.
        - `routes`: routing description files.
        - `beamhopping` (optional): static beam hopping plan.

        ## User Notes
        - ...
        """
    ),
    "scenario/antennapatterns": dedent(
        """\
        # scenario/antennapatterns

        Antenna pattern configuration files.
        This may be a regular directory or a symlink.

        ## Required Items
        - `GeoPos.in` with one line: `Latitude Longitude Altitude`.
        - One or more beam `.txt` files.

        ## Beam File Rules
        - Filename must contain a numeric Beam ID.
        - Line format: `Latitude Longitude Gain_dB`.
        - `Gain_dB` can be numeric or `NaN`/`nan`.

        ## User Notes
        - ...
        """
    ),
    "scenario/beams": dedent(
        """\
        # scenario/beams

        Gateway and frequency-channel assignment by beam.

        ## Required Files
        - `fwdConf.txt`
        - `rtnConf.txt`

        ## Line Format
        `Beam_ID User_Channel_ID Gateway_ID Feeder_Channel_ID`

        ## Requirements
        - Both files must have the same number of lines.
        - Values must be positive integers.
        - Row order maps to Beam IDs.

        ## User Notes
        - ...
        """
    ),
    "scenario/beamhopping": dedent(
        """\
        # scenario/beamhopping

        Static beam hopping plan (BSTP).
        Used only when this directory exists.

        ## Plan Line Format
        `DurationSuperframes, beamId1, beamId2, ...`

        ## Requirements
        - First column: duration in DVB-S2X superframes.
        - Next columns: active beam IDs.
        - No duplicate beam IDs in a row.
        - Plan repeats cyclically.

        ## User Notes
        - ...
        """
    ),
    "scenario/positions": dedent(
        """\
        # scenario/positions

        Position description files for scenario nodes.

        ## Base Files
        - `gw_positions.txt`
        - `ut_positions.txt`
        - `sat_positions.txt` or `tles.txt`
        - `isls.txt` and `start_date.txt` (for constellations)
        - `sat_traces.txt` (for HAP traces)

        ## Coordinate Format
        `Latitude Longitude Height`

        ## Additional Formats
        - In `sat_traces.txt`: `satId traceFile`
        - In trace files: `Time Latitude Longitude Altitude`

        ## User Notes
        - ...
        """
    ),
    "scenario/standard": dedent(
        """\
        # scenario/standard

        Global communication standard selection.

        ## Required File
        - `standard.txt` (first token must be `DVB` or `LORA`)

        ## User Notes
        - ...
        """
    ),
    "scenario/waveforms": dedent(
        """\
        # scenario/waveforms

        Allowed return-link waveforms.

        ## Required Files
        - `waveforms.txt`
        - `default_waveform.txt`

        ## `waveforms.txt` Line Format
        `ID ModBits CodingRate PayloadBytes DurationSymbols [PreambleSymbols]`

        ## Additional Rule
        - `default_waveform.txt` must contain one default waveform ID.

        ## User Notes
        - ...
        """
    ),
    "scenario/routes": dedent(
        """\
        # scenario/routes

        Route table and routing-rule files for the scenario.

        ## Typical Content
        - Files like `route_*.txt`.
        - Static or precomputed routes.

        ## User Notes
        - ...
        """
    ),
    "sims": dedent(
        """\
        # sims

        Simulation outputs (raw and/or aggregated).

        ## Typical Content
        - Result files `result_*.txt`.
        - Additional run artifacts.

        ## User Notes
        - ...
        """
    ),
    "reports": dedent(
        """\
        # reports

        Reports generated from simulation results.

        ## Typical Content
        - Text/tabular reports `report_*.txt`.

        ## User Notes
        - ...
        """
    ),
    "logs": dedent(
        """\
        # logs

        Simulation execution logs (including captured console output).

        ## Typical Content
        - Per-run log files `log_*.txt`.

        ## User Notes
        - ...
        """
    ),
}


PLACEHOLDER_FILES = {
    # Scenario: beams
    "scenario/beams/fwdConf.txt": "1 1 1 1\n",
    "scenario/beams/rtnConf.txt": "1 1 1 1\n",
    # Scenario: standard
    "scenario/standard/standard.txt": "DVB\n",
    # Scenario: waveforms
    "scenario/waveforms/waveforms.txt": "1 2 1/3 14 262\n",
    "scenario/waveforms/default_waveform.txt": "1\n",
    # Scenario: positions
    "scenario/positions/gw_positions.txt": "44.50 13.50 10.0\n",
    "scenario/positions/ut_positions.txt": "44.60 13.40 2.0\n",
    "scenario/positions/sat_positions.txt": "0.0 10.0 35786000.0\n",
    "scenario/positions/tles.txt": "# TLE entries (optional alternative to sat_positions.txt)\n",
    "scenario/positions/isls.txt": "# ISL links (optional)\n",
    "scenario/positions/start_date.txt": "# YYYY-MM-DD HH:MM:SS (optional)\n",
    "scenario/positions/sat_traces.txt": "% satId traceFile\n",
    # Scenario: antenna patterns
    "scenario/antennapatterns/GeoPos.in": "0.0 10.0 35786000.0\n",
    "scenario/antennapatterns/SatAntennaGain_1/origin.timestamp": (
        "# origin timestamp placeholder\n"
    ),
    "scenario/antennapatterns/SatAntennaGain_1/GeoPos.in": "0.0 10.0 35786000.0\n",
    "scenario/antennapatterns/SatAntennaGain_1/SatAntennaGainBeams_1.txt": (
        "40.0 0.0 35.2\n"
    ),
    # Routes, outputs
    "scenario/routes/route_1.txt": "# route placeholder\n",
    "sims/result_1.txt": "# simulation result placeholder\n",
    "reports/report_1.txt": "# report placeholder\n",
    "logs/log_1.txt": "# log placeholder\n",
}


ALL_DIRS = [
    "globalparams",
    "propagation",
    "phy",
    "trafic",
    "scenario",
    "scenario/antennapatterns",
    "scenario/antennapatterns/SatAntennaGain_1",
    "scenario/beams",
    "scenario/beamhopping",
    "scenario/positions",
    "scenario/standard",
    "scenario/waveforms",
    "scenario/routes",
    "sims",
    "reports",
    "logs",
]

REQUIRED_DIRS = set(ALL_DIRS)
REQUIRED_FILES = {
    "README.md",
    "commandLine.txt",
    "fileFormat.txt",
    "scenario/beams/fwdConf.txt",
    "scenario/beams/rtnConf.txt",
    "scenario/standard/standard.txt",
    "scenario/waveforms/waveforms.txt",
    "scenario/waveforms/default_waveform.txt",
}
EXPECTED_FILEFORMAT_LINE = "This archive is a task for SibGu HAP simulator."
RATE_BY_MOD_BITS = {
    1: {"1/3"},
    2: {"1/3", "1/2", "2/3", "3/4", "5/6"},
    3: {"2/3", "3/4", "5/6"},
    4: {"3/4", "5/6"},
}


def write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def build_task_tree(task_root: Path) -> None:
    for rel_dir in ALL_DIRS:
        (task_root / rel_dir).mkdir(parents=True, exist_ok=True)

    for name, content in ROOT_FILES.items():
        if name == "fileFormat.txt":
            write_file(task_root / name, make_file_format_content())
        else:
            write_file(task_root / name, content)

    for rel_dir in ALL_DIRS:
        readme_path = task_root / rel_dir / "README.md"
        readme_content = DIR_READMES.get(
            rel_dir,
            dedent(
                f"""\
                # {rel_dir}

                Directory description for `{rel_dir}`.

                ## User Notes
                - ...
                """
            ),
        )
        write_file(readme_path, readme_content)

    for rel_path, content in PLACEHOLDER_FILES.items():
        write_file(task_root / rel_path, content)


def create_tsk_archive(output_path: Path, force: bool) -> None:
    output_path = output_path.resolve()
    if output_path.suffix.lower() != ".tsk":
        output_path = output_path.with_suffix(".tsk")

    if output_path.exists() and not force:
        raise FileExistsError(
            f"File already exists: {output_path}. Use --force to overwrite."
        )

    with tempfile.TemporaryDirectory(prefix="sibgu_task_") as tmp_dir:
        tmp_root = Path(tmp_dir) / output_path.stem
        tmp_root.mkdir(parents=True, exist_ok=True)
        build_task_tree(tmp_root)

        if output_path.exists():
            output_path.unlink()

        archive_base = output_path.with_suffix("")
        produced = shutil.make_archive(
            base_name=str(archive_base),
            format="zip",
            root_dir=str(tmp_root),
            base_dir=".",
        )
        Path(produced).replace(output_path)


def create_task_template_dir(output_dir: Path, force: bool) -> Path:
    output_dir = output_dir.resolve()
    if output_dir.exists():
        if not force:
            raise FileExistsError(
                f"Directory already exists: {output_dir}. Use --force to overwrite."
            )
        if output_dir.is_file():
            raise FileExistsError(
                f"Path points to a file: {output_dir}. Please provide a directory path."
            )
        shutil.rmtree(output_dir)

    output_dir.mkdir(parents=True, exist_ok=True)
    build_task_tree(output_dir)
    return output_dir


def _normalize_tsk_path(path: Path) -> Path:
    if path.suffix.lower() != ".tsk":
        return path.with_suffix(".tsk")
    return path


def _iter_nonempty_noncomment_lines(
    text: str, comment_prefixes: tuple[str, ...] = ("#",)
) -> list[tuple[int, str]]:
    result: list[tuple[int, str]] = []
    for idx, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        if any(line.startswith(prefix) for prefix in comment_prefixes):
            continue
        result.append((idx, line))
    return result


def _safe_read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def _parse_float(value: str, what: str, errors: list[str], file_rel: str, line_no: int) -> float | None:
    try:
        return float(value)
    except ValueError:
        errors.append(f"{file_rel}:{line_no} -> {what} must be a number, got '{value}'.")
        return None


def _validate_lat_lon_alt(
    lat: float, lon: float, alt: float, errors: list[str], file_rel: str, line_no: int
) -> None:
    if not (-90.0 <= lat <= 90.0):
        errors.append(f"{file_rel}:{line_no} -> Latitude is out of range [-90, 90].")
    if not (-180.0 <= lon <= 180.0):
        errors.append(f"{file_rel}:{line_no} -> Longitude is out of range [-180, 180].")
    if not math.isfinite(alt):
        errors.append(f"{file_rel}:{line_no} -> Altitude/Height must be a finite number.")


def _validate_positions_triplet_file(path: Path, rel: str, errors: list[str]) -> None:
    rows = _iter_nonempty_noncomment_lines(_safe_read_text(path), ("#", "%"))
    if not rows:
        errors.append(f"{rel} -> file must not be empty.")
        return
    for line_no, line in rows:
        parts = line.split()
        if len(parts) != 3:
            errors.append(f"{rel}:{line_no} -> expected 3 values: Latitude Longitude Height.")
            continue
        lat = _parse_float(parts[0], "Latitude", errors, rel, line_no)
        lon = _parse_float(parts[1], "Longitude", errors, rel, line_no)
        alt = _parse_float(parts[2], "Height", errors, rel, line_no)
        if lat is not None and lon is not None and alt is not None:
            _validate_lat_lon_alt(lat, lon, alt, errors, rel, line_no)


def _validate_standard(path: Path, errors: list[str]) -> None:
    rel = "scenario/standard/standard.txt"
    words = _safe_read_text(path).strip().split()
    if not words or words[0] not in {"DVB", "LORA"}:
        errors.append(f"{rel} -> first token must be DVB or LORA.")


def _validate_file_format(path: Path, errors: list[str], mode: str) -> None:
    rel = "fileFormat.txt"
    lines = [line.strip() for line in _safe_read_text(path).splitlines() if line.strip()]
    if not lines:
        errors.append(f"{rel} -> file is empty.")
        return
    if lines[0] != EXPECTED_FILEFORMAT_LINE:
        errors.append(f"{rel} -> first line must be: '{EXPECTED_FILEFORMAT_LINE}'.")
    if mode == "strict":
        version_prefix = "Creator-Version: createtask.py "
        if len(lines) < 2 or not lines[1].startswith(version_prefix):
            errors.append(
                f"{rel} -> second line must start with '{version_prefix}<version>'."
            )


def _validate_beam_conf(path: Path, rel: str, errors: list[str]) -> list[tuple[int, int, int, int]]:
    rows = _iter_nonempty_noncomment_lines(_safe_read_text(path), ("#",))
    parsed: list[tuple[int, int, int, int]] = []
    for line_no, line in rows:
        parts = line.split()
        if len(parts) != 4:
            errors.append(f"{rel}:{line_no} -> expected 4 integers: Beam_ID U_FREQ_ID Gateway_ID F_FREQ_ID.")
            continue
        vals: list[int] = []
        ok = True
        for token in parts:
            if not re.fullmatch(r"[0-9]+", token):
                errors.append(f"{rel}:{line_no} -> '{token}' must be a positive integer.")
                ok = False
                break
            val = int(token)
            if val <= 0:
                errors.append(f"{rel}:{line_no} -> values must be > 0.")
                ok = False
                break
            vals.append(val)
        if ok:
            if vals[0] > 1000:
                errors.append(f"{rel}:{line_no} -> Beam_ID is limited to 1000.")
            parsed.append((vals[0], vals[1], vals[2], vals[3]))
    return parsed


def _validate_waveforms(path: Path, default_path: Path, errors: list[str]) -> None:
    rel = "scenario/waveforms/waveforms.txt"
    rows = _iter_nonempty_noncomment_lines(_safe_read_text(path), ("#",))
    ids: set[int] = set()
    for line_no, line in rows:
        parts = line.split()
        if len(parts) not in {5, 6}:
            errors.append(f"{rel}:{line_no} -> expected 5 or 6 fields.")
            continue
        id_s, mod_s, coding_rate, payload_s, dur_s = (
            parts[0],
            parts[1],
            parts[2],
            parts[3],
            parts[4],
        )
        preamble_s = parts[5] if len(parts) == 6 else None
        int_tokens = {
            "ID": id_s,
            "ModBits": mod_s,
            "PayloadBytes": payload_s,
            "DurationSymbols": dur_s,
        }
        int_values: dict[str, int] = {}
        token_ok = True
        for name, token in int_tokens.items():
            if not re.fullmatch(r"[0-9]+", token):
                errors.append(f"{rel}:{line_no} -> {name} must be a positive integer.")
                token_ok = False
            else:
                int_values[name] = int(token)
        if preamble_s is not None:
            if not re.fullmatch(r"[0-9]+", preamble_s):
                errors.append(f"{rel}:{line_no} -> PreambleSymbols must be an integer >= 0.")
                token_ok = False
        if not token_ok:
            continue
        mod = int_values["ModBits"]
        if mod not in RATE_BY_MOD_BITS:
            errors.append(f"{rel}:{line_no} -> ModBits must be in range 1..4.")
            continue
        if coding_rate not in RATE_BY_MOD_BITS[mod]:
            errors.append(f"{rel}:{line_no} -> CodingRate '{coding_rate}' is invalid for ModBits={mod}.")
        wf_id = int_values["ID"]
        if wf_id in ids:
            errors.append(f"{rel}:{line_no} -> duplicate waveform ID: {wf_id}.")
        ids.add(wf_id)

    default_rel = "scenario/waveforms/default_waveform.txt"
    default_rows = _iter_nonempty_noncomment_lines(_safe_read_text(default_path), ("#",))
    if len(default_rows) != 1:
        errors.append(f"{default_rel} -> must contain exactly one meaningful line.")
    else:
        _, line = default_rows[0]
        if not re.fullmatch(r"[0-9]+", line):
            errors.append(f"{default_rel} -> must be a positive integer (waveform ID).")
        else:
            default_id = int(line)
            if ids and default_id not in ids:
                errors.append(f"{default_rel} -> ID {default_id} is missing in waveforms.txt.")


def _validate_antennapatterns(root: Path, errors: list[str]) -> None:
    base = root / "scenario/antennapatterns"
    rel_base = "scenario/antennapatterns"
    geopos = base / "GeoPos.in"
    if not geopos.exists():
        errors.append(f"{rel_base}/GeoPos.in -> required file is missing.")
    else:
        rows = _iter_nonempty_noncomment_lines(_safe_read_text(geopos), ("#", "%"))
        if len(rows) != 1:
            errors.append(f"{rel_base}/GeoPos.in -> expected exactly one meaningful line.")
        else:
            line_no, line = rows[0]
            parts = line.split()
            if len(parts) != 3:
                errors.append(f"{rel_base}/GeoPos.in:{line_no} -> expected Latitude Longitude Altitude.")
            else:
                lat = _parse_float(parts[0], "Latitude", errors, f"{rel_base}/GeoPos.in", line_no)
                lon = _parse_float(parts[1], "Longitude", errors, f"{rel_base}/GeoPos.in", line_no)
                alt = _parse_float(parts[2], "Altitude", errors, f"{rel_base}/GeoPos.in", line_no)
                if lat is not None and lon is not None and alt is not None:
                    _validate_lat_lon_alt(lat, lon, alt, errors, f"{rel_base}/GeoPos.in", line_no)

    txt_candidates = [
        p for p in base.rglob("*.txt")
        if p.name != "README.md"
    ]
    if not txt_candidates:
        errors.append(f"{rel_base} -> must contain at least one beam pattern .txt file.")
        return

    for txt_path in txt_candidates:
        rel = txt_path.relative_to(root).as_posix()
        if not re.search(r"\d", txt_path.stem):
            errors.append(f"{rel} -> file name must contain a number (Beam ID).")
        rows = _iter_nonempty_noncomment_lines(_safe_read_text(txt_path), ("#", "%"))
        if not rows:
            continue
        lat_lon: list[tuple[float, float]] = []
        for line_no, line in rows:
            parts = line.split()
            if len(parts) != 3:
                errors.append(f"{rel}:{line_no} -> expected format: Latitude Longitude Gain_dB.")
                continue
            lat = _parse_float(parts[0], "Latitude", errors, rel, line_no)
            lon = _parse_float(parts[1], "Longitude", errors, rel, line_no)
            gain_raw = parts[2]
            gain_ok = gain_raw.lower() == "nan"
            if not gain_ok:
                _ = _parse_float(gain_raw, "Gain_dB", errors, rel, line_no)
            if lat is None or lon is None:
                continue
            if not (-90.0 <= lat <= 90.0):
                errors.append(f"{rel}:{line_no} -> Latitude is out of range [-90, 90].")
            if not (-180.0 <= lon <= 180.0):
                errors.append(f"{rel}:{line_no} -> Longitude is out of range [-180, 180].")
            lat_lon.append((lat, lon))
        # Basic duplicate-point check
        if len(set(lat_lon)) != len(lat_lon):
            errors.append(f"{rel} -> contains duplicate points (Latitude, Longitude).")


def _validate_positions(root: Path, errors: list[str]) -> None:
    pos = root / "scenario/positions"
    gw = pos / "gw_positions.txt"
    ut = pos / "ut_positions.txt"
    sat = pos / "sat_positions.txt"
    tles = pos / "tles.txt"
    sat_traces = pos / "sat_traces.txt"

    _validate_positions_triplet_file(gw, "scenario/positions/gw_positions.txt", errors)
    _validate_positions_triplet_file(ut, "scenario/positions/ut_positions.txt", errors)

    sat_rows = _iter_nonempty_noncomment_lines(_safe_read_text(sat), ("#", "%")) if sat.exists() else []
    tle_rows = _iter_nonempty_noncomment_lines(_safe_read_text(tles), ("#", "%")) if tles.exists() else []
    if not sat_rows and not tle_rows:
        errors.append("scenario/positions -> sat_positions.txt or tles.txt must contain data.")
    if sat_rows:
        _validate_positions_triplet_file(sat, "scenario/positions/sat_positions.txt", errors)
    if tle_rows is not None and tles.exists() and not tle_rows and not sat_rows:
        errors.append("scenario/positions/tles.txt -> file exists but has no meaningful lines.")

    if sat_traces.exists():
        rows = _iter_nonempty_noncomment_lines(_safe_read_text(sat_traces), ("#", "%"))
        for line_no, line in rows:
            parts = line.split()
            rel = "scenario/positions/sat_traces.txt"
            if len(parts) != 2:
                errors.append(f"{rel}:{line_no} -> expected format: satId traceFile.")
                continue
            sat_id, trace_file = parts
            if not re.fullmatch(r"[0-9]+", sat_id):
                errors.append(f"{rel}:{line_no} -> satId must be a positive integer.")
                continue
            trace_path = pos / trace_file
            if not trace_path.exists():
                errors.append(f"{rel}:{line_no} -> trace file not found: {trace_file}.")
                continue
            trace_rel = trace_path.relative_to(root).as_posix()
            trace_rows = _iter_nonempty_noncomment_lines(
                _safe_read_text(trace_path), ("#", "%")
            )
            if not trace_rows:
                errors.append(f"{trace_rel} -> trace file is empty.")
                continue
            for tr_line_no, tr_line in trace_rows:
                tparts = tr_line.split()
                if len(tparts) != 4:
                    errors.append(f"{trace_rel}:{tr_line_no} -> expected: Time Latitude Longitude Altitude.")
                    continue
                _ = _parse_float(tparts[0], "Time", errors, trace_rel, tr_line_no)
                lat = _parse_float(tparts[1], "Latitude", errors, trace_rel, tr_line_no)
                lon = _parse_float(tparts[2], "Longitude", errors, trace_rel, tr_line_no)
                alt = _parse_float(tparts[3], "Altitude", errors, trace_rel, tr_line_no)
                if lat is not None and lon is not None and alt is not None:
                    _validate_lat_lon_alt(lat, lon, alt, errors, trace_rel, tr_line_no)


def validate_task_directory(task_root: Path, mode: str = "strict") -> list[str]:
    if mode not in {"basic", "strict"}:
        raise ValueError("mode must be 'basic' or 'strict'.")
    errors: list[str] = []
    for rel_dir in sorted(REQUIRED_DIRS):
        if not (task_root / rel_dir).is_dir():
            errors.append(f"Missing required directory: {rel_dir}")
    for rel_file in sorted(REQUIRED_FILES):
        if not (task_root / rel_file).is_file():
            errors.append(f"Missing required file: {rel_file}")
    if errors:
        return errors

    _validate_file_format(task_root / "fileFormat.txt", errors, mode)
    _validate_standard(task_root / "scenario/standard/standard.txt", errors)

    if mode == "basic":
        return errors

    fwd = _validate_beam_conf(
        task_root / "scenario/beams/fwdConf.txt", "scenario/beams/fwdConf.txt", errors
    )
    rtn = _validate_beam_conf(
        task_root / "scenario/beams/rtnConf.txt", "scenario/beams/rtnConf.txt", errors
    )
    if fwd and rtn and len(fwd) != len(rtn):
        errors.append("scenario/beams -> fwdConf.txt and rtnConf.txt must have the same number of rows.")

    _validate_waveforms(
        task_root / "scenario/waveforms/waveforms.txt",
        task_root / "scenario/waveforms/default_waveform.txt",
        errors,
    )
    _validate_positions(task_root, errors)
    _validate_antennapatterns(task_root, errors)
    return errors


def _collect_zip_items(tsk_path: Path) -> tuple[set[str], set[str]]:
    dirs: set[str] = set()
    files: set[str] = set()
    with zipfile.ZipFile(tsk_path, "r") as zf:
        for info in zf.infolist():
            name = info.filename.rstrip("/")
            if not name:
                continue
            if info.is_dir():
                dirs.add(name)
            else:
                files.add(name)
                parts = Path(name).parts
                for idx in range(1, len(parts)):
                    dirs.add("/".join(parts[:idx]))
    return dirs, files


def validate_tsk_file(tsk_path: Path, mode: str = "strict") -> list[str]:
    errors: list[str] = []
    tsk_path = _normalize_tsk_path(tsk_path).resolve()
    if not tsk_path.exists():
        return [f"File not found: {tsk_path}"]

    if not zipfile.is_zipfile(tsk_path):
        return [f"File is not a zip archive: {tsk_path}"]

    try:
        with zipfile.ZipFile(tsk_path, "r") as zf:
            bad_entry = zf.testzip()
            if bad_entry is not None:
                errors.append(f"Corrupted archive entry: {bad_entry}")
        if errors:
            return errors
        with tempfile.TemporaryDirectory(prefix="sibgu_validate_") as tmp:
            root = Path(tmp) / "task"
            root.mkdir(parents=True, exist_ok=True)
            shutil.unpack_archive(str(tsk_path), str(root), format="zip")
            errors.extend(validate_task_directory(root, mode=mode))
    except zipfile.BadZipFile:
        errors.append(f"Unable to read zip archive: {tsk_path}")

    return errors


def include_missing_entries(
    target_tsk: Path, source_tsk: Path, output_tsk: Path, force: bool
) -> tuple[int, int]:
    target_tsk = _normalize_tsk_path(target_tsk).resolve()
    source_tsk = _normalize_tsk_path(source_tsk).resolve()
    output_tsk = _normalize_tsk_path(output_tsk).resolve()

    if not target_tsk.exists():
        raise FileNotFoundError(f"File 1 (target) not found: {target_tsk}")
    if not source_tsk.exists():
        raise FileNotFoundError(f"File 2 (source) not found: {source_tsk}")
    if output_tsk.exists() and not force:
        raise FileExistsError(
            f"File already exists: {output_tsk}. Use --force to overwrite."
        )

    if not zipfile.is_zipfile(target_tsk):
        raise ValueError(f"File 1 is not a zip/tsk archive: {target_tsk}")
    if not zipfile.is_zipfile(source_tsk):
        raise ValueError(f"File 2 is not a zip/tsk archive: {source_tsk}")

    with zipfile.ZipFile(target_tsk, "r") as z1, zipfile.ZipFile(source_tsk, "r") as z2:
        existing_names = {i.filename for i in z1.infolist()}
        existing_normalized = {name.rstrip("/") for name in existing_names}
        added_files = 0
        added_dirs = 0

        if output_tsk.exists():
            output_tsk.unlink()

        with zipfile.ZipFile(output_tsk, "w", compression=zipfile.ZIP_DEFLATED) as zout:
            for info in z1.infolist():
                data = b"" if info.is_dir() else z1.read(info.filename)
                zout.writestr(info, data)

            for info in z2.infolist():
                norm_name = info.filename.rstrip("/")
                if norm_name in existing_normalized:
                    continue
                data = b"" if info.is_dir() else z2.read(info.filename)
                zout.writestr(info, data)
                if info.is_dir():
                    added_dirs += 1
                else:
                    added_files += 1
                existing_normalized.add(norm_name)

    return added_dirs, added_files


def create_tsk_from_directory(
    source_dir: Path, output_tsk: Path, force: bool, validation_mode: str = "strict"
) -> None:
    source_dir = source_dir.resolve()
    output_tsk = _normalize_tsk_path(output_tsk).resolve()
    if not source_dir.is_dir():
        raise NotADirectoryError(f"Directory not found: {source_dir}")
    if output_tsk.exists() and not force:
        raise FileExistsError(
            f"File already exists: {output_tsk}. Use --force to overwrite."
        )

    # Ensure fileFormat has current utility version before packing.
    write_file(source_dir / "fileFormat.txt", make_file_format_content())

    issues = validate_task_directory(source_dir, mode=validation_mode)
    if issues:
        raise ValueError("Validation failed:\n" + "\n".join(f"- {msg}" for msg in issues))

    if output_tsk.exists():
        output_tsk.unlink()
    archive_base = output_tsk.with_suffix("")
    produced = shutil.make_archive(
        base_name=str(archive_base),
        format="zip",
        root_dir=str(source_dir),
        base_dir=".",
    )
    Path(produced).replace(output_tsk)


def build_tsk_from_input(
    input_path: Path, output_tsk: Path, force: bool, validation_mode: str = "strict"
) -> None:
    input_path = input_path.resolve()
    with tempfile.TemporaryDirectory(prefix="sibgu_pack_") as tmp:
        staging = Path(tmp) / "staging"
        if input_path.is_dir():
            shutil.copytree(input_path, staging)
        elif input_path.is_file() and zipfile.is_zipfile(input_path):
            staging.mkdir(parents=True, exist_ok=True)
            shutil.unpack_archive(str(input_path), str(staging), format="zip")
        else:
            raise ValueError("input must be a directory or a zip archive.")

        # Support input with a single top-level directory
        children = [p for p in staging.iterdir()]
        candidate = staging
        if len(children) == 1 and children[0].is_dir():
            nested = children[0]
            if (nested / "scenario").exists() and (nested / "fileFormat.txt").exists():
                candidate = nested

        create_tsk_from_directory(
            candidate, output_tsk, force=force, validation_mode=validation_mode
        )


def _load_task_to_staging(input_path: Path, staging: Path) -> Path:
    if input_path.is_dir():
        shutil.copytree(input_path, staging)
    elif input_path.is_file() and zipfile.is_zipfile(input_path):
        staging.mkdir(parents=True, exist_ok=True)
        shutil.unpack_archive(str(input_path), str(staging), format="zip")
    else:
        raise ValueError("input must be a directory or a zip archive.")

    children = [p for p in staging.iterdir()]
    if len(children) == 1 and children[0].is_dir():
        nested = children[0]
        if (nested / "scenario").exists() and (nested / "fileFormat.txt").exists():
            return nested
    return staging


def _copy_tree_files(src_dir: Path, dst_dir: Path) -> int:
    copied = 0
    for item in src_dir.rglob("*"):
        if not item.is_file():
            continue
        rel = item.relative_to(src_dir)
        target = dst_dir / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
        copied += 1
    return copied


def _reset_section_dir(task_root: Path, section: str) -> Path:
    section_dir = task_root / section
    if section_dir.exists():
        shutil.rmtree(section_dir)
    section_dir.mkdir(parents=True, exist_ok=True)
    readme_content = DIR_READMES.get(
        section,
        dedent(
            f"""\
            # {section}

            Directory description for `{section}`.

            ## User Notes
            - ...
            """
        ),
    )
    write_file(section_dir / "README.md", readme_content)
    return section_dir


def put_section_data(
    task_input: Path,
    source_dir: Path,
    section: str,
    output_tsk: Path,
    force: bool,
    validation_mode: str = "strict",
) -> int:
    task_input = task_input.resolve()
    source_dir = source_dir.resolve()
    if not source_dir.is_dir():
        raise NotADirectoryError(f"Source directory not found: {source_dir}")

    with tempfile.TemporaryDirectory(prefix=f"sibgu_put_{section}_") as tmp:
        staging = Path(tmp) / "staging"
        candidate = _load_task_to_staging(task_input, staging)
        section_dir = _reset_section_dir(candidate, section)
        copied_count = _copy_tree_files(source_dir, section_dir)
        create_tsk_from_directory(
            candidate,
            output_tsk,
            force=force,
            validation_mode=validation_mode,
        )
        return copied_count


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Utility to create/validate/update .tsk archives.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py create task1.tsk --force
              createtask.py valid task1.tsk --mode strict
              createtask.py include base.tsk extra.tsk merged.tsk --force
              createtask.py pack ./task_dir packed.tsk --mode strict
              createtask.py template ./task_template --force
              createtask.py put-sims task.tsk ./results updated.tsk --force
              createtask.py put-reports task.tsk ./reports-src updated.tsk --force
              createtask.py put-logs task.tsk ./logs-src updated.tsk --force

            About --force:
              create   : overwrite output .tsk if it already exists.
              include  : overwrite output merged .tsk if it already exists.
              pack     : overwrite output .tsk if it already exists.
              template : delete existing template directory and recreate it.
              put-sims : overwrite output .tsk if it already exists.
              put-reports: overwrite output .tsk if it already exists.
              put-logs : overwrite output .tsk if it already exists.
              valid    : this command does not have --force.
            """
        ),
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"createtask.py {TOOL_VERSION}",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    create_parser = subparsers.add_parser(
        "create",
        help="Create a new .tsk container.",
        description="Create a new .tsk container with default structure and placeholder files.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Example:
              createtask.py create my_task.tsk --force
            """
        ),
    )
    create_parser.add_argument(
        "output",
        type=Path,
        help="Output .tsk file name (for example: task1.tsk).",
    )
    create_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )

    valid_parser = subparsers.add_parser(
        "valid",
        help="Validate an existing .tsk file.",
        description="Validate a .tsk archive (zip) using selected validation mode.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py valid my_task.tsk
              createtask.py valid my_task.tsk --mode basic
            """
        ),
    )
    valid_parser.add_argument(
        "input_tsk",
        type=Path,
        help="Path to .tsk file to validate.",
    )
    valid_parser.add_argument(
        "--mode",
        choices=("basic", "strict"),
        default="strict",
        help="Validation mode: basic (structure), strict (structure + content).",
    )

    include_parser = subparsers.add_parser(
        "include",
        help="Add missing files/directories from tsk-file2 into tsk-file1.",
        description=(
            "Create a new archive by taking tsk1 as a base and adding only missing "
            "entries from tsk2 (without overwriting existing entries)."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Example:
              createtask.py include task_a.tsk task_b.tsk merged.tsk --force
            """
        ),
    )
    include_parser.add_argument("tsk1", type=Path, help="Target .tsk file (file 1).")
    include_parser.add_argument("tsk2", type=Path, help="Source .tsk file (file 2).")
    include_parser.add_argument(
        "output",
        type=Path,
        help="Result .tsk file.",
    )
    include_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )

    pack_parser = subparsers.add_parser(
        "pack",
        help=(
            "Create .tsk from a zip archive or a directory. "
            "If validation fails, no output .tsk is created."
        ),
        description=(
            "Build a .tsk archive from an input directory or zip archive.\n"
            "The input is validated first; output is created only if validation passes."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py pack ./task_dir output.tsk --mode strict
              createtask.py pack source.zip output.tsk --mode basic --force
            """
        ),
    )
    pack_parser.add_argument(
        "input_path",
        type=Path,
        help="Path to a zip archive or directory with task tree.",
    )
    pack_parser.add_argument(
        "output",
        type=Path,
        help="Result .tsk file.",
    )
    pack_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )
    pack_parser.add_argument(
        "--mode",
        choices=("basic", "strict"),
        default="strict",
        help="Validation mode before packing: basic or strict.",
    )

    template_parser = subparsers.add_parser(
        "template",
        help=(
            "Generate a task template as a directory "
            "(fill files manually, then use pack)."
        ),
        description="Generate a task directory template with predefined structure.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Example:
              createtask.py template ./my_template_dir --force
            """
        ),
    )
    template_parser.add_argument(
        "output_dir",
        type=Path,
        help="Path to generated template directory.",
    )
    template_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Recreate template directory if it already exists (delete and create again).",
    )

    put_sims_parser = subparsers.add_parser(
        "put-sims",
        help="Recursively copy files into task `sims` directory.",
        description=(
            "Copy files recursively from source directory into task `sims`.\n"
            "Task input can be a directory or a .tsk/.zip archive."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py put-sims task.tsk ./sim_results updated.tsk --force
              createtask.py put-sims ./task_dir ./sim_results updated.tsk --mode basic
            """
        ),
    )
    put_sims_parser.add_argument("task_input", type=Path, help="Task input (directory or .tsk/.zip).")
    put_sims_parser.add_argument("source_dir", type=Path, help="Directory with simulation results to copy.")
    put_sims_parser.add_argument("output", type=Path, help="Result .tsk file.")
    put_sims_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )
    put_sims_parser.add_argument(
        "--mode",
        choices=("basic", "strict"),
        default="strict",
        help="Validation mode before writing output: basic or strict.",
    )

    put_reports_parser = subparsers.add_parser(
        "put-reports",
        help="Recursively copy files into task `reports` directory.",
        description=(
            "Copy files recursively from source directory into task `reports`.\n"
            "Task input can be a directory or a .tsk/.zip archive."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py put-reports task.tsk ./report_files updated.tsk --force
              createtask.py put-reports ./task_dir ./report_files updated.tsk --mode strict
            """
        ),
    )
    put_reports_parser.add_argument("task_input", type=Path, help="Task input (directory or .tsk/.zip).")
    put_reports_parser.add_argument("source_dir", type=Path, help="Directory with report files to copy.")
    put_reports_parser.add_argument("output", type=Path, help="Result .tsk file.")
    put_reports_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )
    put_reports_parser.add_argument(
        "--mode",
        choices=("basic", "strict"),
        default="strict",
        help="Validation mode before writing output: basic or strict.",
    )

    put_logs_parser = subparsers.add_parser(
        "put-logs",
        help="Recursively copy files into task `logs` directory.",
        description=(
            "Copy files recursively from source directory into task `logs`.\n"
            "Task input can be a directory or a .tsk/.zip archive."
        ),
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              createtask.py put-logs task.tsk ./log_files updated.tsk --force
              createtask.py put-logs ./task_dir ./log_files updated.tsk --mode strict
            """
        ),
    )
    put_logs_parser.add_argument("task_input", type=Path, help="Task input (directory or .tsk/.zip).")
    put_logs_parser.add_argument("source_dir", type=Path, help="Directory with log files to copy.")
    put_logs_parser.add_argument("output", type=Path, help="Result .tsk file.")
    put_logs_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        help="Overwrite output .tsk if it already exists.",
    )
    put_logs_parser.add_argument(
        "--mode",
        choices=("basic", "strict"),
        default="strict",
        help="Validation mode before writing output: basic or strict.",
    )

    if len(sys.argv) == 1:
        parser.print_help()
        raise SystemExit(0)

    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "create":
            create_tsk_archive(args.output, force=args.force)
            print(f"Done: archive created {_normalize_tsk_path(args.output).resolve()}")
            return 0

        if args.command == "valid":
            issues = validate_tsk_file(args.input_tsk, mode=args.mode)
            if issues:
                print("File is invalid:")
                for item in issues:
                    print(f"- {item}")
                return 2
            print(f"OK: {_normalize_tsk_path(args.input_tsk).resolve()} is valid.")
            return 0

        if args.command == "include":
            added_dirs, added_files = include_missing_entries(
                args.tsk1, args.tsk2, args.output, force=args.force
            )
            print(
                "Done: merged archive created "
                f"{_normalize_tsk_path(args.output).resolve()}"
            )
            print(f"Added directories: {added_dirs}, files: {added_files}")
            return 0

        if args.command == "pack":
            build_tsk_from_input(
                args.input_path, args.output, force=args.force, validation_mode=args.mode
            )
            print(
                "Done: archive created and validated "
                f"{_normalize_tsk_path(args.output).resolve()}"
            )
            return 0

        if args.command == "template":
            out_dir = create_task_template_dir(args.output_dir, force=args.force)
            print(f"Done: template directory created {out_dir}")
            print("Fill files and package into .tsk using the pack command.")
            return 0

        if args.command == "put-sims":
            copied = put_section_data(
                task_input=args.task_input,
                source_dir=args.source_dir,
                section="sims",
                output_tsk=args.output,
                force=args.force,
                validation_mode=args.mode,
            )
            print(f"Done: copied {copied} file(s) into `sims` and created {_normalize_tsk_path(args.output).resolve()}")
            return 0

        if args.command == "put-reports":
            copied = put_section_data(
                task_input=args.task_input,
                source_dir=args.source_dir,
                section="reports",
                output_tsk=args.output,
                force=args.force,
                validation_mode=args.mode,
            )
            print(f"Done: copied {copied} file(s) into `reports` and created {_normalize_tsk_path(args.output).resolve()}")
            return 0

        if args.command == "put-logs":
            copied = put_section_data(
                task_input=args.task_input,
                source_dir=args.source_dir,
                section="logs",
                output_tsk=args.output,
                force=args.force,
                validation_mode=args.mode,
            )
            print(f"Done: copied {copied} file(s) into `logs` and created {_normalize_tsk_path(args.output).resolve()}")
            return 0

        raise ValueError(f"Unknown command: {args.command}")
    except Exception as exc:  # pragma: no cover - CLI error path
        print(f"Error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
