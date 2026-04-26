#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "click>=8.1",
# ]
# ///

import os
import subprocess
import sys
from pathlib import Path

import click

ROOT = Path(__file__).parent.parent

# Ensure ~/.platformio/penv/bin is on PATH (pio is often installed there)
_pio_bin = Path.home() / ".platformio" / "penv" / "bin"
if _pio_bin.is_dir():
    os.environ["PATH"] = str(_pio_bin) + os.pathsep + os.environ.get("PATH", "")

CPPCHECK_SUPPRESSIONS = [
    "missingIncludeSystem",
    "unmatchedSuppression",
    "noCopyConstructor",
    "noOperatorEq",
]


def run_cmd(cmd: list[str], cwd: Path | None = None, label: str = "") -> bool:
    if label:
        click.echo(click.style(f"  → {label}", fg="cyan"))
    return subprocess.run(cmd, cwd=cwd).returncode == 0


@click.group(context_settings={"help_option_names": ["-h", "--help"]})
def cli() -> None:
    """Run tests for the Outrun library."""
    pass


@cli.command("static-analysis")
def static_analysis() -> None:
    """Run cppcheck on src/."""
    click.echo(click.style("Static Analysis", fg="white", bold=True))
    cmd = ["cppcheck", "--enable=warning", "--error-exitcode=1"]
    for s in CPPCHECK_SUPPRESSIONS:
        cmd.append(f"--suppress={s}")
    cmd.append(str(ROOT / "src"))
    if run_cmd(cmd, cwd=ROOT, label="cppcheck src/"):
        click.echo(click.style("✓ Static analysis passed", fg="green"))
    else:
        click.echo(click.style("✗ Static analysis failed", fg="red"))
        sys.exit(1)


@cli.command("unit")
def unit_tests() -> None:
    """Run unit tests on native platform."""
    click.echo(click.style("Unit Tests", fg="white", bold=True))
    test_dir = ROOT / "test" / "unit"
    if not run_cmd(["pio", "test", "-e", "native"], cwd=test_dir, label="pio test -e native"):
        click.echo(click.style("✗ Unit tests failed", fg="red"))
        sys.exit(1)
    click.echo(click.style("✓ Unit tests passed", fg="green"))


@cli.command("build")
def build_verification() -> None:
    """Build-verify the m5stick-demo PlatformIO example."""
    click.echo(click.style("Build Verification", fg="white", bold=True))
    project = ROOT / "examples" / "m5stick-demo" / "device"
    if not run_cmd(["pio", "run"], cwd=project, label=f"pio run ({project.name})"):
        click.echo(click.style("✗ Build failed", fg="red"))
        sys.exit(1)
    click.echo(click.style("✓ Build passed", fg="green"))


@cli.command("all")
@click.pass_context
def run_all(ctx: click.Context) -> None:
    """Run static analysis, unit tests, and build verification."""
    ctx.invoke(static_analysis)
    click.echo()
    ctx.invoke(unit_tests)
    click.echo()
    ctx.invoke(build_verification)


if __name__ == "__main__":
    cli()
