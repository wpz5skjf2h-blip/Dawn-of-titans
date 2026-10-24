#!/usr/bin/env python3
"""Generates Content/Data/kingdom/buildings.json and army.json from the values known from the original game.

Every known number below comes from the official Dawn of Titans wiki (see docs/Original_Reference.md).
Levels between known points are filled by geometric interpolation, levels past the last known point
continue the last growth rate. Each generated level carries "src": "wiki" when all of its main numbers
are known, otherwise "est".

Run: python3 SimTools/kingdom_data.py
"""
import json
import math
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "Content" / "Data" / "kingdom" / "buildings.json"


class E(float):
    """A value we chose ourselves (not from the wiki). Used as an interpolation anchor, marked "est"."""


def secs(text):
    """'1d 3h 30m' -> seconds."""
    total = 0
    for value, unit in re.findall(r"(\d+)\s*([dhms])", text):
        total += int(value) * {"d": 86400, "h": 3600, "m": 60, "s": 1}[unit]
    return total


def fill(known, max_level, integer=True, round_to=None):
    """Fills levels 1..max_level from {level: value}. Returns (values, estimated_flags)."""
    levels = sorted(known)
    values, est = [], []
    for lv in range(1, max_level + 1):
        if lv in known:
            v = known[lv]
            e = isinstance(v, E)
        else:
            lower = [k for k in levels if k < lv]
            upper = [k for k in levels if k > lv]
            if lower and upper:
                a, b = lower[-1], upper[0]
                va, vb = known[a], known[b]
                if va > 0 and vb > 0:
                    v = va * (vb / va) ** ((lv - a) / (b - a))
                else:
                    v = va + (vb - va) * (lv - a) / (b - a)
            elif lower:
                if len(lower) >= 2:
                    a, b = lower[-2], lower[-1]
                    va, vb = known[a], known[b]
                    ratio = (vb / va) ** (1 / (b - a)) if va > 0 and vb > 0 else 1.0
                    # Growth slows down towards the top levels, as in the original tables.
                    ratio = 1 + (ratio - 1) * 0.85 ** (lv - b)
                    v = vb * ratio ** (lv - b)
                else:
                    v = known[lower[-1]]
            else:
                v = known[upper[0]]
            e = True
        if integer:
            v = int(round(v))
            if round_to and e:
                v = int(round(v / round_to) * round_to)
        values.append(v)
        est.append(e)
    return values, est


def est_gems(seconds):
    """Skip price for levels the wiki does not list, fitted to the Castle column (5 m: 7 ... 9 d: 5 734)."""
    if seconds <= 0:
        return 0
    hours = seconds / 3600
    return max(1, int(round(70 * hours ** 0.82)))


def steps(pairs, max_level=25):
    """[(castle, value), ...] step function -> list indexed by castle level 1..max_level."""
    out = []
    for c in range(1, max_level + 1):
        v = 0
        for cl, val in pairs:
            if c >= cl:
                v = val
        out.append(v)
    return out


BUILDINGS = []


# In the original, build time grows almost linearly with gold cost (Gold Mine: 97 100 -> 4 h 55 m,
# 441 400 -> 20 h, 4 156 300 -> 9 d 2 h), about 0.18 s per gold. Used where the wiki gives no time.
SECONDS_PER_GOLD = 0.18


