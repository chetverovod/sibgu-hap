#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


def _load_createtask():
    root = Path(__file__).resolve().parents[1]
    module_path = root / "createtask.py"
    spec = importlib.util.spec_from_file_location("createtask_module", module_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load module from {module_path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class CreateTaskTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mod = _load_createtask()

    def test_build_task_tree_keeps_topology_empty_without_dnt(self):
        with tempfile.TemporaryDirectory(prefix="ct_test_") as tmp:
            root = Path(tmp) / "task"
            root.mkdir(parents=True, exist_ok=True)
            self.mod.build_task_tree(root, topology_dnt=None, include_topology_template=False)
            topology_dir = root / "scenario/topology"
            self.assertTrue(topology_dir.is_dir())
            self.assertEqual(list(topology_dir.iterdir()), [])

    def test_build_task_tree_copies_topology_dnt_when_provided(self):
        with tempfile.TemporaryDirectory(prefix="ct_test_") as tmp:
            root = Path(tmp) / "task"
            root.mkdir(parents=True, exist_ok=True)
            src_dnt = Path(tmp) / "input.dnt"
            src_dnt.write_text("dummy dnt content", encoding="utf-8")

            with mock.patch.object(self.mod, "_validate_dnt_with_generator") as validate_mock:
                self.mod.build_task_tree(root, topology_dnt=src_dnt, include_topology_template=False)
                validate_mock.assert_called_once()

            copied = root / "scenario/topology/input.dnt"
            self.assertTrue(copied.is_file())
            self.assertEqual(copied.read_text(encoding="utf-8"), "dummy dnt content")

    def test_build_task_tree_generates_topology_template(self):
        with tempfile.TemporaryDirectory(prefix="ct_test_") as tmp:
            root = Path(tmp) / "task"
            root.mkdir(parents=True, exist_ok=True)

            def _fake_generate(out_path: Path, force: bool = True):
                out_path.write_text("template dnt", encoding="utf-8")

            with mock.patch.object(self.mod, "_generate_dnt_template", side_effect=_fake_generate) as gen_mock:
                self.mod.build_task_tree(root, topology_dnt=None, include_topology_template=True)
                gen_mock.assert_called_once()

            generated = root / "scenario/topology/topology_template.dnt"
            self.assertTrue(generated.is_file())
            self.assertEqual(generated.read_text(encoding="utf-8"), "template dnt")

    def test_create_tsk_archive_adds_tsk_suffix(self):
        with tempfile.TemporaryDirectory(prefix="ct_test_") as tmp:
            out = Path(tmp) / "my_task"
            with mock.patch.object(self.mod, "_generate_dnt_template", return_value=None):
                self.mod.create_tsk_archive(out, force=False, topology_dnt=None)
            self.assertTrue((Path(tmp) / "my_task.tsk").is_file())


if __name__ == "__main__":
    unittest.main()

