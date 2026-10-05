"""Archive the release working tree and the actual native dependency sources.

Run after the release binaries have been built. No private keys, local profiles,
Git internals or build products are copied. The worktree may be uncommitted;
source-manifest.json identifies every included file by its SHA-256.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import zipfile


ROOT = Path(__file__).resolve().parent.parent
PROJECT_PATHS = [
    ".gitattributes", ".gitignore", "CMakeLists.txt", "CMakePresets.json",
    "README.md", "CHANGELOG.md", "license", "THIRD-PARTY-NOTICES.txt",
    "Source", "WinUI", "Tests", "Utilities", "Icon", "ThirdParty", "docs", "devlog",
]
SECRET_SUFFIXES = {".pfx", ".p12", ".key", ".pem", ".pvk", ".snk"}


def git(directory, *arguments):
    return subprocess.check_output(
        ["git", "-C", str(directory), *arguments], stderr=subprocess.PIPE
    ).decode("utf-8")


def safe_file(path, boundary):
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(boundary.resolve()):
        raise RuntimeError(f"Source escaped its directory: {path}")
    for item in [path, *path.parents]:
        if item == boundary.parent:
            break
        attributes = item.lstat()
        if stat.S_ISLNK(attributes.st_mode) or getattr(attributes, "st_file_attributes", 0) & 0x400:
            raise RuntimeError(f"Source contains a link or reparse point: {path}")
    if path.suffix.lower() in SECRET_SUFFIXES or path.name == ".env" or path.name.startswith(".env."):
        raise RuntimeError(f"Private/local configuration cannot enter a source archive: {path.name}")
    if not path.is_file():
        raise RuntimeError(f"Missing source file: {path}")
    return path


def tracked_files(directory, *paths, include_untracked=False):
    arguments = ["ls-files", "-z", "--cached"]
    if include_untracked:
        arguments += ["--others", "--exclude-standard"]
    else:
        arguments += ["--recurse-submodules"]
    return sorted(set(filter(None, git(directory, *arguments, "--", *paths).split("\0"))))


def source_tree_files(directory):
    for base, directories, files in os.walk(directory, followlinks=False):
        directories[:] = sorted(name for name in directories if name not in {".git", "__pycache__"})
        for name in sorted(files):
            if name == ".git" or name.endswith((".pyc", ".pyo")):
                continue
            yield Path(base) / name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-directory", required=True)
    parser.add_argument("--build-directory", default="out/build/windows-vs2022")
    args = parser.parse_args()
    output = Path(args.output_directory).resolve()
    if not any(output.is_relative_to(ROOT / name) and output != ROOT / name for name in ("out", "releases")):
        raise RuntimeError("Use a subdirectory of this workspace's out or releases directory.")
    for parent in [output, *output.parents]:
        if parent.exists() and getattr(parent.lstat(), "st_file_attributes", 0) & 0x400:
            raise RuntimeError("Output cannot contain a reparse point.")
    version = re.search(r"project\(LightHostModern VERSION ([0-9]+\.[0-9]+\.[0-9]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    build = (ROOT / args.build_directory).resolve()
    if not build.is_relative_to(ROOT / "out"):
        raise RuntimeError("The built JUCE source must come from the workspace out directory.")
    entries = {}
    for relative in tracked_files(ROOT, *PROJECT_PATHS, include_untracked=True):
        path = ROOT / relative
        if not path.exists():  # A tracked deletion must remain deleted in the snapshot.
            continue
        entries[relative] = safe_file(path, ROOT)

    dependencies = []
    specifications = [
        ("juce-8.0.13", "7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2", build / "juce-lighthost"),
        ("vst3sdk-3.8.0", "9fad9770f2ae8542ab1a548a68c1ad1ac690abe0", ROOT / "out/deps/vst3sdk-3.8.0"),
        ("asio-sdk", "496a0765b8bb9c26f764f22f9a9712a937177db2", ROOT / "out/deps/asio-sdk"),
        ("xaymar-vst2sdk-0.4.0", None, ROOT / "out/deps/xaymar-vst2sdk-0.4.0"),
    ]
    for name, expected_commit, source in specifications:
        original = ROOT / "out/deps" / name
        if expected_commit and git(original, "rev-parse", "HEAD").strip() != expected_commit:
            raise RuntimeError(f"Unexpected upstream commit for {name}.")
        if name == "juce-8.0.13":
            # Archive the patched sources used by this build, not a clean upstream
            # tree which would omit historical local adapter changes.
            candidates = (source / relative for relative in tracked_files(original))
        elif expected_commit:
            candidates = (source / relative for relative in tracked_files(source))
        else:
            candidates = source_tree_files(source)
        count = 0
        for path in candidates:
            safe_file(path, source)
            relative = path.relative_to(source).as_posix()
            entries[f"out/deps/{name}/{relative}"] = path
            count += 1
        dependencies.append({"name": name, "upstreamCommit": expected_commit,
                             "files": count, "asBuiltJucePatchesIncluded": name == "juce-8.0.13"})

    instructions = """LightHostModern release source snapshot

