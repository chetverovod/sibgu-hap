# ext-utils

HAPSimulator utility scripts for task packaging, execution, reporting, and dynamic topology generation.

## Python utilities in this directory

- `createtask.py` - create, validate, pack/unpack, and update `.tsk` task archives.
- `runtask.py` - execute one or more `.tsk` tasks, collect logs, generate report, and repack results.
- `dntgenerator.py` - generate, validate, and snapshot `.dnt` dynamic network topology archives.
- `genreport.py` - build a pdf-report from simulation result files.
- `gentraj.py` - generate trajectory files for moving objects.
- `plottraj.py` - visualize trajectory files.
- `cli_logs_display.py` - display parsed CLI logs in a readable form.
- `cli_logs_parser.py` - helper for the `cli_logs_display.py`.
- `combinecode.py` - combine source/code artifacts into a single output for analysis/export to AI-models.

## How to run tests

Run from `contrib/sibgu-hap/ext-utils`:

```bash
python3 -m unittest discover -s tests -p "test_*.py"
```

Run with coverage:

```bash
python3 -m coverage run -m unittest discover -s tests -p "test_*.py" && python3 -m coverage report -m
```

