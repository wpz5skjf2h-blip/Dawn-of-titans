// Web/kingdom.js - the kingdom screen of the browser demo: the floating island city with its
// buildings, resources, builders, portal stones and captured lands (Dawn of Titans' meta layer).
// The economy is the deterministic C++ module Source/DoTKingdom (dk_* functions of Web/dot_web.cpp);
// this file only draws it and turns taps into actions. Land battles run in Web/app.js (window.dotApp).
(() => {
'use strict';

const T = THREE;
const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const lerp = (a, b, t) => a + (b - a) * t;
const rad = T.MathUtils.degToRad;
const TILE = 2; // world units per grid cell
const SAVE_KEY = 'dot.kingdom.v1';
const HINT_KEY = 'dot.kingdom.welcomed';

// Mirrors dot::kingdom::Error (Source/DoTKingdom/Public/DoTKingdom/Kingdom.h).
const E = { None: 0, BadId: 1, MaxLevel: 2, CastleTooLow: 3, NeedRequirement: 4, NeedExpansion: 5, Locked: 6, LimitReached: 7,
	NoBuilder: 8, NotEnough: 9, BadPlacement: 10, Busy: 11, NotBusy: 12, Nothing: 13, StorageFull: 14, NeedStones: 15, NoLandSlot: 16, NoAttack: 17,
	QueueFull: 18, Researching: 19, TooManySquads: 20, NoTroops: 21, TitanRecovering: 22, NotReady: 23, RosterFull: 24, NotMaxLevel: 25,
	WrongRelic: 26, SameRelic: 27, InventoryFull: 28, NeedVp: 29, AllianceFull: 30, InAlliance: 31, NoAlliance: 32 };
// Mirrors dot::kingdom::Res: shards come from ruins and pay for ascension; VP (victory points) rank the season.
const RES = ['gold', 'food', 'gems', 'stones', 'shards', 'vp'];
const RES_OF = { gold: 'золота', food: 'еды', gems: 'самоцветов', stones: 'камней портала', shards: 'осколков', vp: 'очков победы' };
const SEASON_KEY = 'dot.kingdom.seasonSeen';
const AI_RU = { recruit: 'Новобранец', veteran: 'Ветеран', warlord: 'Полководец', nightmare: 'Кошмар' };
const TROOP_RU = { 'troop.militia': 'ополчение', 'troop.swordsmen': 'мечники', 'troop.spearmen': 'копейщики', 'troop.archers': 'лучники',
	'troop.outriders': 'всадники', 'troop.knights': 'рыцари', 'troop.grenadiers': 'гренадеры', 'troop.chainbreakers': 'цепеломы',
	'troop.priests': 'боевые жрецы', 'troop.pikemen': 'пикинёры', 'troop.goliaths': 'голиафы', 'troop.panthers': 'пантеры',
	'troop.ballista': 'расчёт баллисты' };
const CAVALRY = new Set(['troop.outriders', 'troop.knights', 'troop.panthers']);
/** Troops that deploy behind the melee line. */
const BACK_ROW = new Set(['troop.archers', 'troop.grenadiers', 'troop.priests', 'troop.ballista']);
const CATS = [['resources', 'Ресурсы'], ['troops', 'Войска'], ['councils', 'Советы'], ['spells', 'Заклинания'], ['other', 'Прочее']];

// Shared state of the kingdom screen.
const K = {
	app: null, bridge: null, defs: [], rules: null, sandbox: null, st: null, prev: null,
	sel: null, // { kind: 'b' | 'land', id }
	place: null, // placement or move mode
	sheet: null, // open sheet: { render, live, key }
	armed: null, // gem spend waiting for a second tap: { act, until }
	floaters: [], hits: [], lastStateAt: 0, lastStateSec: 0, lastSave: 0, ready: false, reported: new Set(),
};

// ---------------------------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------------------------
function plural(n, one, few, many) {
	const m10 = n % 10;
	const m100 = n % 100;
	if (m10 === 1 && m100 !== 11) return one;
	if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
	return many;
}
function fmt(n) { return Math.floor(n).toLocaleString('ru-RU'); }
function fmtShort(n) {
	if (n >= 1e6) return (n / 1e6).toFixed(n >= 1e7 ? 1 : 2).replace('.', ',').replace(/,?0+$/, '') + ' млн';
	if (n >= 1e5) return Math.round(n / 1e3) + ' тыс.';
	return fmt(n);
}
function fmtDur(s) {
	s = Math.max(0, Math.ceil(s));
	const d = Math.floor(s / 86400);
	const h = Math.floor((s % 86400) / 3600);
	const m = Math.floor((s % 3600) / 60);
	const sec = s % 60;
	if (d) return d + ' д' + (h ? ' ' + h + ' ч' : '');
	if (h) return h + ' ч' + (m ? ' ' + m + ' мин' : '');
	if (m) return m + ' мин' + (sec && m < 10 ? ' ' + sec + ' с' : '');
	return sec + ' с';
}
function fmtClock(s) {
	s = Math.max(0, Math.ceil(s));
	return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}
function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }

// ---------------------------------------------------------------------------------------------
// Icons: one spec drawn as inline SVG in the HUD and on the overlay canvas.
// ---------------------------------------------------------------------------------------------
const ICONS = {
	gold: [['c', 12, 12, 9.5, '#f2c14e', '#8a5a14', 1.6], ['c', 12, 12, 6, 'none', '#b9821f', 1.4], ['p', 'M8.4 9a4.6 4.6 0 0 1 3.6-2.4', 'none', '#fff3c4', 1.6]],
	food: [['p', 'M12 22V8', 'none', '#7a5a1c', 1.8], ['p', 'M12 8c-2.2-1-2.6-4-1-6 2.2 1 2.6 4 1 6z', '#e8c25a', '#7a5a1c', 1],
		['p', 'M12 12.5c-3 .2-5-2-5.2-4.6 2.8-.2 5 1.8 5.2 4.6z', '#e8c25a', '#7a5a1c', 1], ['p', 'M12 12.5c3 .2 5-2 5.2-4.6-2.8-.2-5 1.8-5.2 4.6z', '#e8c25a', '#7a5a1c', 1],
		['p', 'M12 17c-3 .2-5-2-5.2-4.6 2.8-.2 5 1.8 5.2 4.6z', '#d4ad44', '#7a5a1c', 1], ['p', 'M12 17c3 .2 5-2 5.2-4.6-2.8-.2-5 1.8-5.2 4.6z', '#d4ad44', '#7a5a1c', 1]],
	gems: [['p', 'M6 3.5h12l4.5 5.5L12 21.5 1.5 9z', '#7fe0e6', '#1d6b74', 1.4], ['p', 'M1.5 9h21M8.5 3.5L7 9l5 12.5L17 9l-1.5-5.5', 'none', '#1d6b74', 1]],
	stones: [['p', 'M12 2l7.5 4.5v11L12 22l-7.5-4.5v-11z', '#b48cff', '#4b2a8a', 1.4], ['p', 'M12 2v20M4.5 6.5L12 11l7.5-4.5', 'none', '#e8dcff', 1]],
	builders: [['p', 'M5 20l8.5-8.5', 'none', '#d9c3a0', 3], ['p', 'M10.5 6.5l3.5-3.5 6.5 6.5-3.5 3.5z', '#b5ab9a', '#3a352d', 1.4]],
	shards: [['p', 'M8.5 2l5 6.5-2.5 13.5-6-9.5z', '#9fe3ff', '#1f5f7a', 1.4], ['p', 'M15.5 7l5 4.5-4.5 10-2.6-2.4z', '#c6b3ff', '#4b2a8a', 1.4],
		['p', 'M8.5 2l2.2 10', 'none', '#effcff', 1.1]],
	vp: [['p', 'M12 2.5l2.9 6 6.6.8-4.9 4.5 1.3 6.5L12 17.1l-5.9 3.2 1.3-6.5L2.5 9.3l6.6-.8z', '#ffcf4a', '#8a4a12', 1.4], ['p', 'M12 7.5v5', 'none', '#fff3c4', 1.6]],
};
function iconSvg(kind) {
	const parts = ICONS[kind].map((p) => p[0] === 'c'
		? '<circle cx="' + p[1] + '" cy="' + p[2] + '" r="' + p[3] + '" fill="' + p[4] + '" stroke="' + p[5] + '" stroke-width="' + p[6] + '"/>'
		: '<path d="' + p[1] + '" fill="' + p[2] + '" stroke="' + p[3] + '" stroke-width="' + p[4] + '"/>');
	return '<svg viewBox="0 0 24 24" aria-hidden="true" stroke-linecap="round" stroke-linejoin="round">' + parts.join('') + '</svg>';
}
const pathCache = new Map();
function drawIcon(c, kind, x, y, size) {
	c.save();
	c.translate(x - size / 2, y - size / 2);
	c.scale(size / 24, size / 24);
	c.lineCap = 'round';
	c.lineJoin = 'round';
	for (const p of ICONS[kind]) {
		let path;
		if (p[0] === 'c') { path = new Path2D(); path.arc(p[1], p[2], p[3], 0, Math.PI * 2); } else {
			path = pathCache.get(p[1]);
			if (!path) { path = new Path2D(p[1]); pathCache.set(p[1], path); }
		}
		const fill = p[0] === 'c' ? p[4] : p[2];
		const stroke = p[0] === 'c' ? p[5] : p[3];
		const lw = p[0] === 'c' ? p[6] : p[4];
		if (fill !== 'none') { c.fillStyle = fill; c.fill(path); }
		if (stroke !== 'none') { c.strokeStyle = stroke; c.lineWidth = lw; c.stroke(path); }
	}
	c.restore();
}
/** "1 200 [gold icon]" for costs; red when the player has less. */
function costHtml(cost, have) {
	const out = [];
	cost.forEach((v, i) => {
		if (v > 0) out.push('<span class="cost' + (have && have[i] < v ? ' short' : '') + '">' + iconSvg(RES[i]) + fmtShort(v) + '</span>');
	});
	return out.length ? out.join(' ') : 'бесплатно';
}

// ---------------------------------------------------------------------------------------------
// Bridge to the C++ kingdom (Web/dot_web.cpp)
// ---------------------------------------------------------------------------------------------
class KingdomBridge {
	constructor(M) { this.M = M; }
	str(p) { return this.M.UTF8ToString(p); }
	init() { return this.M._dk_init() === 1; }
	error() { return this.str(this.M._dot_error()); }
	newGame(now, seed) { this.M._dk_new(now, seed); }
	load(text) { return this.M.ccall('dk_load', 'number', ['string'], [text]) === 1; }
	save() { return this.str(this.M._dk_save()); }
	advance(now) { this.M._dk_advance(now); }
	state() { return JSON.parse(this.str(this.M._dk_state())); }
	build(def, x, y, gems) { return this.M._dk_build(def, x, y, gems ? 1 : 0); }
	upgrade(id, gems) { return this.M._dk_upgrade(id, gems ? 1 : 0); }
	speedup(id) { return this.M._dk_speedup(id); }
	collect(id) { return JSON.parse(this.str(this.M._dk_collect(id))); }
	move(id, x, y) { return this.M._dk_move(id, x, y); }
	canPlace(def, x, y, ignore) { return this.M._dk_can_place(def, x, y, ignore || 0) === 1; }
	buyExpansion(gems) { return this.M._dk_buy_expansion(gems ? 1 : 0); }
	buySlot() { return this.M._dk_buy_slot(); }
	refillStones() { return this.M._dk_refill_stones(); }
	collectLand(id) { return JSON.parse(this.str(this.M._dk_collect_land(id))); }
	abandonLand(id) { return this.M._dk_abandon_land(id); }
	upgradeLand(id, gems) { return this.M._dk_land_upgrade(id, gems ? 1 : 0); }
	speedupLand(id) { return this.M._dk_land_speedup(id); }
	attackStart(worldId, capture, squads, titan, spells, reinforce) {
		return this.M.ccall('dk_attack_start', 'number', ['number', 'number', 'string', 'number', 'string', 'number'],
			[worldId, capture ? 1 : 0, (squads || []).join(','), titan || 0, (spells || []).join(','), reinforce ? 1 : 0]);
	}
	attackFinish(worldId, capture, stars, survivors, used) {
		return JSON.parse(this.str(this.M.ccall('dk_attack_finish', 'number', ['number', 'number', 'number', 'string', 'string'],
			[worldId, capture ? 1 : 0, stars, (survivors || []).join(','), (used || []).join(',')])));
	}
	craft(sp) { return this.M._dk_craft(sp); }
	cancelCraft(i) { return this.M._dk_cancel_craft(i); }
	skipCraft() { return this.M._dk_skip_craft(); }
	upgradeSpell(sp, gems) { return this.M._dk_upgrade_spell(sp, gems ? 1 : 0); }
	skipSpell(i) { return this.M._dk_skip_spell(i); }
	equip(titan, relic) { return this.M._dk_equip(titan, relic); }
	attach(building, relic) { return this.M._dk_attach(building, relic); }
	repairUnlock(target, ids) { return this.M.ccall('dk_repair_unlock', 'number', ['number', 'string'], [target, ids.join(',')]) === 1; }
	unequip(relic) { return this.M._dk_unequip(relic); }
	discard(relic) { return this.M._dk_discard(relic); }
	repairChance(target, ids) { return this.M.ccall('dk_repair_chance', 'number', ['number', 'string'], [target, ids.join(',')]); }
	repair(target, ids) { return JSON.parse(this.str(this.M.ccall('dk_repair', 'number', ['number', 'string'], [target, ids.join(',')]))); }
	assign(land, titan) { return this.M._dk_assign(land, titan); }
	recall(land) { return this.M._dk_recall(land); }
	train(t) { return this.M._dk_train(t); }
	cancelTrain(i) { return this.M._dk_cancel_train(i); }
	skipTrain() { return this.M._dk_skip_train(); }
	dismiss(t, n) { return this.M._dk_dismiss(t, n); }
	upgradeTroop(t, gems) { return this.M._dk_upgrade_troop(t, gems ? 1 : 0); }
	skipResearch() { return this.M._dk_skip_research(); }
	openAltar(a) { return JSON.parse(this.str(this.M._dk_open_altar(a))); }
	fuse(target, ids) { return this.M.ccall('dk_fuse', 'number', ['number', 'string'], [target, ids.join(',')]); }
	skipTitan(id) { return this.M._dk_skip_titan(id); }
	ascend(id, gems) { return this.M._dk_ascend(id, gems ? 1 : 0); }
	alliances() { return JSON.parse(this.str(this.M._dk_alliances())); }
	join(i) { return this.M._dk_join(i); }
	create(name, tag) { return this.M.ccall('dk_create', 'number', ['string', 'string'], [name, tag]); }
	leave() { return this.M._dk_leave(); }
	reinforce() { return this.M._dk_reinforce(); }
	visit() { this.M._dk_visit(); }
	setGarrison(squads, titan) { return this.M.ccall('dk_set_garrison', 'number', ['string', 'number'], [(squads || []).join(','), titan || 0]); }
	readRaids() { return this.M._dk_read_raids(); }
	forecast(land) { return JSON.parse(this.str(this.M._dk_forecast(land || 0))); }
}

/** kingdom.json carries // comments (the C++ reader allows them); strips them outside strings. */
function parseJsonc(text) {
	let out = '';
	let inStr = false;
	for (let i = 0; i < text.length; i++) {
		const ch = text[i];
		if (inStr) {
			out += ch;
			if (ch === '\\') out += text[++i] || '';
			else if (ch === '"') inStr = false;
		} else if (ch === '"') {
			inStr = true;
			out += ch;
		} else if (ch === '/' && text[i + 1] === '/') {
			while (i < text.length && text[i] !== '\n') i++;
			out += '\n';
		} else out += ch;
	}
	return JSON.parse(out);
}

// ---------------------------------------------------------------------------------------------
// Clock and saving
// ---------------------------------------------------------------------------------------------
// The kingdom runs on wall-clock seconds (timers keep going while the page is closed, as in the
// original). The developer time scale only moves an offset forward; the clock never runs back.
const clock = { offset: 0, scale: 1, last: 0 };
function nowF() { return Date.now() / 1000 + clock.offset; }
function nowSec() { clock.last = Math.max(clock.last, Math.floor(nowF())); return clock.last; }

// loaded: this page opened a saved kingdom (not a new one); loadedAt: when that save was made.
const saver = { timer: 0, savedAt: 0, loaded: false, loadedAt: 0 };
function readLocal() {
	try {
		const r = JSON.parse(localStorage.getItem(SAVE_KEY) || 'null');
		return r && typeof r.save === 'string' ? r : null;
	} catch (e) { return null; }
}
function persist() {
	if (!K.bridge) return;
	clearTimeout(saver.timer);
	const rec = { v: 1, save: K.bridge.save(), offset: clock.offset, savedAt: Date.now() };
	try { localStorage.setItem(SAVE_KEY, JSON.stringify(rec)); } catch (e) { /* storage unavailable: the cloud copy may still work */ }
	saver.savedAt = rec.savedAt;
	K.lastSave = performance.now();
	cloudPush(rec);
}
/** The player is here: raiders stop coming two days after the last visit (the time limit is in the rules). */
function visit() {
	K.visitAt = performance.now();
	K.bridge.visit();
}
function persistSoon() { clearTimeout(saver.timer); saver.timer = setTimeout(persist, 400); }

// Cloud copy in the viewer's private claude.ai storage (artifact `db` capability), when available.
// "kingdom" is the live copy; "kingdom-prev" keeps another kingdom of the same account (the one not shown after
// two browsers disagreed, or the one before "start again"), so the menu can switch back to it.
const cloud = { ref: null, prevRef: null, prev: null, state: 'off', busy: false, next: null, sentText: '', lastWrite: 0, backoffUntil: 0 };
/** The kingdom's seed: set when it is founded and kept for its whole life, so equal seeds mean the same kingdom. */
function seedOf(text) { const m = /"seed":\s*"?(\d+)/.exec(text || ''); return m ? m[1] : ''; }
/** How far a saved kingdom has gone: building and titan levels, lands and expansions. */
function progressOf(text) {
	try {
		const s = JSON.parse(text);
		let n = 5 * ((s.lands || []).length + (s.expansions || 1));
		for (const b of s.buildings || []) n += b.level || 0;
		for (const t of s.titans || []) n += t.level || 0;
		return n;
	} catch (e) { return -1; }
}
/** "Замок 4, 3 земли" for a saved kingdom. */
function summaryOf(text) {
	try {
		const s = JSON.parse(text);
		const c = (s.buildings || []).find((b) => b.def === 'castle');
		const n = (s.lands || []).length;
		return 'Замок ' + (c ? c.level : 1) + ', ' + n + ' ' + plural(n, 'земля', 'земли', 'земель');
	} catch (e) { return 'королевство'; }
}
async function keepPrev(rec) {
	if (!cloud.prevRef || !rec || typeof rec.save !== 'string') return;
	const doc = { v: rec.v || 1, save: rec.save, offset: +rec.offset || 0, savedAt: +rec.savedAt || Date.now() };
	try { await cloud.prevRef.set(doc); cloud.prev = doc; } catch (e) { console.warn('Kingdom backup not saved:', e); }
}
async function cloudConnect() {
	const c = window.claude;
	if (!c || typeof c.use !== 'function') return;
	try {
		const [db, user] = await Promise.all([c.use('db'), c.use('user')]);
		if (!db || !user) return;
		const id = await user.id();
		if (!id) return;
		const col = db.collection('data/users/' + id);
		cloud.ref = col.doc('kingdom');
		cloud.prevRef = col.doc('kingdom-prev');
		const [snap, prevSnap] = await Promise.all([cloud.ref.get(), cloud.prevRef.get().catch(() => null)]);
		const pd = prevSnap && prevSnap.exists ? prevSnap.data() : null;
		cloud.prev = pd && typeof pd.save === 'string' ? pd : null;
		const d = snap.exists ? snap.data() : null;
		if (d && typeof d.save === 'string') {
			cloud.sentText = d.save;
			const ours = { v: 1, save: K.bridge.save(), offset: clock.offset, savedAt: Date.now() };
			if (d.save !== ours.save) {
				// The account copy wins when this browser had no kingdom yet, when it is the same kingdom saved later
				// (played on another device), or when it is another kingdom that has gone at least as far. The kingdom
				// not shown is kept as "kingdom-prev" before the live copy is overwritten.
				const same = seedOf(d.save) === seedOf(ours.save);
				const take = !saver.loaded || (same ? (+d.savedAt || 0) > saver.loadedAt : progressOf(d.save) >= progressOf(ours.save));
				if (!same && saver.loaded) await keepPrev(take ? ours : d);
				if (take && adopt(d)) toast('Королевство загружено из вашего аккаунта', 'gold');
			}
		}
		cloud.state = 'on';
		persist();
	} catch (e) {
		console.warn('Kingdom cloud save unavailable:', e);
		cloud.state = 'error';
	}
	refreshSheet(true);
}
function cloudPush(rec) {
	if (!cloud.ref || cloud.state !== 'on') return;
	cloud.next = rec;
	cloudPump();
}
async function cloudPump() {
	if (cloud.busy || !cloud.next) return;
	const wait = Math.max(cloud.backoffUntil, cloud.lastWrite + 2000) - Date.now();
	if (wait > 0) { setTimeout(cloudPump, wait + 20); return; }
	const rec = cloud.next;
	cloud.next = null;
	if (rec.save === cloud.sentText) return;
	cloud.busy = true;
	cloud.lastWrite = Date.now();
	try {
		await cloud.ref.set({ v: rec.v, save: rec.save, offset: rec.offset, savedAt: rec.savedAt });
		cloud.sentText = rec.save;
	} catch (e) {
		const code = e && e.code;
		if (code === 'unavailable' || code === 'resource_exhausted' || !code) {
			cloud.backoffUntil = Date.now() + 15000;
			if (!cloud.next) cloud.next = rec;
		} else {
			console.warn('Kingdom cloud save stopped:', e);
			cloud.state = 'error';
		}
	}
	cloud.busy = false;
	if (cloud.next) cloudPump();
}
/** Replaces the running kingdom with a saved one (newer cloud copy). */
function adopt(rec) {
	const before = K.bridge.save();
	if (!K.bridge.load(rec.save)) {
		console.warn('Kingdom save rejected:', K.bridge.error());
		K.bridge.load(before);
		return false;
	}
	clock.offset = +rec.offset || 0;
	clock.last = 0;
	saver.savedAt = +rec.savedAt || Date.now();
	K.bridge.advance(nowSec());
	visit();
	K.sel = null;
	cancelPlace();
	if (K.sheet) closeSheet();
	refresh(true);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Content helpers
// ---------------------------------------------------------------------------------------------
function defOf(i) { return K.defs[i]; }
function maxCount(d, castle) {
	let n = 0;
	for (const [c, v] of d.counts) if (castle >= c) n = v;
	return n;
}
function levelDef(d, lv) { return lv >= 1 ? d.levels[lv - 1] : null; }
function gridHalf() { return K.rules.grid.width / 2; }
function cellCenter(d, x, y) {
	const h = gridHalf();
	return { x: (x + d.size[0] / 2 - h) * TILE, z: (y + d.size[1] / 2 - h) * TILE };
}
function currentArea() {
	const ex = K.rules.expansions;
	return ex[clamp((K.st ? K.st.expansions : 1) - 1, 0, ex.length - 1)].area;
}
function landType(i) { return K.rules.lands.types[i]; }
/** "1 200 золота", "7 ОП", "3 осколка". */
function resAmount(res, n) {
	if (res === 'shards') return fmt(n) + ' ' + plural(n, 'осколок', 'осколка', 'осколков');
	if (res === 'vp') return fmt(n) + ' ОП';
	return fmtShort(n) + ' ' + RES_OF[res];
}
/** A land's production: per hour, or per day for ruins. */
function landRateText(type, rate) {
	const ty = landType(type);
	return (ty.periodHours || 1) >= 24 ? resAmount(ty.resource, rate) + ' в сутки' : resAmount(ty.resource, rate) + '/ч';
}
const RES_COLOR = { gold: '#ffd970', food: '#c8ef8a', vp: '#ffcf4a', shards: '#9fe3ff' };

// ---------------------------------------------------------------------------------------------
// Materials and procedural models
// ---------------------------------------------------------------------------------------------
const PAL = {
	stone: 0xd2c7b0, stone2: 0xa89e8a, stoneD: 0x7a7265, wood: 0x8d5b30, woodD: 0x5b391d, roofB: 0x34589c, roofR: 0xa9432c,
	slate: 0x4d5866, thatch: 0xcaa458, gold: 0xe9b63d, white: 0xece5d5, dirt: 0x8c6a43, sand: 0xbea57a, wheat: 0xdcb94b,
	iron: 0x4d5057, canvas: 0xdcd2ba, cloth: 0x2f6fe0, purple: 0x7d52e0, leaf: 0x4f8b36, rock: 0x8f8879, rock2: 0x76705f,
	green: 0x3f7f4a, sack: 0xc8b08a, skin: 0xe0b48e,
};
const GOLD = { metalness: 0.35, roughness: 0.45, emissive: 0x3a2604 };
const matCache = new Map();
function mat(hex, o) {
	const key = hex + (o ? JSON.stringify(o) : '');
	let m = matCache.get(key);
	if (!m) {
		m = new T.MeshStandardMaterial(Object.assign({ color: hex, roughness: 0.85, metalness: 0, flatShading: true }, o || {}));
		matCache.set(key, m);
	}
	return m;
}
const glow = (hex, i) => mat(hex, { emissive: hex, emissiveIntensity: i || 1, roughness: 0.5 });

// Unit geometries (centred; Mk places them by their base).
function gableGeometry() {
	const v = [
		-0.5, -0.5, 0.5, 0.5, -0.5, 0.5, 0.5, 0.5, 0, -0.5, -0.5, 0.5, 0.5, 0.5, 0, -0.5, 0.5, 0,
		0.5, -0.5, -0.5, -0.5, -0.5, -0.5, -0.5, 0.5, 0, 0.5, -0.5, -0.5, -0.5, 0.5, 0, 0.5, 0.5, 0,
		-0.5, -0.5, -0.5, -0.5, -0.5, 0.5, -0.5, 0.5, 0,
		0.5, -0.5, 0.5, 0.5, -0.5, -0.5, 0.5, 0.5, 0,
	];
	const g = new T.BufferGeometry();
	g.setAttribute('position', new T.Float32BufferAttribute(v, 3));
	g.computeVertexNormals();
	return g;
}
const G = {
	box: new T.BoxGeometry(1, 1, 1),
	ball: new T.IcosahedronGeometry(1, 1),
	ico: new T.IcosahedronGeometry(1, 0),
	rock: new T.DodecahedronGeometry(1, 0),
	oct: new T.OctahedronGeometry(1, 0),
	pyr: (() => { const g = new T.ConeGeometry(Math.SQRT1_2, 1, 4, 1); g.rotateY(Math.PI / 4); return g; })(),
	gable: gableGeometry(),
	torus: new T.TorusGeometry(1, 0.18, 6, 20),
	plane: new T.PlaneGeometry(1, 1),
	disc: new T.CircleGeometry(1.75, 32),
};
let portalMat = null;
const cylCache = new Map();
function cylGeo(top, seg) {
	const key = top + '|' + seg;
	let g = cylCache.get(key);
	if (!g) { g = new T.CylinderGeometry(top, 1, 1, seg); cylCache.set(key, g); }
	return g;
}

const _m4 = new T.Matrix4();
const _q = new T.Quaternion();
const _e = new T.Euler();
const _p = new T.Vector3();
const _s = new T.Vector3();

/** Model builder: static parts are merged per material; animated parts are created per instance. */
class Mk {
	constructor() { this.parts = new Map(); this.dyn = []; this.seed = 1; }
	rnd() { this.seed = (this.seed * 16807) % 2147483647; return this.seed / 2147483647; }
	add(geo, m, x, y, z, sx, sy, sz, ry, rx, rz) {
		_e.set(rx || 0, ry || 0, rz || 0);
		_m4.compose(_p.set(x, y, z), _q.setFromEuler(_e), _s.set(sx, sy, sz));
		const g = (geo.index ? geo.toNonIndexed() : geo.clone()).applyMatrix4(_m4);
		g.deleteAttribute('uv');
		let list = this.parts.get(m);
		if (!list) this.parts.set(m, (list = []));
		list.push(g);
		return this;
	}
	box(w, h, d, x, y, z, m, ry) { return this.add(G.box, m, x, y + h / 2, z, w, h, d, ry); }
	cyl(r, h, x, y, z, m, seg, top) { return this.add(cylGeo(top === undefined ? 1 : top, seg || 10), m, x, y + h / 2, z, r, h, r); }
	cone(r, h, x, y, z, m, seg) { return this.cyl(r, h, x, y, z, m, seg, 0); }
	pyr(w, h, d, x, y, z, m, ry) { return this.add(G.pyr, m, x, y + h / 2, z, w, h, d, ry); }
	gable(w, h, d, x, y, z, m, ry) { return this.add(G.gable, m, x, y + h / 2, z, w, h, d, ry); }
	ball(r, x, y, z, m, sy) { return this.add(G.ball, m, x, y, z, r, r * (sy || 1), r); }
	rock(r, x, y, z, m, sy, ry) { return this.add(G.rock, m, x, y, z, r, r * (sy || 1), r, ry || 0); }
	log(r, len, x, y, z, m, ry) { return this.add(cylGeo(1, 7), m, x, y, z, r, len, r, ry || 0, 0, Math.PI / 2); }
	/** Flag pole with a cloth that waves (per instance). */
	flag(x, y, z, h, color) {
		this.box(0.12, h, 0.12, x, y, z, mat(PAL.woodD));
		this.ball(0.13, x, y + h, z, mat(PAL.gold, GOLD));
		const cloth = mat(color || PAL.cloth, { side: T.DoubleSide, flatShading: false });
		this.dyn.push((grp) => {
			const pivot = new T.Group();
			pivot.position.set(x, y + h - 0.55, z);
			const m = new T.Mesh(G.plane, cloth);
			m.scale.set(1.3, 0.8, 1);
			m.position.x = 0.68;
			m.castShadow = true;
			pivot.add(m);
			grp.add(pivot);
			const ph = Math.random() * 6;
			return (t) => { pivot.rotation.y = Math.sin(t * 1.7 + ph) * 0.35 - 0.4; };
		});
		return this;
	}
	spin(build, update) { this.dyn.push((grp) => { const o = build(); grp.add(o); return (t) => update(o, t); }); return this; }
	bounds() {
		const box = new T.Box3();
		for (const list of this.parts.values()) for (const g of list) { g.computeBoundingBox(); box.union(g.boundingBox); }
		return box;
	}
	build() {
		const meshes = [];
		for (const [m, list] of this.parts) {
			let n = 0;
			for (const g of list) n += g.attributes.position.count;
			const pos = new Float32Array(n * 3);
			const nor = new Float32Array(n * 3);
			let o = 0;
			for (const g of list) {
				pos.set(g.attributes.position.array, o * 3);
				nor.set(g.attributes.normal.array, o * 3);
				o += g.attributes.position.count;
			}
			const geo = new T.BufferGeometry();
			geo.setAttribute('position', new T.BufferAttribute(pos, 3));
			geo.setAttribute('normal', new T.BufferAttribute(nor, 3));
			geo.computeBoundingSphere();
			meshes.push({ geo, mat: m });
		}
		const box = this.bounds();
		for (const list of this.parts.values()) for (const g of list) g.dispose();
		return { meshes, dyn: this.dyn, box };
	}
}

function scaffold(k, w, d, h) {
	const m = mat(PAL.wood);
	const xs = w > 5 ? [-w / 2, 0, w / 2] : [-w / 2, w / 2];
	const zs = d > 5 ? [-d / 2, 0, d / 2] : [-d / 2, d / 2];
	for (const x of xs) for (const z of zs) if (Math.abs(x) === w / 2 || Math.abs(z) === d / 2) k.box(0.14, h, 0.14, x, 0, z, m);
	for (let y = 0.9; y < h + 0.01; y += 0.9) {
		k.box(w, 0.09, 0.14, 0, y, -d / 2, m);
		k.box(w, 0.09, 0.14, 0, y, d / 2, m);
		k.box(0.14, 0.09, d, -w / 2, y, 0, m);
		k.box(0.14, 0.09, d, w / 2, y, 0, m);
	}
	k.box(w * 0.5, 0.06, 0.6, 0, h, d / 2 - 0.3, mat(PAL.woodD));
}
/** A small builder with a hammer next to a construction. */
function builder(k, x, z) {
	k.spin(() => {
		const g = new T.Group();
		g.position.set(x, 0, z);
		const body = new T.Mesh(cylGeo(0.8, 8), mat(0x8a4f2a));
		body.scale.set(0.28, 0.75, 0.28);
		body.position.y = 0.375;
		const head = new T.Mesh(G.ball, mat(PAL.skin));
		head.scale.setScalar(0.2);
		head.position.y = 0.92;
		const hat = new T.Mesh(cylGeo(0.7, 8), mat(PAL.gold, GOLD));
		hat.scale.set(0.24, 0.12, 0.24);
		hat.position.y = 1.08;
		const arm = new T.Group();
		arm.position.set(0.2, 0.68, 0);
		const handle = new T.Mesh(G.box, mat(PAL.woodD));
		handle.scale.set(0.07, 0.5, 0.07);
		handle.position.y = 0.25;
		const headM = new T.Mesh(G.box, mat(PAL.iron));
		headM.scale.set(0.2, 0.12, 0.12);
		headM.position.y = 0.5;
		arm.add(handle, headM);
		for (const m of [body, head, hat, handle, headM]) m.castShadow = true;
		g.add(body, head, hat, arm);
		g.userData.arm = arm;
		g.rotation.y = -Math.PI * 0.75;
		return g;
	}, (g, t) => { g.userData.arm.rotation.x = -0.4 - Math.abs(Math.sin(t * 5)) * 1.2; });
}

// One function per building id: (mk, tier 0..4, footprint width, depth) in world units, origin at the centre.
const MODELS = {
	castle(k, t) {
		const s = mat(PAL.stone);
		const s2 = mat(PAL.stone2);
		const sd = mat(PAL.stoneD);
		const roof = mat(PAL.roofB);
		const gold = mat(PAL.gold, GOLD);
		const dark = mat(0x2a2622);
		k.box(11.2, 0.5, 11.2, 0, 0, 0, sd);
		const wh = 2.3 + t * 0.35;
		const e = 4.75;
		k.box(9.4, wh, 0.9, 0, 0.5, -e, s).box(9.4, wh, 0.9, 0, 0.5, e, s).box(0.9, wh, 9.4, -e, 0.5, 0, s).box(0.9, wh, 9.4, e, 0.5, 0, s);
		for (let i = -3; i <= 3; i += 1.5) {
			k.box(0.6, 0.5, 0.95, i, 0.5 + wh, -e, s2).box(0.6, 0.5, 0.95, i, 0.5 + wh, e, s2);
			k.box(0.95, 0.5, 0.6, -e, 0.5 + wh, i, s2).box(0.95, 0.5, 0.6, e, 0.5 + wh, i, s2);
		}
		const th = 3.9 + t * 0.7;
		for (const [x, z] of [[-e, -e], [e, -e], [-e, e], [e, e]]) {
			k.cyl(1.35, th, x, 0.5, z, s2, 10).cyl(1.5, 0.35, x, 0.5 + th - 0.35, z, s, 10).cone(1.75, 2.1 + t * 0.2, x, 0.5 + th, z, roof, 10);
			if (t >= 3) k.cone(0.22, 1.1, x, 0.5 + th + 2.0 + t * 0.2, z, gold, 6);
		}
		k.box(3.2, wh + 1.3, 1.9, 0, 0.5, e, s2).box(1.5, 2.0, 0.2, 0, 0.5, e + 0.96, dark).gable(3.5, 1.1, 2.1, 0, 0.5 + wh + 1.3, e, roof);
		const kh = 4.4 + t * 1.1;
		k.box(4.6, kh, 4.6, 0, 0.5, -0.9, s).pyr(5.5, 2.6 + t * 0.3, 5.5, 0, 0.5 + kh, -0.9, roof);
		for (let y = 1.6; y < kh - 0.5; y += 1.6) k.box(0.5, 0.8, 0.1, -1.1, 0.5 + y, 1.42, dark).box(0.5, 0.8, 0.1, 1.1, 0.5 + y, 1.42, dark);
		if (t >= 1) k.cyl(1.05, kh + 2.6, 2.7, 0.5, -3.0, s2, 10).cone(1.35, 2.3, 2.7, 0.5 + kh + 2.6, -3.0, roof, 10);
		if (t >= 2) k.cyl(0.95, kh + 1.4, -2.7, 0.5, -2.8, s2, 10).cone(1.25, 2.1, -2.7, 0.5 + kh + 1.4, -2.8, roof, 10);
		if (t >= 3) k.box(4.75, 0.24, 4.75, 0, 0.5 + kh - 0.5, -0.9, gold);
		if (t >= 4) for (const [x, z] of [[0, -e], [-e, 0], [e, 0]]) k.cyl(1.0, th - 0.6, x, 0.5, z, s2, 10).cone(1.3, 1.7, x, 0.5 + th - 0.6, z, roof, 10);
		k.flag(0, 0.5 + kh + 2.4 + t * 0.3, -0.9, 1.8, 0xf2c14e);
	},
	gold_mine(k, t) {
		const r = mat(PAL.rock);
		const r2 = mat(PAL.rock2);
		const w = mat(PAL.wood);
		const wd = mat(PAL.woodD);
		const g = mat(PAL.gold, GOLD);
		k.rock(2.3, 0, 0.7, -0.8, r, 0.8, 0.3).rock(1.5, -1.5, 0.5, 0.5, r2, 0.7, 1.1).rock(1.2, 1.5, 0.45, -1.7, r2, 0.85, 2);
		k.box(1.4, 1.45, 0.4, 0, 0, 0.95, mat(0x1b1815));
		k.box(0.24, 1.75, 0.24, -0.82, 0, 1.15, w).box(0.24, 1.75, 0.24, 0.82, 0, 1.15, w).box(2.1, 0.28, 0.34, 0, 1.72, 1.15, wd);
		k.box(0.1, 0.08, 1.7, -0.32, 0, 2.05, mat(PAL.iron)).box(0.1, 0.08, 1.7, 0.32, 0, 2.05, mat(PAL.iron));
		k.box(0.95, 0.5, 0.75, 0, 0.16, 2.25, wd).ball(0.42, 0, 0.7, 2.25, g, 0.55);
		k.seed = 7;
		for (let i = 0; i < 3 + t * 2; i++) {
			const a = k.rnd() * Math.PI * 2;
			const rr = 0.9 + k.rnd() * 1.1;
			k.add(G.ico, g, Math.cos(a) * rr, 0.9 + k.rnd() * 0.9, -0.8 + Math.sin(a) * rr * 0.8, 0.22 + k.rnd() * 0.12, 0.22, 0.22, a);
		}
		if (t >= 2) k.box(0.18, 2.6, 0.18, -2.0, 0, 1.6, w).box(0.5, 0.4, 0.5, -2.0, 2.6, 1.6, glow(0xffc76a, 0.8));
		if (t >= 3) {
			k.box(0.2, 3.4, 0.2, 1.0, 0, -2.4, w).box(0.2, 3.4, 0.2, 2.2, 0, -2.4, w).box(1.6, 0.2, 0.2, 1.6, 3.3, -2.4, wd);
			k.add(G.torus, wd, 1.6, 3.1, -2.4, 0.5, 0.5, 0.5);
		}
	},
	farm(k, t) {
		const soil = mat(PAL.dirt);
		const wheat = mat(PAL.wheat);
		const wh = mat(PAL.white);
		const roof = mat(PAL.roofR);
		k.box(7.2, 0.16, 4.4, 0, 0, 1.4, soil);
		for (let i = 0; i < 5; i++) k.box(6.7, 0.5 + t * 0.05, 0.48, 0, 0.16, -0.4 + i * 0.92, wheat);
		k.box(2.4, 1.6, 2.0, -2.3, 0, -2.75, wh).gable(2.7, 1.1, 2.3, -2.3, 1.6, -2.75, roof).box(0.6, 0.9, 0.1, -2.3, 0, -1.72, mat(PAL.woodD));
		k.cyl(0.85, 3.4, 2.3, 0, -2.6, wh, 8, 0.7).cone(0.95, 1.0, 2.3, 3.4, -2.6, roof, 8);
		const white = mat(PAL.canvas);
		k.spin(() => {
			const hub = new T.Group();
			hub.position.set(2.3, 3.0, -1.75);
			for (let i = 0; i < 4; i++) {
				const arm = new T.Group();
				arm.rotation.z = (i * Math.PI) / 2;
				const blade = new T.Mesh(G.box, white);
				blade.scale.set(0.36, 2.1, 0.05);
				blade.position.y = 1.15;
				blade.castShadow = true;
				arm.add(blade);
				hub.add(arm);
			}
			return hub;
		}, (o, tt) => { o.rotation.z = tt * 1.1; });
		if (t >= 1) k.log(0.42, 0.55, -0.8, 0.42, -2.6, mat(PAL.thatch), Math.PI / 2).log(0.42, 0.55, 0.2, 0.42, -2.4, mat(PAL.thatch), Math.PI / 2);
		if (t >= 2) for (let x = -3.4; x <= 3.4; x += 1.1) k.box(0.1, 0.6, 0.1, x, 0, 3.75, mat(PAL.wood));
		if (t >= 2) k.box(6.9, 0.08, 0.06, 0, 0.45, 3.75, mat(PAL.wood));
		if (t >= 3) k.box(0.1, 1.5, 0.1, 1.8, 0, 1.2, mat(PAL.woodD)).box(0.9, 0.1, 0.1, 1.8, 1.15, 1.2, mat(PAL.woodD)).ball(0.2, 1.8, 1.55, 1.2, mat(PAL.sack));
	},
	gold_vault(k, t) {
		const s = mat(PAL.stone);
		const g = mat(PAL.gold, GOLD);
		const h = 2.0 + t * 0.4;
		k.box(5, 0.4, 5, 0, 0, 0, mat(PAL.stoneD)).box(4, h, 3.6, 0, 0.4, -0.2, s).box(4.1, 0.22, 3.7, 0, 0.4 + h - 0.22, -0.2, g);
		k.ball(1.55, 0, 0.4 + h, -0.2, g, 0.7).box(1.2, 1.5, 0.15, 0, 0.4, 1.62, mat(PAL.iron));
		for (const x of [-1.75, 1.75]) k.cyl(0.36, 0.25 + t * 0.12, x, 0.4, 1.95, g, 10).cyl(0.3, 0.2, x + 0.15, 0.65 + t * 0.12, 1.95, g, 10);
		if (t >= 2) for (const x of [-1.65, 1.65]) k.cyl(0.28, h, x, 0.4, 1.55, mat(PAL.stone2), 8);
		if (t >= 4) k.cone(0.25, 1.4, 0, 0.4 + h + 1.0, -0.2, g, 6);
	},
	granary(k, t) {
		const w = mat(PAL.wood);
		const th = mat(PAL.thatch);
		const h = 2.5 + t * 0.3;
		k.cyl(1.5, h, -0.6, 0, -0.5, w, 10).cyl(1.56, 0.14, -0.6, 0.7, -0.5, mat(PAL.woodD), 10).cyl(1.56, 0.14, -0.6, h - 0.6, -0.5, mat(PAL.woodD), 10);
		k.cone(1.9, 1.5, -0.6, h, -0.5, th, 10).box(0.7, 1.1, 0.12, -0.6, 0, 1.0, mat(PAL.woodD));
		if (t >= 1) k.cyl(0.95, h * 0.8, 1.6, 0, -1.5, w, 8).cone(1.2, 1.0, 1.6, h * 0.8, -1.5, th, 8);
		const sack = mat(PAL.sack);
		k.ball(0.38, 1.3, 0.32, 1.6, sack, 0.85).ball(0.38, 1.9, 0.32, 1.1, sack, 0.85).ball(0.34, 1.6, 0.8, 1.35, sack, 0.85);
		if (t >= 3) k.ball(0.38, -2.0, 0.32, 1.7, sack, 0.85).ball(0.38, -1.4, 0.32, 2.0, sack, 0.85);
	},
	gold_reserve(k, t) {
		const g = mat(PAL.gold, GOLD);
		const iron = mat(PAL.iron);
		k.box(3.4, 0.3, 3.4, 0, 0, 0, mat(PAL.stoneD)).box(2.4, 1.3 + t * 0.15, 1.9, 0, 0.3, 0, iron);
		const top = 1.6 + t * 0.15;
		k.add(cylGeo(1, 12), g, 0, top, 0, 0.95, 2.45, 0.95, 0, 0, Math.PI / 2);
		k.box(2.5, 0.16, 2.0, 0, 0.6, 0, g).box(0.42, 0.5, 0.1, 0, 0.9, 1.0, g);
	},
	food_reserve(k, t) {
		k.box(3.4, 0.3, 3.4, 0, 0, 0, mat(PAL.stoneD)).box(2.4, 1.4 + t * 0.15, 2.0, 0, 0.3, -0.3, mat(PAL.wood));
		k.gable(2.7, 1.0, 2.3, 0, 1.7 + t * 0.15, -0.3, mat(PAL.thatch));
		for (const x of [-0.9, 0, 0.9]) k.cyl(0.32, 0.75, x, 0.3, 1.2, mat(PAL.woodD), 8, 0.9);
	},
	barracks(k, t) {
		const s = mat(PAL.stone);
		const w = mat(PAL.wood);
		const h = 2.1 + t * 0.3;
		k.box(7.4, 0.2, 7.4, 0, 0, 0, mat(PAL.sand)).box(6.1, 0.35, 3.1, 0, 0.2, -1.9, mat(PAL.stoneD)).box(6, h, 3, 0, 0.55, -1.9, s);
		k.gable(6.4, 1.6, 3.4, 0, 0.55 + h, -1.9, mat(PAL.roofR)).box(1.2, 1.5, 0.12, 0, 0.55, -0.35, mat(PAL.woodD));
		for (const x of [-2, 0, 2]) k.box(0.16, 1.3, 0.16, x, 0.2, 1.8, w).box(1.0, 0.15, 0.15, x, 1.1, 1.8, w).ball(0.24, x, 1.72, 1.8, mat(PAL.sack));
		k.box(1.6, 0.12, 0.3, -2.7, 0.95, 2.9, mat(PAL.woodD));
		for (let i = 0; i < 4; i++) k.box(0.06, 1.7, 0.06, -3.3 + i * 0.4, 0.2, 2.9, mat(PAL.iron));
		if (t >= 2) k.cyl(0.9, h + 1.6, 2.8, 0.2, -2.9, mat(PAL.stone2), 8).cone(1.15, 1.5, 2.8, 1.8 + h, -2.9, mat(PAL.roofR), 8);
		k.flag(-2.9, 0.2, -0.1, 3.6, 0x2f6fe0);
	},
	army_camp(k, t) {
		k.box(7.4, 0.2, 7.4, 0, 0, 0, mat(PAL.sand));
		const n = 3 + Math.min(3, t);
		for (let i = 0; i < n; i++) {
			const a = (i / n) * Math.PI * 2 + 0.5;
			const x = Math.cos(a) * 2.5;
			const z = Math.sin(a) * 2.5;
			k.box(1.6, 0.3, 1.6, x, 0.2, z, mat(PAL.canvas), a).pyr(1.9, 1.5, 1.9, x, 0.5, z, mat(i % 2 ? PAL.canvas : 0x3b64b0), a);
		}
		for (let i = 0; i < 3; i++) k.add(G.box, mat(PAL.woodD), 0, 0.32, 0, 1.0, 0.16, 0.16, (i * Math.PI) / 3);
		for (let i = 0; i < 7; i++) k.rock(0.18, Math.cos(i) * 0.65, 0.25, Math.sin(i) * 0.65, mat(PAL.rock2), 0.7);
		k.spin(() => {
			const f = new T.Mesh(cylGeo(0, 7), glow(0xff8a2a, 1.6));
			f.position.y = 0.75;
			return f;
		}, (o, tt) => { const s = 0.9 + Math.sin(tt * 13) * 0.08 + Math.sin(tt * 7.3) * 0.06; o.scale.set(0.35 * s, 0.85 * s, 0.35 * s); });
		k.flag(0, 0.2, -1.0, 3.8);
		if (t >= 3) for (let i = -3.4; i <= 3.4; i += 0.85) k.cone(0.14, 1.2, i, 0.2, -3.5, mat(PAL.wood), 5).cone(0.14, 1.2, -3.5, 0.2, i, mat(PAL.wood), 5);
	},
	garrison(k, t) {
		const th = 3.6 + t * 0.5;
		k.box(3.4, 0.9, 3.4, 0, 0, 0, mat(PAL.stone2)).cyl(1.25, th, 0, 0.9, 0, mat(PAL.stone), 10).box(3, 0.3, 3, 0, 0.9 + th, 0, mat(PAL.wood));
		for (const [x, z] of [[-1.4, -1.4], [1.4, -1.4], [-1.4, 1.4], [1.4, 1.4]]) k.box(0.16, 1.2, 0.16, x, 1.2 + th, z, mat(PAL.woodD));
		k.pyr(3.3, 1.5, 3.3, 0, 2.4 + th, 0, mat(PAL.roofB));
		for (let i = -2.6; i <= 2.6; i += 0.65) k.cone(0.15, 1.3, i, 0, 2.6, mat(PAL.wood), 5).cone(0.15, 1.3, -2.6, 0, i, mat(PAL.wood), 5);
		k.flag(1.2, 1.2 + th, 1.2, 2.2);
	},
	armoury(k, t) {
		k.box(4.2, 2.2, 3.2, 0, 0, -0.8, mat(PAL.stone)).gable(4.5, 1.3, 3.5, 0, 2.2, -0.8, mat(PAL.slate));
		k.box(0.7, 2.4 + t * 0.2, 0.7, 1.4, 2.0, -1.5, mat(PAL.stoneD)).box(1.0, 0.7, 0.12, 0.9, 0.5, 0.82, glow(0xff7a24, 1.2));
		k.box(0.7, 0.45, 0.35, -0.8, 0, 1.6, mat(PAL.iron)).box(0.95, 0.2, 0.42, -0.8, 0.45, 1.6, mat(PAL.iron));
		for (const x of [-1.6, -0.6]) k.add(cylGeo(1, 10), mat(x < -1 ? 0x2f6fe0 : PAL.iron), x, 1.3, 0.85, 0.38, 0.08, 0.38, 0, Math.PI / 2);
		if (t >= 2) k.box(1.6, 1.4, 1.6, -2.6, 0, 0.4, mat(PAL.wood)).gable(1.8, 0.7, 1.8, -2.6, 1.4, 0.4, mat(PAL.slate));
	},
	medical_school(k, t) {
		const wh = mat(PAL.white);
		const roof = mat(PAL.green);
		const h = 2.2 + t * 0.2;
		k.box(4.2, h, 3.2, 0.3, 0, -0.7, wh).gable(4.5, 1.3, 3.5, 0.3, h, -0.7, roof);
		k.box(1.1, h + 1.6, 1.1, -1.9, 0, 0.6, wh).pyr(1.45, 1.0, 1.45, -1.9, h + 1.6, 0.6, roof);
		k.box(2.2, 0.25, 1.0, 1.0, 0, 1.9, mat(PAL.dirt));
		for (let i = 0; i < 4; i++) k.ball(0.28, 0.3 + i * 0.48, 0.42, 1.9, mat(PAL.leaf));
		k.ball(0.3, 0.3, h + 0.5, 0.95, mat(0x6fbf5a), 0.6);
	},
	alliance_camp(k, t) {
		k.cyl(2.5, 0.3, 0, 0, 0, mat(PAL.stone2), 12);
		for (let i = 0; i < 6; i++) { const a = (i / 6) * Math.PI * 2; k.cyl(0.15, 2.0, Math.cos(a) * 1.9, 0.3, Math.sin(a) * 1.9, mat(PAL.wood), 6); }
		k.cone(2.7, 1.6, 0, 2.3, 0, mat(0x3b64b0), 12).cone(0.2, 0.8, 0, 3.9, 0, mat(PAL.gold, GOLD), 6);
		k.flag(-2.3, 0, 2.2, 3.0, 0x3f9a4a).flag(0, 0, 2.6, 3.4, 0xc23b2a).flag(2.3, 0, 2.2, 3.0, 0xe0b23c);
	},
	arcane_tower(k, t) {
		const h = 4.8 + t * 0.8;
		k.cyl(1.45, 0.4, 0, 0, 0, mat(PAL.stone2), 8).cyl(1.0, h, 0, 0.4, 0, mat(PAL.stone), 8, 0.75).cyl(1.05, 0.4, 0, 0.4 + h, 0, mat(PAL.stoneD), 8);
		k.cone(1.15, 2.1, 0, 0.8 + h, 0, mat(0x5b3d8f), 8);
		for (let y = 1.5; y < h; y += 1.5) k.box(0.3, 0.55, 0.1, 0, 0.4 + y, 0.88 - y * 0.03, glow(0xb48cff, 1.2));
		const top = 0.8 + h + 3.0;
		k.spin(() => { const c = new T.Mesh(G.oct, glow(0xa77bff, 1.4)); c.scale.set(0.5, 0.8, 0.5); c.castShadow = true; return c; },
			(o, tt) => { o.rotation.y = tt * 1.4; o.position.y = top + Math.sin(tt * 2) * 0.2; });
	},
	spell_forge(k, t) {
		k.box(4.0, 2.2, 3.2, 0, 0, -0.8, mat(0x6b6470)).gable(4.3, 1.4, 3.5, 0, 2.2, -0.8, mat(0x5b3d8f));
		k.box(0.6, 0.8, 0.1, -1.0, 0.8, 0.82, glow(0xb48cff, 1.3)).box(0.6, 0.8, 0.1, 1.0, 0.8, 0.82, glow(0xb48cff, 1.3));
		k.cyl(0.7, 0.7, 1.3, 0, 1.7, mat(PAL.iron), 10, 1.15).cyl(0.72, 0.05, 1.3, 0.66, 1.7, glow(0x6ef0c0, 1.2), 10);
		k.box(0.4, 1.2, 0.25, -1.7, 0, 1.8, mat(PAL.stone2)).box(0.4, 0.9, 0.25, -1.1, 0, 2.1, mat(PAL.stone2));
		if (t >= 2) k.box(0.6, 2.2, 0.6, 1.4, 2.0, -1.6, mat(PAL.stoneD));
	},
	portal(k, t) {
		k.cyl(2.7, 0.35, 0, 0, 0, mat(PAL.stone2), 14).cyl(2.2, 0.2, 0, 0.35, 0, mat(PAL.stone), 14);
		k.add(G.torus, mat(PAL.stone), 0, 2.3, 0, 1.9, 1.9, 1.9);
		k.box(0.9, 0.7, 0.7, -1.55, 0.35, 0, mat(PAL.stone2)).box(0.9, 0.7, 0.7, 1.55, 0.35, 0, mat(PAL.stone2));
		for (const [x, z] of [[-2.0, -1.6], [2.0, -1.6], [-2.2, 1.4], [2.2, 1.4]]) k.box(0.45, 1.0 + (x > 0 ? 0.3 : 0), 0.35, x, 0.35, z, mat(PAL.stoneD), x);
		for (let i = 0; i < t; i++) k.add(G.oct, glow(0xb48cff, 1.1), -2.0 + i * 1.0, 0.75, 2.0, 0.22, 0.4, 0.22);
		const swirl = swirlTexture();
		k.spin(() => {
			const m = new T.Mesh(G.disc, portalMat || (portalMat = new T.MeshBasicMaterial({ map: swirl, transparent: true, depthWrite: false, side: T.DoubleSide, blending: T.AdditiveBlending })));
			m.position.y = 2.3;
			return m;
		}, (o, tt) => { o.rotation.z = -tt * 1.6; o.material.opacity = 0.75 + Math.sin(tt * 3) * 0.15; });
	},
	builders_yard(k, t) {
		k.box(5.2, 0.15, 5.2, 0, 0, 0, mat(PAL.sand)).box(2.8, 1.8, 2.2, -0.9, 0, -1.3, mat(PAL.wood));
		k.add(G.box, mat(PAL.woodD), -0.9, 2.0, -1.3, 3.1, 0.15, 2.6, 0, 0.2).box(0.8, 1.2, 0.1, -0.9, 0, -0.18, mat(PAL.woodD));
		const lw = mat(0xb07a45);
		for (let row = 0; row < 3; row++) for (let i = 0; i < 3 - row; i++) k.log(0.2, 2.2, 1.0 + i * 0.42 + row * 0.21, 0.2 + row * 0.36, 1.4, lw, Math.PI / 2);
		k.box(0.6, 0.6, 0.6, -1.8, 0.15, 1.6, mat(PAL.stone2)).box(0.6, 0.6, 0.6, -1.1, 0.15, 1.8, mat(PAL.stone2)).box(0.6, 0.6, 0.6, -1.45, 0.75, 1.7, mat(PAL.stone));
		k.box(0.18, 3.6, 0.18, 1.9, 0.15, -1.7, mat(PAL.woodD)).box(2.3, 0.15, 0.15, 1.0, 3.6, -1.7, mat(PAL.woodD));
		k.box(0.04, 1.2, 0.04, 0.1, 2.4, -1.7, mat(0x222222)).box(0.35, 0.35, 0.35, 0.1, 2.1, -1.7, mat(PAL.stone2));
	},
	hall_of_titans(k, t) {
		const s = mat(PAL.stone);
		const wh = mat(PAL.white);
		const h = 3.1 + t * 0.3;
		k.box(9.4, 0.4, 9.4, 0, 0, 0, mat(PAL.stone2)).box(8.6, 0.4, 8.6, 0, 0.4, 0, s);
		k.box(6, h, 4.6, 0, 0.8, -1.3, s);
		for (let i = 0; i < 6; i++) k.cyl(0.32, h, -3.4 + i * 1.36, 0.8, 1.7, wh, 8);
		k.box(7.6, 0.45, 1.3, 0, 0.8 + h, 1.7, wh);
		k.gable(7.4, 1.6, 7.9, 0, 1.25 + h, -0.4, t >= 3 ? mat(PAL.gold, GOLD) : mat(PAL.roofR), Math.PI / 2);
		const st = mat(0xb8b0a0);
		k.box(1.6, 0.8, 1.6, 0, 0.8, 3.6, mat(PAL.stone2));
		k.box(0.36, 1.2, 0.4, -0.3, 1.6, 3.6, st).box(0.36, 1.2, 0.4, 0.3, 1.6, 3.6, st).box(1.1, 1.3, 0.6, 0, 2.8, 3.6, st).ball(0.34, 0, 4.4, 3.6, st);
		k.add(G.box, st, 0.75, 3.8, 3.6, 0.28, 1.2, 0.28, 0, 0, -0.5).add(G.box, mat(PAL.iron), 1.2, 4.7, 3.6, 0.12, 1.6, 0.2, 0, 0, -0.5);
		k.add(G.box, st, -0.72, 3.0, 3.6, 0.28, 1.0, 0.28, 0, 0, 0.2);
		if (t >= 2) for (const x of [-3.8, 3.8]) k.cyl(0.4, 1.0, x, 0.8, 3.8, mat(PAL.iron), 8, 1.2).cone(0.3, 0.6, x, 1.8, 3.8, glow(0xff8a2a, 1.5), 6);
	},
	relic_temple(k, t) {
		k.box(7.2, 0.6, 7.2, 0, 0, 0, mat(PAL.stone2)).box(5.6, 0.6, 5.6, 0, 0.6, 0, mat(PAL.stone)).box(4.0, 0.6, 4.0, 0, 1.2, 0, mat(PAL.stone2));
		k.box(1.4, 0.6, 1.0, 0, 0, 3.6, mat(PAL.stone)).box(1.4, 0.6, 0.8, 0, 0.6, 2.9, mat(PAL.stone));
		for (const [x, z] of [[-1.4, -1.4], [1.4, -1.4], [-1.4, 1.4], [1.4, 1.4]]) k.box(0.4, 2.2, 0.4, x, 1.8, z, mat(PAL.white));
		k.box(3.6, 0.3, 3.6, 0, 4.0, 0, mat(PAL.white)).pyr(3.8, 1.5, 3.8, 0, 4.3, 0, mat(PAL.roofB)).cone(0.2, 0.7, 0, 5.8, 0, mat(PAL.gold, GOLD), 6);
		k.spin(() => { const o = new T.Mesh(G.ball, glow(0x6fe3ff, 1.5)); o.scale.setScalar(0.5); return o; },
			(o, tt) => { o.position.set(0, 2.9 + Math.sin(tt * 1.8) * 0.18, 0); o.rotation.y = tt; });
	},
	eternal_shrine(k, t) {
		k.box(3.2, 0.3, 3.2, 0, 0, 0, mat(PAL.stone2)).box(2.3, 0.3, 2.3, 0, 0.3, 0, mat(PAL.stone));
		k.add(cylGeo(0.62, 4), mat(PAL.white), 0, 2.2, 0, 0.62, 3.2, 0.62, Math.PI / 4).pyr(0.55, 0.5, 0.55, 0, 3.8, 0, mat(PAL.gold, GOLD));
		for (let i = 0; i < 6; i++) k.ball(0.2, Math.cos(i) * 1.3, 0.75, Math.sin(i) * 1.3, mat(i % 2 ? 0xe86fa0 : 0xf2d15a));
		k.spin(() => { const o = new T.Mesh(G.oct, glow(0x5fe0c8, 1.3)); o.scale.set(0.28, 0.42, 0.28); return o; },
			(o, tt) => { o.position.set(Math.cos(tt) * 1.0, 2.3 + Math.sin(tt * 2) * 0.15, Math.sin(tt) * 1.0); o.rotation.y = tt * 2; });
	},
	adventurers_guild(k, t) {
		k.box(3.8, 2.2, 3.0, -0.5, 0, 0.3, mat(PAL.white)).box(3.9, 0.15, 3.1, -0.5, 1.1, 0.3, mat(PAL.woodD));
		for (const [x, z] of [[-2.35, -1.15], [1.35, -1.15], [-2.35, 1.75], [1.35, 1.75]]) k.box(0.16, 2.2, 0.16, x, 0, z, mat(PAL.woodD));
		k.gable(4.1, 1.3, 3.4, -0.5, 2.2, 0.3, mat(PAL.roofR));
		k.cyl(0.9, 4.2, 1.7, 0, -1.5, mat(PAL.stone), 8).cone(1.1, 1.5, 1.7, 4.2, -1.5, mat(PAL.roofB), 8);
		k.box(0.24, 0.6, 0.2, 1.7, 5.7, -1.5, mat(PAL.gold, GOLD)).ball(0.17, 1.7, 6.45, -1.5, mat(PAL.gold, GOLD));
		k.box(0.08, 0.6, 0.9, 1.55, 1.2, 1.5, mat(PAL.wood)).box(0.6, 0.06, 0.06, 1.3, 1.85, 1.5, mat(PAL.woodD));
	},
	// Envoy councils: each people's embassy in its own style (fire, frost, jade, marsh).
	council_sunreach(k, t) {
		const sand = mat(0xe2c48e);
		const sand2 = mat(0xc9a56a);
		const gold = mat(PAL.gold, GOLD);
		const h = 2.4 + t * 0.3;
		k.box(7.2, 0.3, 7.2, 0, 0, 0, sand2).box(5.0, h, 4.0, 0, 0.3, -1.1, sand);
		k.cyl(1.7, 0.5, 0, 0.3 + h, -1.1, sand2, 12).add(G.ball, gold, 0, 0.8 + h, -1.1, 1.65, 1.5, 1.65).cone(0.18, 1.0, 0, 2.2 + h, -1.1, gold, 6);
		for (const x of [-2.9, 2.9]) k.cyl(0.6, h + 1.6, x, 0.3, -2.4, sand, 8).ball(0.62, x, 1.9 + h, -2.4, gold, 1.3);
		for (const x of [-1.5, -0.5, 0.5, 1.5]) k.box(0.4, 1.7, 0.1, x, 0.3, 0.92, mat(0x7a3a1c));
		k.box(5.4, 0.3, 0.6, 0, 0.3 + h, 0.95, sand2);
		for (const x of [-2.4, 2.4]) {
			k.cyl(0.45, 0.9, x, 0.3, 2.2, mat(PAL.iron), 8, 1.3);
			k.spin(() => { const f = new T.Mesh(cylGeo(0, 7), glow(0xff8a2a, 1.7)); f.position.set(x, 1.2, 2.2); return f; },
				(o, tt) => { const s = 0.9 + Math.sin(tt * 11 + x) * 0.1; o.scale.set(0.38 * s, 0.9 * s, 0.38 * s); });
		}
		k.flag(-3.1, 0.3, 0.6, 3.4 + t * 0.3, 0xe0672a);
		if (t >= 2) k.flag(3.1, 0.3, 0.6, 3.4 + t * 0.3, 0xf2c14e);
	},
	council_rimeheim(k, t) {
		const log = mat(0x6a4a30);
		const ice = mat(0xbfe8ff, { roughness: 0.25, metalness: 0.1 });
		const snow = mat(0xf4f8fb);
		const h = 2.0 + t * 0.25;
		k.box(7.2, 0.25, 7.2, 0, 0, 0, snow).box(6.2, h, 3.2, 0, 0.25, -1.4, log);
		k.gable(6.8, 2.0, 3.8, 0, 0.25 + h, -1.4, mat(0x4f7fa8)).gable(6.9, 0.35, 3.9, 0, 1.95 + h, -1.4, snow);
		for (const x of [-3.3, 3.3]) k.box(0.2, 1.2, 0.2, x, 1.6 + h, 0.4, mat(PAL.woodD)).cone(0.3, 0.5, x, 2.8 + h, 0.4, mat(PAL.woodD), 5);
		k.box(1.2, 1.6, 0.12, 0, 0.25, 0.22, mat(PAL.woodD)).box(5.0, 0.14, 0.14, 0, 0.25 + h - 0.2, 0.25, mat(PAL.woodD));
		k.seed = 11;
		for (let i = 0; i < 5 + t; i++) {
			const a = k.rnd() * Math.PI * 2;
			const r = 2.2 + k.rnd() * 1.0;
			k.add(G.oct, ice, Math.cos(a) * r, 0.5, 1.8 + Math.sin(a) * 0.8, 0.3, 0.7 + k.rnd() * 0.6, 0.3, a);
		}
		for (let i = -2; i <= 2; i++) k.box(0.1, 1.9, 0.1, i * 0.35 - 2.2, 0.25, 2.9, mat(PAL.iron)).cone(0.07, 0.3, i * 0.35 - 2.2, 2.15, 2.9, ice, 4);
		k.spin(() => { const o = new T.Mesh(G.oct, glow(0x8fdcff, 1.2)); o.scale.set(0.35, 0.6, 0.35); return o; },
			(o, tt) => { o.position.set(2.2, 2.0 + Math.sin(tt * 1.5) * 0.15, 2.4); o.rotation.y = tt * 0.9; });
		k.flag(-3.1, 0.25, -3.1, 3.6, 0x8fd0ff);
	},
	council_jade(k, t) {
		const st = mat(0xb9b29c);
		const jade = mat(0x2f9a72, { roughness: 0.4 });
		const red = mat(0xa8322a);
		k.box(7.2, 0.35, 7.2, 0, 0, 0, mat(PAL.stone2)).box(4.6, 1.9, 4.0, 0, 0.35, -1.1, red);
		k.pyr(6.4, 1.1, 5.8, 0, 2.25, -1.1, jade).box(3.4, 1.3 + t * 0.15, 3.0, 0, 3.35, -1.1, red);
		k.pyr(4.8, 1.0, 4.4, 0, 4.65 + t * 0.15, -1.1, jade).cone(0.2, 1.0, 0, 5.65 + t * 0.15, -1.1, mat(PAL.gold, GOLD), 6);
		for (const x of [-1.7, 1.7]) k.cyl(0.18, 1.9, x, 0.35, 0.95, red, 8);
		// A stone giant keeps watch by the gate.
		k.box(0.42, 1.0, 0.5, 2.2, 0.35, 2.1, st).box(0.42, 1.0, 0.5, 2.85, 0.35, 2.1, st).box(1.5, 1.3, 0.9, 2.52, 1.35, 2.1, st);
		k.ball(0.38, 2.52, 2.95, 2.1, st).box(0.3, 1.1, 0.3, 1.6, 1.5, 2.1, st).box(0.3, 1.1, 0.3, 3.45, 1.5, 2.1, st);
		k.cyl(0.22, 1.6, 3.45, 0.35, 2.6, mat(PAL.woodD), 6, 1.6);
		if (t >= 2) k.ball(0.5, -2.6, 0.6, 2.4, jade, 0.8).ball(0.3, -2.0, 0.45, 2.9, jade, 0.8);
		k.flag(-3.1, 0.35, -2.9, 4.2, 0x2f9a72);
	},
	council_mistmoor(k, t) {
		const wood = mat(0x4e3a26);
		const bog = mat(0x3e5a3a);
		const moss = mat(0x5f8a3c);
		k.box(7.2, 0.15, 7.2, 0, 0, 0, bog).cyl(2.2, 0.06, 1.6, 0.15, 2.0, mat(0x2f4a3a, { roughness: 0.3 }), 14);
		for (const [x, z] of [[-2.2, -2.6], [2.2, -2.6], [-2.2, 0.2], [2.2, 0.2]]) k.cyl(0.16, 1.4, x, 0.15, z, wood, 6);
		k.box(5.0, 0.2, 3.4, 0, 1.55, -1.2, wood).box(4.2, 1.9 + t * 0.2, 2.8, 0, 1.75, -1.2, mat(0x6b5236));
		k.cone(3.4, 2.1, 0, 3.65 + t * 0.2, -1.2, mat(PAL.thatch), 7).cone(1.0, 0.8, 0, 5.4 + t * 0.2, -1.2, moss, 7);
		k.add(G.box, wood, -0.6, 0.85, 1.1, 0.9, 0.1, 2.0, 0, 0.75);
		for (const [x, z, s] of [[-2.9, 2.4, 1], [3.0, -2.9, 0.8]]) {
			k.add(cylGeo(0.6, 6), wood, x, 1.3 * s, z, 0.22 * s, 2.6 * s, 0.22 * s, 0, 0.2, 0.25);
			k.ball(0.9 * s, x + 0.35, 2.7 * s, z, moss, 0.7);
		}
		k.box(0.12, 0.7, 0.12, 0.7, 3.65, 0.25, wood).add(G.box, mat(0x1d1a22), 0.7, 4.4, 0.25, 0.7, 0.28, 0.12);
		k.spin(() => {
			const g = new T.Group();
			for (let i = 0; i < 4; i++) { const b = new T.Mesh(G.ball, glow(0x8cff6a, 1.2)); b.scale.setScalar(0.12); g.add(b); }
			return g;
		}, (g, tt) => g.children.forEach((b, i) => b.position.set(1.6 + Math.cos(tt * 0.7 + i * 1.6) * 1.4, 0.6 + Math.sin(tt * 1.3 + i) * 0.25, 2.0 + Math.sin(tt * 0.7 + i * 1.6) * 1.0)));
		k.flag(2.6, 0.15, 0.9, 3.3, 0x5fae3e);
	},
};

/** Generic construction site for a building's first level. */
function siteModel(k, W, D) {
	k.box(W * 0.85, 0.22, D * 0.85, 0, 0, 0, mat(PAL.sand));
	k.box(W * 0.58, 0.9, D * 0.58, 0, 0.22, 0, mat(PAL.stone2));
	scaffold(k, W * 0.66, D * 0.66, Math.min(2.7, 1 + W * 0.3));
	k.log(0.16, 1.6, -W * 0.3, 0.38, D * 0.38, mat(0xb07a45), Math.PI / 2).log(0.16, 1.6, -W * 0.3 + 0.34, 0.38, D * 0.38, mat(0xb07a45), Math.PI / 2);
	k.box(0.5, 0.5, 0.5, W * 0.3, 0.22, D * 0.36, mat(PAL.stone));
}

function tierOf(d, lv) {
	const max = d.levels.length;
	if (max <= 1 || lv <= 1) return 0;
	return clamp(Math.floor(((lv - 1) * 5) / max), 0, 4);
}
const modelCache = new Map();
/** stage: 'new' (first construction), 'up' (upgrading) or 'done'. */
function getModel(defIndex, lv, stage) {
	const d = defOf(defIndex);
	const tier = tierOf(d, lv);
	const key = defIndex + '|' + tier + '|' + stage;
	let m = modelCache.get(key);
	if (m) return m;
	const k = new Mk();
	const W = d.size[0] * TILE;
	const D = d.size[1] * TILE;
	k.box(W - 0.25, 0.06, D - 0.25, 0, 0, 0, mat(0x9b8a62));
	if (stage === 'new') siteModel(k, W, D);
	else {
		(MODELS[d.id] || ((kk) => kk.box(W * 0.7, 2, D * 0.7, 0, 0, 0, mat(PAL.stone))))(k, tier, W, D);
		if (stage === 'up') {
			const b = k.bounds();
			scaffold(k, Math.min(W - 0.4, b.max.x - b.min.x + 0.5), Math.min(D - 0.4, b.max.z - b.min.z + 0.5), Math.min(3.6, b.max.y * 0.6));
		}
	}
	if (stage !== 'done') builder(k, W / 2 - 0.35, D / 2 - 0.1);
	m = k.build();
	modelCache.set(key, m);
	return m;
}
function instantiate(model) {
	const group = new T.Group();
	for (const p of model.meshes) {
		const mesh = new T.Mesh(p.geo, p.mat);
		mesh.castShadow = true;
		mesh.receiveShadow = true;
		group.add(mesh);
	}
	const upd = model.dyn.map((fn) => fn(group)).filter(Boolean);
	return { group, upd };
}

// ---------------------------------------------------------------------------------------------
// Textures drawn at load (no image files)
// ---------------------------------------------------------------------------------------------
function canvasTex(w, h, draw) {
	const c = document.createElement('canvas');
	c.width = w;
	c.height = h;
	draw(c.getContext('2d'), w, h);
	const tex = new T.CanvasTexture(c);
	tex.encoding = T.sRGBEncoding;
	return tex;
}
let swirlTex = null;
function swirlTexture() {
	if (swirlTex) return swirlTex;
	swirlTex = canvasTex(128, 128, (g, w) => {
		const grd = g.createRadialGradient(64, 64, 4, 64, 64, 64);
		grd.addColorStop(0, 'rgba(235,220,255,1)');
		grd.addColorStop(0.5, 'rgba(150,100,255,0.85)');
		grd.addColorStop(1, 'rgba(60,30,160,0)');
		g.fillStyle = grd;
		g.fillRect(0, 0, w, w);
		g.strokeStyle = 'rgba(255,255,255,0.55)';
		g.lineWidth = 3;
		for (let arm = 0; arm < 4; arm++) {
			g.beginPath();
			for (let i = 0; i < 60; i++) {
				const a = (arm * Math.PI) / 2 + i * 0.09;
				const r = i * 1.0;
				g[i ? 'lineTo' : 'moveTo'](64 + Math.cos(a) * r, 64 + Math.sin(a) * r);
			}
			g.stroke();
		}
	});
	return swirlTex;
}

// ---------------------------------------------------------------------------------------------
// World: the floating island, water, clouds, decor, captured lands
// ---------------------------------------------------------------------------------------------
const scene = new T.Scene();
const cam = new T.PerspectiveCamera(45, 1, 1, 2000);
const world = {
	island: null, rim: null, sun: null, shadows: null, fall: null, clouds: [], decor: null, border: null, exp: -1,
	views: new Map(), lands: new Map(), selRing: null, grid: null, ghost: null, pond: null,
};
const hitMat = new T.MeshBasicMaterial({ visible: false });

function mulberry(seed) {
	let a = seed >>> 0;
	return () => {
		a = (a + 0x6d2b79f5) >>> 0;
		let t = Math.imul(a ^ (a >>> 15), 1 | a);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

const ISLE = { grass: 0x6fa645, grass2: 0x63993d, grassD: 0x4f7d30, dirt: 0x8a6a45, rock1: 0x8b7f6c, rock2: 0x6f6555, rock3: 0x5b5246 };
/**
 * A floating island: flat grassy top (out to flatTop of the rim), a bumpy grass edge, then a rocky
 * underside narrowing to a point. calm = [angle, width] keeps the edge flat where a stream leaves.
 */
function islandGeometry(R, depth, seed, seg, flatTop, bumps, calm) {
	const rnd = mulberry(seed);
	const ph = [rnd() * 6.3, rnd() * 6.3, rnd() * 6.3];
	const rim = (a) => R * (1 + 0.045 * Math.sin(3 * a + ph[0]) + 0.03 * Math.sin(5 * a + ph[1]) + 0.02 * Math.sin(9 * a + ph[2]));
	const rings = [
		[0, 0, ISLE.grass], [0.4, 0, ISLE.grass], [0.75, 0, ISLE.grass], [flatTop, 0, ISLE.grass], [0.96, 0, ISLE.grass2, 2], [1.0, 0.1, ISLE.grass],
		[1.02, -0.9, ISLE.grassD], [1.0, -2.0, ISLE.dirt], [0.96, -depth * 0.12, ISLE.rock1, 1], [0.88, -depth * 0.25, ISLE.rock2, 1],
		[0.75, -depth * 0.42, ISLE.rock1, 1], [0.57, -depth * 0.6, ISLE.rock3, 1], [0.37, -depth * 0.78, ISLE.rock2, 1], [0.16, -depth * 0.93, ISLE.rock3, 1],
		[0, -depth, ISLE.rock3],
	];
	const P = rings.map(([f, h, , jit]) => {
		const row = [];
		for (let j = 0; j < seg; j++) {
			const a = (j / seg) * Math.PI * 2;
			let r = rim(a) * f * (jit === 1 ? 1 + (rnd() - 0.5) * 0.16 : 1);
			let y = h;
			if (jit === 2) {
				// Bumps on the top edge, flattened where the stream crosses it.
				const k = calm ? clamp((Math.abs(Math.atan2(Math.sin(a - calm[0]), Math.cos(a - calm[0]))) - calm[1]) / 0.12, 0, 1) : 1;
				y = rnd() * bumps * 1.6 * k;
			} else if (jit) y += (rnd() - 0.5) * depth * 0.05;
			if (f === 0) r = 0;
			row.push(new T.Vector3(Math.cos(a) * r, y, Math.sin(a) * r));
		}
		return row;
	});
	const pos = [];
	const col = [];
	const uv = [];
	const n = new T.Vector3();
	const e1 = new T.Vector3();
	const e2 = new T.Vector3();
	const cc = new T.Color();
	// Top faces must face up, side and underside faces away from the vertical axis.
	const tri = (a, b, c, hex, top) => {
		e1.subVectors(b, a);
		e2.subVectors(c, a);
		n.crossVectors(e1, e2);
		if (n.lengthSq() < 1e-8) return;
		const out = top ? n.y : n.x * (a.x + b.x + c.x) + n.z * (a.z + b.z + c.z);
		if (out < 0) { const t = b; b = c; c = t; }
		cc.setHex(hex);
		const v = 0.92 + rnd() * 0.14;
		for (const p of [a, b, c]) {
			pos.push(p.x, p.y, p.z);
			col.push(cc.r * v, cc.g * v, cc.b * v);
			uv.push(p.x / 14, p.z / 14 + p.y / 10);
		}
	};
	for (let i = 0; i < P.length - 1; i++) {
		for (let j = 0; j < seg; j++) {
			const j2 = (j + 1) % seg;
			const hex = rings[i + 1][1] < -0.5 || i >= 5 ? rings[i + 1][2] : rings[i][2];
			tri(P[i][j], P[i][j2], P[i + 1][j2], hex, i < 5);
			tri(P[i][j], P[i + 1][j2], P[i + 1][j], hex, i < 5);
		}
	}
	const geo = new T.BufferGeometry();
	geo.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
	geo.setAttribute('color', new T.Float32BufferAttribute(col, 3));
	geo.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));
	geo.computeVertexNormals();
	geo.computeBoundingSphere();
	return { geo, rim };
}
let islandMat = null;
function islandMaterial() {
	if (islandMat) return islandMat;
	const tex = canvasTex(128, 128, (g, w, h) => {
		g.fillStyle = '#e6e6e6';
		g.fillRect(0, 0, w, h);
		const rnd = mulberry(99);
		for (let i = 0; i < 900; i++) {
			const v = 200 + Math.floor(rnd() * 55);
			g.fillStyle = 'rgb(' + v + ',' + v + ',' + v + ')';
			g.fillRect(rnd() * w, rnd() * h, 1 + rnd() * 3, 1 + rnd() * 3);
		}
	});
	tex.wrapS = tex.wrapT = T.RepeatWrapping;
	islandMat = new T.MeshStandardMaterial({ vertexColors: true, map: tex, flatShading: true, roughness: 0.95 });
	return islandMat;
}

/** Geometry with a baked vertex colour, for merged decor (trees, rocks). */
function coloredParts(parts) {
	const pos = [];
	const col = [];
	const c = new T.Color();
	for (const [geo, hex, x, y, z, sx, sy, sz] of parts) {
		_m4.compose(_p.set(x, y, z), _q.identity(), _s.set(sx, sy, sz));
		const g = (geo.index ? geo.toNonIndexed() : geo.clone()).applyMatrix4(_m4);
		c.setHex(hex);
		const a = g.attributes.position.array;
		for (let i = 0; i < a.length; i++) pos.push(a[i]);
		for (let i = 0; i < a.length / 3; i++) col.push(c.r, c.g, c.b);
		g.dispose();
	}
	const geo = new T.BufferGeometry();
	geo.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
	geo.setAttribute('color', new T.Float32BufferAttribute(col, 3));
	geo.computeVertexNormals();
	return geo;
}

function buildIsland() {
	const calmAngle = 1.22;
	const isl = islandGeometry(72, 62, 7, 72, 0.9, 1.0, [calmAngle, 0.12]);
	world.rim = isl.rim;
	world.island = new T.Mesh(isl.geo, islandMaterial());
	world.island.receiveShadow = true;
	world.island.castShadow = true;
	scene.add(world.island);

	// Pond, stream and the waterfall pouring off the edge.
	const water = new T.MeshStandardMaterial({ color: 0x3f93c8, roughness: 0.6, metalness: 0, transparent: true, opacity: 0.9 });
	const pc = { x: Math.cos(calmAngle) * 52, z: Math.sin(calmAngle) * 52 };
	const pond = new T.Mesh(new T.CircleGeometry(4.6, 22), water);
	pond.rotation.x = -Math.PI / 2;
	pond.position.set(pc.x, 0.07, pc.z);
	pond.receiveShadow = true;
	scene.add(pond);
	const rimR = isl.rim(calmAngle);
	const len = rimR + 1.4 - 55.5;
	const stream = new T.Mesh(G.plane, water);
	stream.rotation.set(-Math.PI / 2, 0, -calmAngle);
	stream.scale.set(len, 2.0, 1);
	const mid = 55.5 + len / 2;
	stream.position.set(Math.cos(calmAngle) * mid, 0.09, Math.sin(calmAngle) * mid);
	scene.add(stream);
	for (let i = 0; i < 9; i++) {
		const a = (i / 9) * Math.PI * 2;
		const rock = new T.Mesh(G.rock, mat(PAL.rock));
		rock.position.set(pc.x + Math.cos(a) * 4.9, 0.1, pc.z + Math.sin(a) * 4.9);
		rock.scale.set(0.7, 0.45, 0.7);
		if (Math.abs(Math.atan2(Math.sin(a - calmAngle), Math.cos(a - calmAngle))) > 0.5) scene.add(rock);
	}
	const fallTex = canvasTex(64, 128, (g, w, h) => {
		g.fillStyle = 'rgba(205,232,250,0.6)';
		g.fillRect(0, 0, w, h);
		const rnd = mulberry(5);
		for (let i = 0; i < 46; i++) {
			g.fillStyle = 'rgba(255,255,255,' + (0.25 + rnd() * 0.55) + ')';
			g.fillRect(rnd() * w, rnd() * h, 1 + rnd() * 3, 12 + rnd() * 40);
		}
	});
	fallTex.wrapS = fallTex.wrapT = T.RepeatWrapping;
	fallTex.repeat.set(1, 3);
	const fallGeo = new T.PlaneGeometry(1, 1, 1, 14);
	const fp = fallGeo.attributes.position;
	const out = { x: Math.cos(calmAngle), z: Math.sin(calmAngle) };
	const tan = { x: -out.z, z: out.x };
	for (let i = 0; i < fp.count; i++) {
		const u = fp.getX(i) * 2.2;
		const t = 0.5 - fp.getY(i);
		const r = rimR + 0.8 + t * t * 7;
		fp.setXYZ(i, out.x * r + tan.x * u, 0.12 - t * 52, out.z * r + tan.z * u);
	}
	fallGeo.computeVertexNormals();
	world.fall = new T.Mesh(fallGeo, new T.MeshBasicMaterial({ map: fallTex, transparent: true, opacity: 0.85, depthWrite: false, side: T.DoubleSide, fog: true }));
	scene.add(world.fall);
	world.pond = pc;
}

function buildSky() {
	scene.background = canvasTex(4, 256, (g, w, h) => {
		const grd = g.createLinearGradient(0, 0, 0, h);
		grd.addColorStop(0, '#5d9bd6');
		grd.addColorStop(0.5, '#a6cdea');
		grd.addColorStop(0.72, '#d8ebf6');
		grd.addColorStop(1, '#eef5f8');
		g.fillStyle = grd;
		g.fillRect(0, 0, w, h);
	});
	scene.fog = new T.Fog(0xd7e9f4, 280, 680);
	scene.add(new T.HemisphereLight(0xeaf3ff, 0x5d4e33, 0.82));
	const sun = new T.DirectionalLight(0xfff0d8, 1.55);
	sun.position.set(-70, 140, -25);
	sun.shadow.mapSize.set(2048, 2048);
	const sc = sun.shadow.camera;
	sc.left = -80; sc.right = 80; sc.top = 80; sc.bottom = -80; sc.near = 20; sc.far = 400;
	sun.shadow.bias = -0.0006;
	sun.shadow.normalBias = 0.03;
	scene.add(sun);
	scene.add(sun.target);
	world.sun = sun;

	const cloudTex = canvasTex(128, 128, (g) => {
		const rnd = mulberry(3);
		for (let i = 0; i < 7; i++) {
			const x = 30 + rnd() * 68;
			const y = 44 + rnd() * 40;
			const r = 22 + rnd() * 20;
			const grd = g.createRadialGradient(x, y, 2, x, y, r);
			grd.addColorStop(0, 'rgba(255,255,255,0.95)');
			grd.addColorStop(1, 'rgba(255,255,255,0)');
			g.fillStyle = grd;
			g.fillRect(0, 0, 128, 128);
		}
	});
	const rnd = mulberry(11);
	for (let i = 0; i < 26; i++) {
		const high = i % 5 === 0;
		const s = new T.Sprite(new T.SpriteMaterial({ map: cloudTex, transparent: true, depthWrite: false, opacity: 0.55 + rnd() * 0.35, fog: true }));
		const sc2 = 45 + rnd() * 80;
		s.scale.set(sc2, sc2 * 0.55, 1);
		const c = { s, a: rnd() * Math.PI * 2, r: (high ? 190 : 70) + rnd() * 200, y: high ? 6 + rnd() * 26 : -22 - rnd() * 75, w: (0.004 + rnd() * 0.01) * (rnd() < 0.5 ? 1 : -1) };
		world.clouds.push(c);
		scene.add(s);
	}
}

function buildDecor() {
	const pine = coloredParts([
		[cylGeo(0.7, 6), 0x6b4a2b, 0, 0.6, 0, 0.28, 1.2, 0.28],
		[cylGeo(0, 8), 0x3f7a35, 0, 1.9, 0, 1.25, 1.8, 1.25], [cylGeo(0, 8), 0x468a3a, 0, 2.8, 0, 0.95, 1.5, 0.95], [cylGeo(0, 8), 0x4f9640, 0, 3.6, 0, 0.62, 1.2, 0.62],
	]);
	const round = coloredParts([[cylGeo(0.7, 6), 0x6b4a2b, 0, 0.7, 0, 0.26, 1.4, 0.26], [G.ico, 0x5c9a3e, 0, 2.2, 0, 1.25, 1.1, 1.25], [G.ico, 0x6aa848, 0.4, 2.7, 0.2, 0.75, 0.7, 0.75]]);
	const rock = coloredParts([[G.rock, 0x8f8879, 0, 0.3, 0, 0.9, 0.6, 0.8], [G.rock, 0x7c7567, 0.7, 0.2, 0.3, 0.45, 0.35, 0.45]]);
	const dm = new T.MeshStandardMaterial({ vertexColors: true, flatShading: true, roughness: 0.9 });
	const pts = [];
	const rnd = mulberry(4242);
	const h = gridHalf();
	const gw = K.rules.grid.width;
	const gh = K.rules.grid.height;
	for (let cy = 0; cy < gh; cy++) {
		for (let cx = 0; cx < gw; cx++) {
			if (rnd() > 0.2) continue;
			const r = rnd();
			pts.push({ x: (cx + 0.2 + rnd() * 0.6 - h) * TILE, z: (cy + 0.2 + rnd() * 0.6 - h) * TILE, kind: r < 0.5 ? 0 : r < 0.82 ? 1 : 2, s: 0.75 + rnd() * 0.6, ry: rnd() * 6.3, cx, cy });
		}
	}
	const lim = h * TILE + 2;
	for (let i = 0, n = 0; i < 2000 && n < 230; i++) {
		const a = rnd() * Math.PI * 2;
		const r = 40 + rnd() * 34;
		const x = Math.cos(a) * r;
		const z = Math.sin(a) * r;
		if ((Math.abs(x) < lim && Math.abs(z) < lim) || r > world.rim(a) * 0.86) continue;
		if (Math.hypot(x - world.pond.x, z - world.pond.z) < 8) continue;
		if (Math.abs(Math.atan2(Math.sin(a - 1.22), Math.cos(a - 1.22))) < 0.06) continue;
		const k = rnd();
		pts.push({ x, z, kind: k < 0.55 ? 0 : k < 0.85 ? 1 : 2, s: 0.8 + rnd() * 0.7, ry: rnd() * 6.3, cx: -1, cy: -1 });
		n++;
	}
	const counts = [0, 0, 0];
	for (const p of pts) counts[p.kind]++;
	const meshes = [pine, round, rock].map((g, i) => {
		const m = new T.InstancedMesh(g, dm, Math.max(1, counts[i]));
		m.castShadow = i < 2;
		m.receiveShadow = true;
		m.frustumCulled = false;
		scene.add(m);
		return m;
	});
	const border = new T.InstancedMesh(coloredParts([[G.box, 0x4a8233, 0, 0.32, 0, 1.75, 0.64, 0.62], [G.ball, 0x548f3a, 0, 0.6, 0, 0.5, 0.3, 0.35]]), dm, 200);
	border.castShadow = true;
	border.receiveShadow = true;
	border.frustumCulled = false;
	scene.add(border);
	world.decor = { pts, meshes };
	world.border = border;
}

/** Trees and rocks cover the city's locked expansions; a hedge marks the open area. */
function updateDecor() {
	const [x0, y0, x1, y1] = currentArea();
	const n = [0, 0, 0];
	const m = new T.Matrix4();
	const q = new T.Quaternion();
	const up = new T.Vector3(0, 1, 0);
	const s = new T.Vector3();
	const p = new T.Vector3();
	for (const pt of world.decor.pts) {
		if (pt.cx >= 0 && pt.cx >= x0 && pt.cx < x1 && pt.cy >= y0 && pt.cy < y1) continue;
		q.setFromAxisAngle(up, pt.ry);
		m.compose(p.set(pt.x, 0, pt.z), q, s.setScalar(pt.s));
		world.decor.meshes[pt.kind].setMatrixAt(n[pt.kind]++, m);
	}
	world.decor.meshes.forEach((mesh, i) => { mesh.count = n[i]; mesh.instanceMatrix.needsUpdate = true; });
	const h = gridHalf();
	const X0 = (x0 - h) * TILE - 0.45;
	const X1 = (x1 - h) * TILE + 0.45;
	const Z0 = (y0 - h) * TILE - 0.45;
	const Z1 = (y1 - h) * TILE + 0.45;
	let c = 0;
	const put = (x, z, ry) => {
		if (c >= 200) return;
		q.setFromAxisAngle(up, ry);
		m.compose(p.set(x, 0, z), q, s.setScalar(1));
		world.border.setMatrixAt(c++, m);
	};
	for (let x = X0 + 1; x < X1; x += 2) { put(x, Z0, 0); put(x, Z1, 0); }
	for (let z = Z0 + 1; z < Z1; z += 2) { put(X0, z, Math.PI / 2); put(X1, z, Math.PI / 2); }
	world.border.count = c;
	world.border.instanceMatrix.needsUpdate = true;
}

/** Captured lands float around the city: first in front of it, then further around. */
function landSlot(i) {
	const ring = Math.floor(i / 10);
	const order = [0, 1, -1, 2, -2, 3, -3, 4, -4, 5][i % 10];
	const a = rad(45) + order * rad(36) + ring * rad(18);
	const r = 104 + ring * 30;
	return new T.Vector3(Math.cos(a) * r, -10 - ring * 5 + Math.sin(i * 2.3) * 2, Math.sin(a) * r);
}
/** The standing stone with a floating gold star that marks a victory-point land. */
function landStar(k, x, z) {
	k.box(0.9, 1.9, 0.6, x, 0, z, mat(PAL.stone2)).box(1.1, 0.3, 0.8, x, 0, z, mat(PAL.stoneD));
	k.spin(() => { const c = new T.Mesh(G.ico, glow(0xffcf4a, 1.1)); c.scale.set(0.45, 0.45, 0.2); c.position.set(x, 2.6, z); c.castShadow = true; return c; },
		(o, tt) => { o.rotation.y = tt * 1.5; o.position.y = 2.6 + Math.sin(tt * 2.2) * 0.15; });
}
function makeLandView(L, slot) {
	const isl = islandGeometry(11, 16, 1000 + L.id, 22, 0.85, 0.35);
	const group = new T.Group();
	const ground = new T.Mesh(isl.geo, islandMaterial());
	ground.receiveShadow = true;
	group.add(ground);
	const k = new Mk();
	k.seed = 31 + L.id;
	switch (landType(L.type).id) {
	case 'farm_land':
		k.box(10, 0.15, 6.5, 0.5, 0, 1.2, mat(PAL.dirt));
		for (let i = 0; i < 6; i++) k.box(9.4, 0.5, 0.5, 0.5, 0.15, -1.6 + i * 1.1, mat(PAL.wheat));
		k.box(2.6, 1.6, 2.2, -2.5, 0, -4.4, mat(PAL.white)).gable(2.9, 1.1, 2.5, -2.5, 1.6, -4.4, mat(PAL.roofR));
		k.log(0.42, 0.55, 1.4, 0.42, -4.0, mat(PAL.thatch), Math.PI / 2).log(0.42, 0.55, 2.5, 0.42, -4.4, mat(PAL.thatch), Math.PI / 2);
		break;
	case 'forest': {
		// Victory-point lands: forest, lake and mountain, each with a star stone.
		for (let i = 0; i < 9; i++) {
			const a = i * 0.7 + k.rnd() * 0.4;
			const r = 2.6 + k.rnd() * 2.6;
			const x = Math.cos(a) * r;
			const z = Math.sin(a) * r - 0.8;
			const h = 0.9 + k.rnd() * 0.6;
			k.cyl(0.26, 1.1 * h, x, 0, z, mat(0x5a3c22), 6).cone(1.35 * h, 2.2 * h, x, 0.9 * h, z, mat(0x2d6634), 7).cone(0.95 * h, 1.8 * h, x, 2.3 * h, z, mat(0x3a7d3e), 7);
		}
		k.rock(0.9, -1.4, 0.3, 1.8, mat(PAL.rock2), 0.6, 0.7).rock(0.6, 2.4, 0.2, 2.4, mat(PAL.leaf), 0.7);
		landStar(k, 0.4, 2.0);
		break;
	}
	case 'lake': {
		const water = mat(0x3f8fd0, { roughness: 0.25, metalness: 0.1 });
		k.cyl(6.0, 0.06, 0.4, 0, -0.4, mat(PAL.sand), 28).cyl(5.2, 0.12, 0.4, 0, -0.4, water, 28);
		for (let i = 0; i < 14; i++) {
			const a = 3.4 + k.rnd() * 2.6;
			const x = 0.4 + Math.cos(a) * (5.0 + k.rnd() * 0.8);
			const z = -0.4 + Math.sin(a) * (5.0 + k.rnd() * 0.8);
			k.cyl(0.07, 0.9 + k.rnd() * 0.7, x, 0, z, mat(0x6f8f3a), 4);
		}
		// A jetty out into the water with the star stone at its end.
		k.box(1.3, 0.14, 4.2, 3.6, 0.32, 1.6, mat(PAL.wood), 0.5);
		for (const [x, z] of [[3.0, 0.1], [4.1, -0.4], [3.1, 3.3], [4.2, 2.8]]) k.box(0.2, 0.5, 0.2, x, 0, z, mat(PAL.woodD));
		landStar(k, 4.6, -1.8);
		break;
	}
	case 'mountain':
		k.cone(4.2, 5.6, -0.9, 0, -1.6, mat(PAL.rock), 7).cone(1.75, 2.35, -0.9, 3.35, -1.6, mat(PAL.white), 7);
		k.cone(2.6, 3.6, 2.8, 0, 0.6, mat(PAL.rock2), 6).cone(1.0, 1.4, 2.8, 2.25, 0.6, mat(PAL.white), 6);
		k.rock(1.0, -3.6, 0.4, 2.2, mat(PAL.rock2), 0.7, 0.5).rock(0.7, 4.4, 0.3, 3.0, mat(PAL.rock), 0.8);
		landStar(k, 0.6, 3.4);
		break;
	case 'ruins': {
		// Ruins: a broken colonnade on a cracked floor, a shard glowing on its plinth.
		const st = mat(PAL.stone);
		k.box(7.0, 0.2, 5.2, 0, 0, -0.6, mat(PAL.stone2), 0.1);
		for (const [x, z, h] of [[-2.8, -2.6, 3.4], [-0.9, -2.8, 1.5], [1.0, -3.0, 2.8], [2.9, -3.2, 0.9], [-3.0, 1.2, 2.1], [3.2, 0.8, 1.2]]) {
			k.cyl(0.42, h, x, 0.2, z, st, 8).box(1.05, 0.25, 1.05, x, 0.2 + h, z, mat(PAL.stone2));
		}
		k.box(3.2, 0.45, 0.9, -0.4, 0.2, 2.9, st, 0.4).log(0.42, 2.6, 0.6, 0.62, 1.8, st, 0.6).rock(0.6, -1.8, 0.4, 2.4, mat(PAL.stoneD), 0.6);
		k.box(1.3, 1.0, 1.3, 0.4, 0.2, 0.4, mat(PAL.stoneD)).box(1.6, 0.2, 1.6, 0.4, 1.2, 0.4, mat(PAL.stone2));
		k.spin(() => { const c = new T.Mesh(G.oct, glow(0x8fd8ff, 1.3)); c.scale.set(0.42, 0.75, 0.42); c.position.set(0.4, 2.4, 0.4); c.castShadow = true; return c; },
			(o, tt) => { o.rotation.y = tt * 1.2; o.position.y = 2.4 + Math.sin(tt * 1.8) * 0.18; });
		break;
	}
	default:
		k.rock(3.0, 1.0, 1.0, -2.6, mat(PAL.rock), 0.8, 0.4).rock(2.0, -2.4, 0.7, -1.2, mat(PAL.rock2), 0.75, 1.3).rock(1.4, 3.6, 0.5, 0.4, mat(PAL.rock2), 0.8);
		k.box(1.5, 1.5, 0.4, 1.0, 0, -0.1, mat(0x1b1815)).box(0.24, 1.8, 0.24, 0.15, 0, 0.1, mat(PAL.wood)).box(0.24, 1.8, 0.24, 1.85, 0, 0.1, mat(PAL.wood)).box(2.2, 0.28, 0.34, 1.0, 1.75, 0.1, mat(PAL.woodD));
		for (let i = 0; i < 6; i++) {
			const a = k.rnd() * 6.3;
			k.add(G.ico, mat(PAL.gold, GOLD), 1.0 + Math.cos(a) * 1.8, 1.2 + k.rnd() * 1.2, -2.6 + Math.sin(a) * 1.4, 0.3, 0.3, 0.3, a);
		}
		k.box(1.0, 0.5, 0.8, 1.0, 0, 1.6, mat(PAL.woodD)).ball(0.45, 1.0, 0.6, 1.6, mat(PAL.gold, GOLD), 0.55);
	}
	k.seed = 77 + L.id;
	for (let i = 0; i < 4; i++) {
		const a = k.rnd() * 6.3;
		const x = Math.cos(a) * 7.4;
		const z = Math.sin(a) * 7.4;
		k.cyl(0.25, 1.1, x, 0, z, mat(0x6b4a2b), 6).cone(1.1, 1.8, x, 1.0, z, mat(0x447f37), 8).cone(0.75, 1.4, x, 2.1, z, mat(0x4f9640), 8);
	}
	k.flag(-6.2, 0, 2.5, 4.2);
	if (L.up) {
		// Being upgraded: a builder at work by a stack of planks.
		k.box(1.6, 0.22, 0.7, -3.4, 0, 4.6, mat(PAL.wood), 0.3).box(1.4, 0.22, 0.6, -3.4, 0.22, 4.6, mat(PAL.woodD), 0.5);
		builder(k, -2.2, 3.9);
	}
	if (L.titan) {
		// A titan guards the land: a golden statue on a plinth.
		const g = mat(PAL.gold, GOLD);
		k.box(1.8, 0.6, 1.8, 4.6, 0, -4.2, mat(PAL.stone2)).box(0.45, 1.4, 0.5, 4.3, 0.6, -4.2, g).box(0.45, 1.4, 0.5, 4.9, 0.6, -4.2, g);
		k.box(1.4, 1.6, 0.8, 4.6, 2.0, -4.2, g).ball(0.42, 4.6, 4.0, -4.2, g).add(G.box, g, 5.5, 3.3, -4.2, 0.3, 1.3, 0.3, 0, 0, -0.5);
	}
	const model = k.build();
	const inst = instantiate(model);
	group.add(inst.group);
	const pos = landSlot(slot);
	group.position.copy(pos);
	scene.add(group);
	const hit = new T.Mesh(cylGeo(1, 12), hitMat);
	hit.scale.set(12, 9, 12);
	hit.position.set(pos.x, pos.y + 1, pos.z);
	hit.visible = false;
	hit.userData = { kind: 'land', id: L.id };
	scene.add(hit);
	return { id: L.id, group, upd: inst.upd, hit, base: pos.clone(), anchor: new T.Vector3(pos.x, pos.y + 5.2, pos.z), model, isl: isl.geo, phase: L.id * 1.3 };
}
function removeLandView(v) {
	scene.remove(v.group);
	scene.remove(v.hit);
	v.isl.dispose();
	for (const p of v.model.meshes) p.geo.dispose();
}

// ---------------------------------------------------------------------------------------------
// Building views
// ---------------------------------------------------------------------------------------------
function makeBuildingView(b) {
	const d = defOf(b.def);
	const stage = b.lv === 0 ? 'new' : b.up ? 'up' : 'done';
	const model = getModel(b.def, b.lv, stage);
	const inst = instantiate(model);
	const c = cellCenter(d, b.x, b.y);
	inst.group.position.set(c.x, 0, c.z);
	inst.group.userData.bid = b.id;
	scene.add(inst.group);
	const H = Math.max(1.6, model.box.max.y);
	return { id: b.id, group: inst.group, upd: inst.upd, anchor: new T.Vector3(c.x, H + 0.5, c.z), d, b };
}
function removeView(v) {
	scene.remove(v.group);
}

function buildSelRing() {
	const g = new T.Group();
	const m = new T.MeshStandardMaterial({ color: 0xffd36b, emissive: 0xffc23a, emissiveIntensity: 0.9, roughness: 0.6 });
	for (let i = 0; i < 4; i++) {
		const b = new T.Mesh(G.box, m);
		g.add(b);
	}
	g.visible = false;
	scene.add(g);
	world.selRing = g;
}
function placeRing(cx, cz, W, D, color) {
	const g = world.selRing;
	const [a, b, c, d] = g.children;
	const t = 0.42;
	a.scale.set(W + t, 0.24, t); a.position.set(0, 0.12, -D / 2);
	b.scale.set(W + t, 0.24, t); b.position.set(0, 0.12, D / 2);
	c.scale.set(t, 0.24, D + t); c.position.set(-W / 2, 0.12, 0);
	d.scale.set(t, 0.24, D + t); d.position.set(W / 2, 0.12, 0);
	g.position.set(cx, 0, cz);
	g.visible = true;
	a.material.color.setHex(color || 0xffd36b);
	a.material.emissive.setHex(color || 0xffc23a);
}
function updateSelRing() {
	const s = K.sel;
	if (K.place) {
		const d = defOf(K.place.def);
		const c = cellCenter(d, K.place.x, K.place.y);
		placeRing(c.x, c.z, d.size[0] * TILE, d.size[1] * TILE, K.place.ok ? 0x6ee38f : 0xff6b5a);
		return;
	}
	if (s && s.kind === 'b') {
		const v = world.views.get(s.id);
		if (v) {
			const c = cellCenter(v.d, v.b.x, v.b.y);
			placeRing(c.x, c.z, v.d.size[0] * TILE, v.d.size[1] * TILE);
			return;
		}
	}
	world.selRing.visible = false;
}

/** Brings the scene in line with the kingdom state (buildings by id, lands by slot). */
function syncViews() {
	const st = K.st;
	const seen = new Set();
	for (const b of st.b) {
		seen.add(b.id);
		const key = b.def + '|' + b.lv + '|' + b.up + '|' + b.x + '|' + b.y;
		let v = world.views.get(b.id);
		if (!v || v.key !== key) {
			if (v) removeView(v);
			v = makeBuildingView(b);
			v.key = key;
			world.views.set(b.id, v);
		}
		v.b = b;
		v.group.visible = !(K.place && K.place.id === b.id);
	}
	for (const [id, v] of world.views) if (!seen.has(id)) { removeView(v); world.views.delete(id); }
	const seenL = new Set();
	st.lands.forEach((L, i) => {
		seenL.add(L.id);
		const key = L.type + '|' + L.lv + '|' + i + '|' + (L.titan ? 1 : 0) + '|' + L.up;
		let v = world.lands.get(L.id);
		if (!v || v.key !== key) {
			if (v) removeLandView(v);
			v = makeLandView(L, i);
			v.key = key;
			world.lands.set(L.id, v);
		}
		v.L = L;
	});
	for (const [id, v] of world.lands) if (!seenL.has(id)) { removeLandView(v); world.lands.delete(id); }
	if (st.expansions !== world.exp) {
		world.exp = st.expansions;
		updateDecor();
	}
	updateSelRing();
}

// ---------------------------------------------------------------------------------------------
// Camera and input
// ---------------------------------------------------------------------------------------------
const view = { tx: 0, tz: 6, dist: 84, minD: 34, maxD: 230, yaw: Math.PI / 4, vx: 0, vz: 0 };
function placeCamera(dt) {
	if (!input.press || input.press.mode !== 'pan' || !input.press.moved) {
		view.tx += view.vx * dt;
		view.tz += view.vz * dt;
		const k = Math.exp(-dt * 5);
		view.vx *= k;
		view.vz *= k;
	}
	const r = Math.hypot(view.tx, view.tz);
	const max = 150;
	if (r > max) { view.tx *= max / r; view.tz *= max / r; }
	view.dist = clamp(view.dist, view.minD, view.maxD);
	const z = (view.dist - view.minD) / (view.maxD - view.minD);
	const pitch = rad(lerp(38, 58, z));
	const cp = Math.cos(pitch);
	const d = view.dist;
	cam.position.set(view.tx + Math.sin(view.yaw) * cp * d, Math.sin(pitch) * d, view.tz + Math.cos(view.yaw) * cp * d);
	cam.lookAt(view.tx, 0, view.tz);
	cam.updateMatrixWorld();
}
function focusOn(x, z, dist) {
	view.tx = x;
	view.tz = z;
	if (dist) view.dist = dist;
	view.vx = view.vz = 0;
}

const raycaster = new T.Raycaster();
const ndc = new T.Vector2();
function setRay(sx, sy) {
	ndc.set((sx / K.app.viewW) * 2 - 1, -(sy / K.app.viewH) * 2 + 1);
	raycaster.setFromCamera(ndc, cam);
}
function groundAt(sx, sy, h) {
	setRay(sx, sy);
	const o = raycaster.ray.origin;
	const d = raycaster.ray.direction;
	if (Math.abs(d.y) < 1e-6) return null;
	const t = ((h || 0) - o.y) / d.y;
	return t > 0 ? { x: o.x + d.x * t, z: o.z + d.z * t } : null;
}
/** Nearest building or land under a screen point: the models themselves, then the footprint under the point. */
function pickAt(sx, sy) {
	setRay(sx, sy);
	const objs = [];
	for (const v of world.views.values()) if (v.group.visible) objs.push(v.group);
	for (const v of world.lands.values()) objs.push(v.hit);
	const hits = raycaster.intersectObjects(objs, true);
	for (const h of hits) {
		let o = h.object;
		if (o.userData.kind) return o.userData;
		while (o && !o.userData.bid) o = o.parent;
		if (o) return { kind: 'b', id: o.userData.bid };
	}
	const g = groundAt(sx, sy);
	if (!g) return null;
	for (const v of world.views.values()) {
		const c = cellCenter(v.d, v.b.x, v.b.y);
		if (v.group.visible && Math.abs(g.x - c.x) <= v.d.size[0] && Math.abs(g.z - c.z) <= v.d.size[1]) return { kind: 'b', id: v.id };
	}
	return null;
}

const input = { pointers: new Map(), press: null, pinch: null, longTimer: 0 };
function cancelLong() { if (input.longTimer) { clearTimeout(input.longTimer); input.longTimer = 0; } }
function onDown(e) {
	if (K.app.mode !== 'kingdom') return;
	e.preventDefault();
	try { e.target.setPointerCapture(e.pointerId); } catch (err) { /* not capturable */ }
	input.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
	view.vx = view.vz = 0;
	if (input.pointers.size === 2) {
		cancelLong();
		input.press = null;
		const [p1, p2] = [...input.pointers.values()];
		input.pinch = { d0: Math.hypot(p1.x - p2.x, p1.y - p2.y) || 1, dist0: view.dist, anchor: groundAt((p1.x + p2.x) / 2, (p1.y + p2.y) / 2) };
		return;
	}
	if (input.pointers.size > 2) return;
	const press = { id: e.pointerId, x0: e.clientX, y0: e.clientY, x: e.clientX, y: e.clientY, t: performance.now(), moved: false, mode: 'pan',
		anchor: groundAt(e.clientX, e.clientY) };
	if (K.place && press.anchor && ghostContains(press.anchor, 1.2)) {
		const d = defOf(K.place.def);
		const c = cellCenter(d, K.place.x, K.place.y);
		press.mode = 'ghost';
		press.off = { x: c.x - press.anchor.x, z: c.z - press.anchor.z };
	} else if (!K.place && !hitOverlay(e.clientX, e.clientY, true)) {
		input.longTimer = setTimeout(() => {
			input.longTimer = 0;
			if (!input.press || input.press.moved) return;
			const p = pickAt(input.press.x0, input.press.y0);
			if (p && p.kind === 'b') { input.press.mode = 'done'; startMove(p.id); }
		}, 600);
	}
	input.press = press;
}
function onMove(e) {
	const pt = input.pointers.get(e.pointerId);
	if (!pt) return;
	pt.x = e.clientX;
	pt.y = e.clientY;
	if (input.pinch && input.pointers.size >= 2) {
		const [p1, p2] = [...input.pointers.values()];
		const d = Math.hypot(p1.x - p2.x, p1.y - p2.y) || 1;
		view.dist = clamp((input.pinch.dist0 * input.pinch.d0) / d, view.minD, view.maxD);
		placeCamera(0);
		const g = groundAt((p1.x + p2.x) / 2, (p1.y + p2.y) / 2);
		if (g && input.pinch.anchor) { view.tx += input.pinch.anchor.x - g.x; view.tz += input.pinch.anchor.z - g.z; placeCamera(0); }
		return;
	}
	const p = input.press;
	if (!p || p.id !== e.pointerId) return;
	const now = performance.now();
	const dtm = Math.max(1, now - p.t) / 1000;
	p.x = e.clientX;
	p.y = e.clientY;
	if (!p.moved && Math.hypot(p.x - p.x0, p.y - p.y0) > 9) { p.moved = true; cancelLong(); }
	if (!p.moved || p.mode === 'done') return;
	if (p.mode === 'ghost') {
		const g = groundAt(p.x, p.y);
		if (g) moveGhost(g.x + p.off.x, g.z + p.off.z);
		return;
	}
	const g = groundAt(p.x, p.y);
	if (g && p.anchor) {
		const dx = p.anchor.x - g.x;
		const dz = p.anchor.z - g.z;
		view.tx += dx;
		view.tz += dz;
		view.vx = lerp(view.vx, dx / dtm, 0.5);
		view.vz = lerp(view.vz, dz / dtm, 0.5);
		placeCamera(0);
	}
	p.t = now;
}
function onUp(e) {
	if (!input.pointers.has(e.pointerId)) return;
	input.pointers.delete(e.pointerId);
	if (input.pinch) {
		if (input.pointers.size < 2) input.pinch = null;
		input.press = null;
		return;
	}
	const p = input.press;
	if (!p || p.id !== e.pointerId) return;
	cancelLong();
	input.press = null;
	if (performance.now() - p.t > 120) { view.vx = 0; view.vz = 0; }
	if (!p.moved && p.mode !== 'done') tap(p.x0, p.y0);
}
function onWheel(e) {
	if (K.app.mode !== 'kingdom') return;
	e.preventDefault();
	const a = groundAt(e.clientX, e.clientY);
	view.dist = clamp(view.dist * Math.exp(e.deltaY * 0.0012), view.minD, view.maxD);
	placeCamera(0);
	const b = groundAt(e.clientX, e.clientY);
	if (a && b) { view.tx += a.x - b.x; view.tz += a.z - b.z; }
}

function tap(x, y) {
	if (hitOverlay(x, y, false)) return;
	if (K.place) {
		const g = groundAt(x, y);
		if (g) moveGhost(g.x, g.z);
		return;
	}
	const p = pickAt(x, y);
	if (!p) { select(null); return; }
	if (p.kind === 'b') {
		const v = world.views.get(p.id);
		if (v && v.d.role === 'producer' && !v.b.up && v.b.st > 0 && !(K.sel && K.sel.kind === 'b' && K.sel.id === p.id)) collect(p.id, true);
		select({ kind: 'b', id: p.id });
	} else select({ kind: 'land', id: p.id });
}

// ---------------------------------------------------------------------------------------------
// Overlay: collect bubbles, timers, labels, floating numbers
// ---------------------------------------------------------------------------------------------
let kov = null;
let kctx = null;
const _v = new T.Vector3();
function project(v3, out) {
	_v.copy(v3).project(cam);
	if (_v.z > 1 || _v.z < -1) return null;
	out = out || {};
	out.x = ((_v.x + 1) / 2) * K.app.viewW;
	out.y = ((1 - _v.y) / 2) * K.app.viewH;
	return out;
}
function hitOverlay(x, y, peek) {
	for (let i = K.hits.length - 1; i >= 0; i--) {
		const h = K.hits[i];
		const inside = h.r ? Math.hypot(x - h.x, y - h.y) <= h.r : x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h;
		if (inside) { if (!peek) h.fn(); return true; }
	}
	return false;
}
function roundRect(c, x, y, w, h, r) {
	c.beginPath();
	c.moveTo(x + r, y);
	c.arcTo(x + w, y, x + w, y + h, r);
	c.arcTo(x + w, y + h, x, y + h, r);
	c.arcTo(x, y + h, x, y, r);
	c.arcTo(x, y, x + w, y, r);
	c.closePath();
}
const FONT = '"Fira Sans Condensed", "Roboto Condensed", "Arial Narrow", sans-serif';
function bubble(c, x, y, kind, frac, t, fn) {
	const r = 17;
	const by = y - 24 + Math.sin(t * 3 + x * 0.05) * 2;
	c.fillStyle = 'rgba(0,0,0,0.25)';
	c.beginPath();
	c.ellipse(x, y - 2, 9, 3, 0, 0, Math.PI * 2);
	c.fill();
	c.beginPath();
	c.moveTo(x - 6, by + r - 3);
	c.lineTo(x, by + r + 7);
	c.lineTo(x + 6, by + r - 3);
	c.fillStyle = '#fbf6ea';
	c.fill();
	c.beginPath();
	c.arc(x, by, r, 0, Math.PI * 2);
	c.fill();
	c.lineWidth = 3;
	c.strokeStyle = 'rgba(60,45,25,0.35)';
	c.stroke();
	c.beginPath();
	c.arc(x, by, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * clamp(frac, 0, 1));
	c.strokeStyle = frac >= 1 ? '#e5533d' : kind === 'food' ? '#7cbf3c' : '#e0a92c';
	c.stroke();
	drawIcon(c, kind, x, by, 22);
	K.hits.push({ x, y: by, r: r + 8, fn });
}
function pill(c, x, y, text, sub, frac, icon) {
	c.font = '600 12px ' + FONT;
	const im = icon ? DotIcons.image(icon) : null;
	const iw = im ? 20 : 0;
	const w = Math.max(58, c.measureText(text).width + 22 + iw);
	const h = frac === undefined ? 22 : 26;
	roundRect(c, x - w / 2, y - h, w, h, 8);
	c.fillStyle = 'rgba(17,22,27,0.86)';
	c.fill();
	c.strokeStyle = 'rgba(210,162,90,0.55)';
	c.lineWidth = 1;
	c.stroke();
	c.fillStyle = '#efe7d6';
	c.textAlign = 'center';
	c.textBaseline = 'middle';
	c.fillText(text, x + iw / 2, y - (frac === undefined ? 11 : 15));
	if (im) c.drawImage(im, x - w / 2 + 5, y - (frac === undefined ? 20 : 24), 18, 18);
	if (frac !== undefined) {
		roundRect(c, x - w / 2 + 6, y - 7, w - 12, 4, 2);
		c.fillStyle = 'rgba(255,255,255,0.15)';
		c.fill();
		roundRect(c, x - w / 2 + 6, y - 7, Math.max(4, (w - 12) * clamp(frac, 0, 1)), 4, 2);
		c.fillStyle = '#f2c14e';
		c.fill();
	}
	if (sub) {
		c.font = '700 11px ' + FONT;
		c.fillStyle = '#f2c14e';
		c.fillText(sub, x, y - h - 8);
	}
	return { w, h };
}
function drawOverlay(t) {
	const app = K.app;
	const s = app.ovScale;
	const W = Math.round(app.viewW * s);
	const H = Math.round(app.viewH * s);
	if (kov.width !== W || kov.height !== H) { kov.width = W; kov.height = H; }
	const c = kctx;
	c.setTransform(1, 0, 0, 1, 0, 0);
	c.clearRect(0, 0, W, H);
	c.setTransform(s, 0, 0, s, 0, 0);
	K.hits.length = 0;
	const st = K.st;
	const nf = nowF();
	const pt = {};
	const items = [];
	for (const v of world.views.values()) {
		if (!v.group.visible || !project(v.anchor, pt)) continue;
		items.push({ v, x: pt.x, y: pt.y });
	}
	items.sort((a, b) => a.y - b.y);
	for (const it of items) {
		const b = it.v.b;
		const d = it.v.d;
		const selected = K.sel && K.sel.kind === 'b' && K.sel.id === b.id;
		if (b.up) {
			const total = Math.max(1, b.f - b.s);
			const left = b.f - nf;
			pill(c, it.x, it.y, fmtDur(left), selected ? d.name : '', 1 - left / total, 'building.' + d.id);
		} else if (d.role === 'producer' && b.sc > 0 && (b.st >= Math.max(1, b.sc * 0.1) || (selected && b.st > 0))) {
			bubble(c, it.x, it.y, d.resource, b.st / b.sc, t / 1000, () => collect(b.id, true));
		} else if (d.id === 'relic_temple' && b.lv > 0 && st.relicAt <= nf) {
			bubble(c, it.x, it.y, 'gems', 0.99, t / 1000, openRelics); // the daily relic is ready
		} else if (d.id === 'spell_forge' && b.lv > 0 && st.spells.queue.length) {
			pill(c, it.x, it.y, st.spells.qb ? 'Запас полон' : K.army.spells[st.spells.queue[0]].name + ' ' + fmtClock(st.spells.qf - nf), '', undefined, K.army.spells[st.spells.queue[0]].id);
		} else if (d.id === 'barracks' && b.lv > 0 && st.army.queue.length) {
			pill(c, it.x, it.y, st.army.qb ? 'Лагерь полон' : K.army.troops[st.army.queue[0]].name + ' ' + fmtClock(st.army.qf - nf), '', undefined, K.army.troops[st.army.queue[0]].id);
		} else if (d.id === 'garrison' && b.lv > 0 && st.raids.shield > nf) {
			pill(c, it.x, it.y, 'Щит ' + fmtDur(st.raids.shield - nf), selected ? d.name : '', undefined, 'building.' + d.id);
		} else if (selected) {
			pill(c, it.x, it.y, d.name + ' · ур. ' + b.lv, '', undefined, 'building.' + d.id);
		}
	}
	for (const v of world.lands.values()) {
		if (!project(v.anchor, pt)) continue;
		const L = v.L;
		const kind = landType(L.type).resource;
		if (L.up) pill(c, pt.x, pt.y, fmtDur(L.f - nf), K.sel && K.sel.kind === 'land' && K.sel.id === L.id ? landType(L.type).name : '', 1 - (L.f - nf) / Math.max(1, L.f - L.s), landGlyph(L.type));
		else if (L.st >= Math.max(1, L.cap * 0.08)) bubble(c, pt.x, pt.y - 4, kind, L.st / L.cap, t / 1000, () => collectLand(L.id));
		else pill(c, pt.x, pt.y, 'ур. ' + L.lv + ' · ' + (L.shield > nf ? 'щит ' + fmtDur(L.shield - nf) : fmtShort(L.rate) + ((landType(L.type).periodHours || 1) >= 24 ? '/сут' : '/ч')));
	}
	drawExpansionSign(c, pt);
	for (let i = K.floaters.length - 1; i >= 0; i--) {
		const f = K.floaters[i];
		const age = (t - f.t0) / 1000;
		if (age > 1.5 || !project(f.pos, pt)) { if (age > 1.5) K.floaters.splice(i, 1); continue; }
		const y = pt.y - 30 - age * 34;
		c.globalAlpha = clamp(1.6 - age, 0, 1);
		c.font = '800 18px ' + FONT;
		c.textAlign = 'center';
		c.textBaseline = 'middle';
		c.lineWidth = 4;
		c.strokeStyle = 'rgba(20,14,6,0.8)';
		c.strokeText(f.text, pt.x + 10, y);
		c.fillStyle = f.color;
		c.fillText(f.text, pt.x + 10, y);
		if (f.icon) drawIcon(c, f.icon, pt.x - c.measureText(f.text).width / 2 - 4, y, 18);
		c.globalAlpha = 1;
	}
}
function drawExpansionSign(c, pt) {
	const st = K.st;
	const ex = K.rules.expansions;
	if (K.place || st.expansions >= ex.length) return;
	const [, y0, x1, y1] = currentArea();
	const h = gridHalf();
	// Middle of the city's right edge (the bottom corner sits under the HUD buttons).
	const pos = new T.Vector3((x1 - h) * TILE + 3, 0.5, ((y0 + y1) / 2 - h) * TILE);
	if (!project(pos, pt)) return;
	const ck = st.exp;
	const text = ck.err === E.CastleTooLow ? 'Расширение · Замок ' + ck.c : 'Расширить · ' + fmtShort(ck.cost[0]);
	c.font = '700 13px ' + FONT;
	const w = c.measureText(text).width + (ck.err === E.CastleTooLow ? 20 : 40);
	const x = pt.x - w / 2;
	const y = pt.y - 34;
	c.fillStyle = '#6b4a2b';
	c.fillRect(pt.x - 2, y + 26, 4, 10);
	roundRect(c, x, y, w, 28, 8);
	c.fillStyle = ck.err === E.CastleTooLow ? 'rgba(17,22,27,0.82)' : 'rgba(58,40,18,0.92)';
	c.fill();
	c.strokeStyle = '#d2a25a';
	c.lineWidth = 1.5;
	c.stroke();
	c.fillStyle = '#efe7d6';
	c.textAlign = 'left';
	c.textBaseline = 'middle';
	c.fillText(text, x + 10, y + 14);
	if (ck.err !== E.CastleTooLow) drawIcon(c, 'gold', x + w - 16, y + 14, 18);
	K.hits.push({ x, y, w, h: 28, fn: openExpansion });
}
function floatText(pos, text, color, icon) {
	K.floaters.push({ pos: pos.clone(), text, color, icon, t0: performance.now() });
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------
function toast(text, kind) {
	const box = $('kToasts');
	const el = document.createElement('div');
	el.className = 'toast' + (kind ? ' ' + kind : '');
	el.textContent = text;
	box.appendChild(el);
	while (box.children.length > (Math.min(K.app.viewW, K.app.viewH) < 500 ? 2 : 3)) box.removeChild(box.firstChild);
	setTimeout(() => { if (el.parentNode) el.parentNode.removeChild(el); }, 3400);
}
/** Logs an error of the kingdom screen and shows its text once, so the screen keeps running and the text can be passed on. */
function report(where, e) {
	const text = where + ': ' + ((e && e.message) || e);
	if (K.reported.has(text)) return;
	K.reported.add(text);
	console.error('Kingdom ' + where + ' failed:', e);
	try { toast('Ошибка королевства (' + text + ')', 'bad'); } catch (e2) { /* the toast box itself is broken */ }
}
function missingText(cost) {
	const st = K.st;
	const parts = [];
	cost.forEach((v, i) => { if (i !== 3 && v > st.res[i]) parts.push(fmt(v - st.res[i]) + ' ' + RES_OF[RES[i]]); });
	return parts.length ? 'Не хватает ' + parts.join(' и ') : 'Не хватает ресурсов';
}
/** ctx: '' (buildings/army), 'forge' (Spell Forge) or 'arcane' (Arcane Tower). */
function errText(code, ck, ctx) {
	switch (code) {
	case E.MaxLevel: return 'Максимальный уровень';
	case E.CastleTooLow: return ck && ck.c ? 'Нужен Замок ' + ck.c : 'Нужен Замок выше';
	case E.NeedRequirement: return ck && ck.m[0] >= 0 ? 'Нужно: ' + defOf(ck.m[0]).name + ' ур. ' + ck.m[1] + (ck.m[2] > 1 ? ' (' + ck.m[2] + ' шт.)' : '') : 'Не выполнены требования';
	case E.NeedExpansion: return 'Нужно расширение города' + (ck && ck.e ? ' ' + ck.e : '');
	case E.Locked: return ck && ck.c ? 'Откроется на Замке ' + ck.c : 'Пока закрыто';
	case E.LimitReached: return ck && ck.c ? 'Построено максимум, ещё одна на Замке ' + ck.c : 'Построено максимум';
	case E.NoBuilder: return 'Все строители заняты';
	case E.NotEnough: return ck ? missingText(ck.cost) : 'Не хватает ресурсов';
	case E.BadPlacement: return 'Здесь нельзя строить';
	case E.Busy: return ctx === 'arcane' ? 'Заклинание уже улучшается' : ctx === 'land' ? 'Земля улучшается: дождитесь конца или ускорьте' : 'Постройка уже улучшается';
	case E.NotBusy: return 'Сейчас здесь ничего не строится';
	case E.Nothing: return 'Пока нечего собирать';
	case E.StorageFull: return 'Хранилище заполнено';
	case E.NeedStones: return 'Не хватает камней портала';
	case E.NoLandSlot: return 'Нет свободных мест для земель';
	case E.NoAttack: return 'Бой не найден';
	case E.QueueFull: return ctx === 'forge' ? 'Очередь Кузни заполнена' : 'Очередь Казарм заполнена';
	case E.Researching: return ctx === 'arcane' ? 'Все места улучшения в Башне заняты' : 'Арсенал уже улучшает другое войско';
	case E.TooManySquads: return ctx === 'defense' ? 'В Гарнизоне нет столько мест' : 'Слишком много отрядов для Лагеря армии';
	case E.NoTroops: return 'Столько отрядов нет в лагере';
	case E.TitanRecovering: return 'Титан отдыхает после боя';
	case E.NotReady: return 'Ещё не готово';
	case E.RosterFull: return 'Зал титанов заполнен';
	case E.NotMaxLevel: return 'Сначала доведите титана до предела уровня';
	case E.WrongRelic: return 'Эта реликвия сюда не подходит';
	case E.SameRelic: return 'Такая реликвия уже надета: две одинаковые носить нельзя';
	case E.InventoryFull: return 'Склад реликвий полон (' + K.army.relics.maxRelics + ')';
	case E.NeedVp: return 'Нужно ' + fmt(ck && ck.cost[5] ? ck.cost[5] : K.rules.alliances.joinVp) + ' очков победы за этот или прошлый сезон';
	case E.AllianceFull: return 'В альянсе нет мест (' + K.rules.alliances.members[1] + ')';
	case E.InAlliance: return 'Сначала выйдите из своего альянса';
	case E.NoAlliance: return 'Сначала вступите в альянс';
	default: return 'Не получилось (код ' + code + ')';
	}
}
function storageHint(resource) {
	return resource === 'food' ? 'улучшите Амбар или постройте ещё один' : 'улучшите Золотое хранилище или постройте ещё одно';
}

// ---------------------------------------------------------------------------------------------
// State refresh
// ---------------------------------------------------------------------------------------------
function refresh(force) {
	const n = nowSec();
	K.bridge.advance(n);
	const prev = K.st;
	K.st = K.bridge.state();
	K.lastStateSec = Math.floor(nowF());
	K.lastStateAt = performance.now();
	if (prev && !force) announce(prev, K.st);
	announceSeason(K.st);
	announceRaids(K.st);
	syncViews();
	updateHud();
	updatePanel();
	refreshSheet(force);
}
/** A season that ended since this viewer last saw one: its VP, league and reward. */
function announceSeason(st) {
	const last = st.season.last;
	if (last.no < 0) return;
	let seen = -1;
	try { seen = +(localStorage.getItem(SEASON_KEY) || -1); } catch (e) { seen = K.seasonSeen === undefined ? -1 : K.seasonSeen; }
	if (seen >= last.no || K.seasonSeen === last.no) return;
	K.seasonSeen = last.no;
	try { localStorage.setItem(SEASON_KEY, String(last.no)); } catch (e) { /* per-viewer only */ }
	if (last.al >= 0) {
		// The alliance league paid: place, the move, the reward and what the crystal held.
		const alg = K.rules.alliances.leagues[last.tier];
		const got = crystalGot(last, st);
		toast('Сезон завершён: альянс' + (last.aname ? ' «' + last.aname + '»' : '') + ' на ' + last.place + ' месте из ' + last.size + ' в лиге «' + alg.name + '»' +
			(last.move > 0 ? ', повышение!' : last.move < 0 ? ', понижение' : '') + ' · +' + fmt(last.gems) + ' самоцветов' + (last.stones ? ', +' + last.stones + ' камней' : '') +
			(got ? ' · из кристалла: ' + got : ''), 'gold');
		return;
	}
	const lg = last.league >= 0 ? K.rules.seasons.leagues[last.league] : null;
	toast('Сезон завершён: ' + fmt(last.vp) + ' ОП · ' + (lg ? lg.name + ', награда +' + fmt(last.gems) + ' самоцветов' + (last.stones ? ' и +' + last.stones + ' камней' : '') : 'до лиги не хватило'), lg ? 'gold' : '');
}
/** What last season's alliance crystal held: a titan, a relic, or gems when the relic inventory was full. */
function crystalGot(last, st) {
	const t = last.ctitan && st.titans.find((x) => x.id === last.ctitan);
	const rl = last.crelic && st.relics.find((x) => x.id === last.crelic);
	return t ? 'титан ' + titanName(t) + ' ' + '★'.repeat(last.crystal) : rl ? 'реликвия ' + relicName(rl) + ' ' + '★'.repeat(last.crystal)
		: last.cgems ? 'склад реликвий полон, +' + fmt(last.cgems) + ' самоцветов' : '';
}
/** Raids the player has not seen: a toast for one that just happened, the report after an absence. */
function announceRaids(st) {
	const r = st.raids;
	const fresh = r.log.filter((x) => x.at > r.read);
	if (!fresh.length) return;
	K.bridge.readRaids();
	r.read = st.now;
	persistSoon();
	if (fresh.length === 1 && st.now - fresh[0].at < 300) {
		const x = fresh[0];
		toast(raidToast(x), x.stars ? 'bad' : 'gold');
		const v = x.land ? world.lands.get(x.land) : world.views.get((st.b.find((b) => defOf(b.def).id === 'garrison') || st.b.find((b) => b.def === K.rules.castleIndex) || {}).id);
		if (v) floatText(v.anchor, x.stars ? 'Налёт' : 'Отбито', x.stars ? '#ff9a85' : '#f2c14e');
	} else if (K.sheet || K.place) toast('Пока вас не было: ' + raidCount(fresh) + '. Журнал — в Гарнизоне', fresh.some((x) => x.stars) ? 'bad' : 'gold');
	else openRaidLog(fresh);
}
function raidCount(list) {
	const held = list.filter((x) => !x.stars).length;
	return list.length + ' ' + plural(list.length, 'налёт', 'налёта', 'налётов') + (held ? ', отбито ' + held : '');
}
function raidWhere(x) { return x.land ? landType(x.type).name : 'Город'; }
function raidLost(x) { return x.lost.map((v, i) => (v > 0 ? resAmount(RES[i], v) : '')).filter(Boolean).join(', '); }
function raidToast(x) {
	if (!x.stars) return 'Налёт отбит: ' + raidWhere(x) + (x.vp ? ', +' + resAmount('vp', x.vp) : '');
	const lost = raidLost(x);
	return 'Налётчики разграбили: ' + raidWhere(x) + (lost ? ', −' + lost : '') + '. Щит ' + fmtDur(Math.max(0, x.shield - x.at));
}
/** Finished constructions since the last state. */
function announce(prev, st) {
	let done = false;
	for (const b of prev.b) {
		if (!b.up) continue;
		const now = st.b.find((x) => x.id === b.id);
		if (now && !now.up) {
			const d = defOf(now.def);
			toast((b.lv === 0 ? 'Построено: ' : 'Улучшено: ') + d.name + (now.lv > 1 ? ', уровень ' + now.lv : ''), 'gold');
			const v = world.views.get(now.id);
			if (v) floatText(v.anchor, 'Готово', '#f2c14e');
			done = true;
		}
	}
	for (const L of prev.lands) {
		if (!L.up) continue;
		const now = st.lands.find((x) => x.id === L.id);
		if (now && !now.up) {
			toast('Земля улучшена: ' + landType(now.type).name + ', уровень ' + now.lv, 'gold');
			const v = world.lands.get(now.id);
			if (v) floatText(v.anchor, 'Готово', '#f2c14e');
			done = true;
		}
	}
	if (done) persistSoon();
}

// ---------------------------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------------------------
function initHud() {
	for (const [id, kind] of [['rGold', 'gold'], ['rFood', 'food'], ['rGems', 'gems'], ['rStones', 'stones'], ['rVp', 'vp'], ['rShards', 'shards'], ['rBuilders', 'builders']]) {
		$(id).innerHTML = iconSvg(kind) + '<span></span><small></small>' + (kind === 'gold' || kind === 'food' ? '<i class="fill"></i>' : '');
	}
	for (const [id, icon] of [['kAttack', 'ui.attack'], ['kAllyBtn', 'ui.alliance'], ['kArmyBtn', 'ui.army'], ['kShopBtn', 'ui.build']]) $(id).insertAdjacentHTML('afterbegin', DotIcons.svg(icon, 'bi'));
	$('rStones').addEventListener('click', openStones);
	$('rVp').addEventListener('click', openSeason);
	$('rBuilders').addEventListener('click', openBuilders);
	$('kMenuBtn').addEventListener('click', openMenu);
	$('kAttack').addEventListener('click', openWorld);
	$('kShopBtn').addEventListener('click', openShop);
	$('kArmyBtn').addEventListener('click', openArmy);
	$('kAllyBtn').addEventListener('click', () => openAlliance());
	// The alliance founding form keeps what was typed when the sheet is redrawn.
	$('kSheetPanel').addEventListener('input', (e) => {
		if (e.target.id !== 'allyName' && e.target.id !== 'allyTag') return;
		K.allyForm = K.allyForm || { name: '', tag: '' };
		K.allyForm[e.target.id === 'allyName' ? 'name' : 'tag'] = e.target.value;
	});
	$('kCastle').addEventListener('click', () => {
		const c = K.st.b.find((b) => b.def === K.rules.castleIndex);
		if (c) { const v = world.views.get(c.id); if (v) focusOn(v.anchor.x, v.anchor.z); select({ kind: 'b', id: c.id }); }
	});
	$('kPlaceCancel').addEventListener('click', cancelPlace);
	$('kPlaceOk').addEventListener('click', confirmPlace);
	$('kPanel').addEventListener('click', onAct);
	$('kSheetPanel').addEventListener('click', onAct);
	$('kSheet').addEventListener('click', (e) => { if (e.target.id === 'kSheet') closeSheet(); });
	window.addEventListener('keydown', (e) => {
		if (K.app.mode !== 'kingdom') return;
		if (e.key === 'Escape') {
			if (K.sheet) closeSheet();
			else if (K.place) cancelPlace();
			else select(null);
		}
	});
}
function setRes(id, have, cap) {
	const el = $(id);
	el.querySelector('span').textContent = fmtShort(have);
	el.querySelector('small').textContent = '/ ' + fmtShort(cap);
	el.querySelector('.fill').style.width = clamp(have / Math.max(1, cap), 0, 1) * 100 + '%';
	el.classList.toggle('full', have >= cap);
	el.title = (id === 'rGold' ? 'Золото' : 'Еда') + ': ' + fmt(have) + ' из ' + fmt(cap);
}
function updateHud() {
	const st = K.st;
	setRes('rGold', st.res[0], st.cap[0]);
	setRes('rFood', st.res[1], st.cap[1]);
	$('rGems').querySelector('span').textContent = fmt(st.res[2]);
	const stones = $('rStones');
	stones.querySelector('span').textContent = st.res[3] + '/' + st.cap[3];
	stones.querySelector('small').textContent = st.stonesNext ? fmtClock(st.stonesNext - nowF()) : '';
	const vp = $('rVp');
	vp.querySelector('span').textContent = fmtShort(st.res[5]);
	// In an alliance the league is the alliance's (the original's leagues are alliance leagues).
	const lgName = st.ally.my >= 0 ? K.rules.alliances.leagues[st.ally.tier].name : st.season.league >= 0 ? K.rules.seasons.leagues[st.season.league].name : '';
	vp.querySelector('small').textContent = lgName.split(' ')[0];
	vp.title = 'Очки победы за сезон: ' + fmt(st.res[5]) + '. Сезон кончится через ' + fmtDur(Math.max(0, st.season.end - nowF()));
	const ally = st.ally;
	$('kAllySub').textContent = ally.my >= 0 ? '[' + ally.tag + '] ' + ally.place + ' из ' + ally.size : ally.rating >= K.rules.alliances.joinVp ? 'Вступить' : fmtShort(ally.rating) + ' / ' + fmtShort(K.rules.alliances.joinVp) + ' ОП';
	const shards = $('rShards');
	shards.hidden = st.res[4] <= 0 && !st.lands.some((L) => landType(L.type).resource === 'shards');
	shards.querySelector('span').textContent = fmt(st.res[4]);
	shards.title = 'Осколки: ' + fmt(st.res[4]) + '. Из руин; на них титаны возвышаются';
	$('rBuilders').querySelector('span').textContent = st.builders - st.busy + '/' + st.builders;
	$('rBuilders').title = 'Свободные строители: ' + (st.builders - st.busy) + ' из ' + st.builders;
	$('kCastle').textContent = 'Замок ' + st.castle;
	$('kAttackSub').textContent = st.res[3] + ' ' + plural(st.res[3], 'камень', 'камня', 'камней');
	$('kArmySub').textContent = st.army.total + ' / ' + st.army.cap + ' · ' + st.titans.length + ' ' + plural(st.titans.length, 'титан', 'титана', 'титанов');
}

// ---------------------------------------------------------------------------------------------
// Selection panel
// ---------------------------------------------------------------------------------------------
const STATS = {
	produce: 'Добыча в час', store: 'Копит до', capacity: 'Вместимость', protect: 'Защищено от кражи', queue: 'Очередь', troops: 'Войск в лагере',
	slots: 'Отрядов в бою', defenseSlots: 'Отрядов в защите города', guardTier: 'Тир стражников', defenseBonusPct: 'Прибавка к защите, %', maxTroopLevel: 'Предел уровня войск', survival: 'Порог выживания, %', reinforcements: 'Подкреплений',
	spellSlots: 'Улучшений одновременно', maxSpellLevel: 'Предел уровня заклинаний', stones: 'Камней портала', builders: 'Строителей', titans: 'Мест для титанов',
	landCapacity: 'Земель можно захватить',
};
function statLabel(d, k) {
	if (k === 'capacity' && d.id === 'spell_forge') return 'Запас заклинаний';
	if (k === 'queue') return d.id === 'spell_forge' ? 'Очередь создания' : 'Очередь обучения';
	return STATS[k];
}
function statKeys(d, a, b) {
	return Object.keys(STATS).filter((k) => (a && a[k] !== undefined) || (b && b[k] !== undefined));
}
function select(s) {
	K.sel = s;
	K.armed = null;
	updateSelRing();
	updatePanel();
}
let panelKey = '';
function updatePanel() {
	const el = $('kPanel');
	const s = K.sel;
	const wide = K.app.viewW >= 900;
	let r = null;
	try { r = s && !K.place ? (s.kind === 'b' ? buildingPanel(s.id) : landPanel(s.id)) : null; } catch (e) { report('панель', e); }
	if (r && K.sheet) { el.hidden = true; return; }
	if (!r) {
		if (s && !K.place) K.sel = null;
		el.hidden = true;
		panelKey = '';
		$('kBottom').hidden = !!K.place;
		return;
	}
	if (r.key !== panelKey) { el.innerHTML = r.html; panelKey = r.key; }
	if (r.live) r.live(el);
	el.hidden = false;
	$('kBottom').hidden = !wide;
}
function isArmed(act) { return !!K.armed && K.armed.act === act && performance.now() < K.armed.until; }
function arm(act) {
	K.armed = { act, until: performance.now() + 3500 };
	setTimeout(() => { if (K.armed && K.armed.act === act && performance.now() >= K.armed.until) { K.armed = null; updatePanel(); refreshSheet(); } }, 3600);
	updatePanel();
	refreshSheet();
}
function live(el, key, text) { const n = el.querySelector('[data-live="' + key + '"]'); if (n) n.textContent = text; }
function liveBar(el, key, frac) { const n = el.querySelector('[data-live="' + key + '"]'); if (n) n.style.width = clamp(frac, 0, 1) * 100 + '%'; }
const gemSpan = (n) => '<span class="cost">' + iconSvg('gems') + fmt(n) + '</span>';

/** Buildings with their own screen: data-act and button label. */
const BUILDING_ACT = { barracks: ['barracks', 'Обучить'], army_camp: ['army', 'Армия'], armoury: ['armoury', 'Войска'], hall_of_titans: ['titans', 'Титаны'],
	relic_temple: ['relics', 'Реликвии'], medical_school: ['army', 'Армия'], spell_forge: ['forge', 'Заклинания'], arcane_tower: ['arcane', 'Улучшения'],
	alliance_camp: ['alliance-camp', 'Подкрепления'], garrison: ['defense', 'Защита'] };
function buildingPanel(id) {
	const st = K.st;
	const b = st.b.find((x) => x.id === id);
	if (!b) return null;
	const d = defOf(b.def);
	const cur = levelDef(d, b.lv);
	const nx = d.levels[b.lv];
	const armedSkip = isArmed('skip:' + id);
	const brl = buildingRelic(id);
	const gf = d.id === 'garrison' && cur ? K.bridge.forecast(0) : null;
	const shielded = st.raids.shield > nowF();
	const key = ['b', id, b.lv, b.up, b.st > 0, armedSkip, brl ? brl.id + ':' + (brl.dur > 0) + ':' + brl.v.join('.') : '', gf ? [gf.on, gf.hold, gf.titan, gf.sq, st.raids.slots, shielded].join() : '',
		d.role === 'storage' || d.role === 'castle' || d.role === 'portal' || d.role === 'builders' ? st.res.join() + st.busy + st.builders : '',
		BUILDING_ACT[d.id] ? [st.army.queue.length, st.army.total, st.army.cap, st.army.rs.t, st.titans.length, st.army.surv, st.spells.total, st.spells.queue.length, st.spells.labs.length,
			st.ally.my, st.ally.reinf.length].join() : ''].join('|');
	const stats = [];
	if (!cur) stats.push(esc(d.description));
	else if (d.role === 'producer') {
		const rl = buildingRelic(id);
		const prod = Math.floor((cur.produce * (1000 + relicBonus(rl, 'producePct'))) / 1000);
		const keep = Math.floor((cur.store * (1000 + relicBonus(rl, 'storePct'))) / 1000);
		stats.push('Добыча <b>' + fmt(prod) + '/ч</b>' + (prod > cur.produce ? ' (реликвия +' + fmt(prod - cur.produce) + ')' : ''), 'Копит до <b>' + fmt(keep) + '</b>');
		if (!b.up) stats.push('Накоплено <b data-live="st"></b>');
		else stats.push('Во время улучшения добыча стоит');
	} else if (d.role === 'storage') {
		const ri = RES.indexOf(d.resource);
		stats.push('Вместимость <b>' + fmt(cur.capacity) + '</b>', 'Всего ' + RES_OF[d.resource] + ' <b>' + fmt(st.res[ri]) + ' / ' + fmt(st.cap[ri]) + '</b>');
	} else if (d.role === 'reserve') stats.push('Защищено от кражи <b>' + fmt(cur.protect) + '</b>');
	else if (d.role === 'castle') stats.push('Земель можно захватить <b>' + st.landCap + '</b>', 'Строителей <b>' + st.builders + '</b>', 'Расширений <b>' + st.expansions + ' из ' + K.rules.expansions.length + '</b>');
	else if (d.role === 'portal') stats.push('Камни <b>' + st.res[3] + ' / ' + cur.stones + '</b>', 'новый каждые 7 мин');
	else if (d.role === 'builders') stats.push('Строителей <b>' + cur.builders + '</b>', 'заняты <b>' + st.busy + '</b>');
	else if (d.id === 'barracks') stats.push('Очередь <b>' + st.army.queue.length + ' / ' + st.army.qcap + '</b>', 'В лагере <b>' + st.army.total + ' / ' + st.army.cap + '</b>');
	else if (d.id === 'army_camp') stats.push('Отрядов <b>' + st.army.total + ' / ' + st.army.cap + '</b>', 'В бой <b>до ' + st.army.slots + '</b>');
	else if (d.id === 'armoury') stats.push('Предел уровня войск <b>' + st.army.maxLv + '</b>', st.army.rs.t >= 0 ? 'улучшает <b>' + esc(K.army.troops[st.army.rs.t].name) + '</b>' : 'свободен');
	else if (d.id === 'medical_school') stats.push('Возвращаются отряды, сохранившие <b>≥ ' + st.army.surv + '%</b>');
	else if (d.id === 'hall_of_titans') stats.push('Титанов <b>' + st.titans.length + ' / ' + st.tcap + '</b>');
	else if (d.id === 'relic_temple') stats.push('Бесплатная реликвия: <b data-live="relic"></b>');
	else if (d.id === 'spell_forge') stats.push('Запас <b>' + st.spells.total + ' / ' + st.spells.cap + '</b>', 'Очередь <b>' + st.spells.queue.length + ' / ' + st.spells.qcap + '</b>');
	else if (d.id === 'arcane_tower') stats.push('Улучшений сразу <b>' + st.spells.slots + '</b>', 'Предел уровня <b>' + st.spells.maxLv + '</b>');
	else if (d.id === 'alliance_camp') stats.push('Подкреплений <b>' + st.ally.reinf.length + ' / ' + cur.reinforcements + '</b>', st.ally.my >= 0 ? 'альянс <b>[' + esc(st.ally.tag) + ']</b>' : 'вы не в альянсе');
	else if (d.category === 'councils') stats.push('Присылает <b>' + councilTroops(d.id).map((t) => esc(K.army.troops[t].name)).join(', ') + '</b>', 'Предел уровня их войск <b>' + cur.maxTroopLevel + '</b>');
	else if (gf) {
		const t = gf.titan ? st.titans.find((x) => x.id === gf.titan) : null;
		stats.push(gf.on ? 'Удержит налёт <b>' + gf.hold + '%</b>' : 'Налёты с Замка <b>' + st.raids.castle + '</b>', 'Ваших отрядов <b>' + gf.sq + ' / ' + st.raids.slots + '</b>',
			'Титан <b>' + (t ? esc(titanName(t)) : 'нет') + '</b>');
		if (shielded) stats.push('Щит <b data-live="shield"></b>');
	} else for (const k of statKeys(d, cur).slice(0, 3)) stats.push(statLabel(d, k) + ' <b>' + fmt(cur[k]) + '</b>');
	const btn = [];
	if (b.up) {
		btn.push('<button class="btn tall primary" data-act="skip" data-id="' + id + '">' + (armedSkip ? 'Подтвердить' : 'Ускорить') + '<span class="sub" data-live="skip"></span></button>');
	} else {
		if (d.role === 'producer') btn.push('<button class="btn tall" data-act="collect" data-id="' + id + '"' + (b.st > 0 ? '' : ' disabled') + '>Собрать<span class="sub" data-live="stc"></span></button>');
		if (nx) btn.push('<button class="btn tall primary" data-act="upgrade" data-id="' + id + '">' + (d.role === 'builders' ? 'Нанять строителя' : 'Улучшить') + '<span class="sub">до уровня ' + (b.lv + 1) + '</span></button>');
		else btn.push('<button class="btn tall" disabled>Макс. уровень<span class="sub">' + b.lv + ' из ' + d.levels.length + '</span></button>');
	}
	if (b.lv > 0 && relicKindsFor(d.id).length) {
		const rl = buildingRelic(id);
		btn.push('<button class="btn tall" data-act="building-relic" data-id="' + id + '">Реликвия<span class="sub">' + (rl ? esc(relicName(rl)) + (rl.dur > 0 ? '' : ', время вышло') : 'нет') + '</span></button>');
	}
	const extra = b.lv > 0 && (BUILDING_ACT[d.id] || (d.category === 'councils' && ['council', 'Посланники']));
	if (extra) btn.unshift('<button class="btn tall primary" data-act="' + extra[0] + '"' + (extra[0] === 'council' ? ' data-id="' + b.def + '"' : '') + '>' + extra[1] + '<span class="sub" data-live="extra"></span></button>');
	btn.push('<button class="btn tall" data-act="move" data-id="' + id + '">Переместить<span class="sub">или зажмите</span></button>');
	btn.push('<button class="btn tall" data-act="info" data-id="' + id + '">Подробнее</button>');
	const html = '<div class="head">' + buildingIcon(b.def) + '<h3>' + esc(d.name) + '</h3><span class="lvl">' + (b.lv ? 'уровень ' + b.lv : 'строится') + '</span>' +
		'<button class="btn x" data-act="close" aria-label="Закрыть">✕</button></div>' +
		'<div class="stats">' + stats.map((x) => '<span>' + x + '</span>').join('') + '</div>' +
		(b.up ? '<div class="uprow"><span>' + (b.lv ? 'Улучшение до уровня ' + (b.lv + 1) : 'Строительство') + '</span><b data-live="left"></b></div><div class="progress"><i data-live="bar"></i></div>' : '') +
		'<div class="row">' + btn.join('') + '</div>';
	return {
		key, html,
		live(el) {
			const bb = K.st.b.find((x) => x.id === id);
			if (!bb) return;
			live(el, 'st', fmt(bb.st) + ' / ' + fmt(bb.sc));
			live(el, 'stc', bb.st > 0 ? '+' + fmtShort(bb.st) : 'пусто');
			const now = nowF();
			const aa = K.st.army;
			const dd = defOf(bb.def);
			if (dd.id === 'relic_temple') {
				const t = K.st.relicAt > now ? 'через ' + fmtDur(K.st.relicAt - now) : 'готова';
				live(el, 'relic', t);
				live(el, 'extra', t);
			} else if (dd.id === 'barracks') live(el, 'extra', aa.queue.length ? (aa.qb ? 'лагерь полон' : 'следующий ' + fmtClock(aa.qf - now)) : 'очередь пуста');
			else if (dd.id === 'armoury') live(el, 'extra', aa.rs.t >= 0 ? fmtDur(aa.rs.f - now) : 'улучшить');
			else if (dd.id === 'hall_of_titans') live(el, 'extra', K.st.titans.length + ' титанов');
			else if (dd.id === 'army_camp' || dd.id === 'medical_school') live(el, 'extra', aa.total + ' / ' + aa.cap);
			else if (dd.id === 'spell_forge') live(el, 'extra', K.st.spells.queue.length ? 'следующее ' + fmtClock(K.st.spells.qf - now) : 'запас ' + K.st.spells.total);
			else if (dd.id === 'arcane_tower') live(el, 'extra', K.st.spells.labs.length ? fmtDur(K.st.spells.labs[0].until - now) : 'улучшить');
			else if (dd.id === 'alliance_camp') {
				const al = K.st.ally;
				live(el, 'extra', al.my < 0 ? 'нет альянса' : al.reinf.length ? al.reinf.filter((r) => r[2] <= now).length + ' готовы' : al.rck.err === E.None ? 'попросить' : 'пусто');
			}
			else if (gf) {
				live(el, 'extra', gf.on ? 'удержит ' + gf.hold + '%' : 'с Замка ' + K.st.raids.castle);
				live(el, 'shield', fmtDur(K.st.raids.shield - now));
			}
			if (bb.up) {
				const left = bb.f - nowF();
				live(el, 'left', fmtDur(left));
				liveBar(el, 'bar', 1 - left / Math.max(1, bb.f - bb.s));
				const n = el.querySelector('[data-live="skip"]');
				if (n) n.innerHTML = gemSpan(bb.skip);
			}
		},
	};
}
function landPanel(id) {
	const L = K.st.lands.find((x) => x.id === id);
	if (!L) return null;
	const ty = landType(L.type);
	const armedA = isArmed('abandon:' + id);
	const armedSkip = isArmed('lskip:' + id);
	const guard = L.titan ? K.st.titans.find((t) => t.id === L.titan) : null;
	const lf = K.bridge.forecast(id);
	const shielded = L.shield > nowF();
	let up;
	if (L.up) up = '<button class="btn tall primary" data-act="land-skip" data-id="' + id + '">' + (armedSkip ? 'Подтвердить' : 'Ускорить') + '<span class="sub" data-live="skip"></span></button>';
	else if (L.nx.err === E.MaxLevel) up = L.lv < ty.produce.length ? '<button class="btn tall" disabled>Не улучшается<span class="sub">добыча не растёт</span></button>' : '<button class="btn tall" disabled>Макс. уровень<span class="sub">' + L.lv + '</span></button>';
	else up = '<button class="btn tall" data-act="land-upgrade" data-id="' + id + '">Улучшить<span class="sub">' + (L.nx.err === E.CastleTooLow && L.nx.c ? 'Замок ' + L.nx.c : 'до уровня ' + (L.lv + 1)) + '</span></button>';
	const html = '<div class="head">' + landIcon(L.type, L.lv) + '<h3>' + esc(ty.name) + '</h3><span class="lvl">уровень ' + L.lv + '</span><button class="btn x" data-act="close" aria-label="Закрыть">✕</button></div>' +
		'<div class="stats"><span>Добыча <b>' + landRateText(L.type, L.rate) + '</b></span><span>Копит до <b>' + resAmount(ty.resource, L.cap) + '</b></span><span>Накоплено <b data-live="st"></b></span><span>полна за ' + L.fill + ' ч</span>' +
			(lf.on ? '<span>Удержит налёт <b>' + lf.hold + '%</b></span>' : '') + (shielded ? '<span>Щит <b data-live="shield"></b></span>' : '') + '</div>' +
		(ty.resource === 'vp' ? '<p class="fine">Земля очков победы: очки идут в счёт сезона (кнопка ОП вверху).</p>' : ty.resource === 'shards' ? '<p class="fine">Руины дают осколки: на них возвышают титанов в Зале титанов.</p>' : '') +
		(L.up ? '<div class="uprow"><span>Улучшение до уровня ' + (L.lv + 1) + ', добыча идёт</span><b data-live="left"></b></div><div class="progress"><i data-live="bar"></i></div>' : '') +
		'<div class="row"><button class="btn tall primary" data-act="land-collect" data-id="' + id + '">Собрать<span class="sub" data-live="stc"></span></button>' + up +
		'<button class="btn tall" data-act="land-guard" data-id="' + id + '">' + (guard ? 'Страж: ' + esc(titanName(guard)) : 'Поставить титана') + '<span class="sub">' + (guard ? '+' + guard.lv + '% добычи, защита' : 'добыча и защита') + '</span></button>' +
		'<button class="btn tall' + (armedA ? ' danger' : '') + '" data-act="land-abandon" data-id="' + id + '"' + (L.up ? ' disabled' : '') + '>' + (armedA ? 'Точно покинуть?' : 'Покинуть землю') + '<span class="sub">' + (L.up ? 'идёт улучшение' : 'место освободится') + '</span></button></div>';
	return {
		key: ['land', id, L.lv, armedA, armedSkip, L.titan, guard ? guard.lv : 0, L.rate, L.up, L.nx.err, L.nx.c, lf.on, lf.hold, shielded].join('|'), html,
		live(el) {
			const LL = K.st.lands.find((x) => x.id === id);
			if (!LL) return;
			live(el, 'st', fmt(LL.st));
			live(el, 'stc', LL.st > 0 ? '+' + resAmount(landType(LL.type).resource, LL.st) : 'пусто');
			live(el, 'shield', fmtDur(LL.shield - nowF()));
			if (LL.up) {
				const left = LL.f - nowF();
				live(el, 'left', fmtDur(left));
				liveBar(el, 'bar', 1 - left / Math.max(1, LL.f - LL.s));
				const n = el.querySelector('[data-live="skip"]');
				if (n) n.innerHTML = gemSpan(LL.skip);
			}
		},
	};
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
function onAct(e) {
	const el = e.target.closest('[data-act]');
	if (!el || el.disabled) return;
	const a = el.dataset.act;
	const id = +el.dataset.id || 0;
	switch (a) {
	case 'close': select(null); break;
	case 'close-sheet': closeSheet(); break;
	case 'skip': gemAction('skip:' + id, (K.st.b.find((b) => b.id === id) || {}).skip || 0, () => speedup(id)); break;
	case 'collect': collect(id, false); break;
	case 'upgrade': openUpgrade(id); break;
	case 'do-upgrade': doUpgrade(id, false); break;
	case 'do-upgrade-gems': gemAction('up:' + id, +el.dataset.gems, () => doUpgrade(id, true)); break;
	case 'move': startMove(id); break;
	case 'info': openInfo(id); break;
	case 'land-collect': collectLand(id); break;
	case 'land-abandon':
		if (!isArmed('abandon:' + id)) arm('abandon:' + id);
		else { K.bridge.abandonLand(id); K.armed = null; select(null); toast('Земля покинута', ''); persist(); refresh(true); }
		break;
	case 'shop': startPlace(id); break;
	case 'tab': K.shopTab = el.dataset.tab; refreshSheet(true); break;
	case 'raid': startAttack(id, false); break;
	case 'capture': startAttack(id, true); break;
	case 'world': openWorld(); break;
	case 'army': openArmy(); break;
	case 'barracks': openBarracks(); break;
	case 'armoury': openArmoury(); break;
	case 'council': openCouncil(id); break;
	case 'titans': openTitans(); break;
	case 'defense': openDefense(); break;
	case 'raid-log': openRaidLog(null); break;
	case 'def-titan': {
		const code = K.bridge.setGarrison(K.st.raids.squads, id);
		if (code !== E.None) toast(errText(code, null, 'defense'), 'bad');
		else toast(id ? 'Титан на страже города' : 'Титан снят со стражи', 'gold');
		persistSoon();
		refresh(true);
		break;
	}
	case 'def-sq': {
		// Squads the army no longer has drop out of the list first.
		const a = K.st.army;
		const counts = a.n.map(() => 0);
		for (const t of K.st.raids.squads) if (counts[t] < a.n[t]) counts[t]++;
		const used = counts.reduce((x, y) => x + y, 0);
		const d = +el.dataset.d;
		if (d > 0 && counts[id] < a.n[id] && used < K.st.raids.slots) counts[id]++;
		if (d < 0 && counts[id] > 0) counts[id]--;
		const list = [];
		counts.forEach((n, t) => { for (let i = 0; i < n; i++) list.push(t); });
		const code = K.bridge.setGarrison(list, K.st.raids.titan);
		if (code !== E.None) toast(errText(code, null, 'defense'), 'bad');
		persistSoon();
		refresh(true);
		break;
	}
	case 'relics': openRelics(); break;
	case 'train': {
		const code = K.bridge.train(id);
		if (code !== E.None) toast(errText(code, K.st.army.train[id]), 'bad');
		persistSoon();
		refresh(true);
		break;
	}
	case 'cancel-train': K.bridge.cancelTrain(id); toast('Обучение отменено, еда возвращена', ''); persistSoon(); refresh(true); break;
	case 'skip-train': gemAction('tskip', +el.dataset.gems, () => run(K.bridge.skipTrain(), 'Отряды готовы')); break;
	case 'upgrade-troop': run(K.bridge.upgradeTroop(id, false), 'Арсенал улучшает: ' + K.army.troops[id].name, K.st.army.upg[id]); break;
	case 'upgrade-troop-gems': gemAction('ut:' + id, +el.dataset.gems, () => run(K.bridge.upgradeTroop(id, true), 'Арсенал улучшает: ' + K.army.troops[id].name)); break;
	case 'skip-research': gemAction('rskip', +el.dataset.gems, () => run(K.bridge.skipResearch(), 'Улучшение войска завершено')); break;
	case 'skip-titan': gemAction('tr:' + id, +el.dataset.gems, () => run(K.bridge.skipTitan(id), 'Титан готов к бою')); break;
	case 'ascend': run(K.bridge.ascend(id, false), 'Титан возвышен: ещё 10 уровней и слот реликвии', (K.st.titans.find((t) => t.id === id) || {}).ack); break;
	case 'ascend-gems': gemAction('asc:' + id, +el.dataset.gems, () => run(K.bridge.ascend(id, true), 'Титан возвышен: ещё 10 уровней и слот реликвии')); break;
	case 'season': openSeason(); break;
	case 'alliance': openAlliance(); break;
	case 'alliance-camp': openAlliance('reinf'); break;
	case 'ally-tab': K.allyTab = el.dataset.tab; K.armed = null; refreshSheet(true); break;
	case 'ally-view': K.allyView = el.dataset.tab; K.armed = null; refreshSheet(true); break;
	case 'ally-join': {
		const a = K.allyData && K.allyData.all[id];
		if (run(K.bridge.join(id), a ? 'Вы вступили в альянс «' + a.name + '»' : 'Вы в альянсе')) K.allyView = 'league';
		break;
	}
	case 'ally-create': createAlliance(); break;
	case 'ally-leave':
		if (!isArmed('ally-leave')) { arm('ally-leave'); break; }
		K.armed = null;
		run(K.bridge.leave(), K.st.ally.own ? 'Альянс распущен' : 'Вы вышли из альянса');
		break;
	case 'ally-reinforce': run(K.bridge.reinforce(), 'Просьба отправлена: союзники пришлют отряды', K.st.ally.rck); break;
	case 'prep-reinf': if (K.prep) { K.prep.reinf = !K.prep.reinf; refreshSheet(true); } break;
	case 'altar': {
		const go = () => {
			const r = K.bridge.openAltar(id);
			if (r.err !== E.None) { toast(errText(r.err), 'bad'); refresh(true); return; }
			K.lastSummon = r.titan;
			K.lastRelic = r.relic;
			persist();
			refresh(true);
			const t = K.st.titans.find((x) => x.id === r.titan);
			const rl = K.st.relics.find((x) => x.id === r.relic);
			if (t) toast('Призван титан: ' + titanName(t) + ' ' + '★'.repeat(K.army.titans[t.def].stars), 'gold');
			else if (rl) toast('Получена реликвия: ' + relicName(rl) + ' ' + '★'.repeat(rl.r), 'gold');
		};
		if (+el.dataset.gems > 0) gemAction('altar:' + id, +el.dataset.gems, go);
		else go();
		break;
	}
	case 'fuse-start': K.fuse = { target: id, picks: [] }; refreshSheet(true); break;
	case 'fuse-pick': {
		const f = K.fuse;
		if (!f) break;
		const i = f.picks.indexOf(id);
		if (i >= 0) f.picks.splice(i, 1);
		else f.picks.push(id);
		K.armed = null;
		refreshSheet(true);
		break;
	}
	case 'fuse-cancel': K.fuse = null; refreshSheet(true); break;
	case 'fuse-go':
		if (!K.fuse || !K.fuse.picks.length) break;
		if (!isArmed('fuse')) { arm('fuse'); break; }
		K.armed = null;
		if (run(K.bridge.fuse(K.fuse.target, K.fuse.picks), 'Слияние завершено')) K.fuse = null;
		refreshSheet(true);
		break;
	case 'prep-titan': if (K.prep) { K.prep.titan = id; refreshSheet(true); } break;
	case 'prep-sq': {
		const p = K.prep;
		if (!p) break;
		const a = K.st.army;
		const used = p.counts.reduce((x, y) => x + y, 0);
		const d = +el.dataset.d;
		if (d > 0 && p.counts[id] < a.n[id] && used < a.slots) p.counts[id]++;
		if (d < 0 && p.counts[id] > 0) p.counts[id]--;
		refreshSheet(true);
		break;
	}
	case 'prep-go': launchAttack(); break;
	case 'forge': openForge(); break;
	case 'arcane': openArcane(); break;
	case 'craft': {
		const code = K.bridge.craft(id);
		if (code !== E.None) toast(errText(code, K.st.spells.craft[id], 'forge'), 'bad');
		persistSoon();
		refresh(true);
		break;
	}
	case 'cancel-craft': K.bridge.cancelCraft(id); toast('Создание отменено, золото возвращено', ''); persistSoon(); refresh(true); break;
	case 'skip-craft': gemAction('cskip', +el.dataset.gems, () => run(K.bridge.skipCraft(), 'Заклинания готовы')); break;
	case 'upgrade-spell': run(K.bridge.upgradeSpell(id, false), 'Башня улучшает: ' + K.army.spells[id].name, K.st.spells.upg[id], 'arcane'); break;
	case 'upgrade-spell-gems': gemAction('us:' + id, +el.dataset.gems, () => run(K.bridge.upgradeSpell(id, true), 'Башня улучшает: ' + K.army.spells[id].name, null, 'arcane')); break;
	case 'skip-spell': gemAction('ls:' + id, +el.dataset.gems, () => run(K.bridge.skipSpell(id), 'Улучшение заклинания завершено')); break;
	case 'titan-relics': openTitanRelics(id); break;
	case 'titan-play': DotTitans.play(el.closest('.titanStage').querySelector('svg'), el.dataset.clip); break;
	case 'relic-inv': K.repair = null; openRelicInventory(); break;
	case 'equip': run(K.bridge.equip(+el.dataset.titan, id), 'Реликвия надета'); break;
	case 'attach': run(K.bridge.attach(+el.dataset.building, id), 'Реликвия на постройке'); break;
	case 'building-relic': openBuildingRelic(id); break;
	case 'relic-tab': K.relicTab = el.dataset.tab; refreshSheet(true); break;
	case 'discard-r1': {
		if (!isArmed('discard-r1')) { arm('discard-r1'); break; }
		K.armed = null;
		const ids = K.st.relics.filter((r) => r.r === 1 && !r.on && !r.b && relicTabHas(r)).map((r) => r.id);
		ids.forEach((rid) => K.bridge.discard(rid));
		toast('Выброшено реликвий: ' + ids.length, '');
		persist();
		refresh(true);
		break;
	}
	case 'unequip': run(K.bridge.unequip(id), 'Реликвия снята'); break;
	case 'discard':
		if (!isArmed('discard:' + id)) arm('discard:' + id);
		else { K.armed = null; run(K.bridge.discard(id), 'Реликвия выброшена'); }
		break;
	case 'repair-start': K.repair = { target: id, picks: [] }; refreshSheet(true); break;
	case 'repair-pick': {
		const rp = K.repair;
		if (!rp) break;
		const i = rp.picks.indexOf(id);
		if (i >= 0) rp.picks.splice(i, 1);
		else rp.picks.push(id);
		K.armed = null;
		refreshSheet(true);
		break;
	}
	case 'repair-cancel': K.repair = null; refreshSheet(true); break;
	case 'repair-go': {
		const rp = K.repair;
		if (!rp || !rp.picks.length) break;
		if (!isArmed('repair')) { arm('repair'); break; }
		K.armed = null;
		const r = K.bridge.repair(rp.target, rp.picks);
		const fixed = K.bridge.state().relics.find((x) => x.id === rp.target);
		if (r.err === E.Nothing) toast('Реликвия цела и уже в лучшем виде', '');
		else if (r.err !== E.None) toast(errText(r.err), 'bad');
		else if (!r.ok) toast('Починка не удалась, жертвы потеряны', 'bad');
		else if (r.slot >= 0 && fixed) toast((r.unlocked ? 'Открыто новое свойство: ' : 'Свойство усилено: ') + effectDef(fixed, r.slot).name, 'gold');
		else toast('Реликвия починена', 'gold');
		K.repair = null;
		persist();
		refresh(true);
		break;
	}
	case 'land-guard': openLandGuard(id); break;
	case 'assign': run(K.bridge.assign(+el.dataset.land, id), 'Титан охраняет землю'); break;
	case 'recall': run(K.bridge.recall(id), 'Страж отозван'); break;
	case 'land-upgrade': openLandUpgrade(id); break;
	case 'do-land-upgrade': doLandUpgrade(id, false); break;
	case 'do-land-upgrade-gems': gemAction('lup:' + id, +el.dataset.gems, () => doLandUpgrade(id, true)); break;
	case 'land-skip': {
		const L = K.st.lands.find((x) => x.id === id);
		if (L) gemAction('lskip:' + id, L.skip, () => run(K.bridge.speedupLand(id), 'Земля улучшена: ' + landType(L.type).name + ', уровень ' + (L.lv + 1), null, 'land'));
		break;
	}
	case 'goto-land': {
		closeSheet();
		const v = world.lands.get(id);
		if (v) focusOn(v.anchor.x, v.anchor.z);
		select({ kind: 'land', id });
		break;
	}
	case 'prep-spell': {
		const p = K.prep;
		if (!p) break;
		const i = p.spells.indexOf(id);
		if (i >= 0) p.spells.splice(i, 1);
		else if (p.spells.length < K.st.spells.kinds) p.spells.push(id);
		else toast('В бой берётся до ' + K.st.spells.kinds + ' заклинаний', '');
		refreshSheet(true);
		break;
	}
	case 'refill': gemAction('refill', +el.dataset.gems, () => run(K.bridge.refillStones(), 'Камни портала пополнены')); break;
	case 'slot': gemAction('slot', +el.dataset.gems, () => run(K.bridge.buySlot(), 'Новое место для земли открыто')); break;
	case 'expand': run(K.bridge.buyExpansion(false), 'Город расширен: новые участки открыты') && closeSheet(); break;
	case 'expand-gems': gemAction('expand', +el.dataset.gems, () => run(K.bridge.buyExpansion(true), 'Город расширен: новые участки открыты') && closeSheet()); break;
	case 'goto': { closeSheet(); const v = world.views.get(id); if (v) focusOn(v.anchor.x, v.anchor.z); select({ kind: 'b', id }); break; }
	case 'time': clock.scale = +el.dataset.scale || 1; refreshSheet(true); toast(clock.scale > 1 ? 'Время идёт в ' + clock.scale + ' раз быстрее' : 'Обычное время', ''); break;
	case 'training': closeSheet(); select(null); K.app.openTrainingMenu(); break;
	case 'reset':
		if (!isArmed('reset')) arm('reset');
		else resetKingdom();
		break;
	case 'swap-kingdom':
		if (!isArmed('swap')) arm('swap');
		else swapKingdom();
		break;
	case 'welcome-ok': closeSheet(); try { localStorage.setItem(HINT_KEY, '1'); } catch (err) { /* ignore */ } break;
	case 'help': openWelcome(); break;
	case 'builders': openBuilders(); break;
	default: break;
	}
}
/** Gem spends take a second tap (as the original asks to confirm gem purchases). */
function gemAction(act, gems, fn) {
	if (gems > 0 && !isArmed(act)) { arm(act); return; }
	K.armed = null;
	fn();
}
function run(code, okText, ck, ctx) {
	if (code === E.None) {
		if (okText) toast(okText, 'gold');
		persist();
		refresh(true);
		return true;
	}
	toast(errText(code, ck, ctx), 'bad');
	refresh(true);
	return false;
}
function speedup(id) {
	const b = K.st.b.find((x) => x.id === id);
	if (!b) return;
	const d = defOf(b.def);
	run(K.bridge.speedup(id), (b.lv === 0 ? 'Построено: ' : 'Улучшено: ') + d.name);
}
function collect(id, viaTap) {
	const v = world.views.get(id);
	if (!v) return;
	const before = v.b.st;
	const r = K.bridge.collect(id);
	if (r.err === E.None) {
		floatText(v.anchor, '+' + fmt(r.amount), v.d.resource === 'food' ? '#c8ef8a' : '#ffd970', v.d.resource);
		if (r.amount < before) toast('Хранилище заполнено, часть осталась: ' + storageHint(v.d.resource), 'bad');
		persistSoon();
		refresh();
	} else if (r.err === E.StorageFull) toast('Хранилище заполнено: ' + storageHint(v.d.resource), 'bad');
	else if (!viaTap) toast(errText(r.err), '');
}
function collectLand(id) {
	const v = world.lands.get(id);
	if (!v) return;
	const before = v.L.st;
	const r = K.bridge.collectLand(id);
	const res = landType(v.L.type).resource;
	if (r.err === E.None) {
		floatText(v.anchor, '+' + fmt(r.amount), RES_COLOR[res] || '#ffd970', res);
		if (r.amount < before) toast('Хранилище заполнено, часть осталась на земле', 'bad');
		persistSoon();
		refresh();
	} else if (r.err === E.StorageFull) toast('Хранилище заполнено: ' + storageHint(res), 'bad');
	else toast(errText(r.err), '');
}
function doUpgrade(id, gems) {
	const b = K.st.b.find((x) => x.id === id);
	if (!b) return;
	const d = defOf(b.def);
	const code = K.bridge.upgrade(id, gems);
	if (code !== E.None) { toast(errText(code, b.nx), 'bad'); refresh(true); return; }
	closeSheet();
	const t = b.nx.t;
	toast(t > 0 ? 'Улучшение началось: ' + d.name + ' · ' + fmtDur(t) : 'Готово: ' + d.name + ', уровень ' + (b.lv + 1), 'gold');
	persist();
	refresh(true);
}
function doLandUpgrade(id, gems) {
	const L = K.st.lands.find((x) => x.id === id);
	if (!L) return;
	const code = K.bridge.upgradeLand(id, gems);
	if (code !== E.None) { toast(errText(code, L.nx, 'land'), 'bad'); refresh(true); return; }
	closeSheet();
	toast('Улучшение земли началось: ' + landType(L.type).name + ' · ' + fmtDur(L.nx.t), 'gold');
	persist();
	refresh(true);
}
function resetKingdom() {
	K.armed = null;
	if (cloud.state === 'on') keepPrev({ v: 1, save: K.bridge.save(), offset: clock.offset, savedAt: Date.now() }); // the menu can bring it back
	try { localStorage.removeItem(SAVE_KEY); } catch (e) { /* ignore */ }
	clock.offset = 0;
	clock.scale = 1;
	clock.last = 0;
	K.bridge.newGame(nowSec(), Math.floor(Math.random() * 2147483647));
	cancelPlace();
	select(null);
	closeSheet();
	focusOn(0, 6, 84);
	persist();
	refresh(true);
	toast('Новое королевство основано', 'gold');
}

// ---------------------------------------------------------------------------------------------
// Placement and moving
// ---------------------------------------------------------------------------------------------
function findSpot(def, ignore) {
	const d = defOf(def);
	const h = gridHalf();
	const cx = Math.round(view.tx / TILE + h - d.size[0] / 2);
	const cy = Math.round(view.tz / TILE + h - d.size[1] / 2);
	for (let r = 0; r < 40; r++) {
		for (let dy = -r; dy <= r; dy++) {
			for (let dx = -r; dx <= r; dx++) {
				if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) continue;
				if (K.bridge.canPlace(def, cx + dx, cy + dy, ignore)) return { x: cx + dx, y: cy + dy };
			}
		}
	}
	return null;
}
function startPlace(def) {
	const ck = K.st.shop[def].ck;
	if (ck.err !== E.None && ck.err !== E.NotEnough) { toast(errText(ck.err, ck), 'bad'); return; }
	if (ck.err === E.NotEnough && ck.g < 0) { toast(missingText(ck.cost), 'bad'); return; }
	const spot = findSpot(def, 0);
	if (!spot) { toast('Нет свободного места: расширьте город или переставьте постройки', 'bad'); return; }
	closeSheet();
	select(null);
	K.place = { def, id: 0, lv: 1, x: spot.x, y: spot.y, ok: true };
	makeGhost();
}
function startMove(id) {
	const b = K.st.b.find((x) => x.id === id);
	if (!b) return;
	closeSheet();
	K.sel = null;
	K.place = { def: b.def, id, lv: Math.max(1, b.lv), x: b.x, y: b.y, ok: true };
	makeGhost();
	toast('Перетащите постройку на новое место', '');
}
function makeGhost() {
	dropGhost();
	const p = K.place;
	const inst = instantiate(getModel(p.def, p.lv, 'done'));
	const mats = [];
	inst.group.traverse((o) => {
		if (o.isMesh) {
			o.material = o.material.clone();
			o.material.transparent = true;
			o.material.opacity = 0.78;
			o.castShadow = false;
			mats.push(o.material);
		}
	});
	scene.add(inst.group);
	world.ghost = { group: inst.group, upd: inst.upd, mats };
	world.grid.visible = true;
	positionGhost();
	const d = defOf(p.def);
	const c = cellCenter(d, p.x, p.y);
	focusOn(c.x, c.z);
	syncViews();
	updatePanel();
}
function dropGhost() {
	if (!world.ghost) return;
	scene.remove(world.ghost.group);
	for (const m of world.ghost.mats) m.dispose();
	world.ghost = null;
	world.grid.visible = false;
}
function positionGhost() {
	const p = K.place;
	const d = defOf(p.def);
	const c = cellCenter(d, p.x, p.y);
	world.ghost.group.position.set(c.x, 0.2, c.z);
	p.ok = K.bridge.canPlace(p.def, p.x, p.y, p.id);
	updateSelRing();
	updatePlaceUi();
}
function moveGhost(wx, wz) {
	const p = K.place;
	const d = defOf(p.def);
	const h = gridHalf();
	const x = clamp(Math.round(wx / TILE + h - d.size[0] / 2), 0, K.rules.grid.width - d.size[0]);
	const y = clamp(Math.round(wz / TILE + h - d.size[1] / 2), 0, K.rules.grid.height - d.size[1]);
	if (x === p.x && y === p.y) return;
	p.x = x;
	p.y = y;
	if (K.armed && K.armed.act === 'place') K.armed = null;
	positionGhost();
}
function ghostContains(g, margin) {
	const p = K.place;
	const d = defOf(p.def);
	const c = cellCenter(d, p.x, p.y);
	return Math.abs(g.x - c.x) <= (d.size[0] * TILE) / 2 + margin && Math.abs(g.z - c.z) <= (d.size[1] * TILE) / 2 + margin;
}
function updatePlaceUi() {
	const p = K.place;
	const box = $('kPlace');
	if (!p) { box.hidden = true; return; }
	const d = defOf(p.def);
	const ok = $('kPlaceOk');
	if (p.id) {
		$('kPlaceText').textContent = p.ok ? 'Новое место для: ' + d.name : 'Здесь нельзя: место занято или вне города';
		ok.textContent = 'Готово';
	} else {
		const ck = K.st.shop[p.def].ck;
		$('kPlaceText').innerHTML = esc(d.name) + ' · ' + costHtml(ck.cost, K.st.res) + ' · ' + fmtDur(ck.t);
		if (ck.err === E.NotEnough) ok.innerHTML = (isArmed('place') ? 'Подтвердить ' : 'Построить ') + gemSpan(ck.g);
		else ok.textContent = 'Построить';
	}
	ok.disabled = !p.ok;
	box.hidden = false;
	$('kBottom').hidden = true;
}
function confirmPlace() {
	const p = K.place;
	if (!p || !p.ok) return;
	let code;
	if (p.id) {
		code = K.bridge.move(p.id, p.x, p.y);
		if (code === E.None) {
			const id = p.id;
			cancelPlace();
			persist();
			refresh(true);
			select({ kind: 'b', id });
		} else toast(errText(code), 'bad');
		return;
	}
	const ck = K.st.shop[p.def].ck;
	const gems = ck.err === E.NotEnough;
	if (gems && !isArmed('place')) { arm('place'); updatePlaceUi(); return; }
	K.armed = null;
	code = K.bridge.build(p.def, p.x, p.y, gems);
	if (code !== E.None) { toast(errText(code, ck), 'bad'); refresh(true); return; }
	const d = defOf(p.def);
	const x = p.x;
	const y = p.y;
	cancelPlace();
	persist();
	refresh(true);
	const nb = K.st.b.find((b) => b.def === p.def && b.x === x && b.y === y);
	toast(ck.t > 0 ? 'Стройка началась: ' + d.name + ' · ' + fmtDur(ck.t) : 'Построено: ' + d.name, 'gold');
	if (nb) select({ kind: 'b', id: nb.id });
}
function cancelPlace() {
	if (!K.place) return;
	K.place = null;
	if (K.armed && K.armed.act === 'place') K.armed = null;
	dropGhost();
	$('kPlace').hidden = true;
	if (K.st) syncViews();
	updatePanel();
}
function buildGrid() {
	const gw = K.rules.grid.width;
	const tex = canvasTex(1024, 1024, (g, w) => {
		const step = w / gw;
		g.strokeStyle = 'rgba(255,255,255,0.55)';
		g.lineWidth = 2;
		for (let i = 0; i <= gw; i++) {
			g.beginPath(); g.moveTo(i * step, 0); g.lineTo(i * step, w); g.stroke();
			g.beginPath(); g.moveTo(0, i * step); g.lineTo(w, i * step); g.stroke();
		}
	});
	const m = new T.Mesh(G.plane, new T.MeshBasicMaterial({ map: tex, transparent: true, opacity: 0.45, depthWrite: false }));
	m.rotation.x = -Math.PI / 2;
	m.scale.set(gw * TILE, K.rules.grid.height * TILE, 1);
	m.position.y = 0.08;
	m.visible = false;
	scene.add(m);
	world.grid = m;
}

// ---------------------------------------------------------------------------------------------
// Sheets: upgrade, info, shop, world, stones, builders, expansion, menu, welcome
// ---------------------------------------------------------------------------------------------
function openSheet(render) {
	K.sheet = { render, key: '' };
	K.armed = null;
	refreshSheet(true);
}
function closeSheet() {
	K.sheet = null;
	$('kSheet').hidden = true;
	updatePanel();
}
function refreshSheet(force) {
	if (!K.sheet) return;
	let r = null;
	try { r = K.sheet.render(); } catch (e) { report('окно', e); }
	if (!r) { closeSheet(); return; }
	const panel = $('kSheetPanel');
	if (force || r.key !== K.sheet.key) {
		const top = panel.scrollTop;
		panel.innerHTML = r.html;
		panel.scrollTop = top;
		K.sheet.key = r.key;
	}
	if (r.live) r.live(panel);
	if ($('kSheet').hidden) { $('kSheet').hidden = false; updatePanel(); }
}
const closeBtn = '<button class="btn" data-act="close-sheet">Закрыть</button>';
function srcNote(lv) {
	if (!lv) return '';
	return '<p class="fine">' + (lv.src === 'wiki' ? 'Стоимость и время этого уровня взяты из вики оригинала.' : 'Для этого уровня в вики оригинала нет цифр: стоимость и время — наша оценка по соседним уровням.') + '</p>';
}

function castleUnlocks(next) {
	const rows = [];
	K.defs.forEach((d, i) => {
		if (i === K.rules.castleIndex) return;
		const a = maxCount(d, next - 1);
		const b = maxCount(d, next);
		if (b > a) rows.push({ def: i, text: a === 0 ? 'новая постройка: ' + d.name : d.name + ': ' + a + ' → ' + b });
	});
	const levels = K.defs.filter((d, i) => i !== K.rules.castleIndex && d.levels.some((l) => l.castle === next)).map((d) => d.name);
	if (levels.length) rows.push({ def: -1, text: 'новые уровни: ' + levels.join(', ') });
	return rows;
}
function openUpgrade(id) {
	openSheet(() => {
		const st = K.st;
		const b = st.b.find((x) => x.id === id);
		if (!b || b.up) return null;
		const d = defOf(b.def);
		const cur = levelDef(d, b.lv);
		const nx = d.levels[b.lv];
		if (!nx) return null;
		const ck = b.nx;
		const rows = statKeys(d, cur, nx).map((k) => {
			const a = cur && cur[k] !== undefined ? cur[k] : 0;
			const c = nx[k] !== undefined ? nx[k] : a;
			return '<span class="k">' + statLabel(d, k) + '</span><span class="v">' + fmt(a) + '</span><span class="n">' + (c !== a ? '→ ' + fmt(c) : '') + '</span>';
		});
		const unlocks = d.role === 'castle' ? castleUnlocks(b.lv + 1) : [];
		let main;
		const armedU = isArmed('up:' + id);
		if (ck.err === E.None) main = '<button class="btn primary" data-act="do-upgrade" data-id="' + id + '">' + (d.role === 'builders' ? 'Нанять' : 'Улучшить') + '</button>';
		else if (ck.err === E.NotEnough && ck.g > 0) {
			main = '<button class="btn primary" data-act="do-upgrade-gems" data-id="' + id + '" data-gems="' + ck.g + '">' + (armedU ? 'Подтвердить ' : 'Докупить и улучшить ') + gemSpan(ck.g) + '</button>';
		} else if (ck.err === E.NoBuilder) main = '<button class="btn primary" data-act="builders">Ускорить стройку</button>';
		else main = '<button class="btn" disabled>' + esc(errText(ck.err, ck)) + '</button>';
		const html = sheetTitle(buildingIcon(b.def), esc(d.name)) + '<p>Уровень ' + b.lv + ' → ' + (b.lv + 1) + ' из ' + d.levels.length + '</p>' +
			(rows.length ? '<div class="cmp">' + rows.join('') + '</div>' : '') +
			(unlocks.length ? '<h3>Откроет</h3><div class="unlocks">' + unlocks.map((u) => '<span>' + (u.def >= 0 ? mini('building.' + K.defs[u.def].id) : '') + esc(u.text) + '</span>').join('') + '</div>' : '') +
			'<div class="costline"><span>Стоимость</span>' + costHtml(ck.cost, st.res) + '<span>Время</span><b>' + (ck.t > 0 ? fmtDur(ck.t) : 'сразу') + '</b></div>' +
			(ck.err !== E.None ? '<p class="note">' + esc(errText(ck.err, ck)) + (ck.err === E.NotEnough && ck.g > 0 ? '. Как в оригинале, недостающее можно купить за самоцветы.' : '') + '</p>' : '') +
			srcNote(nx) + '<div class="actions">' + main + closeBtn + '</div>';
		return { key: ['up', id, ck.err, ck.g, armedU, st.res[0] >= ck.cost[0], st.res[1] >= ck.cost[1]].join('|'), html };
	});
}
function openLandUpgrade(id) {
	openSheet(() => {
		const st = K.st;
		const L = st.lands.find((x) => x.id === id);
		if (!L || L.up || L.nx.err === E.MaxLevel) return null;
		const ty = landType(L.type);
		const ck = L.nx;
		// The next level's numbers the way the kingdom counts them: a guard adds 1% per titan level.
		const guard = L.titan ? st.titans.find((t) => t.id === L.titan) : null;
		const pct = 100 + (guard ? guard.lv : 0);
		const fills = ty.fillHours || K.rules.lands.fillHours;
		const fill = fills[Math.min(L.lv + 1, fills.length) - 1];
		const rate = Math.floor(ty.produce[L.lv] * pct / 100);
		const cap = Math.floor(ty.produce[L.lv] * pct * fill / (100 * (ty.periodHours || 1)));
		const rows = [['Добыча', landRateText(L.type, L.rate), landRateText(L.type, rate)], ['Копит до', resAmount(ty.resource, L.cap), resAmount(ty.resource, cap)],
			['Полна за', L.fill + ' ч', fill + ' ч']].map((r) => '<span class="k">' + r[0] + '</span><span class="v">' + r[1] + '</span><span class="n">' + (r[2] !== r[1] ? '→ ' + r[2] : '') + '</span>');
		const armedU = isArmed('lup:' + id);
		let main;
		if (ck.err === E.None) main = '<button class="btn primary" data-act="do-land-upgrade" data-id="' + id + '">Улучшить</button>';
		else if (ck.err === E.NotEnough && ck.g > 0) main = '<button class="btn primary" data-act="do-land-upgrade-gems" data-id="' + id + '" data-gems="' + ck.g + '">' + (armedU ? 'Подтвердить ' : 'Докупить и улучшить ') + gemSpan(ck.g) + '</button>';
		else if (ck.err === E.NoBuilder) main = '<button class="btn primary" data-act="builders">Ускорить стройку</button>';
		else main = '<button class="btn" disabled>' + esc(errText(ck.err, ck, 'land')) + '</button>';
		const html = sheetTitle(landIcon(L.type, L.lv), esc(ty.name)) + '<p>Уровень ' + L.lv + ' → ' + (L.lv + 1) + '. Улучшение занимает строителя, а земля всё это время добывает.</p>' +
			'<div class="cmp">' + rows.join('') + '</div>' +
			'<div class="costline"><span>Стоимость</span>' + costHtml(ck.cost, st.res) + '<span>Время</span><b>' + fmtDur(ck.t) + '</b></div>' +
			(ck.err !== E.None ? '<p class="note">' + esc(errText(ck.err, ck, 'land')) + (ck.err === E.NotEnough && ck.g > 0 ? '. Недостающее золото можно купить за самоцветы.' : '') + '</p>' : '') +
			'<p class="fine">В оригинале захваченные земли улучшали, чтобы они больше давали; цен гайды не называют, поэтому цена и время здесь наша оценка. ' +
			'Земля растёт до верхнего уровня земель, которые Замок открывает на карте мира (сейчас ' + st.landMaxLv + ').</p>' +
			'<div class="actions">' + main + closeBtn + '</div>';
		return { key: ['lup', id, ck.err, ck.g, armedU, st.res[0] >= ck.cost[0], st.busy, st.builders, L.rate, L.cap].join('|'), html };
	});
}
function openInfo(id) {
	openSheet(() => {
		const b = K.st.b.find((x) => x.id === id);
		if (!b) return null;
		const d = defOf(b.def);
		const cur = levelDef(d, b.lv) || d.levels[0];
		const rows = statKeys(d, cur).map((k) => '<span class="k">' + statLabel(d, k) + '</span><span class="v">' + fmt(cur[k]) + '</span><span></span>');
		const next = d.levels[b.lv];
		const html = sheetTitle(buildingIcon(b.def), esc(d.name)) + '<p>' + esc(d.description) + '</p>' +
			'<p>Уровень ' + b.lv + ' из ' + d.levels.length + '. Таких построек: ' + K.st.shop[b.def].cnt + ' из ' + K.st.shop[b.def].max + ' на этом уровне Замка.</p>' +
			(rows.length ? '<div class="cmp">' + rows.join('') + '</div>' : '') +
			(next && next.castle > K.st.castle ? '<p class="note">Следующий уровень требует Замок ' + next.castle + '.</p>' : '') +
			srcNote(cur) + '<div class="actions">' + closeBtn + '</div>';
		return { key: 'info|' + id + '|' + b.lv, html };
	});
}

const thumbs = new Map();
/** Renders a picture of every building at level 1 for the shop (once, with the shared renderer). */
function makeThumbs() {
	if (thumbs.size) return;
	const r = K.app.renderer;
	const ts = new T.Scene();
	ts.background = canvasTex(4, 64, (g, w, h) => {
		const grd = g.createLinearGradient(0, 0, 0, h);
		grd.addColorStop(0, '#9cc6e4');
		grd.addColorStop(1, '#e3eef3');
		g.fillStyle = grd;
		g.fillRect(0, 0, w, h);
	});
	ts.add(new T.HemisphereLight(0xeaf3ff, 0x5d4e33, 0.95));
	const l = new T.DirectionalLight(0xfff0d8, 1.4);
	l.position.set(-30, 60, 20);
	ts.add(l);
	const pad = new T.Mesh(cylGeo(1, 24), mat(ISLE.grass));
	ts.add(pad);
	const tc = new T.PerspectiveCamera(28, 4 / 3, 0.5, 300);
	const w = 160;
	const h = 120;
	const pr = r.getPixelRatio();
	const size = r.getSize(new T.Vector2());
	const canvas = r.domElement;
	const out = document.createElement('canvas');
	out.width = Math.round(w * pr);
	out.height = Math.round(h * pr);
	const oc = out.getContext('2d');
	r.setScissorTest(true);
	K.defs.forEach((d, i) => {
		const model = getModel(i, 1, 'done');
		const inst = instantiate(model);
		inst.upd.forEach((u) => u(0.6));
		ts.add(inst.group);
		const sph = model.box.getBoundingSphere(new T.Sphere());
		const rr = Math.max(d.size[0], d.size[1]) * TILE * 0.62;
		pad.scale.set(rr, 0.4, rr);
		pad.position.y = -0.2;
		const dist = (sph.radius / Math.sin(rad(14))) * 1.02;
		const pitch = rad(30);
		const yaw = Math.PI / 4;
		tc.position.set(sph.center.x + Math.sin(yaw) * Math.cos(pitch) * dist, sph.center.y + Math.sin(pitch) * dist, sph.center.z + Math.cos(yaw) * Math.cos(pitch) * dist);
		tc.lookAt(sph.center);
		r.setViewport(0, 0, w, h);
		r.setScissor(0, 0, w, h);
		r.render(ts, tc);
		oc.clearRect(0, 0, out.width, out.height);
		oc.drawImage(canvas, 0, canvas.height - out.height, out.width, out.height, 0, 0, out.width, out.height);
		thumbs.set(i, out.toDataURL('image/png'));
		ts.remove(inst.group);
	});
	r.setScissorTest(false);
	r.setViewport(0, 0, size.x, size.y);
}
function openShop() {
	try { makeThumbs(); } catch (e) { console.warn('Shop pictures unavailable:', e); }
	if (!K.shopTab) K.shopTab = 'resources';
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const tabs = CATS.map(([id, name]) => '<button class="' + (id === K.shopTab ? 'on' : '') + '" data-act="tab" data-tab="' + id + '">' + name + '</button>').join('');
		const cards = [];
		K.defs.forEach((d, i) => {
			if (d.category !== K.shopTab || i === K.rules.castleIndex) return;
			const s = st.shop[i];
			const ck = s.ck;
			const usable = ck.err === E.None || (ck.err === E.NotEnough && ck.g > 0);
			let why = '';
			if (ck.err === E.Locked) why = 'Откроется на Замке ' + ck.c;
			else if (ck.err === E.LimitReached) why = ck.c ? 'Ещё одна — на Замке ' + ck.c : 'Построено максимум';
			else if (!usable) why = errText(ck.err, ck);
			cards.push('<button class="shopCard' + (usable ? '' : ' off') + '" data-act="shop" data-id="' + i + '"' + (usable ? '' : ' aria-disabled="true"') + '>' +
				'<span class="pic">' + (thumbs.has(i) ? '<img alt="" src="' + thumbs.get(i) + '">' : '') + buildingIcon(i) + '</span>' +
				'<b>' + esc(d.name) + '</b><span class="meta"><span>' + s.cnt + ' / ' + s.max + '</span><span>' + (ck.t > 0 ? fmtDur(ck.t) : 'сразу') + '</span></span>' +
				'<span class="meta">' + costHtml(ck.cost, st.res) + '</span>' + (why ? '<span class="why">' + esc(why) + '</span>' : '') + '</button>');
		});
		const html = sheetTitle(icoHtml('ui.build', 'bld other'), 'Строительство') + '<div class="shopTabs">' + tabs + '</div>' +
			'<p class="fine">Свободных строителей: ' + (st.builders - st.busy) + ' из ' + st.builders + '. Число построек каждого вида растёт с уровнем Замка, как в оригинале.</p>' +
			'<div class="shopGrid">' + cards.join('') + '</div><div class="actions">' + closeBtn + '</div>';
		return { key: ['shop', K.shopTab, st.castle, st.busy, st.builders, st.shop.map((s) => s.ck.err + ':' + s.cnt).join(), st.res[0] >> 6, st.res[1] >> 6].join('|'), html };
	});
}

function enemyText(lv, et) {
	const b = K.rules.battles[Math.min(lv, K.rules.battles.length) - 1];
	const n = b.squads.length;
	return AI_RU[b.ai] + ': ' + n + ' ' + plural(n, 'отряд', 'отряда', 'отрядов') + (et ? ', титан ' + et.def.name + ' ур. ' + et.level : b.titan ? ', титан ур. ' + b.titan.level : ', без титана');
}
function openWorld() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const L = K.rules.lands;
		const fullSlots = st.lands.length >= st.landCap;
		const raid = 3;
		const capture = 6;
		const cards = st.world.map((w) => {
			const ty = landType(w.type);
			const rate = ty.produce[Math.min(w.lv, ty.produce.length) - 1];
			const fill = (ty.fillHours || L.fillHours)[Math.min(w.lv, (ty.fillHours || L.fillHours).length) - 1];
			const capBtn = fullSlots ? '<button class="btn" disabled>Захват: нет мест</button>'
				: '<button class="btn primary" data-act="capture" data-id="' + w.id + '"' + (st.res[3] < capture ? ' disabled' : '') + '>Захват · ' + capture + ' ' + iconSvg('stones') + '</button>';
			return '<div class="landCard">' + landIcon(w.type, w.lv) + '<div class="info"><b>' + esc(ty.name) + ' · ур. ' + w.lv + '</b>' +
				'<span>' + esc(enemyText(w.lv, enemyTitan(w))) + (ty.field ? ' · поле боя: ' + esc(ty.field) : '') + '</span>' +
				'<span>Набег: до ' + resAmount(ty.resource, w.loot) + ' · Захват: ' + landRateText(w.type, rate) + ', полна за ' + fill + ' ч · победа: +' + w.vp + ' ОП</span></div>' +
				'<div class="row"><button class="btn" data-act="raid" data-id="' + w.id + '"' + (st.res[3] < raid ? ' disabled' : '') + '>Набег · ' + raid + ' ' + iconSvg('stones') + '</button>' + capBtn + '</div></div>';
		});
		const mine = st.lands.map((l) => {
			const ty = landType(l.type);
			return '<div class="landCard">' + landIcon(l.type, l.lv) + '<div class="info"><b>' + esc(ty.name) + ' · ур. ' + l.lv + '</b>' +
				'<span>' + landRateText(l.type, l.rate) + ' · накоплено ' + fmt(l.st) + ' из ' + fmt(l.cap) + '</span></div>' +
				'<div class="row"><button class="btn" data-act="land-collect" data-id="' + l.id + '"' + (l.st > 0 ? '' : ' disabled') + '>Собрать</button></div></div>';
		});
		const slotPrice = st.extraSlots < L.extraSlotGems.length && st.landCap < L.maxLands ? L.extraSlotGems[st.extraSlots] : 0;
		const refill = (st.cap[3] - st.res[3]) * 4;
		const html = sheetTitle(icoHtml('ui.attack', 'bld troops'), 'Земли') +
			'<p>Мир расколот, и его осколки парят вокруг королевства. Набег приносит то, что даёт земля, захват даёт постоянный доход: земля пристаёт к вашему острову. Атака тратит камни портала: набег 3, захват 6.</p>' +
			'<p class="fine">Как в оригинале: золотые и фермерские земли дают золото и еду; лес, озеро и гора — очки победы для сезона (с Замка 3); руины — осколки для возвышения титанов (с Замка 5). Каждая победа тоже приносит очки победы.</p>' +
			'<div class="costline"><span>Камни</span><b>' + iconSvg('stones') + st.res[3] + ' / ' + st.cap[3] + '</b><span data-live="next"></span>' +
			(refill > 0 ? '<button class="btn" data-act="refill" data-gems="' + refill + '">' + (isArmed('refill') ? 'Подтвердить ' : 'Пополнить ') + gemSpan(refill) + '</button>' : '') + '</div>' +
			'<h3>Можно атаковать</h3>' + cards.join('') +
			'<h3>Ваши земли: ' + st.lands.length + ' из ' + st.landCap + '</h3>' +
			(mine.length ? mine.join('') : '<p class="fine">Пока ни одной. Захватите землю, и она будет приносить ресурсы. Мест для земель: ' + st.landCap + ' (растёт с Замком, до ' + L.maxLands + ').</p>') +
			(slotPrice ? '<div class="actions"><button class="btn" data-act="slot" data-gems="' + slotPrice + '">' + (isArmed('slot') ? 'Подтвердить ' : 'Ещё место для земли ') + gemSpan(slotPrice) + '</button></div>' : '') +
			'<div class="actions">' + closeBtn + '</div>';
		return {
			key: ['world', st.res[3], st.cap[3], st.lands.map((l) => l.id + ':' + (l.st > 0)).join(), st.world.map((w) => w.id).join(), st.landCap, isArmed('refill'), isArmed('slot')].join('|'), html,
			live(el) { live(el, 'next', K.st.stonesNext ? 'следующий через ' + fmtClock(K.st.stonesNext - nowF()) : 'полный запас'); },
		};
	});
}
function openStones() {
	openSheet(() => {
		const st = K.st;
		const refill = (st.cap[3] - st.res[3]) * 4;
		const html = '<h2 id="kSheetTitle">Камни портала</h2><div class="costline"><b>' + iconSvg('stones') + st.res[3] + ' / ' + st.cap[3] + '</b><span data-live="next"></span></div>' +
			'<p>Как в оригинале: камни переносят армию через Портал. Новый камень появляется каждые 7 минут, пока запас не полон. Набег стоит 3 камня, захват земли 6. Запас растёт с уровнем Портала.</p>' +
			'<div class="actions">' + (refill > 0 ? '<button class="btn primary" data-act="refill" data-gems="' + refill + '">' + (isArmed('refill') ? 'Подтвердить ' : 'Пополнить ') + gemSpan(refill) + '</button>' : '') + closeBtn + '</div>';
		return { key: ['stones', st.res[3], st.cap[3], isArmed('refill')].join('|'), html, live(el) { live(el, 'next', K.st.stonesNext ? 'следующий через ' + fmtClock(K.st.stonesNext - nowF()) : 'полный запас'); } };
	});
}
function openSeason() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const S = K.rules.seasons;
		const vp = st.res[5];
		const cur = st.season.league;
		const next = S.leagues[cur + 1];
		const from = cur >= 0 ? S.leagues[cur].vp : 0;
		const rows = S.leagues.map((lg, i) => '<span class="k' + (i === cur ? ' on' : '') + '">' + esc(lg.name) + '</span><span class="v">' + fmt(lg.vp) + ' ОП</span>' +
			'<span class="n">+' + fmt(lg.gems) + ' ' + iconSvg('gems') + ' +' + lg.stones + ' ' + iconSvg('stones') + '</span>');
		const last = st.season.last;
		const lastLg = last.no >= 0 && last.league >= 0 ? S.leagues[last.league] : null;
		const al = st.ally;
		const allyHtml = al.my >= 0 ? '<div class="landCard">' + embHtml(al.tag, al.emb) + '<div class="info"><b>' + esc(al.name) + ' [' + esc(al.tag) + ']</b>' +
			'<span>' + esc(K.rules.alliances.leagues[al.tier].name) + ', место ' + al.place + ' из ' + al.size + '. Награду сезона даёт лига альянса.</span></div>' +
			'<div class="row"><button class="btn primary" data-act="alliance">Альянс</button></div></div>'
			: '<p class="fine">С ' + fmt(K.rules.alliances.joinVp) + ' ОП можно вступить в альянс: тогда награду сезона даёт лига альянса, как в оригинале.</p>';
		const paid = 'получено +' + fmt(last.gems) + ' самоцветов' + (last.stones ? ' и +' + last.stones + ' камней' : '');
		const got = last.al >= 0 ? crystalGot(last, st) : '';
		const lastText = last.no < 0 ? '' : last.al >= 0
			? 'Прошлый сезон: ' + fmt(last.vp) + ' ОП, альянс на ' + last.place + ' месте из ' + last.size + ' в лиге «' + esc(K.rules.alliances.leagues[last.tier].name) + '», ' + paid + (got ? ', из кристалла: ' + esc(got) : '') + '.'
			: 'Прошлый сезон: ' + fmt(last.vp) + ' ОП, ' + (lastLg ? esc(lastLg.name) + ', ' + paid + '.' : 'без лиги.');
		const html = sheetTitle('<div class="ico land vp">' + DotIcons.svg('land.forest') + '</div>', 'Сезон') +
			'<div class="costline"><span>Очки победы</span><b>' + iconSvg('vp') + fmt(vp) + '</b><span>' + (cur >= 0 ? esc(S.leagues[cur].name) : 'пока без лиги') + '</span></div>' +
			(next ? '<div class="progress"><i style="width:' + Math.round((100 * (vp - from)) / Math.max(1, next.vp - from)) + '%"></i></div><p class="fine">До лиги «' + esc(next.name) + '»: ' + fmt(next.vp - vp) + ' ОП.</p>' : '<p class="fine">Высшая лига взята.</p>') +
			'<p>Сезон кончится через <b data-live="left"></b>. Тогда очки победы обнулятся, а лига принесёт награду.</p>' + allyHtml +
			'<h3>Личная лига' + (al.my >= 0 ? ' (платит, если вы не в альянсе)' : '') + '</h3>' +
			'<div class="cmp">' + rows.join('') + '</div>' +
			(lastText ? '<p class="fine">' + lastText + '</p>' : '') +
			'<p class="fine">Как в оригинале: очки победы приносят победы в боях за земли и земли очков победы (лес, озеро, гора, с Замка 3); в конце недели очки у всех обнуляются, а награда зависит от результата. ' +
			'В оригинале лиги — это лиги альянсов, которые повышаются и понижаются; они есть в окне «Альянс». Личная лига для тех, кто без альянса, — наша: её названия, пороги и награды наши.</p>' +
			'<div class="actions"><button class="btn primary" data-act="world">К землям</button>' + closeBtn + '</div>';
		return {
			key: ['season', vp, cur, last.no, st.season.no, al.my, al.tier, al.place].join('|'), html,
			live(el) { live(el, 'left', fmtDur(Math.max(0, K.st.season.end - nowF()))); },
		};
	});
}
function openBuilders() {
	openSheet(() => {
		const st = K.st;
		const busy = st.b.filter((b) => b.up);
		const busyL = st.lands.filter((L) => L.up);
		const yard = st.b.find((b) => defOf(b.def).role === 'builders');
		const rows = busy.map((b) => {
			const d = defOf(b.def);
			return '<div class="landCard">' + buildingIcon(b.def) + '<div class="info"><b>' + esc(d.name) + (b.lv ? ' → ур. ' + (b.lv + 1) : ' · стройка') + '</b>' +
				'<span data-live="t' + b.id + '"></span></div><div class="row"><button class="btn" data-act="goto" data-id="' + b.id + '">Показать</button>' +
				'<button class="btn primary" data-act="skip" data-id="' + b.id + '">' + (isArmed('skip:' + b.id) ? 'Подтвердить ' : 'Ускорить ') + '<span data-live="g' + b.id + '"></span></button></div></div>';
		}).concat(busyL.map((L) => '<div class="landCard">' + landIcon(L.type, L.lv) + '<div class="info"><b>' + esc(landType(L.type).name) + ' → ур. ' + (L.lv + 1) + '</b>' +
			'<span data-live="t' + L.id + '"></span></div><div class="row"><button class="btn" data-act="goto-land" data-id="' + L.id + '">Показать</button>' +
			'<button class="btn primary" data-act="land-skip" data-id="' + L.id + '">' + (isArmed('lskip:' + L.id) ? 'Подтвердить ' : 'Ускорить ') + '<span data-live="g' + L.id + '"></span></button></div></div>'));
		const html = sheetTitle(buildingIconById('builders_yard'), 'Строители') + '<p>Свободно ' + (st.builders - st.busy) + ' из ' + st.builders + '. Каждый строитель ведёт одну стройку; новых нанимают за самоцветы в Дворе строителей (до 5, как в оригинале).</p>' +
			(rows.length ? rows.join('') : '<p class="fine">Сейчас никто не занят.</p>') +
			'<div class="actions">' + (yard ? '<button class="btn" data-act="goto" data-id="' + yard.id + '">Двор строителей</button>' : '') + closeBtn + '</div>';
		return {
			key: ['builders', busy.map((b) => b.id + ':' + isArmed('skip:' + b.id)).join(), busyL.map((L) => L.id + ':' + isArmed('lskip:' + L.id)).join(), st.builders].join('|'), html,
			live(el) {
				for (const b of K.st.b.concat(K.st.lands)) {
					if (!b.up) continue;
					live(el, 't' + b.id, 'осталось ' + fmtDur(b.f - nowF()));
					const n = el.querySelector('[data-live="g' + b.id + '"]');
					if (n) n.innerHTML = gemSpan(b.skip);
				}
			},
		};
	});
}
function openExpansion() {
	openSheet(() => {
		const st = K.st;
		const ex = K.rules.expansions;
		if (st.expansions >= ex.length) return null;
		const ck = st.exp;
		let main;
		if (ck.err === E.None) main = '<button class="btn primary" data-act="expand">Расширить · ' + costHtml(ck.cost) + '</button>';
		else if (ck.err === E.NotEnough && ck.g > 0) main = '<button class="btn primary" data-act="expand-gems" data-gems="' + ck.g + '">' + (isArmed('expand') ? 'Подтвердить ' : 'Докупить и расширить ') + gemSpan(ck.g) + '</button>';
		else main = '<button class="btn" disabled>' + esc(errText(ck.err, ck)) + '</button>';
		const html = '<h2 id="kSheetTitle">Расширение города</h2><p>Участок ' + (st.expansions + 1) + ' из ' + ex.length + '. Расширение убирает лес вокруг города и открывает место для новых построек.</p>' +
			'<p class="fine">Как в оригинале: четыре участка, первый открыт сразу, следующие стоят 33 000, 307 000 и 1 431 000 золота. Уровень Замка для каждого участка — наша оценка.</p>' +
			'<div class="costline"><span>Стоимость</span>' + costHtml(ck.cost, st.res) + '</div>' +
			(ck.err !== E.None ? '<p class="note">' + esc(errText(ck.err, ck)) + '</p>' : '') + '<div class="actions">' + main + closeBtn + '</div>';
		return { key: ['exp', st.expansions, ck.err, ck.g, isArmed('expand')].join('|'), html };
	});
}
/** The account's second kingdom ("kingdom-prev"), when it is not the one on screen. */
function otherKingdom() {
	return cloud.prev && seedOf(cloud.prev.save) !== seedOf(K.bridge.save()) ? cloud.prev : null;
}
function swapKingdom() {
	const other = otherKingdom();
	K.armed = null;
	if (!other) return;
	const cur = { v: 1, save: K.bridge.save(), offset: clock.offset, savedAt: Date.now() };
	if (!adopt(other)) { toast('Это королевство не открылось', 'bad'); return; }
	keepPrev(cur);
	persist();
	toast('Открыто другое королевство: ' + summaryOf(other.save), 'gold');
}
function cloudText() {
	if (cloud.state === 'on') return 'Копия хранится в вашем аккаунте claude.ai и откроется на другом устройстве.';
	if (cloud.state === 'error') return 'Копия в аккаунте сейчас недоступна, прогресс хранится в этом браузере.';
	return 'Прогресс хранится в этом браузере.';
}
function openMenu() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const scales = [1, 10, 100, 1000];
		const other = otherKingdom();
		const otherHtml = other ? '<p class="fine">В аккаунте сохранено и другое королевство: ' + esc(summaryOf(other.save)) + ', от ' + new Date(+other.savedAt || 0).toLocaleString('ru-RU') +
			'. Можно перейти в него, а это останется вторым.</p><div class="actions"><button class="btn' + (isArmed('swap') ? ' danger' : '') + '" data-act="swap-kingdom">' +
			(isArmed('swap') ? 'Точно перейти?' : 'Перейти в другое королевство') + '</button></div>' : '';
		const html = '<h2 id="kSheetTitle">Королевство</h2>' +
			'<p>Остров, постройки, ресурсы, строители, армия, титаны, камни портала и земли работают по правилам оригинала Dawn of Titans. Таймеры идут и когда страница закрыта.</p>' +
			'<div class="actions"><button class="btn primary" data-act="training">Тренировочные бои</button><button class="btn" data-act="help">Как играть</button></div>' +
			'<h3>Ускорение времени</h3><p class="fine">Для проверки: стройки, добыча и камни идут быстрее. В оригинале такого нет.</p>' +
			'<div class="seg">' + scales.map((s) => '<button class="' + (clock.scale === s ? 'on' : '') + '" data-act="time" data-scale="' + s + '">×' + s + '</button>').join('') + '</div>' +
			'<h3>Сохранение</h3><p class="fine">' + cloudText() + '</p>' + otherHtml +
			'<p class="fine">Экономика считается той же детерминированной C++-библиотекой, что войдёт в Unreal-клиент и сервер. Хэш состояния <code data-live="hash"></code>.</p>' +
			'<p class="fine">Фанатский прототип по мотивам Dawn of Titans (NaturalMotion, 2015), не связан с правообладателями. Цифры из вики оригинала помечены в окнах улучшения; остальное — наши оценки.</p>' +
			'<div class="actions"><button class="btn' + (isArmed('reset') ? ' danger' : '') + '" data-act="reset">' + (isArmed('reset') ? 'Точно начать заново?' : 'Начать королевство заново') + '</button>' + closeBtn + '</div>';
		// The hash changes every second, so it is filled in live and the buttons are not rebuilt under the finger.
		return { key: ['menu', clock.scale, cloud.state, isArmed('reset'), isArmed('swap'), other ? other.savedAt : 0].join('|'), html, live(el) { live(el, 'hash', K.st.hash); } };
	});
}
function openWelcome() {
	openSheet(() => ({
		key: 'welcome',
		html: '<h2 id="kSheetTitle">Ваше королевство</h2>' +
			'<p>Город стоит на осколке расколотого мира, парящем в облаках. Замок — сердце королевства: его уровень открывает новые постройки и их уровни.</p>' +
			'<dl class="keys"><dt>Золото и еда</dt><dd>копятся в шахтах и на фермах; нажмите на значок над постройкой, чтобы собрать в хранилища</dd>' +
			'<dt>Строители</dt><dd>каждый ведёт одну стройку; стройки идут по реальному времени, их можно ускорить самоцветами</dd>' +
			'<dt>«Строить»</dt><dd>магазин построек; перетащите новую постройку на свободное место</dd>' +
			'<dt>«Армия»</dt><dd>Казармы обучают отряды за еду, Арсенал поднимает их уровень, Зал титанов и Храм реликвий — ваши титаны; без Лазарета раненые отряды гибнут, как в оригинале</dd>' +
			'<dt>«Атака»</dt><dd>бои за земли вашим титаном и отрядами: набег за добычей или захват земли, которая потом приносит доход</dd>' +
			'<dt>Камни портала</dt><dd>новый каждые 7 минут; набег стоит 3, захват 6</dd>' +
			'<dt>Очки победы и осколки</dt><dd>лес, озеро и гора дают очки победы для недельного сезона с наградами лиг; руины дают осколки, на них возвышают титанов</dd>' +
			'<dt>«Альянс»</dt><dd>с 1 000 очков победы можно вступить в альянс или основать свой; альянсы соревнуются в недельных лигах, а Лагерь альянса принимает подкрепления союзников</dd>' +
			'<dt>Камера</dt><dd>тяните, чтобы двигать; щипок или колесо для зума; зажмите постройку, чтобы переставить</dd></dl>' +
			'<div class="actions"><button class="btn primary" data-act="welcome-ok">Начать</button></div>',
	}));
}

