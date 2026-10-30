#!/usr/bin/env python3
"""Assembles the browser demo.

Inputs: Web/page.html (markup and styles), Web/icons.js (icons), Web/titans.js (titan figures), Web/titans3d.js (titans in battle),
Web/gfx.js (battlefield look), Web/art/*.svg and *.jpg (textures and sprites), Web/app.js (battle), Web/kingdom.js (kingdom), Content/Data/**/*.json,
Web/dist/dotsim*.js (from build.sh).
Outputs in Web/dist:
  artifact.html - page body for hosts that wrap it in their own document (claude.ai artifact)
  index.html    - complete standalone document; serve Web/dist over HTTP and open it
"""
import base64
import json
import pathlib

web = pathlib.Path(__file__).resolve().parent
root = web.parent
data = root / "Content" / "Data"
files = {p.relative_to(data).as_posix(): p.read_text(encoding="utf-8") for p in sorted(data.rglob("*.json"))}
content = json.dumps(files, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
page = (web / "page.html").read_text(encoding="utf-8")
app = (web / "app.js").read_text(encoding="utf-8")
kingdom = (web / "kingdom.js").read_text(encoding="utf-8")
icons = (web / "icons.js").read_text(encoding="utf-8")
titans = (web / "titans.js").read_text(encoding="utf-8")
titans3d = (web / "titans3d.js").read_text(encoding="utf-8")
gfx = (web / "gfx.js").read_text(encoding="utf-8")
art_files = {p.stem: p.read_text(encoding="utf-8") for p in sorted((web / "art").glob("*.svg"))}
# Photo textures travel as data URIs.
art_files.update({p.stem: "data:image/jpeg;base64," + base64.b64encode(p.read_bytes()).decode("ascii") for p in sorted((web / "art").glob("*.jpg"))})
art = json.dumps(art_files, ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
body = (page.replace("/*CONTENT_JSON*/", content).replace("/*ART_JSON*/", art).replace("/*ICONS_JS*/", icons).replace("/*TITANS_JS*/", titans)
        .replace("/*TITANS3D_JS*/", titans3d).replace("/*GFX_JS*/", gfx).replace("/*APP_JS*/", app).replace("/*KINGDOM_JS*/", kingdom))
dist = web / "dist"
dist.mkdir(exist_ok=True)
(dist / "artifact.html").write_text(body, encoding="utf-8")
head, sep, rest = body.partition("</style>")
doc = ("<!doctype html>\n<html lang=\"ru\">\n<head>\n<meta charset=\"utf-8\">\n"
       "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, viewport-fit=cover\">\n"
       + head + sep + "\n</head>\n<body>\n" + rest + "\n</body>\n</html>\n")
(dist / "index.html").write_text(doc, encoding="utf-8")
print("artifact.html", (dist / "artifact.html").stat().st_size, "bytes; index.html", (dist / "index.html").stat().st_size, "bytes")