def building(id, name, category, role, size, unlock, counts, max_level, cost, time, castle,
             gems=None, stats=None, reqs=None, resource=None, cost_res="gold", instant=False, desc="",
             time_from_cost=False):
    """cost/time/gems: {level: value}; castle: {level: castle} or callable; stats: {key: {level: value}}."""
    costs, cost_est = fill(cost, max_level, round_to=100)
    if time_from_cost:
        times, time_est = [], []
        for i in range(max_level):
            lv = i + 1
            if lv in time:
                times.append(int(time[lv]))
                time_est.append(isinstance(time[lv], E))
            else:
                t = max(60, costs[i] * SECONDS_PER_GOLD)
                times.append(int(round(t / 300) * 300) if t > 3600 else int(round(t / 60) * 60))
                time_est.append(True)
    else:
        times, time_est = fill(time, max_level, round_to=300 if max(time.values() or [0]) > 3600 else 60)
    gem_known = gems or {}
    stat_values = {}
    stat_est = {}
    for key, known in (stats or {}).items():
        stat_values[key], stat_est[key] = fill(known, max_level)
    levels = []
    for i in range(max_level):
        lv = i + 1
        c = castle(lv) if callable(castle) else castle.get(lv)
        if c is None:
            c, _ = fill(castle, max_level)
            c = c[i]
        if lv in gem_known:
            g = gem_known[lv]
            g_est = False
        else:
            g = est_gems(times[i])
            g_est = True
        estimated = cost_est[i] or time_est[i] or any(stat_est[k][i] for k in stat_est)
        entry = {
            "level": lv,
            "cost": {cost_res: costs[i]} if costs[i] else {},
            "timeSec": times[i],
            "skipGems": g,
            "castle": max(1, int(c)),
            "src": "est" if estimated else "wiki",
        }
        if g_est:
            entry["skipGemsSrc"] = "est"
        for key in stat_values:
            entry[key] = stat_values[key][i]
        if reqs and lv in reqs:
            entry["requires"] = [{"building": b, "level": l, **({"count": n} if n > 1 else {})} for b, l, n in reqs[lv]]
        levels.append(entry)
    BUILDINGS.append({
        "id": id, "name": name, "category": category, "role": role,
        **({"resource": resource} if resource else {}),
        "size": size, "unlockCastle": unlock, "counts": counts,
        **({"instant": True} if instant else {}),
        "description": desc,
        "levels": levels,
    })


# --- Castle -------------------------------------------------------------------------------
castle_cost = {1: 0, 2: 2000, 3: 4000, 4: 6000, 5: 10600, 6: 29200, 7: 57300, 8: 112900, 9: 176500, 10: 264800,
               11: 393300, 12: 573800, 13: 802500, 14: 1089400, 20: 4069800}
castle_time = {1: 0, 2: secs("5m"), 3: secs("10m"), 4: secs("30m"), 5: secs("1h 30m"), 6: secs("2h 40m"), 7: secs("4h 50m"),
               8: secs("8h 30m"), 9: secs("14h"), 10: secs("19h 40m"), 11: secs("1d 3h 30m"), 12: secs("1d 15h"),
               13: secs("2d 2h"), 14: secs("2d 17h"), 20: secs("9d 3h 10m")}
castle_gems = {1: 0, 2: 7, 3: 18, 4: 130, 5: 156, 6: 208, 7: 291, 8: 452, 9: 615, 10: 863, 11: 1242, 12: 1618,
               13: 1911, 14: 2262, 20: 5734}
# Requirements repeat every three levels (confirmed 5-14 and 20); the rest continues the cycle.
castle_reqs = {
    5: [("garrison", 4, 1), ("army_camp", 3, 1)],
    6: [("food_reserve", 2, 1), ("gold_reserve", 2, 1)],
    7: [("garrison", 6, 1), ("arcane_tower", 1, 1)],
    8: [("armoury", 6, 1), ("hall_of_titans", 2, 1)],
    9: [("arcane_tower", 3, 1), ("portal", 2, 1)],
    10: [("garrison", 9, 1), ("army_camp", 6, 1)],
    11: [("armoury", 9, 1), ("hall_of_titans", 3, 1)],
    12: [("arcane_tower", 6, 1), ("portal", 3, 1)],
    13: [("garrison", 12, 1), ("army_camp", 7, 1)],
    14: [("hall_of_titans", 4, 1), ("armoury", 12, 1)],
    15: [("arcane_tower", 9, 1), ("portal", 4, 1)],
    16: [("garrison", 15, 1), ("army_camp", 8, 1)],
    17: [("armoury", 15, 1), ("hall_of_titans", 5, 1)],
    18: [("arcane_tower", 12, 1), ("portal", 5, 1)],
    19: [("garrison", 18, 1), ("army_camp", 10, 1)],
    20: [("armoury", 18, 1), ("hall_of_titans", 6, 1)],
    21: [("arcane_tower", 15, 1), ("portal", 6, 1)],
    22: [("garrison", 21, 1), ("army_camp", 11, 1)],
    23: [("armoury", 21, 1), ("hall_of_titans", 7, 1)],
    24: [("arcane_tower", 17, 1), ("portal", 7, 1)],
    25: [("garrison", 23, 1), ("army_camp", 13, 1)],
}
land_capacity = {1: 3, 2: 3, 3: 3, 4: 4, 5: 5, 6: 6, 7: 6, 8: 7, 9: 7, 10: 8, 11: 8, 12: 9, 13: 9, 14: 10, 15: 10, 16: 11,
                 17: 11, 18: 12, 19: 12, 20: 13, 21: 13, 22: 14, 23: 14, 24: 15, 25: 15}