// ---------------------------------------------------------------------------------------------
// Battles for lands
// ---------------------------------------------------------------------------------------------
/** Enemy titan for a land: a roster titan whose rarity fits the land level (none below level 5). */
function enemyTitan(w) {
	const b = K.rules.battles[Math.min(w.lv, K.rules.battles.length) - 1];
	if (!b.titan) return null;
	const band = w.lv <= 6 ? [1, 2] : w.lv <= 8 ? [2, 3] : [3, 4];
	const pool = K.army.titans.filter((t) => t.stars >= band[0] && t.stars <= band[1]);
	const def = pool[(Math.imul(w.id, 2246822519) >>> 0) % pool.length];
	return { def, level: Math.min(b.titan.level, 10 + 10 * def.stars) };
}
/** The battle setup for a land: the player's titan and squads against the land's defenders. */
function landScenario(w, prep) {
	const sc = JSON.parse(JSON.stringify(K.sandbox));
	const b = K.rules.battles[Math.min(w.lv, K.rules.battles.length) - 1];
	const slots = K.rules.battleSlots;
	const att = sc.sides.attacker;
	const def = sc.sides.defender;
	const ty = landType(w.type);
	sc.name = ty.name + ', уровень ' + w.lv;
	if (ty.map) sc.map = ty.map; // forest, lake, mountain and ruins lands have battlefields of their own
	sc.seed = (Math.imul(w.id, 2654435761) >>> 0) % 2147483647;
	// The player's army: melee in front, archers behind, one squad per slot.
	const xs = [120, 96, 144, 72, 168, 48, 192];
	let front = 0;
	let back = 0;
	att.name = 'Ваше войско';
	// The player's squads first (the kingdom reads their survivors in this order), then the allies'.
	const sent = prep.squads.map((t) => [t, K.st.army.lv[t]]).concat(prep.reinf || []);
	att.squads = sent.map(([t, lv]) => {
		const tr = K.army.troops[t];
		const lvl = tr.levels[Math.max(1, Math.min(lv, tr.levels.length)) - 1];
		const ranged = BACK_ROW.has(tr.id);
		const pos = ranged ? [xs[back++ % xs.length], 18 - 6 * Math.floor((back - 1) / xs.length)] : [xs[front++ % xs.length], 34 - 6 * Math.floor((front - 1) / xs.length)];
		return { troop: tr.id, tier: lvl.tier, pos, facingDeg: 0, formation: 'Line' };
	});
	const mine = prep.titan ? K.st.titans.find((t) => t.id === prep.titan) : null;
	if (mine) {
		att.titan = { id: K.army.titans[mine.def].id, level: mine.lv, stars: K.army.titans[mine.def].stars, ascended: !!mine.asc, abilityLevels: [1, 1], pos: [120, 26], facingDeg: 0,
			bonus: { atkPct: Math.floor(mine.bonus[0] / 10), hpPct: Math.floor(mine.bonus[1] / 10), armorPct: Math.floor(mine.bonus[2] / 10),
				critPct: Math.floor(mine.bonus[3] / 10), piercePct: Math.floor(mine.bonus[4] / 10) } };
		// Relics on the titan that strengthen the whole army.
		att.troopBonus = { atkPct: Math.floor(mine.bonus[5] / 10), hpPct: Math.floor(mine.bonus[6] / 10) };
	} else delete att.titan;
	// Spells from the Spell Forge stock: every charge goes along, at its Arcane Tower level.
	const sp = K.st.spells;
	att.spells = prep.spells.map((i) => K.army.spells[i].id);
	att.spellCharges = prep.spells.map((i) => sp.stock[i]);
	att.spellLevels = prep.spells.map((i) => sp.lv[i]);
	def.name = 'Защитники земли';
	def.ai = b.ai;
	// Defenders differ by land type (the original since 1.8; which troops is ours).
	const swap = ty.swap || {};
	def.squads = b.squads.map((t0, i) => {
		const troop = swap[t0] || t0;
		return { troop, tier: b.tier, pos: slots[i % slots.length], facingDeg: 180, formation: CAVALRY.has(troop) ? 'Wedge' : 'Line' };
	});
	const et = enemyTitan(w);
	if (et) def.titan = { id: et.def.id, level: et.level, stars: et.def.stars, abilityLevels: [1, 1], pos: [120, 134], facingDeg: 180 };
	else delete def.titan;
	if (w.lv < 5) def.spells = [];
	return sc;
}
function startAttack(wid, capture) {
	const w = K.st.world.find((x) => x.id === wid);
	if (!w) return;
	if (capture && K.st.lands.length >= K.st.landCap) { toast(errText(E.NoLandSlot), 'bad'); return; }
	if (K.st.res[3] < (capture ? 6 : 3)) { toast(errText(E.NeedStones), 'bad'); return; }
	openPrepare(wid, capture);
}
function launchAttack() {
	const p = K.prep;
	const w = p && K.st.world.find((x) => x.id === p.wid);
	if (!w) return;
	const squads = [];
	p.counts.forEach((n, t) => { for (let i = 0; i < n; i++) squads.push(t); });
	if (!squads.length && !p.titan) { toast('Выберите титана или хотя бы один отряд', 'bad'); return; }
	const spells = p.spells.filter((i) => K.st.spells.stock[i] > 0);
	const code = K.bridge.attackStart(p.wid, p.capture, squads, p.titan, spells, p.reinf);
	if (code !== E.None) { toast(errText(code), 'bad'); refresh(true); return; }
	persist();
	// The allies' squads the kingdom sent along (troop, level), after the player's own.
	const reinf = K.bridge.state().ally.pend;
	const sc = landScenario(w, { squads, titan: p.titan, spells, reinf });
	const path = 'scenarios/land_battle.json';
	K.app.sim.addFile(path, JSON.stringify(sc));
	closeSheet();
	select(null);
	K.app.startLandBattle(path, sc.sides.defender.ai, { kind: 'land', worldId: p.wid, capture: p.capture, type: w.type, level: w.lv, squads: squads.length,
		charges: sc.sides.attacker.spellCharges.slice() });
}
/** Each attacking squad's surviving share (%) in the order they were sent, read from the finished simulation. */
function survivorsFromBattle(count) {
	const out = [];
	try {
		const st = K.app.sim.staticInfo();
		const fr = K.app.sim.frame();
		st.squads.forEach((q, i) => {
			if (q.side === 0 && !q.sum) out.push(Math.round((100 * (fr.sq[i] ? fr.sq[i].a : 0)) / Math.max(1, q.start)));
		});
	} catch (e) {
		console.warn('Battle survivors unavailable:', e);
	}
	while (out.length < count) out.push(0);
	return out.slice(0, count);
}
function onBattleEnd(ctx, stars, retreat) {
	if (!K.ready) return;
	let r = null;
	if (ctx && ctx.kind === 'land') {
		const survivors = survivorsFromBattle(ctx.squads || 0);
		// Casts used = charges taken minus charges left in the finished battle.
		let left = [];
		try { left = K.app.sim.frame().sides[0].ch || []; } catch (e) { left = []; }
		const used = (ctx.charges || []).map((c, i) => Math.max(0, c - (left[i] >= 0 ? left[i] : c)));
		r = K.bridge.attackFinish(ctx.worldId, ctx.capture, stars, survivors, used);
		if (r.err === E.None) {
			const res = landType(ctx.type).resource;
			const vpText = r.vp > 0 && res !== 'vp' ? ', +' + fmt(r.vp) + ' ОП за победу' : '';
			const what = { gold: 'золото', food: 'еду', vp: 'очки победы', shards: 'осколки' }[res];
			if (stars > 0 && ctx.capture) toast('Земля захвачена! Теперь она приносит ' + what + vpText, 'gold');
			else if (stars > 0) toast('Набег удался: +' + resAmount(res, res === 'vp' ? r.vp : res === 'shards' ? r.shards : res === 'food' ? r.food : r.gold) + vpText, 'gold');
			else toast((retreat ? 'Отступление' : 'Поражение') + ': камни портала потрачены, земля осталась у врага', 'bad');
			// One message for the army (phones show two at a time).
			const parts = [];
			if (r.lost || r.back) parts.push('вернулось отрядов ' + r.back + ', погибло ' + r.lost + (r.lost && K.st && K.st.army.surv >= 100 ? ' (без Лазарета раненые гибнут)' : ''));
			const t = r.titan && K.bridge.state().titans.find((x) => x.id === r.titan);
			if (t) parts.push(K.army.titans[t.def].name + ' +' + fmt(r.xp) + ' опыта' + (r.levels ? ', уровень ' + t.lv : ''));
			const cast = used.reduce((x, y) => x + y, 0);
			if (cast) parts.push('заклинаний потрачено ' + cast);
			if (r.broken) parts.push('сломано реликвий ' + r.broken);
			const got = (r.relics || []).map((rid) => K.bridge.state().relics.find((x) => x.id === rid)).filter(Boolean);
			if (got.length) parts.push('реликвии: ' + got.map((x) => relicName(x) + ' ' + '★'.repeat(x.r)).join(', '));
			if (r.relicsLost) parts.push('склад реликвий полон, пропало ' + r.relicsLost);
			if (r.reinf) parts.push('подкрепления альянса (' + r.reinf + ') ушли после боя');
			if (parts.length) toast(parts.join(' · ').replace(/^./, (c) => c.toUpperCase()), r.lost ? 'bad' : 'gold');
		}
		persist();
	}
	refresh(true);
	if (!r || r.err !== E.None || stars <= 0) return;
	if (r.land) {
		const v = world.lands.get(r.land);
		if (v) { focusOn(v.base.x * 0.8, v.base.z * 0.8, 125); select({ kind: 'land', id: r.land }); }
	} else {
		const c = K.st.b.find((b) => b.def === K.rules.castleIndex);
		const v = c && world.views.get(c.id);
		const res = landType(ctx.type).resource;
		const got = { gold: r.gold, food: r.food, vp: r.vp, shards: r.shards }[res];
		if (v && got > 0) floatText(v.anchor, '+' + fmt(got), RES_COLOR[res], res);
	}
}

