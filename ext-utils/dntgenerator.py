#!/usr/bin/env python3
"""
Generate a directed network graph (.dot) and package it into a .dnt file.
The file is a zip archive with the following structure:

Archive layout:
  <output>.dnt
    README.md
    db_template.sql
    net/
      graph.dot
      ... optional referenced files (trajectory/activity/fading)

Input data source:
  PostgreSQL tables (default: public.nodes, public.routes)

Expected columns (minimal):
  Table "nodes":
    - id
    - hs_type                 (SAT | HAP | GW | UT)
    - lat, lon, alt
    - trajectory_file         (optional)
    - trajectory_content      (optional text content for trajectory_file)

  Table "routes":
    - src_id, dst_id
    - hs_len
    - hs_beam
    - hs_center_freq
    - hs_bandwidth
    - hs_beam_dir             ("lat,lon,alt" or trajectory filename)
    - hs_fading               (number or filename)
    - hs_activity             ("on", "off", or filename)
    - beam_dir_content        (optional text content for hs_beam_dir if it is filename)
    - fading_content          (optional text content for hs_fading if it is filename)
    - activity_content        (optional text content for hs_activity if it is filename)

README / quick start:
  1) Generate template graph:
     ./dntgenerator.py --template --output my.dnt

  2) Generate from PostgreSQL via DSN:
     ./dntgenerator.py \
       --dsn "host=127.0.0.1 port=5432 dbname=hapnet user=igor password=secret" \
       --schema public \
       --nodes-table nodes \
       --routes-table routes \
       --output my.dnt

  3) Generate from PostgreSQL via separate flags:
     ./dntgenerator.py \
       --db-host 127.0.0.1 --db-port 5432 \
       --db-name hapnet --db-user igor --db-password secret \
       --schema public --nodes-table nodes --routes-table routes \
       --output my.dnt
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path
from textwrap import dedent


TOOL_VERSION = "1.0.0"
ALLOWED_TYPES = {"SAT", "HAP", "GW", "UT"}
DEFAULT_GRAPH_NAME = "hs_net"


@dataclass
class NodeRow:
    node_id: str
    hs_type: str
    lat: float
    lon: float
    alt: float
    trajectory_file: str | None
    trajectory_content: str | None


@dataclass
class RouteRow:
    src_id: str
    dst_id: str
    hs_len: float
    hs_beam: int
    hs_center_freq: float
    hs_bandwidth: float
    hs_beam_dir: str
    hs_fading: str
    hs_activity: str
    beam_dir_content: str | None
    fading_content: str | None
    activity_content: str | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Synthesize a directed network graph from PostgreSQL and pack it into .dnt.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              1) Generate template archive:
                 %(prog)s --template --output my.dnt

              2) Generate from PostgreSQL via DSN:
                 %(prog)s \\
                   --dsn "host=127.0.0.1 port=5432 dbname=hapnet user=igor password=secret" \\
                   --schema public \\
                   --nodes-table nodes \\
                   --routes-table routes \\
                   --output my.dnt

              3) Generate from PostgreSQL via separate flags:
                 %(prog)s \\
                   --db-host 127.0.0.1 --db-port 5432 \\
                   --db-name hapnet --db-user igor --db-password secret \\
                   --schema public --nodes-table nodes --routes-table routes \\
                   --output my.dnt

              4) Build a static snapshot from an existing DNT:
                 %(prog)s --snapshot source.dnt 10.500000

              5) Build another snapshot (microsecond precision):
                 %(prog)s --snapshot source.dnt 123.000001

              6) Snapshot behavior for HS_ACTIVITY:
                 if activity resolves to "off" at snapshot time,
                 the corresponding edge is removed from graph.dot
            """
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Output .dnt archive path (for example: my.dnt).",
    )
    parser.add_argument(
        "--template",
        action="store_true",
        help="Generate a template .dnt archive instead of reading PostgreSQL.",
    )
    parser.add_argument("--dsn", default="", help="Full PostgreSQL DSN string.")
    parser.add_argument("--db-host", default="localhost", help="PostgreSQL host.")
    parser.add_argument("--db-port", type=int, default=5432, help="PostgreSQL port.")
    parser.add_argument("--db-name", default="", help="PostgreSQL database name.")
    parser.add_argument("--db-user", default="", help="PostgreSQL user.")
    parser.add_argument("--db-password", default="", help="PostgreSQL password.")
    parser.add_argument("--schema", default="public", help="PostgreSQL schema (default: public).")
    parser.add_argument("--nodes-table", default="nodes", help="Nodes table name.")
    parser.add_argument("--routes-table", default="routes", help="Routes table name.")
    parser.add_argument(
        "--graph-name",
        default=DEFAULT_GRAPH_NAME,
        help="DOT graph name (default: hs_net).",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite output archive if it already exists.",
    )
    parser.add_argument(
        "--validate",
        type=Path,
        default=None,
        help="Validate an existing .dnt archive and exit.",
    )
    parser.add_argument(
        "--snapshot",
        nargs=2,
        metavar=("INPUT_DNT", "TIME_SECONDS"),
        default=None,
        help=(
            "Build a static snapshot .dnt from INPUT_DNT at TIME_SECONDS "
            "(supports microsecond precision)."
        ),
    )
    parser.add_argument(
        "--version",
        action="version",
        version=f"dntgenerator.py {TOOL_VERSION}",
    )
    return parser.parse_args()