building("castle", "Замок", "other", "castle", [6, 6], 1, [[1, 1]], 25, castle_cost, castle_time,
         lambda lv: max(1, lv - 1), gems=castle_gems, reqs=castle_reqs,
         stats={"landCapacity": land_capacity},
         desc="Сердце королевства. Его уровень открывает новые постройки и уровни остальных зданий.")
# Each Castle level requires the previous one, not itself.
for lv in BUILDINGS[-1]["levels"]:
    lv["castle"] = max(1, lv["level"] - 1)

# --- Resources ----------------------------------------------------------------------------
prod_cost = {1: 200, 2: 800, 3: 2400, 4: 4200, 5: 14600, 6: 31500, 7: 97100, 8: 145600, 9: 312300, 10: 441400,
             11: 791100, 12: 1017700, 13: 1571200, 14: 1892600, 15: 2604000, 16: 2983800, 17: 3765000, 18: 4156300}
prod_time = {1: secs("2m"), 2: secs("5m"), 3: secs("10m"), 4: secs("25m"), 5: secs("40m"), 6: secs("1h 15m"),
             7: secs("4h 55m"), 8: secs("7h 50m"), 9: secs("15h 25m"), 10: secs("20h"), 11: secs("1d 9h 50m"),
             12: secs("1d 18h 15m"), 13: secs("2d 12h 50m"), 14: secs("3d 1h 5m"), 15: secs("4d 9h 10m"),
             16: secs("5d 6h 15m"), 17: secs("7d 13h 50m"), 18: secs("9d 2h 15m")}
prod_rate = {1: 100, 2: 200, 3: 250, 4: 300, 5: 350, 6: 405, 7: 510, 8: 625, 9: 760, 10: 895, 11: 1030, 12: 1175,
             13: 1340, 14: 1505, 15: 1670, 16: 1865, 17: 2060, 18: 2190}
prod_store = {1: 600, 2: 1800, 3: 3000, 4: 4500, 5: 6300, 6: 8505, 7: 12240, 8: 15000, 9: 18240, 10: 21480,
              11: 24720, 12: 28200, 13: 32160, 14: 36120, 15: 40080, 16: 44760, 17: 49440, 18: 52560}
prod_castle = {1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 7: 8, 8: 9, 9: 11, 10: 12, 11: 14, 12: 15, 13: 17,
               14: 18, 15: 19, 16: 21, 17: 22, 18: 24}
prod_counts = [[1, 1], [2, 2], [4, 3], [6, 4], [8, 5], [10, 6], [11, 7], [12, 8], [16, 9], [19, 10]]
farm_gems = {1: 5, 10: 1405, 13: 2757, 18: 7052}
building("gold_mine", "Золотая шахта", "resources", "producer", [3, 3], 1, prod_counts, 18, prod_cost, prod_time, prod_castle,
         gems=farm_gems, resource="gold", stats={"produce": prod_rate, "store": prod_store},
         desc="Добывает золото. Шахта копит добычу до своей вместимости: соберите её в хранилище.")
building("farm", "Ферма", "resources", "producer", [4, 4], 1, prod_counts, 18, prod_cost, prod_time, prod_castle,
         gems=farm_gems, resource="food", stats={"produce": prod_rate, "store": prod_store},
         desc="Выращивает еду для обучения войск. Урожай копится до вместимости фермы.")

store_cap = {1: 3000, 2: 6000, 3: 11300, 4: 20300, 5: 34700, 6: 56700, 7: 88900, 8: 133900, 9: 194500, 10: 273100,
             11: 371700, 12: 491600, 15: 979000, 16: 1179800, 17: 1395900, 18: 1624300, 19: 1861600, 20: 2104500,
             21: 2350000, 22: 2590000}
store_cost = {1: 1000, 2: 2700, 3: 4800, 4: 17500, 5: 37200, 6: 73400, 7: 100300, 8: 172100, 9: 369100, 10: 521600,
              11: 708100, 12: 934900, 15: 1856900, 16: 2236700, 17: 2645400, 18: 3077400, 19: 3526300, 22: 4910000}
store_time = {1: secs("5m"), 2: secs("10m"), 3: secs("25m"), 4: secs("40m"), 5: secs("1h 15m"), 6: secs("2h 10m"),
              7: secs("4h 10m"), 8: secs("7h 50m"), 9: secs("15h 25m"), 10: secs("20h"), 11: secs("1d 2h"),
              12: secs("1d 9h 50m"), 15: secs("2d 12h 50m"), 16: secs("3d 1h 5m"), 17: secs("3d 15h 40m"),
              18: secs("4d 9h 10m"), 19: secs("5d 6h 15m"), 20: secs("6d 7h 30m"), 22: secs("9d 2h 15m")}