// ---------------------------------------------------------------------------------------------
// Army and titans: Barracks, Armoury, Army Camp, Hall of Titans, Relic Temple, battle preparation
// ---------------------------------------------------------------------------------------------
const TROOP_NOTE = {
	'troop.militia': 'сильно против лучников, слабо против копейщиков',
	'troop.spearmen': 'сильны против ополчения и зверей',
	'troop.archers': 'бьют издалека, в ближнем бою слабы',
	'troop.swordsmen': 'щиты держат стрелы, сильны против лучников и копейщиков',
	'troop.grenadiers': 'огненные горшки бьют по площади и поджигают землю; рассыпной строй их ослабляет',
	'troop.outriders': 'быстрые, охотятся на стрелков и жрецов, но гибнут на копьях',
	'troop.priests': 'не сражаются: лечат, ободряют и укрепляют броню отрядов рядом',
	'troop.chainbreakers': 'гарпуны замедляют титана, пять цепей сковывают его',
	'troop.knights': 'тяжёлый таранный удар и топот по лёгкой пехоте',
	'troop.pikemen': 'длинные пики держат конницу и зверей, ледяной удар замедляет врага',
	'troop.goliaths': 'трое великанов: удар дубиной бьёт по площади и валит пехоту с ног',
	'troop.panthers': 'быстрые звери, рвут стрелков, яд жжёт после укуса; гибнут на пиках',
	'troop.ballista': 'три машины: болт пробивает строй и пригвождает титана, но расчёт беззащитен перед конницей',
};
/** Troop indices a council sends (army.json "council"), in content order. */
function councilTroops(councilId) {
	const out = [];
	K.army.troops.forEach((tr, t) => { if (tr.council === councilId) out.push(t); });
	return out;
}
/** Icon tile: a glyph from icons.js on a tile coloured by `tile` (troop, spell, relic r1..r5, bld <category>, titan, altar). */
const icoHtml = (name, tile) => '<div class="ico ' + tile + '">' + DotIcons.svg(name) + '</div>';
const mini = (name) => DotIcons.svg(name, 'mini');
const troopIcon = (t) => icoHtml(K.army.troops[t].id, 'troop');
const spellIcon = (i) => icoHtml(K.army.spells[i].id, 'spell');
function buildingIcon(def) {
	const d = defOf(def);
	return icoHtml('building.' + d.id, 'bld ' + d.category);
}
function buildingIconById(id) { return buildingIcon(K.defs.findIndex((d) => d.id === id)); }
function titanGlyph(t) {
	const n = 'class.' + K.army.titans[t.def].class;
	return DotIcons.has(n) ? n : 'class.Champion';
}
/** Titan tile: the titan's own head and shoulders from titans.js, or its class glyph. */
function titanIcon(t) {
	const id = K.army.titans[t.def].id;
	return typeof DotTitans !== 'undefined' && DotTitans.has(id) ? '<div class="ico titan">' + DotTitans.svg(id, { view: 'bust' }) + '</div>' : icoHtml(titanGlyph(t), 'titan');
}
/** Animated titan figure with buttons that play its attack and both abilities. */
function titanStage(d, abil) {
	if (typeof DotTitans === 'undefined' || !DotTitans.has(d.id)) return '';
	const names = ['Атака'].concat(abil.length === 2 ? abil : DotTitans.info(d.id).abil);
	return '<div class="titanStage">' + DotTitans.svg(d.id) + '<div class="clips">' +
		['atk', 'a1', 'a2'].map((c, i) => '<button class="btn" data-act="titan-play" data-clip="' + c + '">' + esc(names[i]) + '</button>').join('') + '</div></div>';
}
function relicIcon(r) {
	const broken = r.dur <= 0;
	return '<div class="ico relic r' + r.r + (broken ? ' broken' : '') + '">' + DotIcons.svg('relic.' + K.army.relics.kinds[r.kind].id) + (broken ? DotIcons.svg('ui.crack', 'crack') : '') + '</div>';
}
/** Icon tile of a land type (tile colour by its resource) with the level badge. */
function landGlyph(type) {
	const glyph = { gold_land: 'land.gold', farm_land: 'land.food' }[landType(type).id] || 'land.' + landType(type).id;
	return DotIcons.has(glyph) ? glyph : 'land.gold';
}
function landIcon(type, lv) {
	return '<div class="ico ' + landType(type).resource + ' land">' + DotIcons.svg(landGlyph(type)) + '<i class="lvb">' + lv + '</i></div>';
}
/** Sheet heading with an icon tile in front. */
const sheetTitle = (icon, text) => '<h2 id="kSheetTitle" class="ti">' + icon + '<span>' + text + '</span></h2>';
const starsHtml = (n) => '<span class="stars" aria-label="' + n + ' из 4">' + '★'.repeat(n) + '<i>' + '★'.repeat(4 - n) + '</i></span>';
const titanAbil = (id) => (K.app && K.app.titanAbilities ? K.app.titanAbilities(id) : []);
function titanName(t) { return K.army.titans[t.def].name; }
function titanTotalXp(t) {
	let x = t.xp;
	for (let l = 1; l < t.lv; l++) x += K.army.titanXp.next[Math.min(l, K.army.titanXp.next.length) - 1];
	return x;
}
function liveTitan(el, t) {
	const now = nowF();
	live(el, 'tr' + t.id, t.rec > now ? 'отдыхает ' + fmtClock(t.rec - now) : '');
}
function findB(id) { return K.st.b.find((b) => defOf(b.def).id === id && b.lv > 0); }

