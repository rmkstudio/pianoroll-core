#!/usr/bin/env python3
"""Versioned source snapshot/manifest utility; Python standard library only."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import zipfile

ROOT = Path(__file__).resolve().parent.parent
GENERATED = {"SOURCE-MANIFEST.json", "SHA256SUMS.txt"}
EXCLUDED_DIRS = {".git", "__pycache__", "build", "dist", ".cache"}


def is_build_binary(path):
    if path.suffix.lower() in {".o", ".obj", ".a", ".lib", ".so", ".dll", ".exe", ".pdb"}:
        return True
    # Extensionless standalone compiler outputs must never enter source archives.
    with path.open("rb") as stream:
        magic = stream.read(4)
    return magic.startswith(b"\x7fELF") or magic.startswith(b"MZ")


def source_paths(root):
    return sorted((p for p in root.rglob("*")
                   if p.is_file() and p.name not in GENERATED
                   and not any(part in EXCLUDED_DIRS or part.startswith("build-")
                               for part in p.relative_to(root).parts)
                   and p.suffix not in {".pyc", ".zip"}
                   and not is_build_binary(p)),
                  key=lambda p: p.relative_to(root).as_posix())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def collect(root):
    files = {str(p.relative_to(root).as_posix()): digest(p) for p in source_paths(root)}
    sums = "".join(f"{value}  {name}\n" for name, value in files.items())
    return files, sums, hashlib.sha256(sums.encode("utf-8")).hexdigest()


def source_version(root):
    header = (root / "include/PianoRollCore/Core.h").read_text(encoding="utf-8")
    match = re.search(r'^inline constexpr const char\* version = "(\d+\.\d+\.\d+)";', header, re.MULTILINE)
    if not match:
        raise ValueError("missing release version in Core.h")
    return match.group(1)


def refresh(root):
    (root / "VERSION").write_text(source_version(root) + "\n", encoding="utf-8")
    files, sums, tree_hash = collect(root)
    data = {"schema": 1, "name": "PianoRollCore", "version": (root / "VERSION").read_text().strip(),
            "source_tree_sha256": tree_hash,
            "source_tree_hash_definition": "SHA256 of UTF-8 SHA256SUMS.txt, sorted relative paths, LF newlines",
            "files": files}
    (root / "SOURCE-MANIFEST.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    (root / "SHA256SUMS.txt").write_text(sums, encoding="utf-8", newline="\n")
    return data


def verify(root):
    root = Path(root).resolve()
    if (root / "VERSION").read_text().strip() != source_version(root):
        raise ValueError("VERSION differs from Core.h; refresh the release manifest")
    data = json.loads((root / "SOURCE-MANIFEST.json").read_text(encoding="utf-8"))
    files, sums, tree_hash = collect(root)
    if data.get("schema") != 1 or data.get("name") != "PianoRollCore":
        raise ValueError("unsupported source manifest")
    if files != data["files"]:
        names = sorted(set(files) | set(data["files"]))
        bad = [n for n in names if files.get(n) != data["files"].get(n)]
        raise ValueError("source snapshot differs: " + ", ".join(bad))
    if tree_hash != data["source_tree_sha256"]:
        raise ValueError("source tree hash mismatch")
    if (root / "SHA256SUMS.txt").read_bytes() != sums.encode("utf-8"):
        raise ValueError("SHA256SUMS differs from manifest")
    if (root / "VERSION").read_text().strip() != data["version"]:
        raise ValueError("VERSION differs from manifest")
    return data


def snapshot(root, destination):
    data = verify(root)
    destination = Path(destination).resolve()
    if destination.exists():
        existing = verify(destination)
        if existing["source_tree_sha256"] != data["source_tree_sha256"]:
            raise ValueError("destination exists with a different version/hash; use a new directory")
        return data  # idempotent exact snapshot; never overwrite local changes
    if destination == root or root in destination.parents:
        raise ValueError("snapshot destination must be outside source directory")
    destination.mkdir(parents=True)
    for name in list(data["files"]) + sorted(GENERATED):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(root / name, target)
    verify(destination)
    return data


def package(root, filename):
    data = verify(root)
    filename = Path(filename).resolve()
    filename.parent.mkdir(parents=True, exist_ok=True)
    prefix = f'PianoRollCore-{data["version"]}'
    with zipfile.ZipFile(filename, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in list(data["files"]) + sorted(GENERATED):
            info = zipfile.ZipInfo(f"{prefix}/{name}", (2026, 9, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, (root / name).read_bytes())
    return filename, digest(filename)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refresh-manifest", action="store_true")
    parser.add_argument("--verify", type=Path, metavar="SOURCE_DIRECTORY")
    parser.add_argument("--snapshot", type=Path, metavar="DESTINATION_DIRECTORY")
    parser.add_argument("--zip", type=Path, metavar="OUTPUT_ZIP")
    args = parser.parse_args()
    try:
        if args.refresh_manifest:
            refresh(ROOT)
        data = verify(args.verify or ROOT)
        if args.snapshot:
            snapshot(ROOT, args.snapshot)
        output = {"version": data["version"], "source_tree_sha256": data["source_tree_sha256"],
                  "source_files": len(data["files"]), "verified": str(args.verify or ROOT)}
        if args.zip:
            path, value = package(ROOT, args.zip)
            output.update(zip=str(path), zip_sha256=value)
        print(json.dumps(output, indent=2))
    except (OSError, ValueError, KeyError) as exc:
        parser.exit(1, f"Snapshot verification failed: {exc}\n")


if __name__ == "__main__":
    main()
