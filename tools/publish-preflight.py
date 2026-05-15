#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "requests>=2.31",
#   "packaging>=23.0",
# ]
# ///

"""Pre-flight checks before publishing to PlatformIO and ESP Component registries.

Checks:
1. Versions in library.json and idf_component.yml match
2. Version has no pre-release suffix (e.g. -dev)
3. Local version is greater than the latest published version on each registry
4. Required secrets/env vars are present (when --check-secrets is passed)
"""

import json
import os
import sys
from pathlib import Path

import requests
from packaging.version import Version


def get_local_versions(repo_root: Path) -> tuple[str, str]:
    """Read versions from library.json and idf_component.yml."""
    with open(repo_root / "library.json") as f:
        pio_version = json.load(f)["version"]

    idf_version = None
    with open(repo_root / "idf_component.yml") as f:
        for line in f:
            if line.startswith("version:"):
                idf_version = line.split(":", 1)[1].strip().strip('"').strip("'")
                break

    if idf_version is None:
        print("ERROR: Could not find version in idf_component.yml")
        sys.exit(1)

    return pio_version, idf_version


def get_pio_latest(owner: str, name: str) -> str | None:
    """Get latest version from PlatformIO registry."""
    url = f"https://api.registry.platformio.org/v3/packages/{owner}/library/{name}"
    resp = requests.get(url, timeout=10)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    return resp.json().get("version", {}).get("name")


def get_idf_latest(namespace: str, name: str) -> str | None:
    """Get latest version from ESP Component Registry."""
    url = f"https://components.espressif.com/api/components/{namespace}/{name}"
    resp = requests.get(url, timeout=10)
    if resp.status_code == 404:
        return None
    resp.raise_for_status()
    versions = resp.json().get("versions", [])
    if not versions:
        return None
    return versions[0].get("version")


def main():
    check_secrets = "--check-secrets" in sys.argv

    repo_root = Path(__file__).resolve().parent.parent
    pio_version, idf_version = get_local_versions(repo_root)

    errors = []

    # Check versions match
    if pio_version != idf_version:
        errors.append(
            f"Version mismatch: library.json has '{pio_version}', "
            f"idf_component.yml has '{idf_version}'"
        )

    local_version = pio_version
    print(f"Local version: {local_version}")

    # Check no pre-release suffix
    v = Version(local_version)
    if v.pre is not None or v.dev is not None or v.post is not None:
        errors.append(
            f"Version '{local_version}' has a pre-release/dev suffix — "
            "strip it before publishing"
        )

    # Check against PlatformIO registry
    pio_latest = get_pio_latest("inanimate", "resident")
    if pio_latest:
        print(f"PlatformIO registry latest: {pio_latest}")
        if Version(local_version) <= Version(pio_latest):
            errors.append(
                f"Local version {local_version} is not greater than "
                f"PlatformIO registry version {pio_latest}"
            )
    else:
        print("PlatformIO registry: no published version found (first publish)")

    # Check against ESP Component Registry
    idf_latest = get_idf_latest("inanimate", "resident")
    if idf_latest:
        print(f"ESP Component Registry latest: {idf_latest}")
        if Version(local_version) <= Version(idf_latest):
            errors.append(
                f"Local version {local_version} is not greater than "
                f"ESP Component Registry version {idf_latest}"
            )
    else:
        print("ESP Component Registry: no published version found (first publish)")

    # Check secrets
    if check_secrets:
        if not os.environ.get("PLATFORMIO_AUTH_TOKEN"):
            errors.append("PLATFORMIO_AUTH_TOKEN is not set in GitHub repo settings")
        if not os.environ.get("IDF_COMPONENT_API_TOKEN"):
            errors.append("IDF_COMPONENT_API_TOKEN is not set in GitHub repo settings")

    if errors:
        print()
        for err in errors:
            print(f"ERROR: {err}")
        sys.exit(1)

    print("\nAll pre-flight checks passed.")


if __name__ == "__main__":
    main()