function openArmy() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const a = K.st.army;
		const rows = K.army.troops.map((tr, t) => '<div class="troopRow">' + troopIcon(t) + '<div class="info"><b>' + esc(tr.name) + ' · ур. ' + a.lv[t] + '</b><span>' +
			esc(TROOP_NOTE[tr.id] || '') + '</span></div><b class="count">×' + a.n[t] + '</b></div>');
		const titans = K.st.titans.slice().sort((x, y) => K.army.titans[y.def].stars * 100 + y.lv - (K.army.titans[x.def].stars * 100 + x.lv)).slice(0, 4)
			.map((t) => '<span class="chip">' + mini(titanGlyph(t)) + esc(titanName(t)) + ' ' + starsHtml(K.army.titans[t.def].stars) + ' ур. ' + t.lv + '</span>').join('');
		const html = sheetTitle(icoHtml('ui.army', 'bld troops'), 'Армия') +
			'<div class="costline"><span>В лагере</span><b>' + a.total + ' / ' + a.cap + '</b><span>В бой</span><b>до ' + a.slots + ' отрядов</b><span>Очередь</span><b>' + a.queue.length + ' / ' + a.qcap + '</b></div>' +
			rows.join('') +
			'<p class="fine">' + (a.surv >= 100 ? 'Лазарета нет: каждый раненый отряд после боя погибает, как в оригинале. Лазарет (Замок 7) возвращает отряды, сохранившие ≥ 85% бойцов.'
				: 'Лазарет возвращает отряды, сохранившие не меньше ' + a.surv + '% бойцов; остальные погибают.') + '</p>' +
			'<div class="actions"><button class="btn primary" data-act="barracks">Казармы</button><button class="btn" data-act="armoury">Арсенал</button></div>' +
			'<h3>Заклинания: ' + K.st.spells.total + (K.st.spells.cap ? ' из ' + K.st.spells.cap : '') + '</h3><div class="toggles">' +
			K.army.spells.map((d, i) => (K.st.spells.stock[i] ? '<span class="chip">' + mini(d.id) + esc(d.name) + ' ×' + K.st.spells.stock[i] + '</span>' : '')).join('') + '</div>' +
			'<div class="actions"><button class="btn" data-act="forge">Кузня заклинаний</button><button class="btn" data-act="arcane">Магическая башня</button></div>' +
			'<h3>Титаны: ' + K.st.titans.length + '</h3><div class="toggles">' + titans + '</div>' +
			'<div class="actions"><button class="btn" data-act="titans">Зал титанов</button><button class="btn" data-act="relics">Храм реликвий</button>' + closeBtn + '</div>';
		return { key: ['army', a.n.join(), a.lv.join(), a.total, a.cap, a.queue.length, K.st.spells.stock.join(), K.st.titans.map((t) => t.id + ':' + t.lv).join()].join('|'), html };
	});
}
/** One Barracks row: troop, level, time, note and the train button (also used by council screens). */
function trainRow(t) {
	const a = K.st.army;
	const tr = K.army.troops[t];
	const ck = a.train[t];
	const ok = ck.err === E.None;
	return '<div class="troopRow">' + troopIcon(t) + '<div class="info"><b>' + esc(tr.name) + ' · ур. ' + a.lv[t] + '</b><span>в лагере ' + a.n[t] + ' · ' + fmtDur(ck.t) + ' · ' +
		esc(TROOP_NOTE[tr.id] || '') + '</span>' + (ok ? '' : '<span class="why">' + esc(errText(ck.err, ck)) + '</span>') + '</div>' +
		'<button class="btn' + (ok ? ' primary' : '') + '" data-act="train" data-id="' + t + '"' + (ok ? '' : ' disabled') + '>Обучить ' + costHtml(ck.cost, K.st.res) + '</button></div>';
}
/** Envoy council screen: the troops it sends, their level cap and what the next council level opens. */
function openCouncil(def) {
	cancelPlace();
	select(null);
	openSheet(() => {
		const a = K.st.army;
		const d = defOf(def);
		const b = K.st.b.find((x) => x.def === def);
		const lv = b ? b.lv : 0;
		const cap = lv > 0 ? levelDef(d, lv).maxTroopLevel : 1;
		const troops = councilTroops(d.id);
		const next = d.levels[lv];
		const html = sheetTitle(buildingIcon(def), esc(d.name)) +
			'<p class="fine">' + esc(d.description) + '</p>' +
			'<div class="costline"><span>Уровень совета</span><b>' + lv + ' / ' + d.levels.length + '</b><span>Предел уровня войск</span><b>' + cap + '</b>' +
			(next ? '<span>На ур. ' + (lv + 1) + '</span><b>' + next.maxTroopLevel + '</b>' : '') + '</div>' +
			'<p class="fine">Войска совета обучаются в Казармах, как в оригинале. Их уровень не выше предела совета и Арсенала (сейчас ' + Math.min(cap, a.maxLv) + ').</p>' +
			troops.map(trainRow).join('') +
			'<div class="actions"><button class="btn primary" data-act="barracks">Казармы</button><button class="btn" data-act="armoury">Арсенал</button>' + closeBtn + '</div>';
		return { key: ['council', def, lv, a.n.join(), a.lv.join(), a.lvCap.join(), a.train.map((c) => c.err).join(), K.st.res[1] >> 4].join('|'), html };
	});
}
function openBarracks() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const a = K.st.army;
		const own = [];
		const sent = [];
		K.army.troops.forEach((tr, t) => (tr.council ? sent : own).push(trainRow(t)));
		const queue = a.queue.map((t, i) => '<div class="qitem"><span>' + (i + 1) + '. ' + mini(K.army.troops[t].id) + esc(K.army.troops[t].name) + '</span><b data-live="q' + i + '"></b>' +
			'<button class="btn x" data-act="cancel-train" data-id="' + i + '" aria-label="Отменить">✕</button></div>');
		const armed = isArmed('tskip');
		const html = sheetTitle(buildingIconById('barracks'), 'Казармы') +
			'<p class="fine">Отряды обучаются по одному за еду, как в оригинале, и встают в Лагерь армии. Очередь растёт с уровнем Казарм.</p>' +
			'<div class="costline"><span>В лагере</span><b>' + a.total + ' / ' + a.cap + '</b><span>Очередь</span><b>' + a.queue.length + ' / ' + a.qcap + '</b><span>Еда</span><b>' + iconSvg('food') + fmtShort(K.st.res[1]) + '</b></div>' +
			own.join('') + '<h3>Войска советов</h3><p class="fine">Их присылают Советы посланников (вкладка «Советы» в строительстве); обучают их здесь же.</p>' + sent.join('') +
			(a.queue.length ? '<h3>Очередь</h3>' + queue.join('') : '') +
			(a.qb ? '<p class="note">Лагерь армии полон: следующий отряд ждёт места.</p>' : '') +
			'<div class="actions">' + (a.tskip > 0 ? '<button class="btn" data-act="skip-train" data-gems="' + a.tskip + '">' + (armed ? 'Подтвердить ' : 'Завершить сразу ') + gemSpan(a.tskip) + '</button>' : '') + closeBtn + '</div>';
		return {
			key: ['barracks', a.n.join(), a.queue.join(), a.qb, a.train.map((c) => c.err).join(), armed, K.st.res[1] >> 4].join('|'), html,
			live(el) {
				const aa = K.st.army;
				const now = nowF();
				aa.queue.forEach((t, i) => live(el, 'q' + i, i === 0 ? (aa.qb ? 'ждёт места' : fmtClock(aa.qf - now)) : fmtDur(K.army.troops[t].levels[aa.lv[t] - 1].trainSec)));
			},
		};
	});
}
function openArmoury() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const a = K.st.army;
		const rs = a.rs;
		const rows = K.army.troops.map((tr, t) => {
			const ck = a.upg[t];
			const armed = isArmed('ut:' + t);
			let btn;
			if (ck.err === E.None) btn = '<button class="btn primary" data-act="upgrade-troop" data-id="' + t + '">Улучшить ' + costHtml(ck.cost, K.st.res) + '</button>';
			else if (ck.err === E.NotEnough && ck.g > 0) btn = '<button class="btn primary" data-act="upgrade-troop-gems" data-id="' + t + '" data-gems="' + ck.g + '">' + (armed ? 'Подтвердить ' : 'Докупить еду ') + gemSpan(ck.g) + '</button>';
			else btn = '<button class="btn" disabled>' + (ck.err === E.MaxLevel ? 'Макс. уровень' : 'Улучшить') + '</button>';
			const lv = tr.levels[a.lv[t] - 1];
			return '<div class="troopRow">' + troopIcon(t) + '<div class="info"><b>' + esc(tr.name) + ' · ур. ' + a.lv[t] + ' из ' + tr.levels.length + '</b>' +
				'<span>обучение ' + fmt(lv.cost) + ' еды · ' + (ck.t ? 'улучшение ' + fmtDur(ck.t) : '') + (lv.src === 'wiki' ? ' · цифры из вики' : ' · оценка') +
				(tr.council && a.lvCap[t] < a.maxLv ? ' · предел ур. ' + a.lvCap[t] + ' задаёт совет' : '') + '</span>' +
				(ck.err !== E.None && ck.err !== E.NotEnough ? '<span class="why">' + esc(errText(ck.err, ck)) + '</span>' : '') + '</div>' + btn + '</div>';
		});
		const html = sheetTitle(buildingIconById('armoury'), 'Арсенал') +
			'<p class="fine">Улучшение поднимает уровень войска: все его отряды становятся сильнее, новые стоят дороже. Предел уровня задаёт Арсенал (сейчас ' + a.maxLv + '). Одно улучшение за раз.</p>' +
			(rs.t >= 0 ? '<div class="uprow"><span>Улучшается: ' + mini(K.army.troops[rs.t].id) + esc(K.army.troops[rs.t].name) + ' → ур. ' + (a.lv[rs.t] + 1) + '</span><b data-live="rleft"></b></div><div class="progress"><i data-live="rbar"></i></div>' +
				'<div class="actions"><button class="btn" data-act="skip-research" data-gems="' + rs.g + '">' + (isArmed('rskip') ? 'Подтвердить ' : 'Завершить сразу ') + '<span data-live="rgems"></span></button></div>' : '') +
			rows.join('') + '<div class="actions">' + closeBtn + '</div>';
		return {
			key: ['armoury', a.lv.join(), rs.t, a.upg.map((c) => c.err + ':' + c.g).join(), K.army.troops.map((x, t) => isArmed('ut:' + t)).join(), isArmed('rskip')].join('|'), html,
			live(el) {
				const r = K.st.army.rs;
				if (r.t < 0) return;
				const left = r.f - nowF();
				live(el, 'rleft', fmtDur(left));
				liveBar(el, 'rbar', 1 - left / Math.max(1, r.f - r.s));
				const n = el.querySelector('[data-live="rgems"]');
				if (n) n.innerHTML = gemSpan(r.g);
			},
		};
	});
}
function fusionPreview(target, picks) {
	const f = K.army.fusion;
	let xp = 0;
	let food = 0;
	for (const id of picks) {
		const t = K.st.titans.find((x) => x.id === id);
		if (!t) continue;
		const st = K.army.titans[t.def].stars;
		xp += f.xpPerStarSq * st * st + Math.floor((titanTotalXp(t) * f.keepXpPct) / 100);
		food += f.foodPerStar * st;
	}
	return { xp, food };
}
function openTitans() {
	cancelPlace();
	select(null);
	K.fuse = null;
	openSheet(() => {
		const st = K.st;
		const fz = K.fuse;
		const canFuse = st.castle >= st.fuseCastle;
		const list = st.titans.slice().sort((x, y) => K.army.titans[y.def].stars * 100 + y.lv - (K.army.titans[x.def].stars * 100 + x.lv));
		const cards = list.map((t) => {
			const d = K.army.titans[t.def];
			const target = fz && fz.target === t.id;
			const picked = fz && fz.picks.includes(t.id);
			const resting = t.rec > nowF();
			let act = '';
			if (fz) act = target ? '<span class="tag">цель слияния</span>' : '<button class="btn' + (picked ? ' primary' : '') + '" data-act="fuse-pick" data-id="' + t.id + '">' + (picked ? 'Выбран' : 'В жертву') + '</button>';
			else {
				act += '<button class="btn" data-act="titan-relics" data-id="' + t.id + '">Титан и реликвии ' + t.relics.length + '/' + t.slots + '</button>';
				if (canFuse && t.lv < t.max) act += '<button class="btn" data-act="fuse-start" data-id="' + t.id + '">Слияние</button>';
				if (t.lv >= t.max && !t.asc) {
					const ck = t.ack;
					const gems = ck.err === E.NotEnough && ck.g > 0 ? ck.g : 0;
					act += ck.err === E.None ? '<button class="btn primary" data-act="ascend" data-id="' + t.id + '">Возвысить · ' + costHtml(ck.cost, st.res) + '</button>'
						: gems ? '<button class="btn" data-act="ascend-gems" data-id="' + t.id + '" data-gems="' + gems + '">' + (isArmed('asc:' + t.id) ? 'Подтвердить ' : 'Возвысить, докупив еду ') + gemSpan(gems) + '</button>'
						: '<button class="btn" disabled>Возвышение · ' + costHtml(ck.cost, st.res) + '</button>';
				}
				if (resting) act += '<button class="btn" data-act="skip-titan" data-id="' + t.id + '" data-gems="' + t.skip + '">' + (isArmed('tr:' + t.id) ? 'Подтвердить ' : 'Разбудить ') + gemSpan(t.skip) + '</button>';
			}
			return '<div class="titanCard' + (target ? ' on' : '') + '"><div class="head" data-act="titan-relics" data-id="' + t.id + '">' + titanIcon(t) + '<div class="nm"><b>' + esc(d.name) + '</b>' + starsHtml(d.stars) + '</div></div>' +
				'<span class="meta">' + esc(d.classRu) + ' · ур. ' + t.lv + ' из ' + t.max + (t.asc ? ' · возвышен' : '') + '</span>' +
				'<div class="progress"><i style="width:' + (t.lv >= t.max ? 100 : Math.round((100 * t.xp) / Math.max(1, t.next))) + '%"></i></div>' +
				'<span class="meta">' + esc(titanAbil(d.id).join(' · ')) + '</span>' +
				'<span class="meta" data-live="tr' + t.id + '"></span><div class="row">' + act + '</div></div>';
		});
		// The rest of the roster, greyed out: the original showed every titan you could still summon.
		const locked = K.army.titans.map((d, i) => ({ d, i })).filter((x) => !st.titans.some((t) => t.def === x.i))
			.sort((x, y) => x.d.stars - y.d.stars).map(({ d, i }) =>
				'<div class="titanCard locked"><div class="head">' + titanIcon({ def: i }) + '<div class="nm"><b>' + esc(d.name) + '</b>' + starsHtml(d.stars) + '</div></div>' +
				'<span class="meta">' + esc(d.classRu) + ' · ещё не призван</span>' +
				'<span class="meta">' + esc(titanAbil(d.id).join(' · ')) + '</span>' +
				'<div class="row"><button class="btn" data-act="relics">Призвать на алтаре</button></div></div>');
		let head = '';
		if (fz) {
			const pv = fusionPreview(fz.target, fz.picks);
			const tt = st.titans.find((x) => x.id === fz.target);
			head = '<div class="costline"><span>Слияние в</span><b>' + esc(tt ? titanName(tt) : '') + '</b><span>жертв</span><b>' + fz.picks.length + '</b><span>опыт</span><b>+' + fmt(pv.xp) + '</b>' +
				costHtml([0, pv.food, 0, 0], st.res) + '</div><div class="actions"><button class="btn primary" data-act="fuse-go"' + (fz.picks.length && st.res[1] >= pv.food ? '' : ' disabled') + '>' +
				(st.res[1] < pv.food ? 'Не хватает еды' : isArmed('fuse') ? 'Точно слить?' : 'Слить') + '</button><button class="btn" data-act="fuse-cancel">Отмена</button></div>';
		}
		const html = sheetTitle(buildingIconById('hall_of_titans'), 'Зал титанов') +
			'<p class="fine">Титанов: ' + st.titans.length + ' из ' + st.tcap + '. Редкость от одной до четырёх звёзд задаёт предел уровня: 20, 30, 40 и 50, как в оригинале версии 1.8. ' +
			'Опыт титаны получают в боях; после боя титаны выше 10-го уровня отдыхают. ' +
			'Титана на пределе уровня можно возвысить за осколки из руин и еду: ещё 10 уровней и слот реликвии, как в оригинале. ' + (canFuse ? 'Слияние за еду переносит силу ненужных титанов в выбранного, как в оригинале.' : 'Слияние откроется на Замке ' + st.fuseCastle + '.') + '</p>' +
			head + '<div class="shopGrid">' + cards.join('') + '</div>' +
			(locked.length && !fz ? '<h3>Ещё не призваны</h3><p class="fine">Новых титанов дают алтари Храма реликвий: бесплатный раз в сутки, серебряный и золотой за самоцветы. Попробовать любого титана можно в меню боя, раздел «Ваш титан».</p><div class="shopGrid">' + locked.join('') + '</div>' : '') +
			'<div class="actions"><button class="btn" data-act="relic-inv">Реликвии (' + st.relics.length + ')</button><button class="btn" data-act="relics">Храм реликвий</button>' + closeBtn + '</div>';
		return {
			key: ['titans', st.titans.map((t) => t.id + ':' + t.lv + ':' + t.xp + ':' + (t.rec > nowF()) + ':' + isArmed('tr:' + t.id) + ':' + t.relics.length + ':' + t.asc + ':' + t.ack.err + ':' + isArmed('asc:' + t.id)).join(), st.res[4], st.relics.length, fz ? fz.target + '>' + fz.picks.join('.') : '', isArmed('fuse'), st.res[0] >> 8, st.res[1] >> 8].join('|'), html,
			live(el) { for (const t of K.st.titans) liveTitan(el, t); },
		};
	});
}
function openRelics() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const cards = K.army.altars.map((a, i) => {
			const ck = st.altars[i];
			const stars = (ws) => ws.map((w, k) => (w ? '★'.repeat(k + 1) + ' ' + w + '%' : '')).filter(Boolean).join(' · ');
			const R = K.army.relics;
			const odds = a.relics ? 'реликвия: ' + stars(a.weights) : a.cooldownSec > 0 ? 'реликвия ' + R.dailyRelicPct + '% (' + stars(R.dailyRarity) + '), иначе титан: ' + stars(a.weights) : 'титан: ' + stars(a.weights);
			let btn;
			if (ck.err === E.None) btn = a.gems ? '<button class="btn primary" data-act="altar" data-id="' + i + '" data-gems="' + a.gems + '">' + (isArmed('altar:' + i) ? 'Подтвердить ' : 'Открыть ') + gemSpan(a.gems) + '</button>'
				: '<button class="btn primary" data-act="altar" data-id="' + i + '">Открыть бесплатно</button>';
			else if (ck.err === E.NotReady) btn = '<button class="btn" disabled>Через <span data-live="relic' + i + '"></span></button>';
			else btn = '<button class="btn" disabled>' + esc(errText(ck.err, ck)) + '</button>';
			return '<div class="landCard">' + icoHtml('altar.' + a.id, 'altar ' + a.id) + '<div class="info"><b>' + esc(a.name) + '</b><span>' + odds + '</span></div><div class="row">' + btn + '</div></div>';
		});
		const last = K.lastSummon ? st.titans.find((t) => t.id === K.lastSummon) : null;
		const lastRelic = K.lastRelic ? st.relics.find((r) => r.id === K.lastRelic) : null;
		const html = sheetTitle(buildingIconById('relic_temple'), 'Храм реликвий') +
			'<p class="fine">Как в оригинале, раз в день здесь бесплатная награда: реликвия или титан. Алтари за 1 000 и 4 000 самоцветов призывают редких титанов, четыре звезды только там. ' +
			'Алтарь силы за 1 000 самоцветов даёт реликвию от ★2 до ★5, как Altar of Power в оригинале. Ещё реликвии приносят победы в боях: по одной за звезду, на дальних землях чаще редкие. Шансы — наша оценка.</p>' +
			(last ? '<div class="costline"><span>Получен титан</span><b>' + mini(titanGlyph(last)) + esc(titanName(last)) + '</b>' + starsHtml(K.army.titans[last.def].stars) + '<span>' + esc(K.army.titans[last.def].classRu) + '</span></div>' : '') +
			(lastRelic ? '<div class="costline"><span>Получена реликвия</span><b>' + mini('relic.' + K.army.relics.kinds[lastRelic.kind].id) + esc(relicName(lastRelic)) + '</b>' + starsHtml5(lastRelic.r) + '<span>' + esc(relicStats(lastRelic)) + '</span></div>' : '') +
			cards.join('') + '<div class="actions"><button class="btn" data-act="titans">Зал титанов</button>' + closeBtn + '</div>';
		return {
			key: ['relics', st.altars.map((c) => c.err).join(), K.lastSummon, K.lastRelic, K.army.altars.map((a, i) => isArmed('altar:' + i)).join(), st.titans.length].join('|'), html,
			live(el) { K.st.altars.forEach((c, i) => { if (c.err === E.NotReady) live(el, 'relic' + i, fmtDur(K.st.relicAt - nowF())); }); },
		};
	});
}
// Relic helpers. As in the original: rarities R1..R5; combat relics are worn by titans and wear out in
// battles, economy relics sit on a building and work for a time; one to three effects, the locked ones
// open through repair, and a repair can also raise an effect's level.
function relicKind(r) { return K.army.relics.kinds[r.kind]; }
const isEconomy = (r) => relicKind(r).type === 'economy';
function relicName(r) { return relicKind(r).name; }
function effectDef(r, i) { const id = relicKind(r).effects[i]; return K.army.relics.effects.find((e) => e.id === id); }
/** Tenths of a percent as text: 92 → "9,2". */
const fmtPm = (v) => (v % 10 ? (v / 10).toFixed(1).replace('.', ',') : String(v / 10));
// These effects shorten a time; the rest add to a value.
const LESS_TIME = ['trainPct', 'craftPct', 'researchPct', 'restPct'];
function effectText(r, i) { const e = effectDef(r, i); return (LESS_TIME.includes(e.stat) ? '−' : '+') + fmtPm(r.v[i]) + '% ' + e.name; }
/** Open effects as text (toasts, short lines). */
function relicStats(r) { return r.lv.map((l, i) => (l ? effectText(r, i) : '')).filter(Boolean).join(', '); }
/** Effects with their levels; locked ones greyed out. */
function relicEffects(r) {
	const max = K.army.relics.levels;
	return r.lv.map((l, i) => (l ? '<span class="eff">' + esc(effectText(r, i)) + ' <i>ур. ' + l + '/' + max + '</i></span>'
		: '<span class="eff locked">закрыто: ' + esc(effectDef(r, i).name) + '</span>')).join('');
}
const starsHtml5 = (n) => '<span class="stars">' + '★'.repeat(n) + '<i>' + '★'.repeat(5 - n) + '</i></span>';
/** An economy relic at work: its time runs, the sheet ticks it in place. */
const liveRelic = (r) => isEconomy(r) && r.b && r.dur > 0;
/** Sheet key part of a relic; working economy relics tick in place instead of re-rendering. */
const relicKey = (r) => r.id + ':' + r.on + ':' + r.b + ':' + (liveRelic(r) ? 'live' : r.dur) + ':' + r.lv.join('.');
function relicDur(r) {
	if (!isEconomy(r)) return r.dur > 0 ? 'прочность ' + r.dur + ' из ' + r.max + ' ' + plural(r.max, 'боя', 'боёв', 'боёв') : 'сломана';
	if (r.dur <= 0) return 'время вышло';
	return liveRelic(r) ? 'работает ещё <span data-live="rl' + r.id + '">' + fmtDur(r.dur) + '</span>' : 'время ' + fmtDur(r.dur) + ' из ' + fmtDur(r.max);
}
function relicPlace(r) {
	const t = r.on ? K.st.titans.find((x) => x.id === r.on) : null;
	if (t) return ' · на ' + esc(titanName(t));
	const b = r.b ? K.st.b.find((x) => x.id === r.b) : null;
	return b ? ' · ' + esc(defOf(b.def).name) : '';
}
function relicRow(r, buttons) {
	return '<div class="troopRow">' + relicIcon(r) + '<div class="info"><b>' + esc(relicName(r)) + ' ' + starsHtml5(r.r) + '</b>' +
		'<span class="effs">' + relicEffects(r) + '</span><span>' + relicDur(r) + relicPlace(r) + '</span>' +
		'<div class="progress thin"><i' + (liveRelic(r) ? ' data-live="rlb' + r.id + '"' : '') + ' style="width:' + Math.round((100 * Math.max(0, r.dur)) / Math.max(1, r.max)) + '%"></i></div></div>' +
		'<div class="row">' + buttons + '</div></div>';
}
/** Ticks the working economy relics shown in a sheet. */
function liveRelics(el) {
	const now = nowF();
	for (const r of K.st.relics) {
		if (!liveRelic(r)) continue;
		const left = K.st.now + r.dur - now;
		live(el, 'rl' + r.id, fmtDur(left));
		liveBar(el, 'rlb' + r.id, left / Math.max(1, r.max));
	}
}
const buildingRelic = (bid) => K.st.relics.find((r) => r.b === bid) || null;
/** A building relic's bonus to one stat, tenths of a percent (0 when its time ran out). */
function relicBonus(r, stat) { return r && r.dur > 0 ? r.lv.reduce((s, l, i) => s + (l && effectDef(r, i).stat === stat ? r.v[i] : 0), 0) : 0; }
/** Economy relic kinds that fit a building. */
const relicKindsFor = (defId) => K.army.relics.kinds.filter((k) => k.type === 'economy' && (k.buildings || []).includes(defId));
function relicTabHas(r) { const t = K.relicTab || 'all'; return t === 'all' || (t === 'building') === isEconomy(r); }

