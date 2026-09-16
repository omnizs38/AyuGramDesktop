#!/usr/bin/env python3
"""Check the composite actions that `Release` and `Cache warm` share.

`actionlint` globs `.github/workflows` only, so nothing else reads these files
until a runner executes them, halfway through a three-hour build.
"""

import pathlib
import re
import subprocess
import sys

import yaml

RUN_STEP_KEYS = frozenset(
    {"name", "id", "if", "run", "shell", "env", "working-directory", "continue-on-error"})
USES_STEP_KEYS = frozenset({"name", "id", "if", "uses", "with", "env", "continue-on-error"})
EXPRESSION = re.compile(r"\$\{\{.*?\}\}", re.DOTALL)
INPUT_REFERENCE = re.compile(r"\binputs\.([A-Za-z0-9_-]+)")


def check(path, complain):
    action = yaml.safe_load(path.read_text())
    runs = action.get("runs") or {}
    if runs.get("using") != "composite":
        return
    declared = set(action.get("inputs") or {})
    for step in runs.get("steps") or []:
        where = f"{path}: step {step.get('name', '<unnamed>')!r}"
        allowed, required = ((USES_STEP_KEYS, {"uses"}) if "uses" in step
                             else (RUN_STEP_KEYS, {"run", "shell"}))
        for key in sorted(set(step) - allowed):
            complain(f"{where} uses {key!r}, which the runner rejects here")
        for key in sorted(required - set(step)):
            complain(f"{where} has no {key!r}, which the runner requires here")
        for name in sorted(set(INPUT_REFERENCE.findall(yaml.safe_dump(step))) - declared):
            complain(f"{where} reads undeclared input {name!r}")
        if step.get("shell") == "bash" and "run" in step:
            script = EXPRESSION.sub("expression", step["run"])
            if subprocess.run(["shellcheck", "--shell=bash", "-"],
                              input=script, text=True).returncode:
                complain(f"{where} failed shellcheck")


def main():
    failures = []
    for path in sorted(pathlib.Path(".github/actions").rglob("action.yml")):
        check(path, failures.append)
    for failure in failures:
        print(f"::error::{failure}")
    print(f"{len(failures)} problem(s) in the composite actions.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