def _normalize_dnt_path(path: Path) -> Path:
    return path if path.suffix.lower() == ".dnt" else path.with_suffix(".dnt")


def validate_dnt_file(input_path: Path) -> list[str]:
    errors: list[str] = []
    dnt_path = _normalize_dnt_path(input_path).resolve()
    if not dnt_path.exists():
        return [f"File not found: {dnt_path}"]
    if not zipfile.is_zipfile(dnt_path):
        return [f"File is not a .dnt (zip) archive: {dnt_path}"]

    required_files = {"README.md", "db_template.sql", "net/graph.dot"}
    try:
        with zipfile.ZipFile(dnt_path, "r") as zf:
            bad = zf.testzip()
            if bad is not None:
                errors.append(f"Corrupted archive entry: {bad}")
                return errors

            names = {info.filename.rstrip("/") for info in zf.infolist() if info.filename}
            for req in sorted(required_files):
                if req not in names:
                    errors.append(f"Missing required file: {req}")

            # Lightweight semantic checks for interoperability with other generators.
            if "net/graph.dot" in names:
                dot_text = zf.read("net/graph.dot").decode("utf-8", errors="replace")
                if "digraph" not in dot_text:
                    errors.append("net/graph.dot does not look like a DOT digraph.")
    except zipfile.BadZipFile:
        return [f"Unable to read zip archive: {dnt_path}"]
    return errors


def _split_attr_items(attr_blob: str) -> list[str]:
    items: list[str] = []
    token: list[str] = []
    in_quotes = False
    escaped = False
    for ch in attr_blob:
        if escaped:
            token.append(ch)
            escaped = False
            continue
        if ch == "\\" and in_quotes:
            token.append(ch)
            escaped = True
            continue
        if ch == '"':
            in_quotes = not in_quotes
            token.append(ch)
            continue
        if ch == "," and not in_quotes:
            part = "".join(token).strip()
            if part:
                items.append(part)
            token = []
            continue
        token.append(ch)
    part = "".join(token).strip()
    if part:
        items.append(part)
    return items