function openForge() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const sp = K.st.spells;
		const rows = K.army.spells.map((d, i) => {
			const ck = sp.craft[i];
			const ok = ck.err === E.None;
			return '<div class="troopRow">' + spellIcon(i) + '<div class="info"><b>' + esc(d.name) + ' · ур. ' + sp.lv[i] + '</b><span>в запасе ' + sp.stock[i] + ' · ' + fmtDur(ck.t) + '</span>' +
				(ok || ck.err === E.NotEnough ? '' : '<span class="why">' + esc(errText(ck.err, ck, 'forge')) + '</span>') + '</div>' +
				'<button class="btn' + (ok ? ' primary' : '') + '" data-act="craft" data-id="' + i + '"' + (ok ? '' : ' disabled') + '>Создать ' + costHtml(ck.cost, K.st.res) + '</button></div>';
		});
		const queue = sp.queue.map((i, k) => '<div class="qitem"><span>' + (k + 1) + '. ' + mini(K.army.spells[i].id) + esc(K.army.spells[i].name) + '</span><b data-live="c' + k + '"></b>' +
			'<button class="btn x" data-act="cancel-craft" data-id="' + k + '" aria-label="Отменить">✕</button></div>');
		const html = sheetTitle(buildingIconById('spell_forge'), 'Кузня заклинаний') +
			'<p class="fine">Как в оригинале, Кузня создаёт заклинания в запас, а каждое применение в бою тратит одно. Уровень Кузни открывает новые заклинания, удлиняет очередь и увеличивает запас. Цены — наша оценка.</p>' +
			(sp.cap ? '<div class="costline"><span>Запас</span><b>' + sp.total + ' / ' + sp.cap + '</b><span>Очередь</span><b>' + sp.queue.length + ' / ' + sp.qcap + '</b></div>' : '<p class="note">Кузня заклинаний ещё не построена (Замок 5).</p>') +
			rows.join('') + (queue.length ? '<h3>Очередь</h3>' + queue.join('') : '') + (sp.qb ? '<p class="note">Запас полон: следующее заклинание ждёт места.</p>' : '') +
			'<div class="actions">' + (sp.skip > 0 ? '<button class="btn" data-act="skip-craft" data-gems="' + sp.skip + '">' + (isArmed('cskip') ? 'Подтвердить ' : 'Завершить сразу ') + gemSpan(sp.skip) + '</button>' : '') +
			'<button class="btn" data-act="arcane">Магическая башня</button>' + closeBtn + '</div>';
		return {
			key: ['forge', sp.stock.join(), sp.queue.join(), sp.qb, sp.craft.map((c) => c.err).join(), isArmed('cskip'), K.st.res[0] >> 6].join('|'), html,
			live(el) {
				const s2 = K.st.spells;
				const now = nowF();
				s2.queue.forEach((i, k) => live(el, 'c' + k, k === 0 ? (s2.qb ? 'ждёт места' : fmtClock(s2.qf - now)) : fmtDur(K.army.spells[i].craftSec)));
			},
		};
	});
}
function openArcane() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const sp = K.st.spells;
		const rows = K.army.spells.map((d, i) => {
			const ck = sp.upg[i];
			const armed = isArmed('us:' + i);
			let btn;
			if (ck.err === E.None) btn = '<button class="btn primary" data-act="upgrade-spell" data-id="' + i + '">Улучшить ' + costHtml(ck.cost, K.st.res) + '</button>';
			else if (ck.err === E.NotEnough && ck.g > 0) btn = '<button class="btn primary" data-act="upgrade-spell-gems" data-id="' + i + '" data-gems="' + ck.g + '">' + (armed ? 'Подтвердить ' : 'Докупить золото ') + gemSpan(ck.g) + '</button>';
			else btn = '<button class="btn" disabled>Улучшить</button>';
			return '<div class="troopRow">' + spellIcon(i) + '<div class="info"><b>' + esc(d.name) + ' · ур. ' + sp.lv[i] + '</b><span>' + (ck.t ? 'до ур. ' + (sp.lv[i] + 1) + ' · ' + fmtDur(ck.t) : '') + ' · +8% к силе за уровень</span>' +
				(ck.err !== E.None && ck.err !== E.NotEnough ? '<span class="why">' + esc(errText(ck.err, ck, 'arcane')) + '</span>' : '') + '</div>' + btn + '</div>';
		});
		const labs = sp.labs.map((l, k) => '<div class="uprow"><span>' + mini(K.army.spells[l.s].id) + esc(K.army.spells[l.s].name) + ' → ур. ' + (sp.lv[l.s] + 1) + '</span><b data-live="l' + k + '"></b></div><div class="progress"><i data-live="lb' + k + '"></i></div>' +
			'<div class="actions"><button class="btn" data-act="skip-spell" data-id="' + k + '" data-gems="' + l.g + '">' + (isArmed('ls:' + k) ? 'Подтвердить ' : 'Завершить сразу ') + gemSpan(l.g) + '</button></div>');
		const html = sheetTitle(buildingIconById('arcane_tower'), 'Магическая башня') +
			'<p class="fine">Башня улучшает заклинания: сколько улучшений идёт одновременно (' + sp.slots + ') и предел уровня (' + sp.maxLv + ') задаёт её уровень, как в оригинале. Цены и время — наша оценка.</p>' +
			labs.join('') + rows.join('') + '<div class="actions"><button class="btn" data-act="forge">Кузня заклинаний</button>' + closeBtn + '</div>';
		return {
			key: ['arcane', sp.lv.join(), sp.labs.map((l) => l.s).join(), sp.upg.map((c) => c.err + ':' + c.g).join(), K.army.spells.map((x, i) => isArmed('us:' + i)).join(), sp.labs.map((l, k) => isArmed('ls:' + k)).join()].join('|'), html,
			live(el) {
				const now = nowF();
				K.st.spells.labs.forEach((l, k) => { live(el, 'l' + k, fmtDur(l.until - now)); liveBar(el, 'lb' + k, 1 - (l.until - now) / Math.max(1, l.until - l.from)); });
			},
		};
	});
}
/** One titan: its relic slots, worn relics and the combat relics it can wear. */
const BONUS_RU = ['урон', 'здоровье', 'броня', 'шанс крита', 'пробитие брони', 'урон войск', 'здоровье войск', 'опыт'];
function openTitanRelics(id) {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const t = st.titans.find((x) => x.id === id);
		if (!t) return null;
		const d = K.army.titans[t.def];
		const worn = st.relics.filter((r) => r.on === t.id);
		const kinds = worn.map((r) => r.kind);
		const free = st.relics.filter((r) => !isEconomy(r) && r.on !== t.id).sort((a, b) => b.r - a.r || b.dur - a.dur);
		const full = t.relics.length >= t.slots;
		const nextSlot = K.army.relics.slotLevels.find((l) => l > t.lv);
		const abil = K.app && K.app.titanAbilities ? K.app.titanAbilities(d.id) : [];
		const bonus = t.bonus.map((v, i) => (v ? '+' + fmtPm(v) + '% ' + BONUS_RU[i] : '')).filter(Boolean);
		const wearBtn = (r) => {
			const same = kinds.includes(r.kind);
			const off = full || same || r.dur <= 0;
			return '<button class="btn' + (off ? '' : ' primary') + '" data-act="equip" data-id="' + r.id + '" data-titan="' + t.id + '"' + (off ? ' disabled' : '') + '>' +
				(same ? 'Такая уже надета' : r.dur <= 0 ? 'Сломана' : 'Надеть') + '</button>';
		};
		const html = sheetTitle(titanIcon(t), esc(d.name) + ' ' + starsHtml(d.stars)) +
			titanStage(d, abil) +
			'<p>' + esc(d.classRu) + ' · ур. ' + t.lv + ' из ' + t.max + '. ' + (bonus.length ? 'Бонус реликвий: ' + bonus.join(', ') + '.' : 'Бонусов от реликвий нет.') + '</p>' +
			(abil.length ? '<p>Способности в бою: <b>' + abil.map(esc).join('</b>, <b>') + '</b>.</p>' : '') +
			'<h3>Реликвии: ' + t.relics.length + ' из ' + t.slots + '</h3>' +
			(worn.length ? worn.map((r) => relicRow(r, '<button class="btn" data-act="unequip" data-id="' + r.id + '">Снять</button>')).join('') : '<p class="fine">Ничего не надето.</p>') +
			'<p class="fine">Как в оригинале: сначала у титана один слот, новые открываются с уровнем' + (nextSlot ? ' (следующий на ' + nextSlot + '-м)' : '') + '. ' +
			'Боевая реликвия изнашивается в боях (★1 хватает на 20 боёв, ★4 на 8, свиток опыта на 2); сломанная не действует, пока её не починят. Две одинаковые реликвии на одного титана не надеть.</p>' +
			'<h3>Можно надеть</h3>' +
			(free.length ? free.map((r) => relicRow(r, wearBtn(r))).join('') : '<p class="fine">Свободных боевых реликвий нет. Их дают Храм реликвий и победы в боях.</p>') +
			'<div class="actions"><button class="btn" data-act="relic-inv">Все реликвии</button><button class="btn" data-act="titans">Зал титанов</button>' + closeBtn + '</div>';
		return { key: ['trel', id, t.lv, t.slots, t.relics.join(), t.bonus.join(), st.relics.map(relicKey).join()].join('|'), html };
	});
}
/** All relics in tabs: repair or improve (from Castle 8) by sacrificing others, or throw away. */
function openRelicInventory() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const RL = K.army.relics;
		const rp = K.repair;
		const tab = K.relicTab || 'all';
		const canRepair = st.castle >= st.repairCastle;
		const raisable = (r) => r.lv.some((l) => l > 0 && l < RL.levels);
		const count = (k) => st.relics.filter((r) => k === 'all' || (k === 'building') === isEconomy(r)).length;
		const tabs = [['all', 'Все'], ['titan', 'Для титанов'], ['building', 'Для построек']]
			.map(([k, n]) => '<button class="' + (k === tab ? 'on' : '') + '" data-act="relic-tab" data-tab="' + k + '">' + n + ' · ' + count(k) + '</button>').join('');
		const target = rp ? st.relics.find((r) => r.id === rp.target) : null;
		let head = '';
		let foodOk = true;
		if (target) {
			const chance = K.bridge.repairChance(rp.target, rp.picks);
			const unlock = rp.picks.length > 0 && K.bridge.repairUnlock(rp.target, rp.picks);
			const raise = raisable(target);
			const whole = target.dur >= target.max;
			const nothing = whole && !raise && !unlock;
			const food = st.repairFood[target.r - 1];
			foodOk = st.res[1] >= food;
			const what = raise && unlock ? 'усилить свойство или открыть закрытое' : raise ? 'усилить одно из свойств' : unlock ? 'открыть закрытое свойство' : '';
			const hint = (what ? 'При успехе ' + RL.enhancePct[target.r - 1] + '% шанс ' + what + '. ' : '') +
				(target.lv.includes(0) && !unlock ? 'Закрытое свойство откроется, только если в жертву идёт реликвия от ' + target.r + '★. ' : '');
			head = '<h3>' + (whole ? 'Улучшение' : 'Починка') + '</h3>' + relicRow(target, '') +
				'<div class="costline"><span>жертв</span><b>' + rp.picks.length + '</b><span>шанс успеха</span><b>' + chance + '%</b>' + costHtml([0, food, 0, 0], st.res) + '</div>' +
				(hint ? '<p class="fine">' + hint + '</p>' : '') +
				'<div class="actions"><button class="btn primary" data-act="repair-go"' + (rp.picks.length && foodOk && !nothing ? '' : ' disabled') + '>' +
				(!foodOk ? 'Не хватает еды' : nothing && rp.picks.length ? 'Нечего улучшать' : isArmed('repair') ? 'Точно?' : whole ? 'Улучшить' : 'Чинить') + '</button>' +
				'<button class="btn" data-act="repair-cancel">Отмена</button></div><h3>Выберите жертвы</h3>';
		}
		const list = st.relics.filter((r) => relicTabHas(r) && (!target || r.id !== target.id)).sort((a, b) => b.r - a.r || a.kind - b.kind || a.dur - b.dur);
		const rows = list.map((r) => {
			let b = '';
			if (target) {
				const on = rp.picks.includes(r.id);
				b = '<button class="btn' + (on ? ' primary' : '') + '" data-act="repair-pick" data-id="' + r.id + '">' + (on ? 'Выбрана' : 'В жертву +' + RL.repairPct[target.r - 1][r.r - 1] + '%') + '</button>';
			} else {
				if (canRepair && (r.dur < r.max || raisable(r) || r.lv.includes(0))) b += '<button class="btn" data-act="repair-start" data-id="' + r.id + '">' + (r.dur < r.max ? 'Починить' : 'Улучшить') + '</button>';
				b += '<button class="btn' + (isArmed('discard:' + r.id) ? ' danger' : '') + '" data-act="discard" data-id="' + r.id + '">' + (isArmed('discard:' + r.id) ? 'Точно?' : 'Выбросить') + '</button>';
			}
			return relicRow(r, b);
		});
		const junk = st.relics.filter((r) => r.r === 1 && !r.on && !r.b && relicTabHas(r)).length;
		const html = sheetTitle(icoHtml('relic.armor', 'relic r5'), 'Реликвии') +
			'<p class="fine">Всего ' + st.relics.length + ' из ' + RL.maxRelics + '. ' + (canRepair
				? 'Починка, как в оригинале: стоит еду, а в жертву идут другие реликвии; шанс каждой зависит от её редкости и редкости чинимой (таблица оригинала, для ★5 наша оценка). Жертвы пропадают в любом случае. Успех возвращает прочность и с шансом усиливает свойство или открывает закрытое, поэтому чинить можно и целую реликвию. Цена в еде — наша оценка.'
				: 'Починка и улучшение откроются на Замке ' + st.repairCastle + ', как в оригинале.') +
			' Реликвии построек прикрепляются в окне постройки, кнопка «Реликвия».</p>' +
			head + '<div class="shopTabs">' + tabs + '</div>' +
			(rows.length ? rows.join('') : '<p class="fine">' + (st.relics.length ? 'В этом разделе пусто.' : 'Реликвий пока нет. Их дают Храм реликвий и победы в боях.') + '</p>') +
			'<div class="actions">' + (junk && !target ? '<button class="btn' + (isArmed('discard-r1') ? ' danger' : '') + '" data-act="discard-r1">' + (isArmed('discard-r1') ? 'Точно выбросить ' + junk + '?' : 'Выбросить свободные ★1 · ' + junk) + '</button>' : '') +
			'<button class="btn" data-act="titans">Зал титанов</button>' + closeBtn + '</div>';
		return {
			key: ['inv', tab, st.relics.map((r) => relicKey(r) + ':' + isArmed('discard:' + r.id)).join(), rp ? rp.target + '>' + rp.picks.join('.') + '>' + foodOk : '', isArmed('repair'), isArmed('discard-r1'), st.castle >= st.repairCastle].join('|'), html,
			live: liveRelics,
		};
	});
}
/** The relic slot of a building: one economy relic of a fitting kind works on it for a time. */
function openBuildingRelic(bid) {
	cancelPlace();
	openSheet(() => {
		const st = K.st;
		const b = st.b.find((x) => x.id === bid);
		if (!b) return null;
		const d = defOf(b.def);
		const kinds = relicKindsFor(d.id);
		const cur = buildingRelic(bid);
		const fits = st.relics.filter((r) => kinds.includes(relicKind(r)) && r.b !== bid).sort((a, x) => x.r - a.r || x.dur - a.dur);
		const hours = K.army.relics.hours;
		const html = sheetTitle(buildingIcon(b.def), esc(d.name)) +
			'<p class="fine">Сюда подходит ' + kinds.map((k) => mini('relic.' + k.id) + esc(k.name)).join(', ') + '. Как в оригинале, реликвия постройки работает ограниченное время, у редких оно дольше ' +
			'(от ' + hours[0] + ' ч у ★1 до ' + hours[hours.length - 1] + ' ч у ★5, часы — наша оценка). Время идёт, только пока реликвия на постройке, её можно снять и вернуть позже (наше упрощение). На постройке одна реликвия.</p>' +
			'<h3>Сейчас</h3>' + (cur ? relicRow(cur, '<button class="btn" data-act="unequip" data-id="' + cur.id + '">Снять</button>') : '<p class="fine">Реликвии нет.</p>') +
			'<h3>Можно прикрепить</h3>' +
			(fits.length ? fits.map((r) => relicRow(r, '<button class="btn' + (r.dur > 0 ? ' primary' : '') + '" data-act="attach" data-id="' + r.id + '" data-building="' + bid + '"' + (r.dur > 0 ? '' : ' disabled') + '>' +
				(r.dur <= 0 ? 'Время вышло' : cur ? 'Заменить' : 'Прикрепить') + '</button>')).join('')
				: '<p class="fine">Подходящих реликвий нет. Их дают Храм реликвий, Алтарь силы и победы в боях.</p>') +
			'<div class="actions"><button class="btn" data-act="relic-inv">Все реликвии</button>' + closeBtn + '</div>';
		return { key: ['brel', bid, b.lv, st.relics.map(relicKey).join()].join('|'), html, live: liveRelics };
	});
}
/** Pick the titan that guards a land (+1% production per titan level). */
function openLandGuard(landId) {
	cancelPlace();
	openSheet(() => {
		const st = K.st;
		const L = st.lands.find((x) => x.id === landId);
		if (!L) return null;
		const opts = st.titans.slice().sort((a, b) => b.lv - a.lv).map((t) => {
			const where = st.lands.find((x) => x.titan === t.id);
			const city = st.raids.titan === t.id;
			return '<button class="opt wi' + (L.titan === t.id ? ' on' : '') + '" data-act="assign" data-id="' + t.id + '" data-land="' + landId + '">' + titanIcon(t) + '<b>' + esc(titanName(t)) + ' ' + starsHtml(K.army.titans[t.def].stars) + '</b>' +
				'<span>ур. ' + t.lv + ' → +' + t.lv + '% добычи' + (where && where.id !== landId ? ' · сейчас на другой земле' : city ? ' · сейчас стережёт город' : '') + '</span></button>';
		});
		const html = sheetTitle(landIcon(L.type, L.lv), 'Страж земли') +
			'<p class="fine">Как в оригинале: титан на земле охраняет её и прибавляет 1% добычи за каждый свой уровень; звёзды не важны. Один титан охраняет одно место (землю или город) и всё равно может ходить в бой.</p>' +
			'<p class="fine">Страж защищает землю от налётчиков, пока не отдыхает после боя. Налётчики всегда приходят с титаном, так что без стража земля почти беззащитна.</p>' +
			'<div class="opts">' + opts.join('') + '</div>' +
			'<div class="actions">' + (L.titan ? '<button class="btn" data-act="recall" data-id="' + landId + '">Отозвать стража</button>' : '') + closeBtn + '</div>';
		return { key: ['guard', landId, L.titan, st.lands.map((x) => x.titan).join(), st.titans.map((t) => t.lv).join()].join('|'), html };
	});
}

