#!/usr/bin/env python3
"""
Run a SibGu HAP task archive created by createtask.py.
"""

from __future__ import annotations

import argparse
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate, unpack, run, report, and repack a .tsk task file."
    )
    parser.add_argument(
        "task_file",
        type=Path,
        help="Path to input task file (.tsk). The file is updated in place on success.",
    )
    parser.add_argument(
        "--keep",
        action="store_true",
        help="Keep temporary unpack directory after execution.",
    )
    return parser.parse_args()


def run_cmd(
    cmd: list[str],
    cwd: Path | None = None,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=str(cwd) if cwd else None,
        check=False,
        text=True,
        capture_output=capture,
    )


def ensure_ok(result: subprocess.CompletedProcess[str], what: str) -> None:
    if result.returncode != 0:
        out = result.stdout or ""
        err = result.stderr or ""
        message = f"{what} failed with exit code {result.returncode}."
        if out.strip():
            message += f"\nstdout:\n{out}"
        if err.strip():
            message += f"\nstderr:\n{err}"
        raise RuntimeError(message)


def clear_dir_contents(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    for item in path.iterdir():
        if item.is_dir():
            shutil.rmtree(item)
        else:
            item.unlink()


def read_command_line(command_file: Path) -> str:
    if not command_file.exists():
        raise FileNotFoundError(f"commandLine.txt not found: {command_file}")
    raw = command_file.read_text(encoding="utf-8", errors="replace")
    parts: list[str] = []
    for line in raw.splitlines():
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        parts.append(text)
    if not parts:
        raise ValueError("commandLine.txt does not contain a runnable command line.")
    # Join wrapped editor lines and collapse whitespace so tokens like --simulationDuration= stay intact.
    return " ".join(" ".join(parts).split())


def build_ns3_hapsimulator_shell_command(program_argv: list[str]) -> str:
    """
    Shell form expected by users and ns3: ./ns3 run "hapsimulator <argv...>"
    (one argv to `run`, same as interactive copy-paste).
    """
    inner = "hapsimulator3 " + " ".join(program_argv)  # Change to "hapsimulator"
    escaped = (
        inner.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("$", "\\$")
        .replace("`", "\\`")
    )
    return f'./ns3 run "{escaped}"'


def has_any_non_hidden_file(path: Path) -> bool:
    """True if any regular file exists under path (recursive)."""
    for item in path.rglob("*"):
        if item.is_file():
            return True
    return False


def _absolutize_sat_traces_positions(scenario_dir: Path) -> None:
    """
    sat_traces.txt lists (satId, traceFile). Satellite opens traceFile as given; a bare filename is
    resolved from the ns-3 process cwd, not from scenario/positions/. Rewrite relative paths to
    absolute paths under scenario/positions/ so runtask works with temp unpack dirs.
    """
    path = scenario_dir / "positions" / "sat_traces.txt"
    if not path.is_file():
        return
    backup = scenario_dir / "positions" / ".sat_traces.runtask_orig"
    if not backup.exists():
        shutil.copy2(path, backup)
    out_lines: list[str] = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if not line or line.startswith("%"):
            out_lines.append(raw)
            continue
        parts = line.split()
        if len(parts) != 2:
            out_lines.append(raw)
            continue
        sat_id, trace = parts[0], parts[1]
        tp = Path(trace)
        if tp.is_absolute():
            out_lines.append(raw)
            continue
        if "/" not in trace:
            resolved = (scenario_dir / "positions" / trace).resolve()
        else:
            resolved = (scenario_dir / trace).resolve()
        out_lines.append(f"{sat_id} {resolved}")
    path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")


def _restore_sat_traces_positions(scenario_dir: Path) -> None:
    """Undo _absolutize_sat_traces_positions so createtask strict pack/validate stays portable."""
    backup = scenario_dir / "positions" / ".sat_traces.runtask_orig"
    path = scenario_dir / "positions" / "sat_traces.txt"
    if backup.is_file():
        shutil.move(str(backup), str(path))


def _print_simulation_log_to_console(log_path: Path) -> None:
    """Echo full simulation log to the console (stderr) after failure."""
    body = log_path.read_text(encoding="utf-8", errors="replace")
    print("\n--- simulation.log ---", file=sys.stderr)
    sys.stderr.write(body)


def _resolve_results_for_report(sims: Path) -> Path | None:
    """
    Locate the directory that contains ns-3 / hapsimulator outputs.

    scratch/hapsimulator.cc uses SimulationHelper(simulationName) with
    simulationName == \"sat-handover-hap\", so results live under
    <simsDir>/sat-handover-hap/. Older runtask versions incorrectly assumed
    \"hapsimulator\".
    """
    for name in ("sat-handover-hap", "hapsimulator"):
        candidate = sims / name
        if candidate.is_dir() and has_any_non_hidden_file(candidate):
            return candidate
    for child in sorted(sims.iterdir(), key=lambda p: p.name):
        if child.is_dir() and has_any_non_hidden_file(child):
            return child
    if has_any_non_hidden_file(sims):
        return sims
    return None


def main() -> int:
    args = parse_args()

    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parents[2]  # .../ns-3.43
    createtask = script_dir / "createtask.py"
    genreport = script_dir / "genreport.py"

    task_file = args.task_file.resolve()
    if task_file.suffix.lower() != ".tsk":
        task_file = task_file.with_suffix(".tsk")

    if not task_file.exists():
        print(f"Error: task file not found: {task_file}")
        return 1

    if args.keep:
        temp_root = Path(tempfile.mkdtemp(prefix="sibgu_runtask_"))
        auto_cleanup = False
    else:
        temp_ctx = tempfile.TemporaryDirectory(prefix="sibgu_runtask_")
        temp_root = Path(temp_ctx.name)
        auto_cleanup = True

    unpack_dir = temp_root / "task"
    scenario_dir = unpack_dir / "scenario"
    sims_dir = unpack_dir / "sims"
    logs_dir = unpack_dir / "logs"
    reports_dir = unpack_dir / "reports"
    log_file = logs_dir / "simulation.log"

    try:
        # 1-2) Validate input task
        print(f"[INFO] Validating task: {task_file}")
        res = run_cmd(
            [sys.executable, str(createtask), "valid", str(task_file), "--mode", "strict"],
            capture=True,
        )
        ensure_ok(res, "Task validation")
        print("[INFO] Validation passed.")

        # 3) Unpack to temp directory and clear sims/logs/reports
        print(f"[INFO] Unpacking task to temporary directory: {unpack_dir}")
        res = run_cmd(
            [
                sys.executable,
                str(createtask),
                "unpack",
                str(task_file),
                str(unpack_dir),
                "--mode",
                "strict",
                "--force",
            ],
            capture=True,
        )
        ensure_ok(res, "Task unpack")

        clear_dir_contents(sims_dir)
        clear_dir_contents(logs_dir)
        clear_dir_contents(reports_dir)
        print("[INFO] Cleared temporary sims/logs/reports directories.")

        _absolutize_sat_traces_positions(scenario_dir)
        try:
            # 4) Build run command from commandLine.txt + --simsDir
            command_line_txt = unpack_dir / "commandLine.txt"
            task_cmd_args = read_command_line(command_line_txt)
            program_argv = shlex.split(task_cmd_args, posix=True)
            program_argv.append(f"--simsDir={sims_dir}")
            program_argv.append(f"--scenarioPath={scenario_dir}")
            run_command_shell = build_ns3_hapsimulator_shell_command(program_argv)
            print(f"[INFO] Run command: {run_command_shell}")

            # 5) Run simulation and store full log into logs/simulation.log
            with log_file.open("w", encoding="utf-8") as lf:
                lf.write(f"$ {run_command_shell}\n\n")
                proc = subprocess.run(
                    run_command_shell,
                    shell=True,
                    cwd=str(project_root),
                    text=True,
                    capture_output=True,
                )
                lf.write(proc.stdout or "")
                if proc.stderr:
                    lf.write("\n[stderr]\n")
                    lf.write(proc.stderr)
                lf.write(f"\n[exit_code] {proc.returncode}\n")

            if proc.returncode != 0:
                _print_simulation_log_to_console(log_file)
                raise RuntimeError(
                    f"Simulation failed with exit code {proc.returncode}. "
                    f"See log file: {log_file}"
                )

            # 6) Generate PDF report if we can find statistics under task/sims
            report_results_dir = _resolve_results_for_report(sims_dir)
            if report_results_dir is not None:
                report_pdf = reports_dir / "results_report.pdf"
                print(f"[INFO] Generating report from: {report_results_dir}")
                res = run_cmd(
                    [
                        sys.executable,
                        str(genreport),
                        str(report_results_dir),
                        "--output",
                        str(report_pdf),
                    ],
                    capture=True,
                )
                ensure_ok(res, "Report generation")
            else:
                print(
                    "[INFO] No result files under task/sims (expected e.g. sat-handover-hap/ after "
                    "hapsimulator); report generation skipped."
                )

            # 7) Repack modified task into the same file
            # Restore portable sat_traces before pack: strict validation uses paths under the task tree.
            _restore_sat_traces_positions(scenario_dir)
            print(f"[INFO] Packing updated task back to: {task_file}")
            res = run_cmd(
                [
                    sys.executable,
                    str(createtask),
                    "pack",
                    str(unpack_dir),
                    str(task_file),
                    "--mode",
                    "strict",
                    "--force",
                ],
                capture=True,
            )
            ensure_ok(res, "Task repack")
            print("[INFO] Task completed successfully.")
            return 0
        finally:
            _restore_sat_traces_positions(scenario_dir)
    except Exception as exc:
        print(f"Error: {exc}")
        if args.keep:
            print(f"[INFO] Temporary directory kept: {temp_root}")
        return 1
    finally:
        if auto_cleanup:
            temp_ctx.cleanup()
        elif args.keep:
            print(f"[INFO] Temporary directory kept: {temp_root}")


if __name__ == "__main__":
    raise SystemExit(main())
