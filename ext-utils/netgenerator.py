#!/usr/bin/env python3
"""
Generate a directed network graph (.dot) and package it into a .tpl archive.

Archive layout:
  <output>.tpl
    net/
      graph.dot
      ... optional referenced files (trajectory/activity/fading)

Input data source:
  PostgreSQL tables (default: public.nodes, public.routes)

Expected columns (minimal):
  nodes:
    - id
    - hs_type                 (SAT | HAP | GW | UT)
    - lat, lon, alt
    - mob_vx, mob_vy, mob_vz (optional if trajectory_file is set)
    - trajectory_file         (optional)
    - trajectory_content      (optional text content for trajectory_file)

  routes:
    - src_id, dst_id
    - hs_len
    - hs_beam
    - hs_center_freq
    - hs_bandwidth
    - hs_fading               (number or filename)
    - hs_activity             (0, 1, or filename)
    - fading_content          (optional text content for hs_fading if it is filename)
    - activity_content        (optional text content for hs_activity if it is filename)

README / quick start:
  1) Generate template archive:
     ./netgenerator.py --template --output my.tpl

  2) Generate from PostgreSQL via DSN:
     ./netgenerator.py \
       --dsn "host=127.0.0.1 port=5432 dbname=hapnet user=igor password=secret" \
       --schema public \
       --nodes-table nodes \
       --routes-table routes \
       --output my.tpl

  3) Generate from PostgreSQL via separate flags:
     ./netgenerator.py \
       --db-host 127.0.0.1 --db-port 5432 \
       --db-name hapnet --db-user igor --db-password secret \
       --schema public --nodes-table nodes --routes-table routes \
       --output my.tpl
"""

from __future__ import annotations