/** The city's defense against raiders: the titan on guard and the player's squads in the Garrison's slots. */
function openDefense() {
	cancelPlace();
	select(null);
	openSheet(() => {
		const st = K.st;
		const r = st.raids;
		const a = st.army;
		const R = K.rules.raids;
		const f = K.bridge.forecast(0);
		const now = nowF();
		const counts = a.n.map(() => 0);
		for (const t of r.squads) counts[t]++;
		const used = r.squads.length;
		const titanOpts = st.titans.slice().sort((x, y) => K.army.titans[y.def].stars * 100 + y.lv - (K.army.titans[x.def].stars * 100 + x.lv)).map((t) => {
			const resting = t.rec > now;
			const land = st.lands.find((x) => x.titan === t.id);
			return '<button class="opt wi' + (r.titan === t.id ? ' on' : '') + '" data-act="def-titan" data-id="' + t.id + '">' + titanIcon(t) + '<b>' + esc(titanName(t)) + ' ' + starsHtml(K.army.titans[t.def].stars) + '</b>' +
				'<span>ур. ' + t.lv + (resting ? ' · отдыхает, пока не защищает' : '') + (land ? ' · уйдёт с земли «' + esc(landType(land.type).name) + '»' : '') + '</span></button>';
		});
		titanOpts.push('<button class="opt wi' + (!r.titan ? ' on' : '') + '" data-act="def-titan" data-id="0">' + icoHtml('ui.army', 'troop') + '<b>Без титана</b><span>налётчики приходят с титаном</span></button>');
		const rows = K.army.troops.map((tr, t) => (a.n[t] || counts[t] ? '<div class="troopRow">' + troopIcon(t) + '<div class="info"><b>' + esc(tr.name) + ' · ур. ' + a.lv[t] + '</b><span>в лагере ' + a.n[t] + '</span></div>' +
			'<div class="stepper"><button class="btn" data-act="def-sq" data-id="' + t + '" data-d="-1"' + (counts[t] > 0 ? '' : ' disabled') + '>−</button><b>' + counts[t] + '</b>' +
			'<button class="btn" data-act="def-sq" data-id="' + t + '" data-d="1"' + (counts[t] < a.n[t] && used < r.slots ? '' : ' disabled') + '>+</button></div></div>' : '')).filter(Boolean);
		const guards = R.guards.map((id) => TROOP_RU[id] || id).join(', ');
		const html = sheetTitle(buildingIconById('garrison'), 'Защита города') +
			(f.on ? '<div class="costline"><span>Удержит налёт</span><b>' + f.hold + '%</b><span>Защита</span><b>' + fmtShort(f.dp) + '</b><span>Налётчики</span><b>≈ ' + fmtShort(f.rp) + ' · ур. ' + f.lo + (f.hi > f.lo ? '–' + f.hi : '') + '</b></div>'
				: '<p class="note">Налётчики начнут приходить с Замка ' + r.castle + '. Защиту можно готовить заранее.</p>') +
			(r.shield > now ? '<p>Город под щитом ещё <b data-live="shield"></b>: налётчики его не тронут.</p>' : '') +
			'<h3>Титан на страже</h3><div class="opts">' + titanOpts.join('') + '</div>' +
			'<h3>Ваши отряды: ' + used + ' из ' + r.slots + '</h3>' + (rows.length ? rows.join('') : '<p class="fine">В лагере пока нет войск: их обучают в Казармах.</p>') +
			'<p class="fine">Свободные места займут стражники Гарнизона (' + guards + ', тир ' + r.gtier + ')' + (r.bonus ? '; Гарнизон прибавляет защите ' + r.bonus + '%' : '') +
			'. Отряды в защите остаются в лагере и ходят в бой, а пока они в бою, их место занимают стражники. В защите никто не гибнет и титан не устаёт.</p>' +
			'<p class="fine">Налётчики приходят раз в ' + R.everyHours[0] + '–' + R.everyHours[1] + ' ч, и пока игра закрыта тоже, но не дольше ' + R.awayHours + ' ч после вашего последнего визита. ' +
			'Удачный налёт уносит до ' + R.stealPct + '% золота и еды сверх защищённого резервами и до ' + R.vpPct + '% очков победы, после него город ' + R.cityShieldHours + ' ч под щитом. Отбитый налёт приносит очки победы.</p>' +
			'<div class="actions"><button class="btn" data-act="raid-log"' + (r.log.length ? '' : ' disabled') + '>Журнал налётов</button>' + closeBtn + '</div>';
		return {
			key: ['defense', f.on, f.hold, f.dp, f.rp, r.titan, r.squads.join(), r.slots, a.n.join(), r.shield > now, r.log.length, st.titans.map((t) => t.id + ':' + t.lv + ':' + (t.rec > now)).join(), st.lands.map((x) => x.titan).join()].join('|'), html,
			live(el) { live(el, 'shield', fmtDur(K.st.raids.shield - nowF())); },
		};
	});
}
/** Raid reports: the ones that came while the player was away, or the whole log (newest first). */
function openRaidLog(away) {
	cancelPlace();
	openSheet(() => {
		const st = K.st;
		const list = (away || st.raids.log).slice().reverse();
		if (!list.length) return null;
		const now = nowF();
		const rows = list.map((x) => {
			const L = x.land ? st.lands.find((l) => l.id === x.land) : null;
			const icon = x.land ? landIcon(x.type, L ? L.lv : x.lv) : buildingIconById('garrison');
			const t = x.titan ? st.titans.find((y) => y.id === x.titan) : null;
			const lost = raidLost(x);
			const what = x.stars ? (lost ? 'унесли ' + lost : 'унести было нечего') : x.vp ? '+' + resAmount('vp', x.vp) : 'без потерь';
			const who = (t ? esc(titanName(t)) : 'без титана') + (x.land ? '' : ', ваших отрядов ' + x.sq);
			return '<div class="troopRow">' + icon + '<div class="info">' + (x.stars ? '<b class="why">' + esc(raidWhere(x)) + ': налёт удался ' + '★'.repeat(x.stars) : '<b>' + esc(raidWhere(x)) + ': налёт отбит') + '</b>' +
				'<span>налётчики ур. ' + x.lv + ' · ' + what + '</span><span>защита: ' + who + ' · ' + fmtDur(Math.max(0, now - x.at)) + ' назад</span></div></div>';
		});
		const lostAll = [0, 0, 0, 0, 0, 0];
		for (const x of list) x.lost.forEach((v, i) => { lostAll[i] += v; });
		const gained = list.reduce((sum, x) => sum + x.vp, 0);
		const sum = lostAll.map((v, i) => (v > 0 ? resAmount(RES[i], v) : '')).filter(Boolean).join(', ');
		const bare = list.some((x) => x.stars && !x.titan);
		const html = sheetTitle(buildingIconById('garrison'), away ? 'Пока вас не было' : 'Журнал налётов') +
			'<div class="costline"><span>Налётов</span><b>' + list.length + '</b><span>Отбито</span><b>' + list.filter((x) => !x.stars).length + '</b>' +
			(sum ? '<span>Потеряно</span><b>' + sum + '</b>' : '') + (gained ? '<span>Получено</span><b>' + resAmount('vp', gained) + '</b>' : '') + '</div>' +
			rows.join('') +
			(bare ? '<p class="note">Налётчики всегда приходят с титаном. Поставьте своего титана в Гарнизон, а стражей на земли: без них защита почти не держится.</p>' : '') +
			'<div class="actions"><button class="btn primary" data-act="defense">Защита города</button>' + closeBtn + '</div>';
		return { key: ['raidlog', !!away, list.map((x) => x.at).join(), st.titans.length].join('|'), html };
	});
}

