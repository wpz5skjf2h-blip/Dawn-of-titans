# Web demo

Browser build of the kingdom and the M0 battle. The battle is `Source/DoTSim` and the economy is `Source/DoTKingdom`,
both compiled with Emscripten; no game rules live in JavaScript.

| File | Role |
|---|---|
| `dot_web.cpp` | C API over `dot::BattleSimulation` (`dot_*`: content from memory, stepping, commands, snapshots, replay verification) and `dot::kingdom::Kingdom` (`dk_*`: state as JSON, build, upgrade, collect, lands, attacks with the chosen army and spells, training, troop research, titans, ascension, relic items and repair, fusion, Spell Forge, Arcane Tower, land guards, the season, save/load) |
| `build.sh` | Builds `dist/dotsim.js` (WebAssembly embedded as base64) and `dist/dotsim-js.js` (pure JS fallback) |
| `page.html` | Markup and styles of the page and HUD |
| `icons.js` | Hand-drawn SVG icons (troops, buildings, spells, relics, titan classes, altars, lands, menu) shared by both screens; also cached as images for the island overlay |
| `app.js` | Battle: Three.js view, procedural titan animation, touch/mouse/keyboard gestures, HUD logic, modes |
| `kingdom.js` | Kingdom: floating island, procedural building and land models, overlay, shop, panels, lands (gold, farm, victory-point, ruins), season sheet, Barracks, Armoury, army, Hall of Titans, Relic Temple, relic inventory and repair, Spell Forge, Arcane Tower, land guards, battle preparation, saves (browser and claude.ai `db`) |
| `bundle.py` | Inlines `icons.js`, `app.js`, `kingdom.js` and `Content/Data` into `dist/index.html` (standalone) and `dist/artifact.html` (body only) |
| `make_content.py` | Writes `Content/Data` as one JSON file (handy for tools) |

Run: `python3 -m http.server 8000 --directory Web/dist`, then open `http://localhost:8000`.

Saves: the kingdom is kept in the browser (`localStorage`) and, inside claude.ai, in the viewer's private `db`
documents `kingdom` (the live copy) and `kingdom-prev` (a second kingdom of the same account). On start the account
copy wins when the browser had no kingdom, when it is the same kingdom (same seed) saved later, or when it is another
kingdom that has gone at least as far; the kingdom not shown goes to `kingdom-prev`, and the menu can switch to it.
"Start again" also keeps the old kingdom there.

Errors: a failing frame, panel or window of the kingdom shows its text once and the island keeps being drawn; if the
kingdom cannot start, the battle screen shows why.
