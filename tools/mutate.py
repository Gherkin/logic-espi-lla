#!/usr/bin/env python3
"""Mutation testing over the facts the decoder depends on.

Rules R1-R4 stop a test from agreeing with itself. They do not prove a test
constrains anything at all -- a value no vector touches is just as untested as
one checked against itself. This measures that directly: corrupt a fact, and
see whether the suite notices.

    KILLED    the suite went red. The fact is constrained by a test.
    SURVIVED  the suite stayed green with a wrong value compiled in.
              That is a missing vector. It is the only number worth watching.

Run inside the build container, from the repo root:

    docker run --rm -v "$PWD":/work espi-build ./tools/mutate.py

Each mutation is applied to a throwaway copy of the tree, so the working tree
is never modified.
"""

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

IGNORE = shutil.ignore_patterns("build", ".git", "__pycache__", "*.pyc")


def run(cmd, cwd):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)


def apply_mutation(tree: Path, mutation: dict) -> str | None:
    """Apply one mutation in-place. Returns an error string, or None on success."""
    target = tree / mutation["file"]
    if not target.exists():
        return f"file not found: {mutation['file']}"

    text = target.read_text(encoding="utf-8")
    count = text.count(mutation["find"])
    if count == 0:
        return f"pattern not found in {mutation['file']}: {mutation['find']!r}"
    if count > 1:
        return f"pattern matched {count} times in {mutation['file']}, need exactly 1"

    target.write_text(text.replace(mutation["find"], mutation["replace"]), encoding="utf-8")
    return None


def evaluate(source: Path, mutation: dict, verbose: bool) -> tuple[str, str]:
    """Build and test a mutated copy. Returns (verdict, detail)."""
    with tempfile.TemporaryDirectory(prefix="espi-mut-") as tmp:
        tree = Path(tmp) / "tree"
        shutil.copytree(source, tree, ignore=IGNORE)

        err = apply_mutation(tree, mutation)
        if err:
            return "ERROR", err

        cfg = run(["cmake", "-S", ".", "-B", "build", "-G", "Ninja"], cwd=tree)
        if cfg.returncode != 0:
            return "ERROR", "cmake configure failed\n" + cfg.stderr[-800:]

        build = run(["cmake", "--build", "build"], cwd=tree)
        if build.returncode != 0:
            # A mutation that will not compile still proves the fact is load
            # bearing, but it is weaker evidence than a red test -- the
            # compiler caught it, not us. Reported separately so it cannot be
            # mistaken for test coverage.
            return "NOCOMPILE", build.stderr.strip().splitlines()[-1] if build.stderr else ""

        test = run(["ctest", "-R", mutation["tests"], "--output-on-failure"], cwd=tree / "build")
        if test.returncode != 0:
            detail = ""
            for line in test.stdout.splitlines():
                if line.startswith("FAIL") or "Failed" in line:
                    detail = line.strip()
                    break
            return "KILLED", detail
        return "SURVIVED", (test.stdout.strip().splitlines() or [""])[-1] if verbose else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="tools/mutations.json")
    ap.add_argument("--filter", default="", help="only run mutations whose label contains this")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    source = Path.cwd()
    config = json.loads((source / args.config).read_text(encoding="utf-8"))
    mutations = [m for m in config["mutations"] if args.filter.lower() in m["label"].lower()]

    if not mutations:
        print(f"no mutations match filter {args.filter!r}", file=sys.stderr)
        return 2

    print(f"running {len(mutations)} mutation(s)\n")
    tally = {"KILLED": 0, "SURVIVED": 0, "NOCOMPILE": 0, "ERROR": 0}
    survivors, errors = [], []

    for m in mutations:
        print(f"  {m['label']:<56}", end="", flush=True)
        verdict, detail = evaluate(source, m, args.verbose)
        tally[verdict] += 1
        print(verdict)
        if detail and (args.verbose or verdict in ("SURVIVED", "ERROR")):
            print(f"      {detail}")
        if verdict == "SURVIVED":
            survivors.append(m["label"])
        elif verdict == "ERROR":
            errors.append((m["label"], detail))

    print(
        f"\n  {tally['KILLED']} killed, {tally['SURVIVED']} survived, "
        f"{tally['NOCOMPILE']} did not compile, {tally['ERROR']} errored"
    )

    if survivors:
        print("\nSURVIVORS -- these facts are not constrained by any test:")
        for s in survivors:
            print(f"  - {s}")
    if errors:
        print("\nERRORS -- mutation could not be applied (stale pattern?):")
        for label, detail in errors:
            print(f"  - {label}: {detail}")

    return 1 if (survivors or errors) else 0


if __name__ == "__main__":
    sys.exit(main())
