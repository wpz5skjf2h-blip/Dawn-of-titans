#!/usr/bin/env python3
"""Bundles Content/Data into one JSON object {relative path: file text} for the browser demo."""
import json, pathlib, sys
root = pathlib.Path(__file__).resolve().parent.parent / "Content" / "Data"
files = {p.relative_to(root).as_posix(): p.read_text(encoding="utf-8") for p in sorted(root.rglob("*.json"))}
out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else root.parent.parent / "Web" / "dist" / "content.json")
out.write_text(json.dumps(files, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
print(f"{len(files)} files, {out.stat().st_size} bytes -> {out}")