/** Before a land battle: pick the titan and the squads (up to the Army Camp's battle slots). */
function openPrepare(wid, capture) {
	cancelPlace();
	select(null);
	const a = K.st.army;
	const now = nowF();
	const ready = K.st.titans.filter((t) => t.rec <= now).sort((x, y) => K.army.titans[y.def].stars * 100 + y.lv - (K.army.titans[x.def].stars * 100 + x.lv));
	const counts = a.n.map(() => 0);
	let total = 0;
	for (let guard = 0; guard < 100 && total < a.slots; guard++) {
		let added = false;
		for (let t = 0; t < counts.length && total < a.slots; t++) if (counts[t] < a.n[t]) { counts[t]++; total++; added = true; }
		if (!added) break;
	}
	const sp = K.st.spells;
	const spells = sp.stock.map((n, i) => [n, i]).filter((x) => x[0] > 0).sort((x, y) => y[0] - x[0]).slice(0, sp.kinds).map((x) => x[1]);
	K.prep = { wid, capture, titan: ready.length ? ready[0].id : 0, counts, spells, reinf: true };
	openSheet(() => {
		const st = K.st;
		const p = K.prep;
		const w = st.world.find((x) => x.id === p.wid);
		if (!w) return null;
		const aa = st.army;
		const used = p.counts.reduce((x, y) => x + y, 0);
		const titanOpts = st.titans.slice().sort((x, y) => K.army.titans[y.def].stars * 100 + y.lv - (K.army.titans[x.def].stars * 100 + x.lv)).map((t) => {
			const resting = t.rec > nowF();
			return '<button class="opt wi' + (p.titan === t.id ? ' on' : '') + '" data-act="prep-titan" data-id="' + t.id + '"' + (resting ? ' disabled' : '') + '>' + titanIcon(t) + '<b>' + esc(titanName(t)) + ' ' + starsHtml(K.army.titans[t.def].stars) + '</b>' +
				'<span>' + esc(K.army.titans[t.def].classRu) + ' · ур. ' + t.lv + (resting ? ' · отдыхает' : '') + '</span></button>';
		});
		titanOpts.push('<button class="opt wi' + (!p.titan ? ' on' : '') + '" data-act="prep-titan" data-id="0">' + icoHtml('ui.army', 'troop') + '<b>Без титана</b><span>только войска</span></button>');
		const rows = K.army.troops.map((tr, t) => '<div class="troopRow">' + troopIcon(t) + '<div class="info"><b>' + esc(tr.name) + ' · ур. ' + aa.lv[t] + '</b><span>в лагере ' + aa.n[t] + '</span></div>' +
			'<div class="stepper"><button class="btn" data-act="prep-sq" data-id="' + t + '" data-d="-1"' + (p.counts[t] > 0 ? '' : ' disabled') + '>−</button><b>' + p.counts[t] + '</b>' +
			'<button class="btn" data-act="prep-sq" data-id="' + t + '" data-d="1"' + (p.counts[t] < aa.n[t] && used < aa.slots ? '' : ' disabled') + '>+</button></div></div>');
		const ty = landType(w.type);
		const et = enemyTitan(w);
		const cost = p.capture ? 6 : 3;
		const sp = st.spells;
		const spellOpts = K.army.spells.map((d, i) => (sp.stock[i] > 0 ? '<button class="opt wi' + (p.spells.includes(i) ? ' on' : '') + '" data-act="prep-spell" data-id="' + i + '">' + spellIcon(i) + '<b>' + esc(d.name) + '</b>' +
			'<span>×' + sp.stock[i] + ' · ур. ' + sp.lv[i] + '</span></button>' : '')).filter(Boolean);
		// Allies' squads that have arrived in the Alliance Camp fight once and are gone (the original).
		const arrived = st.ally.reinf.filter((r) => r[2] <= nowF());
		const reinfHtml = arrived.length ? '<h3>Подкрепления альянса</h3><div class="opts"><button class="opt wi' + (p.reinf ? ' on' : '') + '" data-act="prep-reinf">' + icoHtml('ui.alliance', 'troop') +
			'<b>' + (p.reinf ? 'Взять ' : 'Не брать ') + arrived.length + ' ' + plural(arrived.length, 'отряд', 'отряда', 'отрядов') + '</b><span>' + arrived.map((r) => esc(K.army.troops[r[0]].name) + ' ' + r[1]).join(', ') + '</span></button></div>' +
			'<p class="fine">Они не занимают места армии и после боя уходят, сколько бы ни выжило.</p>' : '';
		const html = sheetTitle(landIcon(w.type, w.lv), (p.capture ? 'Захват' : 'Набег') + ': ' + esc(ty.name) + ', ур. ' + w.lv) +
			'<p>Противник: ' + esc(enemyText(w.lv, et)) + '.' + (ty.field ? ' Поле боя: ' + esc(ty.field) + (ty.fieldNote ? ', ' + esc(ty.fieldNote) : '') + '.' : '') + '</p>' +
			'<h3>Титан</h3><div class="opts">' + titanOpts.join('') + '</div>' +
			'<h3>Отряды: ' + used + ' из ' + aa.slots + '</h3>' + rows.join('') + reinfHtml +
			'<h3>Заклинания: ' + p.spells.length + ' из ' + sp.kinds + '</h3>' +
			(spellOpts.length ? '<div class="opts">' + spellOpts.join('') + '</div><p class="fine">Берутся все заряды выбранных заклинаний; потраченные уходят из запаса.</p>'
				: '<p class="fine">Заклинаний в запасе нет: их создаёт Кузня заклинаний (Замок 5).</p>') +
			'<p class="fine">' + (aa.surv >= 100 ? 'Лазарета нет: каждый раненый отряд погибнет.' : 'Вернутся отряды, сохранившие не меньше ' + aa.surv + '% бойцов.') + '</p>' +
			'<div class="actions"><button class="btn primary" data-act="prep-go"' + (used || p.titan ? '' : ' disabled') + '>В бой · ' + cost + ' ' + iconSvg('stones') + '</button><button class="btn" data-act="world">Назад</button></div>';
		return { key: ['prep', p.titan, p.counts.join(), p.spells.join(), aa.n.join(), sp.stock.join(), st.titans.map((t) => t.id + ':' + (t.rec > nowF())).join(), p.reinf, arrived.length].join('|'), html };
	});
}