store_gems = {1: 12, 2: 52, 3: 130, 4: 182, 5: 265, 6: 371, 7: 533, 8: 1133, 9: 1432, 10: 1725, 11: 2014, 12: 2284,
              15: 2802, 16: 3171, 17: 3620, 18: 4170, 19: 4724, 22: 7252}
store_castle = lambda lv: 1 if lv == 1 else (lv + 1 if lv <= 12 else lv + 2)
store_counts = [[1, 1], [5, 2], [10, 3], [15, 4]]
vault_reqs = {18: [("gold_mine", 14, 4)], 19: [("gold_mine", 15, 4)], 20: [("gold_mine", 16, 4)], 21: [("gold_mine", 16, 5)],
              22: [("gold_mine", 17, 5)]}
granary_reqs = {lv: [("farm", b, n) for (_, b, n) in reqs] for lv, reqs in vault_reqs.items()}
building("gold_vault", "Золотое хранилище", "resources", "storage", [3, 3], 1, store_counts, 22,
         {**store_cost, 7: 114700}, store_time, store_castle, gems=store_gems, resource="gold",
         stats={"capacity": store_cap}, reqs=vault_reqs, desc="Хранит собранное золото. Улучшение увеличивает вместимость.")
building("granary", "Амбар", "resources", "storage", [3, 3], 1, store_counts, 22, store_cost, store_time, store_castle,
         gems=store_gems, resource="food", stats={"capacity": store_cap}, reqs=granary_reqs,
         desc="Хранит собранную еду. Улучшение увеличивает вместимость.")

res_protect = {1: 2000, 2: 5000, 5: 22000, 10: 148000, 22: 1148000}
res_cost = {1: 4200, 2: 13100, 5: 105900, 10: 762600, 22: 5959500}
res_time = {1: secs("40m"), 22: secs("12d 18h")}
res_castle = lambda lv: 1 if lv == 1 else lv + 3
for rid, rname, rres in (("gold_reserve", "Золотой резерв", "gold"), ("food_reserve", "Продовольственный резерв", "food")):
    building(rid, rname, "resources", "reserve", [2, 2], 3, [[3, 1], [8, 2]], 22, res_cost, res_time, res_castle,
             resource=rres, stats={"protect": res_protect}, time_from_cost=True,
             desc="Защищает часть запасов: эти ресурсы нельзя украсть при нападении.")

# --- Troops -------------------------------------------------------------------------------
building("barracks", "Казармы", "troops", "other", [4, 4], 1, [[1, 1]], 10,
         {1: 600, 3: 2400, 5: 114700, 9: 1859500, 10: 2924900},
         {1: secs("1m"), 5: secs("7h"), 9: secs("2d 21h 45m"), 10: secs("4d 4h 30m")},
         {1: 1, 2: 2, 3: 4, 4: 6, 5: 8, 6: 10, 7: 12, 8: 14, 9: 16, 10: 18},
         stats={"queue": {1: 6, 5: 12, 9: 20, 10: E(22)}},
         desc="Обучает войска. Улучшение удлиняет очередь обучения.")
building("army_camp", "Лагерь армии", "troops", "other", [4, 4], 1, [[1, 1]], 13,
         {1: 800, 7: 494000, 8: 947800, 13: 6700000}, {1: secs("1m"), 13: secs("17d")},
         {1: 1, 2: 2, 3: 4, 4: 6, 5: 7, 6: 9, 7: 11, 8: 14, 9: 15, 10: 18, 11: 21, 12: 23, 13: 24},
         stats={"troops": {1: 10, 2: 20, 3: 30, 4: 50, 5: 70, 6: 140, 7: 180, 8: 225, 13: E(480)},
                "slots": {1: 3, 13: 14}}, time_from_cost=True,
         reqs={7: [("alliance_camp", 5, 1)], 8: [("alliance_camp", 6, 1)]},
         desc="Здесь стоят обученные войска. Улучшение увеличивает их число и слоты армии в бою.")
