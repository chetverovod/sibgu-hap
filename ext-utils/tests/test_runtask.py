#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


def _load_runtask():
    root = Path(__file__).resolve().parents[1]
    module_path = root / "runtask.py"
    spec = importlib.util.spec_from_file_location("runtask_module", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class RunTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = _load_runtask()

    def test_read_command_line_ignores_comments_and_collapses_whitespace(self):
        with tempfile.TemporaryDirectory(prefix="rt_test_") as tmp:
            cmd = Path(tmp) / "commandLine.txt"
            cmd.write_text(
                "# comment\n\n"
                "  --packetSize=512  \n"
                " --interval=100 \n"
                "\n",
                encoding="utf-8",
            )
            line = self.mod.read_command_line(cmd)
            self.assertEqual(line, "--packetSize=512 --interval=100")

    def test_resolve_results_prefers_sat_handover_hap(self):
        with tempfile.TemporaryDirectory(prefix="rt_test_") as tmp:
            sims = Path(tmp) / "sims"
            a = sims / "sat-handover-hap"
            b = sims / "hapsimulator"
            a.mkdir(parents=True, exist_ok=True)
            b.mkdir(parents=True, exist_ok=True)
            (a / "file.txt").write_text("x", encoding="utf-8")
            (b / "file.txt").write_text("y", encoding="utf-8")
            resolved = self.mod._resolve_results_for_report(sims)
            self.assertEqual(resolved, a)

    def test_run_single_task_returns_error_for_missing_file(self):
        with tempfile.TemporaryDirectory(prefix="rt_test_") as tmp:
            missing = Path(tmp) / "missing.tsk"
            rc = self.mod.run_single_task(
                missing,
                project_root=Path(tmp),
                createtask=Path(tmp) / "createtask.py",
                genreport=Path(tmp) / "genreport.py",
                keep=False,
            )
            self.assertEqual(rc, 1)

    def test_main_aggregates_success_and_failures(self):
        fake_args = types.SimpleNamespace(
            task_files=[Path("a.tsk"), Path("b.tsk"), Path("c")],
            keep=False,
        )
        with mock.patch.object(self.mod, "parse_args", return_value=fake_args), \
             mock.patch.object(self.mod, "run_single_task", side_effect=[0, 1, 0]), \
             mock.patch.object(self.mod.time, "perf_counter", side_effect=[0.0, 1.0, 2.0, 3.0, 4.0, 5.0]):
            rc = self.mod.main()
        self.assertEqual(rc, 1)


if __name__ == "__main__":
    unittest.main()