// ---------------------------------------------------------------------------------------------
// Alliances and alliance leagues
// ---------------------------------------------------------------------------------------------
const ROLE_RU = ['лидер', 'офицер', 'участник'];
const allyLeague = (tier) => K.rules.alliances.leagues[tier];
const embHtml = (tag, emb) => '<div class="emb e' + (emb % 12) + '" aria-hidden="true">' + esc(tag) + '</div>';
/** A league place's reward: [gems, stones, crystal rarity, titan chance %]. */
const payHtml = (p) => '+' + fmt(p[0]) + ' ' + iconSvg('gems') + ' +' + p[1] + ' ' + iconSvg('stones') + (p[2] ? ' + ' + mini('ui.crystal') + '★'.repeat(p[2]) : '');
const crystalNote = (p) => (p[2] ? 'Кристалл ' + '★'.repeat(p[2]) + ': титан этой редкости с шансом ' + p[3] + '%, иначе реликвия.' : '');
/** The member who sent a squad (index into the alliance's member list). */
function senderName(data, from) {
	const m = data && data.members[from];
	return m ? (m.you ? 'вы' : m.name) : '';
}
function openAlliance(view) {
	cancelPlace();
	select(null);
	if (view) K.allyView = view;
	openSheet(() => {
		const st = K.st;
		const data = K.bridge.alliances();
		K.allyData = data;
		return st.ally.my >= 0 ? allianceMine(st, data) : allianceList(st, data);
	});
}
/** Outside an alliance: the 1 000 VP gate, alliances to join, founding an own one. */
function allianceList(st, data) {
	const al = st.ally;
	const A = K.rules.alliances;
	const open = al.rating >= A.joinVp;
	const tab = K.allyTab || (open ? 'fit' : 'all');
	const tabs = [['fit', 'Подходящие'], ['all', 'Все'], ['create', 'Основать']];
	let body = '';
	let key = '';
	if (tab === 'create') {
		const ck = al.create;
		const f = K.allyForm || { name: '', tag: '' };
		const gems = A.createGems;
		const btn = ck.err === E.None ? '<button class="btn primary" data-act="ally-create">' + (isArmed('ally-create') ? 'Подтвердить ' : 'Основать ') + gemSpan(gems) + '</button>'
			: '<button class="btn" disabled>' + esc(errText(ck.err, ck)) + '</button>';
		body = '<p>Свой альянс начинает в низшей лиге. Новобранцы приходят сами, по одному каждые ' + A.recruitHours + ' ч (до ' + A.recruits + '); чем выше лига, тем сильнее они играют. ' +
			'Если выйти из своего альянса, он распустится.</p>' +
			'<div class="form2"><label class="field">Название<input id="allyName" maxlength="24" autocomplete="off" spellcheck="false" placeholder="Дом Рассвета" value="' + esc(f.name) + '"></label>' +
			'<label class="field">Тег<input id="allyTag" maxlength="5" autocomplete="off" spellcheck="false" placeholder="ДОМ" value="' + esc(f.tag) + '"></label></div>' +
			'<div class="actions">' + btn + '</div>' +
			'<p class="fine">В мастер-промпте альянс стоит 100 самоцветов; новобранцы и их число — наше решение, пока нет сервера с живыми игроками.</p>';
		// Typing must not rebuild the form: the key holds nothing that changes by the second.
		key = ['ally-create', ck.err, isArmed('ally-create')].join('|');
	} else {
		const list = data.all.filter((a) => !a.own && (tab === 'all' || a.err === E.None)).sort((x, y) => y.tier - x.tier || y.vp - x.vp);
		const cards = list.map((a) => '<div class="landCard">' + embHtml(a.tag, a.emb) + '<div class="info"><b>' + esc(a.name) + ' [' + esc(a.tag) + ']</b>' +
			'<span>' + esc(allyLeague(a.tier).name) + ' · ' + a.m + ' из ' + A.members[1] + ' · <span data-live="avp' + a.i + '"></span> ОП за сезон</span>' +
			'<span>Принимает от ' + fmt(a.join) + ' ОП</span></div><div class="row">' +
			(a.err === E.None ? '<button class="btn primary" data-act="ally-join" data-id="' + a.i + '">Вступить</button>'
				: '<button class="btn" disabled>' + esc(errText(a.err, { cost: [0, 0, 0, 0, 0, a.join], m: [-1, 0, 0] })) + '</button>') + '</div></div>');
		body = cards.length ? cards.join('') : '<p class="fine">' + (open ? 'Сейчас нет альянсов, которые вас примут: сильные просят больше очков. Посмотрите «Все» или основайте свой.' : 'Пока ни один альянс вас не примет.') + '</p>';
		key = ['ally-list', tab, list.map((a) => a.i + ':' + a.err + ':' + a.tier + ':' + a.m).join()].join('|');
	}
	const html = sheetTitle(icoHtml('ui.alliance', 'bld other'), 'Альянсы') +
		'<p>Как в оригинале: в альянс можно вступить с ' + fmt(A.joinVp) + ' очков победы. Очки участников идут и им самим, и альянсу; альянсы соревнуются в лигах, каждую неделю лучшие поднимаются, худшие опускаются, а награду каждый участник получает по месту своего альянса.</p>' +
		'<div class="costline"><span>Ваш рейтинг</span><b>' + iconSvg('vp') + fmt(al.rating) + '</b><span>' + (open ? 'альянсы открыты' : 'нужно ' + fmt(A.joinVp)) + '</span></div>' +
		(open ? '' : '<div class="progress"><i style="width:' + Math.round((100 * al.rating) / A.joinVp) + '%"></i></div>') +
		'<p class="fine">Рейтинг: очки победы этого сезона или прошлого, если их было больше.' + (open ? '' : ' Их приносят победы в боях за земли и земли очков победы.') + '</p>' +
		'<div class="seg three">' + tabs.map(([id, t]) => '<button class="' + (tab === id ? 'on' : '') + '" data-act="ally-tab" data-tab="' + id + '">' + t + '</button>').join('') + '</div>' +
		body + '<p class="fine">Другие альянсы и их участники пока управляются компьютером: у игры нет сервера. Их состав и сила зависят от вашего королевства, а очки растут в течение недели.</p>' +
		'<div class="actions">' + closeBtn + '</div>';
	return {
		key, html,
		live(el) {
			for (const a of (K.allyData || data).all) live(el, 'avp' + a.i, fmt(a.vp));
		},
	};
}
/** Inside an alliance: its league table, members, rewards by place and reinforcements. */
function allianceMine(st, data) {
	const al = st.ally;
	const A = K.rules.alliances;
	const lg = allyLeague(al.tier);
	const top = A.leagues.length - 1;
	const view = K.allyView || 'league';
	const zone = (place, size) => (place <= A.promote && al.tier < top ? 'up' : place > size - A.promote && al.tier > 0 ? 'down' : '');
	const myZone = zone(al.place, al.size);
	const pay = data.pay[al.place - 1] || [0, 0, 0, 0];
	const late = al.since > st.season.end - A.memberHours * 3600;
	const views = [['league', 'Лига'], ['members', 'Участники'], ['pay', 'Награды'], ['reinf', 'Подкрепления']];
	let body = '';
	let key = '';
	if (view === 'league') {
		const rows = data.tables[al.tier].map((i, n) => {
			const a = data.all[i];
			const z = zone(n + 1, al.size);
			return '<span class="pl ' + z + '">' + (z === 'up' ? '▲' : z === 'down' ? '▼' : '') + (n + 1) + '</span><span class="nm' + (i === al.my ? ' me' : '') + '">' + esc(a.name) +
				'<small>[' + esc(a.tag) + '] · ' + a.m + '</small></span><span class="v' + (i === al.my ? ' me' : '') + '" data-live="avp' + i + '"></span>';
		});
		body = '<div class="league">' + rows.join('') + '</div><p class="fine">▲ — зона повышения, ▼ — зона понижения: в конце сезона по ' + A.promote + ' альянса меняются местами с соседней лигой.</p>';
		key = data.tables[al.tier].join();
	} else if (view === 'members') {
		const ms = data.members.slice().sort((x, y) => y.vp - x.vp);
		body = '<div class="league">' + ms.map((m, n) => '<span class="pl">' + (n + 1) + '</span><span class="nm' + (m.you ? ' me' : '') + '">' + (m.you ? 'Вы' : esc(m.name)) +
			'<small>' + ROLE_RU[m.role] + '</small></span><span class="v' + (m.you ? ' me' : '') + '">' + fmt(m.vp) + '</span>').join('') + '</div>' +
			(al.own ? '<p class="fine">Новобранцы приходят по одному каждые ' + A.recruitHours + ' ч, до ' + A.recruits + '.</p>' : '');
		key = ms.map((m) => m.name + ':' + Math.floor(m.vp / 25)).join();
	} else if (view === 'pay') {
		body = '<div class="cmp">' + data.pay.map((p, n) => '<span class="k' + (n + 1 === al.place ? ' on' : '') + '">' + (n + 1) + ' место</span><span class="v">' + payHtml(p) + '</span>' +
			'<span class="n">' + (p[2] ? 'титан ' + p[3] + '%' : '') + '</span>').join('') + '</div>' +
			'<p class="fine">Как в оригинале: награда каждому участнику — самоцветы, камни портала и кристалл. Кристалл выше в лигах повыше, ★★★★ — только в высшей; чем выше место, тем больше шанс, что в нём титан. Сами числа наши.</p>';
		key = data.pay.map((p) => p.join(':')).join();
	} else {
		body = reinforceHtml(st, data);
		key = ['reinf', al.cap, al.rck.err, al.reinf.map((r) => r.join(':')).join(), isArmed('ally-reinforce')].join();
	}
	const leaveArmed = isArmed('ally-leave');
	const html = sheetTitle(embHtml(al.tag, al.emb), esc(al.name) + ' [' + esc(al.tag) + ']') +
		'<p>' + esc(lg.name) + ' · место <b>' + al.place + '</b> из ' + al.size + ' · ' + al.members + ' ' + plural(al.members, 'участник', 'участника', 'участников') + (al.own ? ' · ваш альянс' : '') + '</p>' +
		'<div class="costline"><span>Очки альянса</span><b>' + iconSvg('vp') + '<span data-live="myvp"></span></b><span>ваши ' + fmt(st.res[5]) + '</span></div>' +
		(myZone === 'up' ? '<p class="fine">Сейчас альянс в зоне повышения.</p>' : myZone === 'down' ? '<p class="note">Сейчас альянс в зоне понижения.</p>' : '') +
		'<p>Сезон кончится через <b data-live="left"></b>. За это место каждый получит ' + payHtml(pay) + '. ' + crystalNote(pay) + '</p>' +
		(late ? '<p class="note">Вы в альянсе меньше ' + A.memberHours + ' ч до конца сезона: в этот раз награду даст личная лига, награда альянса — со следующего сезона.</p>' : '') +
		'<div class="seg">' + views.map(([id, t]) => '<button class="' + (view === id ? 'on' : '') + '" data-act="ally-view" data-tab="' + id + '">' + t + '</button>').join('') + '</div>' +
		body +
		'<div class="actions"><button class="btn' + (leaveArmed ? ' danger' : '') + '" data-act="ally-leave">' + (leaveArmed ? (al.own ? 'Точно распустить?' : 'Точно выйти?') : al.own ? 'Распустить альянс' : 'Выйти из альянса') + '</button>' + closeBtn + '</div>';
	return {
		key: ['ally', al.my, al.tier, al.place, al.members, view, key, leaveArmed, late, myZone, st.res[5]].join('|'), html,
		live(el) {
			const d = K.allyData || data;
			live(el, 'left', fmtDur(Math.max(0, K.st.season.end - nowF())));
			live(el, 'myvp', fmt(K.st.ally.vp));
			for (const i of d.tables[K.st.ally.tier] || []) live(el, 'avp' + i, fmt(d.all[i].vp));
			liveReinforce(el, d);
		},
	};
}
/** The Alliance Camp: squads allies sent (on their way or waiting for a battle) and the request button. */
function reinforceHtml(st, data) {
	const al = st.ally;
	const A = K.rules.alliances;
	const camp = K.defs.find((d) => d.id === 'alliance_camp');
	if (!al.cap) {
		return '<p>Подкрепления принимает Лагерь альянса' + (camp ? ' (с Замка ' + camp.unlockCastle + ')' : '') + '. Как в оригинале, союзники присылают в него отряды, а уровень Лагеря задаёт, сколько.</p>';
	}
	const rows = al.reinf.map((r, i) => '<div class="troopRow">' + troopIcon(r[0]) + '<div class="info"><b>' + esc(K.army.troops[r[0]].name) + ' · ур. ' + r[1] + '</b>' +
		'<span data-live="ra' + i + '"></span></div></div>');
	const ck = al.rck;
	let btn;
	if (ck.err === E.None) btn = '<button class="btn primary" data-act="ally-reinforce">Попросить подкрепления</button>';
	else if (ck.err === E.NotReady) btn = '<button class="btn" disabled>Снова можно через <span data-live="rnext"></span></button>';
	else if (ck.err === E.StorageFull) btn = '<button class="btn" disabled>Лагерь альянса полон</button>';
	else if (ck.err === E.Nothing) btn = '<button class="btn" disabled>Пока некому прислать: новобранцев ещё нет</button>';
	else btn = '<button class="btn" disabled>' + esc(errText(ck.err, ck)) + '</button>';
	return '<h3>Лагерь альянса: ' + al.reinf.length + ' из ' + al.cap + '</h3>' + (rows.length ? rows.join('') : '<p class="fine">Лагерь пуст.</p>') +
		'<div class="actions">' + btn + '</div>' +
		'<p class="fine">Как в оригинале: отряды союзников сражаются в одном бою и уходят; их берут в бой за землю в окне подготовки. ' +
		'Наше: союзники присылают по отряду в минуту войска, доступные вашему Замку, уровня своей лиги, но не выше предела вашего Арсенала; просить можно раз в ' + Math.round(A.reinforceCooldownSec / 60) + ' минут.</p>';
}
function liveReinforce(el, data) {
	const al = K.st.ally;
	const now = nowF();
	al.reinf.forEach((r, i) => {
		const who = senderName(data, r[3]);
		live(el, 'ra' + i, (r[2] > now ? 'в пути, прибудет через ' + fmtClock(r[2] - now) : 'ждёт боя') + (who ? ' · прислал' + (who === 'вы' ? 'и вы' : ' ' + who) : ''));
	});
	if (al.next > now) live(el, 'rnext', fmtClock(al.next - now));
}
function createAlliance() {
	const name = (($('allyName') || {}).value || '').trim();
	const tag = (($('allyTag') || {}).value || '').trim();
	K.allyForm = { name, tag };
	if (!name || [...name].length > 24) { toast('Название: от 1 до 24 букв', 'bad'); return; }
	if (!tag || [...tag].length > 5) { toast('Тег: от 1 до 5 букв', 'bad'); return; }
	gemAction('ally-create', K.rules.alliances.createGems, () => {
		if (run(K.bridge.create(name, tag), 'Альянс «' + name + '» основан')) { K.allyForm = null; K.allyView = 'members'; }
		refreshSheet(true);
	});
}

// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------
// Each part of a frame is guarded: an error shows once and the island keeps being drawn.
function frame(dt, t) {
	if (!K.ready) return;
	try { frameUpdate(dt, t); } catch (e) { report('кадр', e); }
	try { K.app.renderer.render(scene, cam); } catch (e) { report('рисунок', e); }
	try { drawOverlay(t); } catch (e) { report('значки', e); }
}
function frameUpdate(dt, t) {
	const app = K.app;
	// Real elapsed time (the loop's dt is capped), at most a second per frame.
	const real = K.lastT ? clamp((t - K.lastT) / 1000, 0, 1) : dt;
	K.lastT = t;
	if (clock.scale !== 1) clock.offset += real * (clock.scale - 1);
	const sec = Math.floor(nowF());
	if (sec !== K.lastStateSec && performance.now() - K.lastStateAt > (clock.scale > 1 ? 180 : 0)) refresh();
	if (performance.now() - K.lastSave > 30000) persist();
	const r = app.renderer;
	if (r.shadowMap.enabled !== world.shadows) {
		world.shadows = r.shadowMap.enabled;
		world.sun.castShadow = world.shadows;
		scene.traverse((o) => { if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => { m.needsUpdate = true; }); });
	}
	const aspect = app.viewW / app.viewH;
	const fov = aspect < 1 ? 62 : 45;
	if (cam.aspect !== aspect || cam.fov !== fov) { cam.aspect = aspect; cam.fov = fov; cam.updateProjectionMatrix(); }
	placeCamera(dt);
	const ts = t / 1000;
	for (const v of world.views.values()) for (const u of v.upd) u(ts);
	for (const v of world.lands.values()) {
		const bob = Math.sin(ts * 0.6 + v.phase) * 0.6;
		v.group.position.y = v.base.y + bob;
		v.anchor.y = v.base.y + 5.2 + bob;
		for (const u of v.upd) u(ts);
	}
	for (const c of world.clouds) {
		c.a += c.w * dt;
		c.s.position.set(Math.cos(c.a) * c.r, c.y, Math.sin(c.a) * c.r);
	}
	world.fall.material.map.offset.y = (world.fall.material.map.offset.y + dt * 1.1) % 1;
	if (world.selRing.visible) world.selRing.children[0].material.emissiveIntensity = 0.75 + Math.sin(ts * 5) * 0.45;
	if (world.ghost) {
		world.ghost.group.position.y = 0.2 + Math.abs(Math.sin(ts * 4)) * 0.25;
		for (const u of world.ghost.upd) u(ts);
	}
}

// ---------------------------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------------------------
function init(app) {
	try {
		K.app = app;
		const sim = app.sim;
		if (!sim || !sim.M || typeof sim.M._dk_init !== 'function') return false;
		K.bridge = new KingdomBridge(sim.M);
		if (!K.bridge.init()) { console.warn(K.bridge.error()); return false; }
		const files = app.contentFiles();
		K.defs = JSON.parse(files['kingdom/buildings.json']).buildings;
		K.rules = parseJsonc(files['kingdom/kingdom.json']);
		K.rules.castleIndex = K.defs.findIndex((d) => d.id === 'castle');
		K.sandbox = JSON.parse(files['scenarios/m0_sandbox.json']);
		K.army = JSON.parse(files['kingdom/army.json']);

		const local = readLocal();
		let loaded = false;
		if (local) {
			clock.offset = +local.offset || 0;
			loaded = K.bridge.load(local.save);
			if (!loaded) {
				console.warn('Kingdom save rejected, starting a new kingdom:', K.bridge.error());
				try { localStorage.setItem(SAVE_KEY + '.rejected', JSON.stringify(local)); } catch (e) { /* ignore */ }
				clock.offset = 0;
			} else saver.savedAt = +local.savedAt || 0;
		}
		saver.loaded = loaded;
		saver.loadedAt = saver.savedAt;
		if (!loaded) K.bridge.newGame(nowSec(), Math.floor(Math.random() * 2147483647));
		// Raids while the page was closed resolve here; the visit then lets the raiders come again.
		K.bridge.advance(nowSec());
		visit();
		const st = K.bridge.state();
		if (st.pending) {
			// The page closed during a land battle: it counts as a retreat with the army intact (stones are spent).
			K.bridge.attackFinish(st.pending, !!st.pendingCapture, 0, st.army.away.map(() => 100), st.spells.away.map(() => 0));
			setTimeout(() => toast('Прерванный бой за землю засчитан как отступление', 'bad'), 600);
		}

		kov = $('kov');
		kctx = kov.getContext('2d');
		buildSky();
		buildIsland();
		buildDecor();
		buildGrid();
		buildSelRing();
		initHud();
		kov.addEventListener('pointerdown', onDown);
		window.addEventListener('pointermove', onMove, { passive: true });
		window.addEventListener('pointerup', onUp);
		window.addEventListener('pointercancel', onUp);
		kov.addEventListener('wheel', onWheel, { passive: false });
		kov.addEventListener('contextmenu', (e) => e.preventDefault());
		window.addEventListener('pagehide', () => { if (K.ready) persist(); });
		document.addEventListener('visibilitychange', () => {
			if (!K.ready) return;
			if (document.hidden) persist();
			else { refresh(); visit(); }
		});
		window.addEventListener('pointerdown', () => { if (K.ready && performance.now() - K.visitAt > 60000) visit(); }, { capture: true, passive: true });
		K.ready = true;
		refresh(true);
		placeCamera(0);
		persist();
		cloudConnect();
		let welcomed = false;
		try { welcomed = !!localStorage.getItem(HINT_KEY); } catch (e) { /* ignore */ }
		if (!welcomed && !loaded) openWelcome();
		// Debug and test hooks (used by the automated browser tests).
		window.dotKingdomDebug = {
			K, world, view, clock, refresh, persist, cloud,
			screenOf(id) { const v = world.views.get(id) || world.lands.get(id); return v ? project(new T.Vector3(v.anchor.x, v.anchor.y * 0.5, v.anchor.z)) : null; },
		};
		return true;
	} catch (e) {
		console.error('Kingdom screen failed to start:', e);
		K.ready = false;
		window.dotKingdom.error = (e && e.message) || String(e);
		return false;
	}
}

window.dotKingdom = { init, frame, onBattleEnd };
})();