building("garrison", "Гарнизон", "troops", "other", [3, 3], 2, [[2, 1]], 23,
         {1: 800, 5: 17500, 10: 334300, 15: 1665400, 20: 4497800, 22: 5886600, 23: 6640000},
         {1: secs("5m"), 5: secs("2h 15m"), 10: secs("23h 25m"), 15: secs("3d 17h 50m"), 20: secs("9d 7h 35m"), 22: secs("13d 10h")},
         lambda lv: max(1, lv), gems={1: 5, 5: 291, 10: 1458, 15: 2869, 20: 5562, 22: 7453},
         stats={"defenders": {1: E(5), 23: E(60)}},
         reqs={5: [("gold_reserve", 2, 1)], 10: [("food_reserve", 7, 2)]},
         desc="Войска гарнизона защищают город от нападений других игроков.")
building("armoury", "Арсенал", "troops", "other", [3, 3], 2, [[2, 1]], 24,
         {1: E(1500), 15: 1999000, 16: 2513900, 17: 3096900, 18: 3662800, 19: 4261100, 24: E(7200000)},
         {1: secs("5m"), 15: secs("3d 23h 10m"), 16: secs("4d 18h 10m"), 17: secs("5d 17h"), 18: secs("6d 20h 25m"),
          19: secs("8d 5h 15m"), 24: E(secs("20d 6h"))},
         lambda lv: 2 if lv == 1 else min(25, lv + 1), gems={15: 4497, 16: 5190, 17: 6107, 18: 7080, 19: 8083},
         stats={"maxTroopLevel": {lv: E(lv + 1) for lv in range(1, 25)}}, time_from_cost=True,
         reqs={5: [("barracks", 4, 1)], 7: [("barracks", 5, 1)], 15: [("barracks", 7, 1)], 16: [("barracks", 8, 1)]},
         desc="Повышает уровень производимых войск. Улучшение поднимает предел уровня войск.")
building("medical_school", "Медицинская школа", "troops", "other", [3, 3], 7, [[7, 1]], 11,
         {1: 62100, 4: 817100, 11: 8002800}, {4: secs("1d 8h")},
         {1: 7, 2: 9, 3: 11, 4: 13, 5: 15, 6: 17, 7: 19, 8: 21, 9: 23, 10: 24, 11: 25},
         gems={1: 130, 11: 11302}, stats={"survival": {1: 85, 11: 75}}, time_from_cost=True,
         desc="Раненые в бою бойцы лечатся и возвращаются в армию. Улучшение снижает порог выживания.")
building("alliance_camp", "Лагерь альянса", "troops", "other", [3, 3], 6, [[6, 1]], 10,
         {1: E(30000), 10: E(5500000)}, {},
         {1: 6, 2: 7, 3: 7, 4: 8, 5: 9, 6: 12, 7: 15, 8: 18, 9: 21, 10: 24},
         stats={"reinforcements": {1: E(1), 10: E(10)}}, time_from_cost=True,
         desc="Сюда союзники присылают подкрепления. Улучшение увеличивает их число.")

# --- Spells -------------------------------------------------------------------------------
building("arcane_tower", "Магическая башня", "spells", "other", [2, 2], 6, [[6, 1]], 18,
         {1: 57200, 17: 6009300, 18: 6708500}, {},
         lambda lv: 6 if lv == 1 else min(25, 6 + lv),
         stats={"spellSlots": {1: 1, 18: 6}, "maxSpellLevel": {1: 6, 17: 22, 18: 23}}, time_from_cost=True,
         desc="Улучшает заклинания. Уровень башни задаёт число одновременных улучшений и их предел.")
building("spell_forge", "Кузня заклинаний", "spells", "other", [3, 3], 5, [[5, 1]], 9,
         {1: 10200, 9: 4991000}, {},
         {1: 5, 2: 7, 3: 9, 4: 11, 5: 13, 6: 15, 7: 17, 8: 19, 9: 21},
         stats={"queue": {1: 2, 9: 10}, "capacity": {1: 10, 9: 65}}, time_from_cost=True,
         desc="Создаёт заклинания для боя. Улучшение открывает новые заклинания и увеличивает запас.")

# --- Other --------------------------------------------------------------------------------
building("portal", "Портал", "other", "portal", [3, 3], 1, [[1, 1]], 7,
         {1: 0, 2: 132400, 3: 494000, 4: 1290000, 5: 2630000, 6: 4450000, 7: 6710000},
         {1: 0, 2: secs("6h 20m"), 3: secs("17h 20m"), 4: secs("1d 14h"), 5: secs("2d 20h 30m"), 6: secs("4d 22h 20m"),
          7: secs("8d 12h 35m")},
         {1: 1, 2: 8, 3: 11, 4: 14, 5: 17, 6: 20, 7: 23}, gems={1: 0, 2: 69, 7: 6966},
         stats={"stones": {1: 18, 2: 20, 3: 22, 4: 24, 5: 26, 6: 28, 7: 30}},
         desc="Хранит камни портала, на которых войска переносятся в бой. Камень восстанавливается раз в 7 минут.")