def _unquote_dot_value(value: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        inner = text[1:-1]
        return inner.replace('\\"', '"').replace("\\\\", "\\")
    return text


def _parse_dot_attrs(attr_blob: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for item in _split_attr_items(attr_blob):
        if "=" not in item:
            continue
        key, raw_val = item.split("=", 1)
        out.append((key.strip(), _unquote_dot_value(raw_val)))
    return out


def _interp_linear(x0: float, y0: float, x1: float, y1: float, x: float) -> float:
    if x1 == x0:
        return y0
    return y0 + (y1 - y0) * ((x - x0) / (x1 - x0))


def _interp_series(times: list[float], values: list[float], t: float) -> float:
    if not times:
        raise ValueError("Interpolation series is empty.")
    if t <= times[0]:
        return values[0]
    if t >= times[-1]:
        return values[-1]
    for i in range(1, len(times)):
        if t <= times[i]:
            return _interp_linear(times[i - 1], values[i - 1], times[i], values[i], t)
    return values[-1]


def _read_noncomment_rows(path: Path) -> list[list[str]]:
    rows: list[list[str]] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("%") or line.startswith("#"):
            continue
        rows.append(line.split())
    return rows


def _resolve_trajectory_csv(path: Path, t: float) -> str:
    rows = _read_noncomment_rows(path)
    if not rows:
        raise ValueError(f"Trajectory file is empty: {path}")
    times: list[float] = []
    lats: list[float] = []
    lons: list[float] = []
    alts: list[float] = []
    for row in rows:
        if len(row) < 4:
            raise ValueError(f"Trajectory row must be: time lat lon alt in {path}")
        times.append(float(row[0]))
        lats.append(float(row[1]))
        lons.append(float(row[2]))
        alts.append(float(row[3]))
    lat = _interp_series(times, lats, t)
    lon = _interp_series(times, lons, t)
    alt = _interp_series(times, alts, t)
    return ",".join((_format_number(lat), _format_number(lon), _format_number(alt)))


def _resolve_scalar_from_file(path: Path, t: float) -> str:
    rows = _read_noncomment_rows(path)
    if not rows:
        raise ValueError(f"Scalar time-series file is empty: {path}")
    times: list[float] = []
    values: list[float] = []
    for row in rows:
        if len(row) < 2:
            raise ValueError(f"Scalar row must be: time value in {path}")
        times.append(float(row[0]))
        values.append(float(row[1]))
    return _format_number(_interp_series(times, values, t))


def _state_to_float(state: str) -> float:
    s = state.strip().lower()
    if s in {"on", "1", "true"}:
        return 1.0
    if s in {"off", "0", "false", "of"}:
        return 0.0
    raise ValueError(f"Unsupported activity state: {state}")


def _resolve_activity_from_file(path: Path, t: float) -> str:
    rows = _read_noncomment_rows(path)
    if not rows:
        raise ValueError(f"Activity file is empty: {path}")
    pairs: list[tuple[float, float]] = []
    for row in rows:
        if len(row) < 2:
            raise ValueError(f"Activity row must be: time state in {path}")
        pairs.append((float(row[0]), _state_to_float(row[1])))
    pairs.sort(key=lambda x: x[0])
    # Step-wise behavior: use the state from the latest row at or before snapshot time.
    selected = pairs[0][1]
    for ts, state in pairs:
        if ts <= t:
            selected = state
        else:
            break
    return "on" if selected >= 0.5 else "off"


def _format_dot_attr(key: str, value: str) -> str:
    if key in {"HS_LEN", "HS_BEAM", "HS_CENTER_FREQ", "HS_BANDWIDTH", "HS_FADING"} and _is_numeric_token(value):
        return f"{key}={value}"
    if key == "HS_ACTIVITY" and value in {"on", "off"}:
        return f"{key}={value}"
    return f"{key}={_dot_quote(value)}"


def _snapshot_graph(dot_path: Path, net_dir: Path, t: float) -> None:
    node_re = re.compile(r'^\s*"([^"]+)"\s*\[(.*)\]\s*;\s*$')
    edge_re = re.compile(r'^\s*"([^"]+)"\s*->\s*"([^"]+)"\s*\[(.*)\]\s*;\s*$')
    out_lines: list[str] = []

    for raw in dot_path.read_text(encoding="utf-8", errors="replace").splitlines():
        edge_m = edge_re.match(raw)
        if edge_m:
            src, dst, attrs_blob = edge_m.group(1), edge_m.group(2), edge_m.group(3)
            attrs = _parse_dot_attrs(attrs_blob)
            new_attrs: list[tuple[str, str]] = []
            drop_edge = False
            for key, val in attrs:
                new_val = val
                if key == "HS_BEAM_DIR" and not _is_lat_lon_alt_csv(val):
                    new_val = _resolve_trajectory_csv(net_dir / _sanitize_filename(val), t)
                elif key == "HS_FADING" and not _is_numeric_token(val):
                    new_val = _resolve_scalar_from_file(net_dir / _sanitize_filename(val), t)
                elif key == "HS_ACTIVITY":
                    v = val.strip().lower()
                    if v not in {"on", "off"}:
                        v = _resolve_activity_from_file(net_dir / _sanitize_filename(val), t)
                    new_val = v
                    if v == "off":
                        drop_edge = True
                new_attrs.append((key, new_val))
            if not drop_edge:
                attrs_text = ", ".join(_format_dot_attr(k, v) for k, v in new_attrs)
                out_lines.append(f'  "{src}" -> "{dst}" [{attrs_text}];')
            continue

        node_m = node_re.match(raw)
        if node_m:
            node_id, attrs_blob = node_m.group(1), node_m.group(2)
            attrs = _parse_dot_attrs(attrs_blob)
            new_attrs: list[tuple[str, str]] = []
            for key, val in attrs:
                new_val = val
                if key == "HS_POS" and not _is_lat_lon_alt_csv(val):
                    new_val = _resolve_trajectory_csv(net_dir / _sanitize_filename(val), t)
                new_attrs.append((key, new_val))
            attrs_text = ", ".join(_format_dot_attr(k, v) for k, v in new_attrs)
            out_lines.append(f'  "{node_id}" [{attrs_text}];')
            continue

        out_lines.append(raw)

    dot_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")


def generate_snapshot_archive(input_dnt: Path, snapshot_time: float) -> Path:
    issues = validate_dnt_file(input_dnt)
    if issues:
        raise ValueError("Input .dnt is invalid:\n" + "\n".join(f"- {x}" for x in issues))

    source = _normalize_dnt_path(input_dnt).resolve()
    time_label = f"{snapshot_time:.6f}".rstrip("0").rstrip(".")
    output = source.with_name(f"{source.stem}_{time_label}.dnt")

    with tempfile.TemporaryDirectory(prefix="sibgu_dnt_snapshot_") as tmp:
        root = Path(tmp) / "dnt"
        root.mkdir(parents=True, exist_ok=True)
        shutil.unpack_archive(str(source), str(root), format="zip")
        net_dir = root / "net"
        dot_path = net_dir / "graph.dot"
        if not dot_path.is_file():
            raise FileNotFoundError(f"Missing graph.dot in archive: {source}")
        _snapshot_graph(dot_path, net_dir, snapshot_time)
        return _pack_dnt(output, root, force=False)


def _dot_quote(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _format_number(value: float) -> str:
    if math.isfinite(value) and float(value).is_integer():
        return str(int(value))
    return f"{value:.12g}"


def _is_numeric_token(text: str) -> bool:
    return re.fullmatch(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", text.strip()) is not None


def _is_lat_lon_alt_csv(text: str) -> bool:
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 3:
        return False
    return all(_is_numeric_token(p) for p in parts)


def _sanitize_filename(name: str) -> str:
    trimmed = name.strip().replace("\\", "/")
    safe = Path(trimmed).name
    if not safe:
        raise ValueError(f"Invalid filename value: '{name}'")
    return safe


def _column_names(cursor, table: str, schema: str) -> set[str]:
    cursor.execute(
        """
        SELECT column_name
        FROM information_schema.columns
        WHERE table_schema = %s AND table_name = %s
        """,
        (schema, table),
    )
    return {row[0] for row in cursor.fetchall()}


def _run_query_as_dicts(conn, query: str) -> list[dict]:
    try:
        # psycopg v3
        from psycopg.rows import dict_row  # type: ignore

        with conn.cursor(row_factory=dict_row) as cur:
            cur.execute(query)
            return list(cur.fetchall())
    except ImportError:
        # psycopg2 fallback
        try:
            from psycopg2.extras import RealDictCursor  # type: ignore

            with conn.cursor(cursor_factory=RealDictCursor) as cur:
                cur.execute(query)
                return list(cur.fetchall())
        except ImportError as exc:  # pragma: no cover
            raise RuntimeError(
                "Unable to fetch rows as dictionaries. Install psycopg (v3) or psycopg2."
            ) from exc


def _connect_postgres(args: argparse.Namespace):
    if args.dsn:
        dsn = args.dsn
    else:
        if not args.db_name or not args.db_user:
            raise ValueError("--db-name and --db-user are required when --dsn is not provided.")
        dsn = (
            f"host={args.db_host} port={args.db_port} dbname={args.db_name} "
            f"user={args.db_user} password={args.db_password}"
        )

    try:
        import psycopg  # type: ignore

        return psycopg.connect(dsn)
    except ImportError:
        try:
            import psycopg2  # type: ignore

            return psycopg2.connect(dsn)
        except ImportError as exc:  # pragma: no cover
            raise RuntimeError(
                "Cannot connect to PostgreSQL. Install psycopg (preferred) or psycopg2, "
                "and verify connection parameters."
            ) from exc


def _build_nodes(conn, schema: str, nodes_table: str) -> list[NodeRow]:
    with conn.cursor() as cur:
        cols = _column_names(cur, nodes_table, schema)

    required = {"id", "hs_type", "lat", "lon", "alt"}
    missing = sorted(required - cols)
    if missing:
        raise ValueError(f"Missing required node columns in {schema}.{nodes_table}: {missing}")

    def has(name: str) -> bool:
        return name in cols

    query = dedent(
        f"""
        SELECT
          id,
          hs_type,
          lat,
          lon,
          alt,
          {"trajectory_file" if has("trajectory_file") else "NULL::text AS trajectory_file"},
          {"trajectory_content" if has("trajectory_content") else "NULL::text AS trajectory_content"}
        FROM "{schema}"."{nodes_table}"
        ORDER BY id
        """
    )
    rows = _run_query_as_dicts(conn, query)
    out: list[NodeRow] = []
    seen: set[str] = set()
    for row in rows:
        node_id = str(row["id"])
        if node_id in seen:
            raise ValueError(f"Duplicate node id: {node_id}")
        seen.add(node_id)
        hs_type = str(row["hs_type"]).upper()
        if hs_type not in ALLOWED_TYPES:
            raise ValueError(f"Invalid HS_TYPE for node '{node_id}': {hs_type}")
        out.append(
            NodeRow(
                node_id=node_id,
                hs_type=hs_type,
                lat=float(row["lat"]),
                lon=float(row["lon"]),
                alt=float(row["alt"]),
                trajectory_file=None if row["trajectory_file"] is None else str(row["trajectory_file"]),
                trajectory_content=(
                    None if row["trajectory_content"] is None else str(row["trajectory_content"])
                ),
            )
        )
    return out


def _build_routes(conn, schema: str, routes_table: str) -> list[RouteRow]:
    with conn.cursor() as cur:
        cols = _column_names(cur, routes_table, schema)

    required = {
        "src_id",
        "dst_id",
        "hs_len",
        "hs_beam",
        "hs_center_freq",
        "hs_bandwidth",
        "hs_beam_dir",
        "hs_fading",
        "hs_activity",
    }
    missing = sorted(required - cols)
    if missing:
        raise ValueError(f"Missing required route columns in {schema}.{routes_table}: {missing}")

    def has(name: str) -> bool:
        return name in cols

    query = dedent(
        f"""
        SELECT
          src_id,
          dst_id,
          hs_len,
          hs_beam,
          hs_center_freq,
          hs_bandwidth,
          hs_beam_dir,
          hs_fading,
          hs_activity,
          {"beam_dir_content" if has("beam_dir_content") else "NULL::text AS beam_dir_content"},
          {"fading_content" if has("fading_content") else "NULL::text AS fading_content"},
          {"activity_content" if has("activity_content") else "NULL::text AS activity_content"}
        FROM "{schema}"."{routes_table}"
        ORDER BY src_id, dst_id
        """
    )
    rows = _run_query_as_dicts(conn, query)
    out: list[RouteRow] = []
    for row in rows:
        out.append(
            RouteRow(
                src_id=str(row["src_id"]),
                dst_id=str(row["dst_id"]),
                hs_len=float(row["hs_len"]),
                hs_beam=int(row["hs_beam"]),
                hs_center_freq=float(row["hs_center_freq"]),
                hs_bandwidth=float(row["hs_bandwidth"]),
                hs_beam_dir=str(row["hs_beam_dir"]),
                hs_fading=str(row["hs_fading"]),
                hs_activity=str(row["hs_activity"]),
                beam_dir_content=None if row["beam_dir_content"] is None else str(row["beam_dir_content"]),
                fading_content=None if row["fading_content"] is None else str(row["fading_content"]),
                activity_content=None if row["activity_content"] is None else str(row["activity_content"]),
            )
        )
    return out


def _render_dot(
    graph_name: str,
    nodes: list[NodeRow],
    routes: list[RouteRow],
    net_dir: Path,
) -> None:
    node_ids = {n.node_id for n in nodes}
    for route in routes:
        if route.src_id not in node_ids:
            raise ValueError(f"Route source node does not exist: {route.src_id}")
        if route.dst_id not in node_ids:
            raise ValueError(f"Route destination node does not exist: {route.dst_id}")

    lines: list[str] = [f"digraph {graph_name} {{", "  rankdir=LR;"]

    for node in nodes:
        hs_pos = ",".join((_format_number(node.lat), _format_number(node.lon), _format_number(node.alt)))
        if node.trajectory_file and node.trajectory_file.strip():
            trajectory_name = _sanitize_filename(node.trajectory_file)
            hs_pos = trajectory_name
            content = (node.trajectory_content or "").strip()
            if content:
                (net_dir / trajectory_name).write_text(content.rstrip() + "\n", encoding="utf-8")
            elif not (net_dir / trajectory_name).exists():
                (net_dir / trajectory_name).write_text(
                    "% time lat lon alt\n0.000 0.0 0.0 0.0\n",
                    encoding="utf-8",
                )

        attrs = (
            f'HS_TYPE={_dot_quote(node.hs_type)}, '
            f'HS_POS={_dot_quote(hs_pos)}'
        )
        lines.append(f"  {_dot_quote(node.node_id)} [{attrs}];")

    for route in routes:
        hs_beam_dir = route.hs_beam_dir.strip()
        hs_fading = route.hs_fading.strip()
        hs_activity = route.hs_activity.strip()

        if not _is_lat_lon_alt_csv(hs_beam_dir):
            beam_dir_name = _sanitize_filename(hs_beam_dir)
            hs_beam_dir = beam_dir_name
            content = (route.beam_dir_content or "").strip()
            if content:
                (net_dir / beam_dir_name).write_text(content.rstrip() + "\n", encoding="utf-8")
            elif not (net_dir / beam_dir_name).exists():
                (net_dir / beam_dir_name).write_text(
                    "% time lat lon alt\n0.000 0.0 0.0 0.0\n",
                    encoding="utf-8",
                )

        if not _is_numeric_token(hs_fading):
            fading_name = _sanitize_filename(hs_fading)
            hs_fading = fading_name
            content = (route.fading_content or "").strip()
            if content:
                (net_dir / fading_name).write_text(content.rstrip() + "\n", encoding="utf-8")
            elif not (net_dir / fading_name).exists():
                (net_dir / fading_name).write_text(
                    "% time fading\n0.000 0.0\n",
                    encoding="utf-8",
                )

        hs_activity_lc = hs_activity.lower()
        if hs_activity_lc in {"on", "off"}:
            hs_activity = hs_activity_lc
        else:
            activity_name = _sanitize_filename(hs_activity)
            hs_activity = activity_name
            content = (route.activity_content or "").strip()
            if content:
                (net_dir / activity_name).write_text(content.rstrip() + "\n", encoding="utf-8")
            elif not (net_dir / activity_name).exists():
                (net_dir / activity_name).write_text(
                    "% time state\n0.000 on\n1.000 off\n",
                    encoding="utf-8",
                )

        attrs = ", ".join(
            [
                f"HS_LEN={_format_number(route.hs_len)}",
                f"HS_BEAM={route.hs_beam}",
                f"HS_BEAM_DIR={_dot_quote(hs_beam_dir)}",
                f"HS_CENTER_FREQ={_format_number(route.hs_center_freq)}",
                f"HS_BANDWIDTH={_format_number(route.hs_bandwidth)}",
                f"HS_FADING={_dot_quote(hs_fading) if not _is_numeric_token(hs_fading) else hs_fading}",
                f"HS_ACTIVITY={_dot_quote(hs_activity) if hs_activity not in {'on', 'off'} else hs_activity}",
            ]
        )
        lines.append(f"  {_dot_quote(route.src_id)} -> {_dot_quote(route.dst_id)} [{attrs}];")

    lines.append("}")
    (net_dir / "graph.dot").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_template(net_dir: Path) -> None:
    (net_dir / "README.md").write_text(
        dedent(
            """\
            # net template

            This folder stores a directed graph in DOT format and optional files
            referenced by node/edge attributes:

            - `graph.dot`               : directed graph
            - `*.trajectory.txt`        : node position trajectories (`time lat lon alt`)
            - `*.beam_trajectory.txt`   : edge beam direction trajectories (`time lat lon alt`)
            - `*.activity.txt`          : edge activity (`time state`)
            - `*.fading.txt`            : edge fading values (format chosen by your model)
            """
        ),
        encoding="utf-8",
    )
    (net_dir / "sample_trajectory.txt").write_text(
        "% time lat lon alt\n"
        "0.000 56.011944444 92.967913547 20000.000\n"
        "1.000 56.012034376 92.967913413 20000.000\n",
        encoding="utf-8",
    )
    (net_dir / "sample_activity.txt").write_text(
        "% time state\n0.000 on\n1.000 off\n2.000 on\n",
        encoding="utf-8",
    )
    (net_dir / "sample_beam_trajectory.txt").write_text(
        "% time lat lon alt\n"
        "0.000 56.011944444 92.967913547 20000.000\n"
        "1.000 56.012034376 92.967913413 20000.000\n",
        encoding="utf-8",
    )
    (net_dir / "graph.dot").write_text(
        dedent(
            """\
            digraph hs_net {
              rankdir=LR;
              "sat1" [HS_TYPE="SAT", HS_POS="sample_trajectory.txt"];
              "gw1"  [HS_TYPE="GW",  HS_POS="55.0,92.9,300"];
              "sat1" -> "gw1" [HS_LEN=450000, HS_BEAM=1, HS_BEAM_DIR="sample_beam_trajectory.txt", HS_CENTER_FREQ=2.1e9, HS_BANDWIDTH=2.0e7, HS_FADING=0.0001, HS_ACTIVITY="sample_activity.txt"];
            }
            """
        ),
        encoding="utf-8",
    )
    _write_sql_template(net_dir.parent)


def _write_sql_template(root_dir: Path) -> None:
    (root_dir / "db_template.sql").write_text(
        dedent(
            """\
            -- Minimal schema template for dntgenerator.py
            CREATE TABLE IF NOT EXISTS nodes (
              id TEXT PRIMARY KEY,
              hs_type TEXT NOT NULL,
              lat DOUBLE PRECISION NOT NULL,
              lon DOUBLE PRECISION NOT NULL,
              alt DOUBLE PRECISION NOT NULL,
              trajectory_file TEXT,
              trajectory_content TEXT
            );

            CREATE TABLE IF NOT EXISTS routes (
              src_id TEXT NOT NULL,
              dst_id TEXT NOT NULL,
              hs_len DOUBLE PRECISION NOT NULL,
              hs_beam INTEGER NOT NULL,
              hs_beam_dir TEXT NOT NULL,
              hs_center_freq DOUBLE PRECISION NOT NULL,
              hs_bandwidth DOUBLE PRECISION NOT NULL,
              hs_fading TEXT NOT NULL,
              hs_activity TEXT NOT NULL,
              beam_dir_content TEXT,
              fading_content TEXT,
              activity_content TEXT
            );
            """
        ),
        encoding="utf-8",
    )


def _write_root_readme(root_dir: Path) -> None:
    (root_dir / "README.md").write_text(
        dedent(
            """\
            # Dynamic Network Topology Archive (.dnt)

            This archive stores a dynamic network topology.

            ## Generator
            Generated by `dntgenerator.py` version: TOOL_VERSION_PLACEHOLDER

            ## Archive contents
            - `net/graph.dot` - a directed network graph (nodes and directed edges).
            - `net/*.txt` - referenced node trajectory, beam direction trajectory, link activity, and/or fading files.
            - `db_template.sql` - SQL schema template for `nodes` and `routes` tables.

            ## SQL file description
            The `db_template.sql` file in the archive root describes the minimal
            PostgreSQL table structure expected by `dntgenerator.py`.
            """
        ).replace("TOOL_VERSION_PLACEHOLDER", TOOL_VERSION),
        encoding="utf-8",
    )


def _pack_dnt(output_path: Path, source_dir: Path, force: bool) -> Path:
    out = _normalize_dnt_path(output_path).resolve()
    if out.exists() and not force:
        raise FileExistsError(f"File already exists: {out}. Use --force to overwrite.")
    if out.exists():
        out.unlink()

    base = out.with_suffix("")
    produced = shutil.make_archive(str(base), "zip", root_dir=str(source_dir), base_dir=".")
    Path(produced).replace(out)
    return out


def generate_template_archive(output_path: Path, force: bool) -> Path:
    with tempfile.TemporaryDirectory(prefix="sibgu_net_template_") as tmp:
        root = Path(tmp)
        net_dir = root / "net"
        net_dir.mkdir(parents=True, exist_ok=True)
        _write_template(net_dir)
        _write_root_readme(root)
        return _pack_dnt(output_path, root, force=force)


def generate_from_db(args: argparse.Namespace) -> Path:
    with _connect_postgres(args) as conn:
        nodes = _build_nodes(conn, args.schema, args.nodes_table)
        routes = _build_routes(conn, args.schema, args.routes_table)

    if not nodes:
        raise ValueError("nodes query returned zero rows.")

    with tempfile.TemporaryDirectory(prefix="sibgu_net_graph_") as tmp:
        root = Path(tmp)
        net_dir = root / "net"
        net_dir.mkdir(parents=True, exist_ok=True)
        _render_dot(args.graph_name, nodes, routes, net_dir)
        _write_sql_template(root)
        _write_root_readme(root)
        return _pack_dnt(args.output, root, force=args.force)


def main() -> int:
    args = parse_args()
    try:
        if args.validate is not None:
            issues = validate_dnt_file(args.validate)
            if issues:
                print("Invalid .dnt archive:")
                for item in issues:
                    print(f"- {item}")
                return 2
            print(f"OK: {_normalize_dnt_path(args.validate).resolve()} is valid.")
            return 0

        if args.snapshot is not None:
            snap_input = Path(args.snapshot[0])
            try:
                snap_time = float(args.snapshot[1])
            except ValueError as exc:
                raise ValueError(f"Invalid snapshot time value: {args.snapshot[1]}") from exc
            archive = generate_snapshot_archive(snap_input, snap_time)
            print(f"Done: snapshot archive created {archive}")
            return 0

        if args.output is None:
            raise ValueError("--output is required unless --validate or --snapshot is used.")

        if args.template:
            archive = generate_template_archive(args.output, force=args.force)
            print(f"Done: template archive created {archive}")
            return 0

        archive = generate_from_db(args)
        print(f"Done: network archive created {archive}")
        return 0
    except Exception as exc:
        print(f"Error: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