import argparse
import math
import re
import shutil
import sys
import tempfile
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
    mob_vx: float | None
    mob_vy: float | None
    mob_vz: float | None
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
    hs_fading: str
    hs_activity: str
    fading_content: str | None
    activity_content: str | None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Synthesize a directed network graph from PostgreSQL and pack it into .tpl.",
        formatter_class=argparse.RawTextHelpFormatter,
        epilog=dedent(
            """\
            Examples:
              1) Generate template archive:
                 %(prog)s --template --output my.tpl

              2) Generate from PostgreSQL via DSN:
                 %(prog)s \\
                   --dsn "host=127.0.0.1 port=5432 dbname=hapnet user=igor password=secret" \\
                   --schema public \\
                   --nodes-table nodes \\
                   --routes-table routes \\
                   --output my.tpl

              3) Generate from PostgreSQL via separate flags:
                 %(prog)s \\
                   --db-host 127.0.0.1 --db-port 5432 \\
                   --db-name hapnet --db-user igor --db-password secret \\
                   --schema public --nodes-table nodes --routes-table routes \\
                   --output my.tpl
            """
        ),
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output .tpl archive path (for example: my.tpl).",
    )
    parser.add_argument(
        "--template",
        action="store_true",
        help="Generate a template .tpl archive instead of reading PostgreSQL.",
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
    return parser.parse_args()


def _normalize_tpl_path(path: Path) -> Path:
    return path if path.suffix.lower() == ".tpl" else path.with_suffix(".tpl")


def _dot_quote(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def _format_number(value: float) -> str:
    if math.isfinite(value) and float(value).is_integer():
        return str(int(value))
    return f"{value:.12g}"


def _is_numeric_token(text: str) -> bool:
    return re.fullmatch(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", text.strip()) is not None


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
        except ImportError as exc:  # pragma: no cover - runtime import path
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
        except ImportError as exc:  # pragma: no cover - runtime import path
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
          {"mob_vx" if has("mob_vx") else "NULL::double precision AS mob_vx"},
          {"mob_vy" if has("mob_vy") else "NULL::double precision AS mob_vy"},
          {"mob_vz" if has("mob_vz") else "NULL::double precision AS mob_vz"},
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
                mob_vx=None if row["mob_vx"] is None else float(row["mob_vx"]),
                mob_vy=None if row["mob_vy"] is None else float(row["mob_vy"]),
                mob_vz=None if row["mob_vz"] is None else float(row["mob_vz"]),
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
          hs_fading,
          hs_activity,
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
                hs_fading=str(row["hs_fading"]),
                hs_activity=str(row["hs_activity"]),
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
            hs_mob = trajectory_name
            content = (node.trajectory_content or "").strip()
            if content:
                (net_dir / trajectory_name).write_text(content.rstrip() + "\n", encoding="utf-8")
            elif not (net_dir / trajectory_name).exists():
                (net_dir / trajectory_name).write_text(
                    "% time lat lon alt\n0.000 0.0 0.0 0.0\n",
                    encoding="utf-8",
                )
        else:
            vx = 0.0 if node.mob_vx is None else node.mob_vx
            vy = 0.0 if node.mob_vy is None else node.mob_vy
            vz = 0.0 if node.mob_vz is None else node.mob_vz
            hs_mob = ",".join((_format_number(vx), _format_number(vy), _format_number(vz)))

        attrs = (
            f'HS_TYPE={_dot_quote(node.hs_type)}, '
            f'HS_POS={_dot_quote(hs_pos)}, '
            f'HS_MOB={_dot_quote(hs_mob)}'
        )
        lines.append(f"  {_dot_quote(node.node_id)} [{attrs}];")

    for route in routes:
        hs_fading = route.hs_fading.strip()
        hs_activity = route.hs_activity.strip()

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

        if hs_activity not in {"0", "1"}:
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
                f"HS_CENTER_FREQ={_format_number(route.hs_center_freq)}",
                f"HS_BANDWIDTH={_format_number(route.hs_bandwidth)}",
                f"HS_FADING={_dot_quote(hs_fading) if not _is_numeric_token(hs_fading) else hs_fading}",
                f"HS_ACTIVITY={_dot_quote(hs_activity) if hs_activity not in {'0', '1'} else hs_activity}",
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
            - `*.trajectory.txt`        : node trajectories (`time lat lon alt`)
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
    (net_dir / "graph.dot").write_text(
        dedent(
            """\
            digraph hs_net {
              rankdir=LR;
              "sat1" [HS_TYPE="SAT", HS_POS="56.0,93.0,20000", HS_MOB="sample_trajectory.txt"];
              "gw1"  [HS_TYPE="GW",  HS_POS="55.0,92.9,300",   HS_MOB="0,0,0"];
              "sat1" -> "gw1" [HS_LEN=450000, HS_BEAM=1, HS_CENTER_FREQ=2.1e9, HS_BANDWIDTH=2.0e7, HS_FADING=0.0001, HS_ACTIVITY="sample_activity.txt"];
            }
            """
        ),
        encoding="utf-8",
    )
    (net_dir.parent / "db_template.sql").write_text(
        dedent(
            """\
            -- Minimal schema template for netgenerator.py
            CREATE TABLE IF NOT EXISTS nodes (
              id TEXT PRIMARY KEY,
              hs_type TEXT NOT NULL,
              lat DOUBLE PRECISION NOT NULL,
              lon DOUBLE PRECISION NOT NULL,
              alt DOUBLE PRECISION NOT NULL,
              mob_vx DOUBLE PRECISION,
              mob_vy DOUBLE PRECISION,
              mob_vz DOUBLE PRECISION,
              trajectory_file TEXT,
              trajectory_content TEXT
            );

            CREATE TABLE IF NOT EXISTS routes (
              src_id TEXT NOT NULL,
              dst_id TEXT NOT NULL,
              hs_len DOUBLE PRECISION NOT NULL,
              hs_beam INTEGER NOT NULL,
              hs_center_freq DOUBLE PRECISION NOT NULL,
              hs_bandwidth DOUBLE PRECISION NOT NULL,
              hs_fading TEXT NOT NULL,
              hs_activity TEXT NOT NULL,
              fading_content TEXT,
              activity_content TEXT
            );
            """
        ),
        encoding="utf-8",
    )


def _pack_tpl(output_path: Path, source_dir: Path, force: bool) -> Path:
    out = _normalize_tpl_path(output_path).resolve()
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
        return _pack_tpl(output_path, root, force=force)


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
        return _pack_tpl(args.output, root, force=args.force)


def main() -> int:
    args = parse_args()
    try:
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