for lv in BUILDINGS[-1]["levels"]:
    if lv["level"] >= 3:
        lv["expansion"] = lv["level"] - 2
building("builders_yard", "Двор строителей", "other", "builders", [3, 3], 1, [[1, 1]], 5,
         {1: 0, 2: 1500, 3: 4000, 4: 9000, 5: 20000}, {1: 0, 2: 0, 3: 0, 4: 0, 5: 0}, {lv: 1 for lv in range(1, 6)},
         gems={lv: 0 for lv in range(1, 6)}, stats={"builders": {1: 1, 2: 2, 3: 3, 4: 4, 5: 5}}, cost_res="gems", instant=True,
         desc="Строители возводят и улучшают постройки. Каждый строитель ведёт одну стройку одновременно.")
for lv in BUILDINGS[-1]["levels"]:
    lv["src"] = "wiki"
building("hall_of_titans", "Зал титанов", "other", "other", [5, 5], 1, [[1, 1]], 7,
         {1: 0, 2: 83500, 3: 354000, 4: 904200, 5: 2208200, 6: 3907000, 7: 5947900},
         {1: 0, 7: secs("9d 11h 20m")},
         {1: 1, 2: 7, 3: 10, 4: 13, 5: 16, 6: 19, 7: 22},
         stats={"titans": {1: 100, 2: 120, 3: 140, 4: 160, 5: 180, 6: 200, 7: 220}},
         reqs={2: [("medical_school", 1, 1)]}, time_from_cost=True,
         desc="Здесь живут ваши титаны. Улучшение увеличивает число титанов, которых можно держать.")
building("relic_temple", "Храм реликвий", "other", "other", [4, 4], 3, [[3, 1]], 1, {1: E(25000)}, {1: E(secs("1h"))}, {1: 3},
         desc="Призыв титанов и реликвий. Раз в день даёт бесплатную реликвию.")
building("eternal_shrine", "Вечное святилище", "other", "other", [2, 2], 5, [[5, 1]], 1, {1: E(50000)}, {1: E(secs("2h"))}, {1: 5},
         desc="Украшение королевства. Каждый день даёт 11 божественных самоцветов.")
building("adventurers_guild", "Гильдия искателей приключений", "other", "other", [3, 3], 8, [[8, 1]], 1,
         {1: E(120000)}, {1: E(secs("4h"))}, {1: 8},
         desc="Здесь открываются временные кампании. Новая кампания отмечена золотой статуей на башне.")

OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps({"schemaVersion": 1, "source": "docs/Original_Reference.md", "buildings": BUILDINGS},
                          ensure_ascii=False, indent=1), encoding="utf-8")
known = sum(1 for b in BUILDINGS for lv in b["levels"] if lv["src"] == "wiki")
total = sum(len(b["levels"]) for b in BUILDINGS)
print(f"{len(BUILDINGS)} buildings, {total} levels, {known} fully from the wiki -> {OUT.relative_to(ROOT)}")

# --- Army and titans (army.json) ----------------------------------------------------------
# Troops of the original's human line: Militia, Archers, Spearmen; then the rest of the battle roster (below).
# Wiki: Militia L1..7 food per unit 135, 215, 540, 910, 1595, 2400, 2640 and upgrade 3500, 9800, 12500, 18000,
# 46400, 72800, 111500; train time 1 m (1 m 20 s at L10); Spearmen L1 145 food, upgrade 6600, train 5 m;
# Archers L1 200 food, upgrade 8800. Other levels follow the Militia progression (estimate).
TROOP_LEVELS = 21
mil_cost, mil_cost_est = fill({1: 135, 2: 215, 3: 540, 4: 910, 5: 1595, 6: 2400, 7: 2640}, TROOP_LEVELS, round_to=5)
mil_up, mil_up_est = fill({1: 3500, 2: 9800, 3: 12500, 4: 18000, 5: 46400, 6: 72800, 7: 111500}, TROOP_LEVELS - 1, round_to=100)
up_time, _ = fill({1: E(secs("30m")), 13: secs("2d 19h")}, TROOP_LEVELS - 1, round_to=60)


