#!/usr/bin/env bash
# Builds the browser demo's simulation module from the same sources as the Unreal DoTSim module.
#   source /path/to/emsdk/emsdk_env.sh && Web/build.sh
# Output: Web/dist/dotsim.js (WebAssembly, embedded) and Web/dist/dotsim-js.js (pure JS fallback
# for hosts whose content security policy forbids WebAssembly compilation).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/Web/dist"
mkdir -p "$OUT"
SOURCES="$(ls "$ROOT"/Source/DoTSim/Private/*.cpp | grep -v DoTSimModule.cpp) $(ls "$ROOT"/Source/DoTKingdom/Private/*.cpp | grep -v DoTKingdomModule.cpp)"
EXPORTS='["_dot_add_file","_dot_load_content","_dot_error","_dot_start","_dot_replay","_dot_is_replay","_dot_cmd","_dot_cmd_path","_dot_step","_dot_tick","_dot_soldiers","_dot_soldier_count","_dot_titans","_dot_titan_count","_dot_height","_dot_pick","_dot_static","_dot_frame","_dot_verify","_dot_hash","_malloc","_free","_dk_init","_dk_new","_dk_load","_dk_save","_dk_advance","_dk_state","_dk_build","_dk_upgrade","_dk_speedup","_dk_collect","_dk_collect_all","_dk_move","_dk_can_place","_dk_buy_expansion","_dk_buy_slot","_dk_refill_stones","_dk_collect_land","_dk_abandon_land","_dk_attack_start","_dk_attack_finish","_dk_train","_dk_cancel_train","_dk_skip_train","_dk_dismiss","_dk_upgrade_troop","_dk_skip_research","_dk_open_altar","_dk_fuse","_dk_fuse_check","_dk_skip_titan","_dk_craft","_dk_cancel_craft","_dk_skip_craft","_dk_upgrade_spell","_dk_skip_spell","_dk_equip","_dk_attach","_dk_unequip","_dk_discard","_dk_repair_chance","_dk_repair_unlock","_dk_repair","_dk_assign","_dk_recall","_dk_ascend"]'
COMMON=(-std=c++17 -O3 -fno-exceptions -fno-rtti -Wall -Wextra -Wshadow
  -I"$ROOT/Source/DoTSim/Public" -I"$ROOT/Source/DoTKingdom/Public"
  -sMODULARIZE=1 -sEXPORT_NAME=createDotSim -sENVIRONMENT=web,worker,node
  -sFILESYSTEM=0 -sSTACK_SIZE=1MB -sSINGLE_FILE=1 -sSINGLE_FILE_BINARY_ENCODE=0
  -sEXPORTED_FUNCTIONS="$EXPORTS"
  -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","lengthBytesUTF8","HEAPF32","HEAPF64"]')
em++ "${COMMON[@]}" -sALLOW_MEMORY_GROWTH=1 $SOURCES "$ROOT/Web/dot_web.cpp" -o "$OUT/dotsim.js"
em++ "${COMMON[@]}" -sWASM=0 -sINITIAL_MEMORY=64MB $SOURCES "$ROOT/Web/dot_web.cpp" -o "$OUT/dotsim-js.js"
ls -la "$OUT"
