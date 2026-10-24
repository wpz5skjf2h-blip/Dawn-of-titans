# Web demo

Browser build of the kingdom and the M0 battle. The battle is `Source/DoTSim` and the economy is `Source/DoTKingdom`,
both compiled with Emscripten; no game rules live in JavaScript.

| File | Role |
|---|---|
| `dot_web.cpp` | C API over `dot::BattleSimulation` (`dot_*`: content from memory, stepping, commands, snapshots, replay verification) and `dot::kingdom::Kingdom` (`dk_*`: state as JSON, build, upgrade, collect, lands, attacks with the chosen army and spells, training, troop research, titans, relic items and repair, fusion, Spell Forge, Arcane Tower, land guards, save/load) |
| `build.sh` | Builds `dist/dotsim.js` (WebAssembly embedded as base64) and `dist/dotsim-js.js` (pure JS fallback) |
| `page.html` | Markup and styles of the page and HUD |
| `icons.js` | Hand-drawn SVG icons (troops, buildings, spells, relics, titan classes, altars, lands, menu) shared by both screens; also cached as images for the island overlay |
| `app.js` | Battle: Three.js view, procedural titan animation, touch/mouse/keyboard gestures, HUD logic, modes |
| `kingdom.js` | Kingdom: floating island, procedural building models, overlay, shop, panels, lands, Barracks, Armoury, army, Hall of Titans, Relic Temple, relic inventory and repair, Spell Forge, Arcane Tower, land guards, battle preparation, saves (browser and claude.ai `db`) |
| `bundle.py` | Inlines `icons.js`, `app.js`, `kingdom.js` and `Content/Data` into `dist/index.html` (standalone) and `dist/artifact.html` (body only) |
| `make_content.py` | Writes `Content/Data` as one JSON file (handy for tools) |

Run: `python3 -m http.server 8000 --directory Web/dist`, then open `http://localhost:8000`.