def tier_of(level):
    return 1 if level <= 2 else 2 if level <= 5 else 3 if level <= 9 else 4 if level <= 14 else 5


def troop(id, name, unlock, train, cost1, up1, scale_src):
    train_t, train_est = fill(train, TROOP_LEVELS, round_to=5)
    levels = []
    for i in range(TROOP_LEVELS):
        lv = i + 1
        if lv == 1:
            cost, up = cost1, up1
            src = "wiki" if not isinstance(train.get(1), E) else "est"
        else:
            cost = int(round(mil_cost[i] * cost1 / 135 / 5) * 5)
            up = int(round(mil_up[i] * up1 / 3500 / 100) * 100) if i < TROOP_LEVELS - 1 else 0
            src = "wiki" if scale_src and not mil_cost_est[i] and (i >= TROOP_LEVELS - 1 or not mil_up_est[i]) else "est"
        levels.append({"level": lv, "cost": cost, "trainSec": train_t[i], "upgradeCost": up if lv < TROOP_LEVELS else 0,
                       "upgradeSec": up_time[i] if lv < TROOP_LEVELS else 0, "tier": tier_of(lv), "src": src})
    return {"id": id, "name": name, "unlockCastle": unlock, "levels": levels}


TROOPS = [
    troop("troop.militia", "Ополчение", 1, {1: 60, 8: 60, 10: 80}, 135, 3500, True),
    troop("troop.archers", "Лучники", 1, {1: E(180)}, 200, 8800, False),
    troop("troop.spearmen", "Копейщики", 3, {1: 300}, 145, 6600, False),
    # Beyond the original's three human troops: the rest of the battle roster (master prompt 4.9). The original gave other
    # peoples' troops through envoy councils (grenadiers among them); here the Barracks train them, opening with the Castle.
    # Costs and times are ours, scaled from the master prompt's training table against the Militia.
    troop("troop.swordsmen", "Мечники", 4, {1: E(120)}, 230, 9000, False),
    troop("troop.grenadiers", "Гренадеры", 5, {1: E(180)}, 300, 12000, False),
    troop("troop.outriders", "Всадники", 6, {1: E(150)}, 330, 11000, False),
    troop("troop.priests", "Боевые жрецы", 8, {1: E(200)}, 280, 10000, False),
    troop("troop.chainbreakers", "Цепеломы", 9, {1: E(200)}, 320, 12000, False),
    troop("troop.knights", "Рыцари", 11, {1: E(240)}, 480, 16000, False),
]
# Titans: the original sorts them by rarity R1-R4 (stars) and class; max level 10 + 10 x stars (wiki, version 1.8).
# The names are ours (mythology); the original roster has its own characters.
TITANS = [
    ("titan.talos", "Талос", "Champion", 1), ("titan.minotaur", "Минотавр", "Berserker", 1), ("titan.cyclops", "Циклоп", "Guardian", 1),
    ("titan.perun", "Перун", "Berserker", 2), ("titan.atlas", "Атлант", "Guardian", 2), ("titan.ymir", "Имир", "Champion", 2),
    ("titan.surtr", "Сурт", "Berserker", 3), ("titan.hecate", "Геката", "Sorcerer", 3), ("titan.artemis", "Артемида", "Ranger", 3),
    ("titan.thor", "Тор", "Berserker", 4),
]
CLASS_RU = {"Champion": "Чемпион", "Berserker": "Берсерк", "Guardian": "Страж", "Sorcerer": "Чародей", "Ranger": "Следопыт"}
ARMY = {
    "schemaVersion": 1,
    "source": "docs/Original_Reference.md",
    "troops": TROOPS,
    "titans": [{"id": i, "name": n, "class": c, "classRu": CLASS_RU[c], "stars": st} for i, n, c, st in TITANS],
    # Our estimates: XP to the next level, XP per land battle by land level, share of XP by stars (0..3),
    # recovery after a battle for titans above level 10.
    "titanXp": {"next": [30 * lv * lv + 70 * lv for lv in range(1, 60)], "battle": [60, 90, 120, 160, 200, 250, 300, 360, 420],
                "starsPct": [30, 100, 120, 150], "recoveryFromLevel": 10, "recoverySecPerLevel": 120},
    # Fusion unlocks at Castle 7 and costs food (wiki); XP and the food amount are estimates.
    "fusion": {"castle": 7, "foodPerStar": 2000, "xpPerStarSq": 150, "keepXpPct": 50},
    # Relic Temple: a free relic every day (wiki); altars for 1 000 and 4 000 gems (wiki); R4 only from altars (wiki).
    # Odds by rarity R1..R4 are estimates. The daily altar may give a relic item instead (see "relics").
    "altars": [
        {"id": "daily", "name": "Ежедневная реликвия", "gems": 0, "cooldownSec": 86400, "weights": [70, 25, 5, 0]},
        {"id": "silver", "name": "Серебряный алтарь", "gems": 1000, "cooldownSec": 0, "weights": [0, 60, 35, 5]},
        {"id": "gold", "name": "Золотой алтарь", "gems": 4000, "cooldownSec": 0, "weights": [0, 0, 70, 30]},
    ],
    # The original's spells (wiki: Fireball, Shield, Freeze, Terror, Lightning, Rage, Acid Cloud, Raise Dead).
    # The Spell Forge crafts them into stock (queue and capacity by level, wiki) and each cast uses one.
    # Which Forge level opens each spell, craft prices and times are estimates.
    "spells": [
        {"id": "spell.fireball", "name": "Огненный шар", "forge": 1, "craftGold": 1500, "craftSec": 600},
        {"id": "spell.shield", "name": "Щит", "forge": 1, "craftGold": 1200, "craftSec": 480},
        {"id": "spell.freeze", "name": "Заморозка", "forge": 2, "craftGold": 1800, "craftSec": 720},
        {"id": "spell.lightning", "name": "Молния", "forge": 3, "craftGold": 2200, "craftSec": 900},
        {"id": "spell.rage", "name": "Ярость", "forge": 4, "craftGold": 1600, "craftSec": 600},
        {"id": "spell.terror", "name": "Ужас", "forge": 5, "craftGold": 2000, "craftSec": 720},
        {"id": "spell.acid_cloud", "name": "Кислотное облако", "forge": 6, "craftGold": 2400, "craftSec": 900},
        {"id": "spell.raise_dead", "name": "Поднять мёртвых", "forge": 7, "craftGold": 2800, "craftSec": 1080},
    ],
    # Arcane Tower research: one spell level at a time per research slot (slots and level cap by tower level, wiki).
    # Price and time for level L -> L + 1: base x growth^(L - 1) (estimate). Craft price grows 15% per spell level.
    "spellResearch": {"goldBase": 20000, "goldGrowth": 1.55, "secBase": 7200, "secGrowth": 1.45, "craftPctPerLevel": 15},
    # Spells a battle can take (distinct kinds); every charge in stock goes along.
    "battleSpellKinds": 3,
    # Relics (wiki: they raise a titan's damage, health, armor; one slot at first, more as the titan levels up;
    # they wear out with use; repair by sacrificing relics with a chance, from Castle 8; rarity R1-R5).
    # Repair also costs food (wiki: food goes to troops, relic repair and fusion). New slots at titan levels 20, 30, 40, 50
    # (guide). Bonus, durability, repair weights, repair food and drop odds are estimates.
    "relics": {
        "kinds": [{"id": "atk", "name": "Реликвия ярости", "stat": "atkPct"}, {"id": "hp", "name": "Реликвия стойкости", "stat": "hpPct"},
                  {"id": "armor", "name": "Реликвия брони", "stat": "armorPct"}],
        "bonusPct": [3, 5, 8, 12, 16], "durability": [10, 14, 18, 24, 30], "slotLevels": [1, 20, 30, 40, 50],
        "repairCastle": 8, "repairWeight": [10, 20, 35, 50, 70], "repairFood": [1000, 2500, 6000, 15000, 30000], "maxRelics": 100,
        "dailyRelicPct": 60, "dailyRarity": [60, 35, 5, 0, 0],
    },
    # A titan on a land adds 1% production per titan level (wiki).
    "landTitanPctPerLevel": 1,
    # Training and research skips: one gem per started minute (estimate).
    "skipSecPerGem": 60,
    "start": {"army": {"troop.militia": 3, "troop.archers": 1},
              "titans": [{"id": "titan.perun", "level": 5}, {"id": "titan.talos", "level": 5}]},
}
ARMY_OUT = OUT.parent / "army.json"
ARMY_OUT.write_text(json.dumps(ARMY, ensure_ascii=False, indent=1), encoding="utf-8")
print(f"{len(TROOPS)} troops x {TROOP_LEVELS} levels, {len(TITANS)} titans -> {ARMY_OUT.relative_to(ROOT)}")