This archive contains the actual project working-tree files and native dependency
sources used for this candidate, including the patched JUCE source. Each file is
identified in source-manifest.json. The base Git commit alone is NOT the source
identity: unpublished working-tree changes are included.

On Windows, install the Visual Studio/MSVC, Windows SDK, WinUI and CMake tooling
listed in docs/build-and-release.md. NuGet restores the exact package versions in
WinUI/LightHostModern.WinUI/packages.config. Native dependency sources are already
under out/deps and the build helpers discover them without downloading them.
Utilities/PatchJuce.cmake is idempotent over the included patched JUCE sources.

From this folder:
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "Utilities/Build Windows.ps1"
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "Utilities/Build Dev.ps1" -SkipBuild

For MSI/ZIP packaging, install WiX as described in the build documentation and run
Utilities/Build Release.ps1. Rebuilding does not require the maintainer's signing
key. Without a signing identity, packages support manual installation only.
To distribute independently signed updates, provision your OWN update trust key;
the maintainer's private key/password are intentionally absent.

See license, THIRD-PARTY-NOTICES.txt and docs/licensing.md. Microsoft development
tools and system runtimes retain their own license terms and are not included as
GPL/AGPL source. This snapshot is not a claim of byte-for-byte reproducible PE/MSI
output: toolchain versions, timestamps and signing can change binary hashes.
"""
    output.mkdir(parents=True, exist_ok=True)
    target = output / f"LightHostModern-{version}-Source.zip"
    partial = target.with_suffix(".zip.partial")
    if target.exists() or partial.exists():
        raise RuntimeError("Source archive already exists; select a new output directory.")
    prefix = f"LightHostModern-{version}-Source/"
    manifest = {"formatVersion": 1, "version": version,
                "baseGitCommit": git(ROOT, "rev-parse", "HEAD").strip(),
                "includesWorkingTreeChanges": True, "dependencies": dependencies, "files": []}
    with zipfile.ZipFile(partial, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for relative, path in sorted(entries.items()):
            content = path.read_bytes()
            manifest["files"].append({"path": relative, "size": len(content), "sha256": hashlib.sha256(content).hexdigest()})
            archive.writestr(prefix + relative, content)
        content = instructions.encode("utf-8")
        manifest["files"].append({"path": "SOURCE-BUILD.txt", "size": len(content), "sha256": hashlib.sha256(content).hexdigest()})
        archive.writestr(prefix + "SOURCE-BUILD.txt", content)
        archive.writestr(prefix + "source-manifest.json", json.dumps(manifest, indent=2).encode("utf-8"))
    with zipfile.ZipFile(partial) as archive:
        if len(archive.namelist()) != len(manifest["files"]) + 1:
            raise RuntimeError("Source archive inventory mismatch.")
        for entry in manifest["files"]:
            data = archive.read(prefix + entry["path"])
            if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
                raise RuntimeError(f"Source verification failed: {entry['path']}")
    partial.rename(target)
    summary = {"version": version, "name": target.name, "size": target.stat().st_size,
               "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
               "fileCount": len(manifest["files"]) + 1, "verified": True,
               "dependencies": dependencies, "baseGitCommit": manifest["baseGitCommit"]}
    (output / "source-verification.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
