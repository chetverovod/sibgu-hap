#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


def _load_dntgenerator():
    root = Path(__file__).resolve().parents[1]
    module_path = root / "dntgenerator.py"
    spec = importlib.util.spec_from_file_location("dntgenerator_module", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _create_dnt(path: Path, graph_dot: str, extra_files: dict[str, str]) -> Path:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
        zf.writestr(
            "README.md",
            "# Dynamic Network Topology Archive (.dnt)\nGenerated for tests.\n",
        )
        zf.writestr("db_template.sql", "-- test schema\n")
        zf.writestr("net/graph.dot", graph_dot)
        for rel, content in extra_files.items():
            zf.writestr(f"net/{rel}", content)
    return path


class DntGeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = _load_dntgenerator()

    def test_validate_dnt_file_reports_missing_required_files(self):
        with tempfile.TemporaryDirectory(prefix="dnt_test_") as tmp:
            dnt_path = Path(tmp) / "broken.dnt"
            with zipfile.ZipFile(dnt_path, "w", compression=zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("README.md", "x")
            issues = self.mod.validate_dnt_file(dnt_path)
            self.assertTrue(any("Missing required file: db_template.sql" in x for x in issues))
            self.assertTrue(any("Missing required file: net/graph.dot" in x for x in issues))

    def test_generate_template_archive_contains_expected_entries(self):
        with tempfile.TemporaryDirectory(prefix="dnt_test_") as tmp:
            out = Path(tmp) / "template.dnt"
            self.mod.generate_template_archive(out, force=True)
            with zipfile.ZipFile(out, "r") as zf:
                names = {i.filename.rstrip("/") for i in zf.infolist() if i.filename}
            self.assertIn("README.md", names)
            self.assertIn("db_template.sql", names)
            self.assertIn("net/graph.dot", names)
            self.assertIn("net/sample_trajectory.txt", names)
            self.assertIn("net/sample_beam_trajectory.txt", names)
            self.assertIn("net/sample_activity.txt", names)

    def test_snapshot_resolves_file_attributes_and_keeps_edge_on_previous_activity(self):
        graph = (
            "digraph hs_net {\n"
            '  "n1" [HS_TYPE="SAT", HS_POS="node_traj.txt"];\n'
            '  "n2" [HS_TYPE="GW", HS_POS="55,92,300"];\n'
            '  "n1" -> "n2" [HS_LEN=10, HS_BEAM=1, HS_BEAM_DIR="beam_traj.txt", '
            'HS_CENTER_FREQ=2.0e9, HS_BANDWIDTH=2.0e7, HS_FADING="fading.txt", HS_ACTIVITY="activity.txt"];\n'
            "}\n"
        )
        files = {
            "node_traj.txt": "% time lat lon alt\n0 0 0 100\n10 10 20 300\n",
            "beam_traj.txt": "% time lat lon alt\n0 1 2 3\n10 11 12 13\n",
            "fading.txt": "% time fading\n0 0\n10 10\n",
            "activity.txt": "% time state\n0 on\n10 off\n",
        }
        with tempfile.TemporaryDirectory(prefix="dnt_test_") as tmp:
            src = _create_dnt(Path(tmp) / "source.dnt", graph, files)
            out = self.mod.generate_snapshot_archive(src, 5.0)
            with zipfile.ZipFile(out, "r") as zf:
                dot = zf.read("net/graph.dot").decode("utf-8", errors="replace")

            self.assertIn('HS_POS="5,10,200"', dot)
            self.assertIn('HS_BEAM_DIR="6,7,8"', dot)
            self.assertIn("HS_FADING=5", dot)
            self.assertIn("HS_ACTIVITY=on", dot)
            self.assertNotIn("node_traj.txt", dot)
            self.assertNotIn("beam_traj.txt", dot)
            self.assertNotIn("fading.txt", dot)
            self.assertNotIn("activity.txt", dot)

    def test_snapshot_removes_edge_when_activity_is_off(self):
        graph = (
            "digraph hs_net {\n"
            '  "n1" [HS_TYPE="SAT", HS_POS="0,0,100"];\n'
            '  "n2" [HS_TYPE="GW", HS_POS="55,92,300"];\n'
            '  "n1" -> "n2" [HS_LEN=10, HS_BEAM=1, HS_BEAM_DIR="1,2,3", '
            'HS_CENTER_FREQ=2.0e9, HS_BANDWIDTH=2.0e7, HS_FADING=0.1, HS_ACTIVITY="activity.txt"];\n'
            "}\n"
        )
        files = {
            "activity.txt": "% time state\n0 on\n10 off\n",
        }
        with tempfile.TemporaryDirectory(prefix="dnt_test_") as tmp:
            src = _create_dnt(Path(tmp) / "source_off.dnt", graph, files)
            out = self.mod.generate_snapshot_archive(src, 10.0)
            with zipfile.ZipFile(out, "r") as zf:
                dot = zf.read("net/graph.dot").decode("utf-8", errors="replace")
            self.assertNotIn("->", dot)


if __name__ == "__main__":
    unittest.main()

