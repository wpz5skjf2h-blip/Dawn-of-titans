# Web demo

Browser build of the kingdom and the M0 battle. The battle is `Source/DoTSim` and the economy is `Source/DoTKingdom`,
both compiled with Emscripten; no game rules live in JavaScript.

| File | Role |
|---|---|
| `dot_web.cpp` | C API over `dot::BattleSimulation` (`dot_*`: content from memory, stepping, commands, snapshots, replay verification) and `dot::kingdom::Kingdom` (`dk_*`: state as JSON, build, upgrade, collect, lands, attacks with the chosen army and spells, training, troop research, titans, ascension, combat and building relics with repair and improvement, fusion, Spell Forge, Arcane Tower, land guards, the season, save/load) |
| `build.sh` | Builds `dist/dotsim.js` (WebAssembly embedded as base64) and `dist/dotsim-js.js` (pure JS fallback) |
| `page.html` | Markup and styles of the page and HUD |
| `admin.js` | Error log (always on, kept in the browser) and the hidden admin mode for hunting bugs: edits the kingdom through its save, moves time, starts any battle, checks invariants, plays the kingdom at random, copies a bug report |
| `icons.js` | Hand-drawn SVG icons (troops, buildings, spells, relics, titan classes, altars, lands, menu) shared by both screens; also cached as images for the island overlay |
| `app.js` | Battle: Three.js view, procedural titan animation, touch/mouse/keyboard gestures, HUD logic, modes |
| `kingdom.js` | Kingdom: floating island, procedural building and land models, overlay, shop, panels, lands (gold, farm, victory-point, ruins), season sheet, Barracks, Armoury, envoy councils, army, Hall of Titans, Relic Temple with the Altar of Power, relic inventory (tabs, repair and improvement), building relic slots, Spell Forge, Arcane Tower, land guards, battle preparation, saves (browser and claude.ai `db`) |
| `bundle.py` | Inlines `admin.js`, `icons.js`, `app.js`, `kingdom.js` and `Content/Data` into `dist/index.html` (standalone) and `dist/artifact.html` (body only) |
| `make_content.py` | Writes `Content/Data` as one JSON file (handy for tools) |

Run: `python3 -m http.server 8000 --directory Web/dist`, then open `http://localhost:8000`.

Saves: the kingdom is kept in the browser (`localStorage`) and, inside claude.ai, in the viewer's private `db`
documents `kingdom` (the live copy) and `kingdom-prev` (a second kingdom of the same account). On start the account
copy wins when the browser had no kingdom, when it is the same kingdom (same seed) saved later, or when it is another
kingdom that has gone at least as far; the kingdom not shown goes to `kingdom-prev`, and the menu can switch to it.
"Start again" also keeps the old kingdom there.

Errors: a failing frame, panel or window of the kingdom shows its text once and the island keeps being drawn; if the
kingdom cannot start, the battle screen shows why.

Admin mode: open the kingdom menu (☰) and tap the line with the state hash seven times (in a battle: the engine line of
the battle menu), or open the page with `?admin`. A wrench button appears on the right; the same seven taps or the
panel's button switch it off. The panel has tabs for resources and the city, buildings, army and spells, titans and
relics, time (skips, speed, timers, the season, reinforcements), lands (world lands of any level and type, attacks,
owned lands), battles (speed, pause, auto, end a land battle with 0–3 stars, any scenario with any titans and AI),
checks, the save and the log. Kingdom edits change the C++ save (`dk_save` → JSON → `dk_load`, which clamps it) and keep a
copy of the kingdom from before the first edit, which the save tab brings back.
"Проверки" runs invariants (save round trip, the same hash after loading, six hours at once equal to 72 steps of five
minutes, resources, limits, building placement, relics, guards, the season). "Случайная игра" plays hundreds of random
actions through the regular `dk_*` calls (battles with random results, time jumps), checks the invariants after every
action, reports the first action that broke each one and restores the kingdom. The log records page errors, rejected
promises, console errors and warnings and files that failed to load; "Скопировать отчёт" puts the browser, screen,
kingdom summary, the last 60 entries and the save on the clipboard (or in a .txt file; inside claude.ai files are offered
through the artifact's `downloads` capability, so the page is published with `downloads` besides `db` and `user`).
