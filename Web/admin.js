// Web/admin.js - the hidden admin mode of the browser demo, for hunting bugs.
// Always on: an error log (page errors, rejected promises, console errors and warnings, files that failed to load),
// kept in the browser across reloads. Switched on by tapping the state hash in the kingdom menu, or the engine line in
// the battle menu, seven times: a wrench button then opens a panel that edits the kingdom, moves time, finishes timers,
// starts any battle, checks the kingdom's invariants, plays it at random, and shows, copies and loads the save and the log.
// The kingdom stays in the C++ module: the panel changes it through its own save format (Kingdom::Save / Load) or
// through the regular dk_* actions of Web/dot_web.cpp, so no game rule lives here.
(() => {
'use strict';

const ON_KEY = 'dot.admin';
const LOG_KEY = 'dot.admin.log';
const TAB_KEY = 'dot.admin.tab';
const BACKUP_KEY = 'dot.admin.backup';
const SAVE_KEY = 'dot.kingdom.v1'; // Web/kingdom.js
const LOG_MAX = 300;
const TAPS = 7;

const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
function esc(s) { return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c])); }
function fmt(n) { return Math.floor(n).toLocaleString('ru-RU'); }
function fmtDur(s) {
	s = Math.max(0, Math.round(s));
	const d = Math.floor(s / 86400);
	const h = Math.floor((s % 86400) / 3600);
	const m = Math.floor((s % 3600) / 60);
	if (d) return d + ' д' + (h ? ' ' + h + ' ч' : '');
	if (h) return h + ' ч' + (m ? ' ' + m + ' мин' : '');
	if (m) return m + ' мин';
	return s + ' с';
}
function clockText(t) { const d = new Date(t); return d.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit', second: '2-digit' }); }
function dateText(t) { return new Date(t).toLocaleString('ru-RU'); }
function lsGet(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
function lsSet(k, v) { try { if (v === null) localStorage.removeItem(k); else localStorage.setItem(k, v); return true; } catch (e) { return false; } }

// ---------------------------------------------------------------------------------------------
// Error log (always recorded, shown in the admin panel)
// ---------------------------------------------------------------------------------------------
const log = { items: [], seen: 0, timer: 0, busy: false, lastToast: 0 };
try {
	const old = JSON.parse(lsGet(LOG_KEY) || '[]');
	if (Array.isArray(old)) log.items = old.filter((x) => x && typeof x.m === 'string').slice(-LOG_MAX);
} catch (e) { /* a broken log starts again */ }
if (log.items.length) log.items.push({ t: Date.now(), l: 'info', m: 'Страница открыта заново', w: '' });
log.seen = log.items.length;

function text(a) {
	if (a instanceof Error) return a.stack && a.stack.indexOf(a.message) >= 0 ? a.stack : a.message + (a.stack ? '\n' + a.stack : '');
	if (a && typeof a === 'object') { try { return JSON.stringify(a); } catch (e) { return String(a); } }
	return String(a);
}
function where() {
	const b = document.body;
	if (!window.dotApp || !window.dotApp.sim) return 'загрузка';
	return b && b.classList.contains('mode-kingdom') ? 'королевство' : 'бой';
}
function saveLog() { lsSet(LOG_KEY, JSON.stringify(log.items)); }
/** level: 'error' | 'warn' | 'info'. Repeats of the last entry are counted instead of added. */
function addLog(level, msg) {
	if (log.busy) return;
	log.busy = true;
	try {
		const m = String(msg).slice(0, 4000);
		const last = log.items[log.items.length - 1];
		if (last && last.m === m && last.l === level) { last.n = (last.n || 1) + 1; last.t = Date.now(); } else log.items.push({ t: Date.now(), l: level, m, w: where() });
		if (log.items.length > LOG_MAX) {
			const cut = log.items.length - LOG_MAX;
			log.items.splice(0, cut);
			log.seen = Math.max(0, log.seen - cut);
		}
		clearTimeout(log.timer);
		log.timer = setTimeout(saveLog, 800);
		if (level === 'error') onNewError(m);
	} catch (e) { /* the log must never break the page */ }
	log.busy = false;
}
for (const level of ['error', 'warn']) {
	const orig = console[level];
	console[level] = function (...args) {
		addLog(level, args.map(text).join(' '));
		return orig.apply(this, args);
	};
}
window.addEventListener('error', (e) => {
	const t = e.target;
	if (t && t !== window && t.tagName) { addLog(t.tagName === 'LINK' ? 'warn' : 'error', 'Не загрузился файл: ' + (t.src || t.href || t.tagName)); return; }
	const at = e.filename ? ' (' + String(e.filename).split('/').pop() + ':' + e.lineno + ':' + e.colno + ')' : '';
	addLog('error', (e.message || 'Ошибка') + at + (e.error && e.error.stack ? '\n' + e.error.stack : ''));
}, true);
window.addEventListener('unhandledrejection', (e) => addLog('error', 'Необработанный промис: ' + text(e.reason)));
function newErrors() { let n = 0; for (let i = log.seen; i < log.items.length; i++) if (log.items[i].l === 'error') n++; return n; }
function onNewError(m) {
	updateButton();
	if (!admin.on || admin.open) return;
	const now = Date.now();
	if (now - log.lastToast < 2500) return;
	log.lastToast = now;
	setTimeout(() => toast('Ошибка записана в лог: ' + m.split('\n')[0].slice(0, 140), 'bad'), 0);
}

// ---------------------------------------------------------------------------------------------
// State of the admin mode
// ---------------------------------------------------------------------------------------------
const admin = {
	on: lsGet(ON_KEY) === '1' || /[?#&]admin\b/.test(location.search + location.hash),
	open: false, tab: lsGet(TAB_KEY) || 'res', taps: [], msg: '', backup: null, checks: null, monkey: null, showSave: false, logErrorsOnly: false,
	battle: { sc: '', titan: '', tlv: 0, tst: 0, asc: false, etitan: '', elv: 0, est: 0, ai: 'veteran' },
	relic: { kind: 0, rarity: 3, full: true }, land: { type: 0, lv: 1 }, monkeySteps: 200, monkeySeed: 1, keepMonkey: false,
	typed: {}, // text in the panel's fields, kept across re-renders
};
try { admin.backup = JSON.parse(lsGet(BACKUP_KEY) || 'null'); } catch (e) { admin.backup = null; }

/** The kingdom screen's test hooks (Web/kingdom.js), when the kingdom runs. */
function kd() { const d = window.dotKingdomDebug; return d && d.K && d.K.ready && d.K.bridge ? d : null; }
function app() { return window.dotApp || null; }
function demo() { return window.dotDemo || null; }
function inBattle() { const a = app(); return !!a && a.mode === 'battle'; }

function toast(t, kind) {
	admin.msg = t;
	const m = $('admMsg');
	if (m) { m.textContent = t; m.className = 'admMsg ' + (kind || ''); }
	const a = app();
	const d = window.dotKingdomDebug;
	try {
		if (a && a.mode !== 'battle' && d && d.toast) d.toast(t, kind || '');
		else if (a && a.toast) a.toast(t, kind || '');
	} catch (e) { /* toasts are a convenience */ }
}

// ---------------------------------------------------------------------------------------------
// Save editing: the kingdom's own JSON save, changed and loaded back (Kingdom::Load checks and clamps it)
// ---------------------------------------------------------------------------------------------
const BIG = /("(?:seed|worldCounter|relicCounter)":\s*)(\d+)/g; // 64-bit counters stay exact as strings
function parseSave(t) { return JSON.parse(t.replace(BIG, '$1"$2"')); }
function saveText(s) { return JSON.stringify(s).replace(/("(?:seed|worldCounter|relicCounter)":)"(\d+)"/g, '$1$2'); }

/** Keeps the kingdom as it was before the first admin change (until the viewer replaces or drops the copy). */
function keepBackup() {
	const d = kd();
	if (!d || admin.backup) return;
	admin.backup = { v: 1, save: d.K.bridge.save(), offset: d.clock.offset, savedAt: Date.now() };
	lsSet(BACKUP_KEY, JSON.stringify(admin.backup));
}
function applied(ok, kind) {
	const d = kd();
	d.K.bridge.advance(d.nowSec());
	d.persist();
	d.refresh(true);
	if (ok) toast(ok, kind || 'gold');
	render();
}
/** fn(save, state) edits the parsed save in place; false cancels. */
function edit(fn, ok) {
	const d = kd();
	if (!d) { toast('Королевство не запущено', 'bad'); return false; }
	if (inBattle()) { toast('Королевство меняется только вне боя', 'bad'); return false; }
	const B = d.K.bridge;
	keepBackup();
	const before = B.save();
	let s;
	try {
		s = parseSave(before);
		if (fn(s, B.state()) === false) return false;
	} catch (e) {
		addLog('error', 'Админ: правка не удалась: ' + text(e));
		toast('Правка не удалась: ' + (e.message || e), 'bad');
		return false;
	}
	if (!B.load(saveText(s))) {
		const err = B.error();
		B.load(before);
		addLog('warn', 'Админ: игра не приняла правку: ' + err);
		toast('Игра не приняла правку: ' + err, 'bad');
		return false;
	}
	applied(ok);
	return true;
}
function newId(s) { const id = s.nextId || 1; s.nextId = id + 1; return id; }
const army = () => kd().K.army;
const defs = () => kd().K.defs;
const rules = () => kd().K.rules;
function defIndex(id) { return defs().findIndex((d) => d.id === id); }
function castleOf(s) { const c = s.buildings.find((b) => b.def === 'castle'); return c ? c.level : 1; }
/** Highest level of a building the Castle allows (the Castle itself: its last level). */
function maxForCastle(d, castle) {
	if (d.id === 'castle') return d.levels.length;
	let n = 1;
	d.levels.forEach((l, i) => { if ((l.castle || 1) <= castle) n = i + 1; });
	return n;
}

// ---------------------------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------------------------
function skipTime(sec) {
	const d = kd();
	if (!d) return;
	if (inBattle()) { toast('Время двигается только вне боя', 'bad'); return; }
	keepBackup();
	d.clock.offset += sec;
	applied('Время ушло вперёд на ' + fmtDur(sec));
}
/** Every running timer ends now: constructions, training and research queues, spell labs, titans' rest, reinforcements. */
function finishTimers(which) {
	const all = !which;
	return edit((s) => {
		const t = s.time;
		if (all || which === 'build') for (const b of s.buildings) if (b.upgrading) { b.finishAt = t; b.startAt = Math.min(b.startAt, t); }
		if (all || which === 'army') {
			if (s.army.queue.length) s.army.queueStart = t - 1e8;
			if (s.army.research) s.army.researchUntil = t;
		}
		if (all || which === 'spells') {
			if (s.spells.queue.length) s.spells.queueStart = t - 1e8;
			for (const lab of s.spells.research) lab.until = t;
		}
		if (all || which === 'titans') for (const ti of s.titans) ti.recoverUntil = 0;
		if (all || which === 'ally') {
			for (const r of s.alliance.reinforcements) r.at = Math.min(r.at, t);
			s.alliance.asked = false;
		}
	}, all ? 'Все таймеры завершены' : 'Готово');
}

// ---------------------------------------------------------------------------------------------
// Checks: invariants of the kingdom and determinism of saving and of time
// ---------------------------------------------------------------------------------------------
function firstDiff(a, b) {
	let i = 0;
	while (i < a.length && a[i] === b[i]) i++;
	return '…' + a.slice(Math.max(0, i - 40), i + 40) + '… / …' + b.slice(Math.max(0, i - 40), i + 40) + '…';
}
/** Rules a kingdom state must keep whatever was played: add(level, text) for each broken one. */
function invariants(st, add) {
	const d = kd();
	const K = d.K;
	const B = K.bridge;
	const res = ['золото', 'еда', 'самоцветы', 'камни портала', 'осколки', 'очки победы'];
	st.res.forEach((v, i) => { if (!Number.isFinite(v) || v < 0) add('bad', 'Ресурс «' + res[i] + '» = ' + v); });
	if (st.res[0] > st.cap[0]) add('warn', 'Золота больше хранилищ: ' + fmt(st.res[0]) + ' из ' + fmt(st.cap[0]));
	if (st.res[1] > st.cap[1]) add('warn', 'Еды больше хранилищ: ' + fmt(st.res[1]) + ' из ' + fmt(st.cap[1]));
	if (st.res[3] > st.cap[3]) add('warn', 'Камней портала больше лимита: ' + st.res[3] + ' из ' + st.cap[3]);
	if (st.busy > st.builders) add('bad', 'Строек больше, чем строителей: ' + st.busy + ' из ' + st.builders);
	for (const b of st.b) {
		const df = K.defs[b.def];
		if (!B.canPlace(b.def, b.x, b.y, b.id)) add('bad', df.name + ' (#' + b.id + ') стоит вне участка или поверх другой постройки (' + b.x + ', ' + b.y + ')');
		if (b.lv > df.levels.length || b.lv < 0) add('bad', df.name + ': уровень ' + b.lv + ' из ' + df.levels.length);
		if (b.up && b.f < st.now) add('bad', df.name + ': стройка должна была закончиться ' + fmtDur(st.now - b.f) + ' назад');
	}
	st.shop.forEach((c, i) => { if (c.cnt > c.max && c.max >= 0) add('warn', K.defs[i].name + ': построено ' + c.cnt + ', а Замок разрешает ' + c.max); });
	const a = st.army;
	if (a.total > a.cap) add('warn', 'Отрядов больше, чем вмещает Лагерь армии: ' + a.total + ' из ' + a.cap);
	a.lv.forEach((lv, i) => { if (lv > a.lvCap[i]) add('warn', K.army.troops[i].name + ': уровень ' + lv + ' выше потолка ' + a.lvCap[i]); });
	a.n.forEach((n, i) => { if (n < 0) add('bad', K.army.troops[i].name + ': отрядов ' + n); });
	if (st.titans.length > st.tcap) add('warn', 'Титанов больше места в Зале титанов: ' + st.titans.length + ' из ' + st.tcap);
	const owner = new Map();
	for (const t of st.titans) {
		const nm = K.army.titans[t.def].name + ' (#' + t.id + ')';
		if (t.lv > t.max || t.lv < 1) add('bad', nm + ': уровень ' + t.lv + ' из ' + t.max);
		if (t.relics.length > t.slots) add('bad', nm + ': реликвий ' + t.relics.length + ', слотов ' + t.slots);
		for (const r of t.relics) {
			if (owner.has(r)) add('bad', 'Реликвия #' + r + ' надета на двух титанов');
			owner.set(r, t.id);
			if (!st.relics.some((x) => x.id === r)) add('bad', nm + ' носит реликвию #' + r + ', которой нет');
		}
	}
	const onB = new Map();
	for (const r of st.relics) {
		if (r.dur > r.max) add('bad', 'Реликвия #' + r.id + ': прочность ' + r.dur + ' из ' + r.max);
		if (r.b) { if (onB.has(r.b)) add('bad', 'На постройке #' + r.b + ' две реликвии'); onB.set(r.b, r.id); }
		if (r.b && r.on) add('bad', 'Реликвия #' + r.id + ' одновременно на титане и на постройке');
	}
	const maxRelics = K.army.relics.maxRelics || 200;
	if (st.relics.length > maxRelics) add('warn', 'Реликвий больше склада: ' + st.relics.length + ' из ' + maxRelics);
	if (st.lands.length > st.landCap) add('warn', 'Земель больше лимита: ' + st.lands.length + ' из ' + st.landCap);
	const guards = new Map();
	for (const l of st.lands) {
		if (l.titan && !st.titans.some((t) => t.id === l.titan)) add('bad', 'Землю #' + l.id + ' охраняет титан #' + l.titan + ', которого нет');
		if (l.titan && guards.has(l.titan)) add('bad', 'Титан #' + l.titan + ' охраняет две земли');
		if (l.titan) guards.set(l.titan, l.id);
		if (l.st > l.cap) add('bad', 'Земля #' + l.id + ': в хранилище ' + l.st + ' из ' + l.cap);
	}
	if (st.world.length !== rules().lands.worldSize) add('bad', 'Земель мира ' + st.world.length + ', а должно быть ' + rules().lands.worldSize);
	if (st.spells.total > st.spells.cap) add('warn', 'Заклинаний больше вместимости Кузницы: ' + st.spells.total + ' из ' + st.spells.cap);
	if (!(st.season.end > st.now)) add('bad', 'Сезон ' + st.season.no + ' должен был закончиться');
	if (st.pending && !inBattle()) add('warn', 'Висит незавершённый бой за землю #' + st.pending);
}
/** Returns [[level, text]] with level 'ok' | 'warn' | 'bad'; the kingdom is left as it was. */
function runChecks() {
	const d = kd();
	const B = d.K.bridge;
	const out = [];
	const add = (l, t) => out.push([l, t]);
	const s0 = B.save();
	let st;
	try { st = B.state(); } catch (e) { add('bad', 'Состояние королевства не читается: ' + (e.message || e)); return out; }
	try {
		if (!B.load(s0)) add('bad', 'Сохранение не загружается обратно: ' + B.error());
		else {
			const s1 = B.save();
			const st1 = B.state();
			if (s1 !== s0) add('bad', 'Сохранить → загрузить → сохранить даёт другой текст: ' + firstDiff(s0, s1));
			else add('ok', 'Сохранение загружается без потерь');
			if (st1.hash !== st.hash) add('bad', 'Хэш состояния изменился после загрузки: ' + st.hash + ' → ' + st1.hash);
			else add('ok', 'Хэш состояния после загрузки тот же');
		}
		// Time must give the same kingdom whether it passes at once or in steps (the page advances every second).
		const t0 = st.now;
		B.load(s0);
		B.advance(t0 + 21600);
		const hA = B.state().hash;
		const sA = B.save();
		B.load(s0);
		for (let i = 1; i <= 72; i++) B.advance(t0 + 300 * i);
		const hB = B.state().hash;
		const sB = B.save();
		if (hA !== hB) add('bad', '6 часов разом и по 5 минут дают разные королевства: ' + firstDiff(sA, sB));
		else add('ok', '6 часов разом и по 5 минут дают одно и то же королевство');
	} catch (e) {
		add('bad', 'Проверка сохранения упала: ' + (e.message || e));
	} finally {
		B.load(s0);
	}
	invariants(st, add);
	if (out.filter((x) => x[0] !== 'ok').length === 0) add('ok', 'Ресурсы, постройки, армия, титаны, реликвии, земли и сезон в порядке');
	return out;
}

// ---------------------------------------------------------------------------------------------
// Random play: many random actions through the regular dk_* calls, checking the kingdom after each one
// ---------------------------------------------------------------------------------------------
function rng(seed) {
	let a = seed >>> 0;
	return () => {
		a = (a + 0x6D2B79F5) >>> 0;
		let t = a;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}
function runMonkey(steps, seed, keep) {
	const d = kd();
	const B = d.K.bridge;
	const M = B.M;
	const r = rng(seed);
	const pick = (list) => (list.length ? list[Math.floor(r() * list.length)] : undefined);
	const start = B.save();
	const startNow = B.state().now;
	let t = startNow;
	const tally = {};
	const fails = [];
	const seen = new Set();
	const kindOf = (txt) => txt.replace(/[#\d][\d\s,]*/g, '#');
	// Breaks the kingdom already had (an admin edit, say) are not the random play's.
	invariants(B.state(), (l, txt) => seen.add(kindOf(txt)));
	let done = 0;
	const t0 = performance.now();
	const acts = [
		['сбор', 6, () => JSON.parse(B.str(M._dk_collect_all())).err],
		['улучшение', 8, (st) => { const b = pick(st.b.filter((x) => !x.up)); return b ? B.upgrade(b.id, r() < 0.3) : -1; }],
		['постройка', 4, (st) => {
			const i = pick(st.shop.map((c, k) => k).filter((k) => st.shop[k].cnt < st.shop[k].max));
			if (i === undefined) return -1;
			const p = d.findSpot(i);
			return p ? B.build(i, p.x, p.y, r() < 0.3) : -1;
		}],
		['ускорение стройки', 3, (st) => { const b = pick(st.b.filter((x) => x.up)); return b ? B.speedup(b.id) : -1; }],
		['обучение', 6, (st) => B.train(Math.floor(r() * st.army.lv.length))],
		['отмена обучения', 1, (st) => (st.army.queue.length ? B.cancelTrain(Math.floor(r() * st.army.queue.length)) : -1)],
		['ускорение обучения', 2, () => B.skipTrain()],
		['роспуск', 1, (st) => { const i = pick(st.army.n.map((n, k) => k).filter((k) => st.army.n[k] > 0)); return i === undefined ? -1 : B.dismiss(i, 1); }],
		['исследование войск', 3, (st) => B.upgradeTroop(Math.floor(r() * st.army.lv.length), r() < 0.3)],
		['ускорение исследования', 1, () => B.skipResearch()],
		['заклинание', 3, (st) => B.craft(Math.floor(r() * st.spells.stock.length))],
		['ускорение заклинаний', 1, () => B.skipCraft()],
		['улучшение заклинания', 2, (st) => B.upgradeSpell(Math.floor(r() * st.spells.stock.length), r() < 0.3)],
		['алтарь', 3, (st) => B.openAltar(Math.floor(r() * st.altars.length)).err],
		['надеть реликвию', 2, (st) => { const ti = pick(st.titans); const rl = pick(st.relics.filter((x) => !x.on && !x.b)); return ti && rl ? B.equip(ti.id, rl.id) : -1; }],
		['снять реликвию', 1, (st) => { const rl = pick(st.relics.filter((x) => x.on || x.b)); return rl ? B.unequip(rl.id) : -1; }],
		['реликвия на постройку', 1, (st) => { const b = pick(st.b); const rl = pick(st.relics.filter((x) => !x.on && !x.b)); return b && rl ? B.attach(b.id, rl.id) : -1; }],
		['починка реликвии', 2, (st) => {
			const target = pick(st.relics);
			if (!target) return -1;
			const ids = st.relics.filter((x) => x.id !== target.id && !x.on && !x.b && r() < 0.4).slice(0, 4).map((x) => x.id);
			return ids.length ? B.repair(target.id, ids).err : -1;
		}],
		['выбросить реликвию', 1, (st) => { const rl = pick(st.relics.filter((x) => !x.on && !x.b)); return rl ? B.discard(rl.id) : -1; }],
		['слияние титанов', 1, (st) => {
			const target = pick(st.titans);
			if (!target) return -1;
			const ids = st.titans.filter((x) => x.id !== target.id && r() < 0.5).slice(0, 3).map((x) => x.id);
			return ids.length ? B.fuse(target.id, ids) : -1;
		}],
		['возвышение', 1, (st) => { const ti = pick(st.titans); return ti ? B.ascend(ti.id, r() < 0.5) : -1; }],
		['отдых титана', 1, (st) => { const ti = pick(st.titans.filter((x) => x.rec > st.now)); return ti ? B.skipTitan(ti.id) : -1; }],
		['страж земли', 2, (st) => { const l = pick(st.lands); const ti = pick(st.titans); return l && ti ? B.assign(l.id, ti.id) : -1; }],
		['отозвать стража', 1, (st) => { const l = pick(st.lands.filter((x) => x.titan)); return l ? B.recall(l.id) : -1; }],
		['бой за землю', 6, (st) => {
			const w = pick(st.world);
			if (!w) return -1;
			const capture = r() < 0.5;
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = pick(st.titans.filter((x) => x.rec <= st.now));
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const code = B.attackStart(w.id, capture, squads, ti ? ti.id : 0, spells, r() < 0.3);
			if (code !== 0) return code;
			const st2 = B.state();
			const stars = Math.floor(r() * 4);
			const surv = st2.army.away.map(() => Math.floor(r() * 101));
			const used = st2.spells.away.map(() => (r() < 0.5 ? 1 : 0));
			return B.attackFinish(w.id, capture, stars, surv, used).err;
		}],
		['сбор с земли', 2, (st) => { const l = pick(st.lands); return l ? B.collectLand(l.id).err : -1; }],
		['бросить землю', 1, (st) => { const l = pick(st.lands); return l && r() < 0.3 ? B.abandonLand(l.id) : -1; }],
		['расширение', 1, () => B.buyExpansion(r() < 0.5)],
		['слот земли', 1, () => B.buySlot()],
		['камни портала', 1, () => B.refillStones()],
		['вступить в альянс', 1, () => { const list = B.alliances(); return B.join(Math.floor(r() * (list && list.all ? list.all.length : 1))); }],
		['основать альянс', 1, () => B.create('Тест ' + Math.floor(r() * 1000), 'Т' + Math.floor(r() * 100))],
		['выйти из альянса', 1, () => (r() < 0.3 ? B.leave() : -1)],
		['подкрепления', 2, () => B.reinforce()],
		['время', 12, () => { t += r() < 0.08 ? Math.floor(86400 * (1 + r() * 3)) : Math.floor(1 + r() * 14400); B.advance(t); return 0; }],
	];
	const total = acts.reduce((x, y) => x + y[1], 0);
	try {
		for (let i = 1; i <= steps && fails.length < 20; i++) {
			let pickW = r() * total;
			let act = acts[0];
			for (const x of acts) { pickW -= x[1]; if (pickW <= 0) { act = x; break; } }
			let st;
			try {
				st = B.state();
				const code = act[2](st);
				const key = act[0] + (code === 0 ? '' : ' (отказ)');
				if (code !== -1) tally[key] = (tally[key] || 0) + 1;
				B.advance(t);
				const st2 = B.state();
				if (st2.res.some((v) => !Number.isFinite(v) || v < 0)) fails.push([i, act[0], 'ресурсы стали ' + st2.res.join(', ')]);
				if (st2.busy > st2.builders) fails.push([i, act[0], 'строек ' + st2.busy + ' при ' + st2.builders + ' строителях']);
				const s1 = B.save();
				if (!B.load(s1)) fails.push([i, act[0], 'сохранение не загружается: ' + B.error()]);
				else if (B.save() !== s1) fails.push([i, act[0], 'сохранение меняется при загрузке: ' + firstDiff(s1, B.save())]);
				else if (B.state().hash !== st2.hash) fails.push([i, act[0], 'хэш меняется при загрузке']);
				// The same kind of break is reported once, at the action that first caused it.
				invariants(st2, (l, txt) => {
					const kind = kindOf(txt);
					if (!seen.has(kind)) { seen.add(kind); fails.push([i, act[0], (l === 'warn' ? 'предупреждение: ' : '') + txt]); }
				});
			} catch (e) {
				fails.push([i, act[0], 'исключение: ' + (e.message || e)]);
			}
			done = i;
		}
	} finally {
		const result = B.save();
		if (!keep) B.load(start);
		else {
			keepBackup();
			// The kingdom played ahead of the page clock: move the clock so it does not wait for real time to catch up.
			if (t > d.nowSec()) d.clock.offset += t - d.nowSec();
		}
		admin.monkey = { steps: done, seed, ms: Math.round(performance.now() - t0), tally, fails, kept: !!keep, played: fmtDur(t - startNow), start, result };
		if (fails.length) addLog('error', 'Случайная игра (зерно ' + seed + '): ' + fails.length + ' сбоев, первый на ходу ' + fails[0][0] + ' (' + fails[0][1] + '): ' + fails[0][2]);
		applied('', '');
	}
}

// ---------------------------------------------------------------------------------------------
// Battles
// ---------------------------------------------------------------------------------------------
function parseLines(t) { return JSON.parse(String(t).replace(/^\s*\/\/.*$/gm, '')); }
function scenarios() {
	const a = app();
	const files = a.contentFiles();
	const named = new Map((a.scenarios || []).map((s) => [s.path, s.name]));
	const out = [];
	for (const p of Object.keys(files).sort()) {
		if (!/^scenarios\/[^_][^/]*\.json$/.test(p)) continue;
		try {
			const sc = parseLines(files[p]);
			out.push({ path: p, name: named.get(p) || sc.name || p, sc });
		} catch (e) { /* not a scenario */ }
	}
	out.sort((x, y) => (named.has(y.path) ? 1 : 0) - (named.has(x.path) ? 1 : 0));
	return out;
}
function titanIds() { const a = app(); return a.titanIds || []; }
function titanName(id) { const a = app(); return (a.titanNames && a.titanNames[id]) || id.replace('titan.', ''); }
const AIS = [['recruit', 'Новобранец'], ['veteran', 'Ветеран'], ['warlord', 'Полководец'], ['nightmare', 'Кошмар']];
function startBattle() {
	const a = app();
	const b = admin.battle;
	const list = scenarios();
	const base = list.find((x) => x.path === b.sc) || list[0];
	if (!base) { toast('Нет сценариев', 'bad'); return; }
	const sc = JSON.parse(JSON.stringify(base.sc));
	const side = (s, id, lv, stars, asc, pos, face) => {
		if (!id) return;
		if (id === '-') { delete s.titan; return; }
		s.titan = Object.assign({ abilityLevels: [1, 1], pos, facingDeg: face }, s.titan || {}, { id });
		if (lv) s.titan.level = lv;
		if (stars) s.titan.stars = stars;
		if (asc) s.titan.ascended = true;
	};
	side(sc.sides.attacker, b.titan, b.tlv, b.tst, b.asc, [120, 26], 0);
	side(sc.sides.defender, b.etitan, b.elv, b.est, false, [120, 134], 180);
	sc.name = base.name + ' (админ)';
	const path = 'scenarios/_admin_battle.json';
	a.sim.addFile(path, JSON.stringify(sc));
	closePanel();
	if (!a.startLandBattle(path, b.ai, { kind: 'training' })) toast('Бой не запустился, см. лог', 'bad');
}
function endLandBattle(stars) {
	const a = app();
	const g = demo() && demo().game;
	if (!g || !g.context || g.context.kind !== 'land') { toast('Сейчас не бой за землю', 'bad'); return; }
	closePanel();
	a.exitToKingdom(stars, false);
}

// ---------------------------------------------------------------------------------------------
// Save, backup and bug report
// ---------------------------------------------------------------------------------------------
async function copyText(t, what) {
	let ok = false;
	try { if (navigator.clipboard && navigator.clipboard.writeText) { await navigator.clipboard.writeText(t); ok = true; } } catch (e) { ok = false; }
	if (!ok) {
		const ta = document.createElement('textarea');
		ta.value = t;
		ta.setAttribute('readonly', '');
		ta.style.cssText = 'position:fixed;left:-9999px;top:0;opacity:0';
		document.body.appendChild(ta);
		ta.select();
		try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
		ta.remove();
	}
	if (ok) toast(what + ' скопирован' + (what.endsWith('а') ? 'а' : '') + ' в буфер обмена', 'gold');
	else {
		admin.copyFallback = t;
		toast('Браузер не дал скопировать: текст показан ниже, выделите и скопируйте его вручную', 'bad');
		render();
	}
}
function download(name, t) {
	try {
		const url = URL.createObjectURL(new Blob([t], { type: 'application/json' }));
		const el = document.createElement('a');
		el.href = url;
		el.download = name;
		document.body.appendChild(el);
		el.click();
		el.remove();
		setTimeout(() => URL.revokeObjectURL(url), 5000);
		toast('Файл ' + name + ' отдан браузеру', 'gold');
	} catch (e) {
		toast('Скачивание недоступно: ' + (e.message || e), 'bad');
	}
}
function kingdomSummary() {
	const d = kd();
	if (!d) {
		const err = window.dotKingdom && window.dotKingdom.error;
		return 'королевство не запущено' + (err ? ': ' + err : '');
	}
	const st = d.K.bridge.state();
	let s = {};
	try { s = parseSave(d.K.bridge.save()); } catch (e) { /* summary only */ }
	const ahead = Math.round(d.clock.offset);
	return 'Замок ' + st.castle + ', хэш ' + st.hash + ', зерно ' + s.seed + ', формат ' + s.formatVersion + ', контент ' + s.contentHash +
		', земель ' + st.lands.length + ', титанов ' + st.titans.length + (ahead > 0 ? ', время игры впереди реального на ' + fmtDur(ahead) : '');
}
function report(withSave) {
	const a = app();
	const g = demo() && demo().game;
	const lines = ['Dawn of Titans — отчёт для поиска бага', 'Время: ' + dateText(Date.now()), 'Браузер: ' + navigator.userAgent,
		'Экран: ' + window.innerWidth + '×' + window.innerHeight + ' @' + (window.devicePixelRatio || 1) + (a && a.isTouch ? ', сенсорный' : ''),
		'Режим: ' + where() + (g && g.context ? ' (' + g.context.kind + ')' : '') + ', симуляция ' + (a && a.sim ? a.sim.mode : 'не загружена'),
		'Королевство: ' + kingdomSummary()];
	if (admin.checks) lines.push('Проверки: ' + admin.checks.filter((x) => x[0] !== 'ok').map((x) => (x[0] === 'bad' ? '✗ ' : '⚠ ') + x[1]).join('; ') || 'все в порядке');
	if (admin.monkey) {
		const m = admin.monkey;
		lines.push('Случайная игра: зерно ' + m.seed + ', ходов ' + m.steps + ', сбоев ' + m.fails.length + (m.fails.length ? ': ' + m.fails.map((f) => 'ход ' + f[0] + ' «' + f[1] + '» ' + f[2]).join('; ') : ''));
		if (m.fails.length && withSave) lines.push('Королевство перед случайной игрой:', m.start);
	}
	const items = log.items.slice(-60);
	lines.push('', 'Лог (' + items.length + ' последних из ' + log.items.length + '):');
	for (const x of items) lines.push('[' + clockText(x.t) + '] ' + ({ error: 'ошибка', warn: 'предупреждение', info: 'инфо' }[x.l] || x.l) + (x.w ? ' (' + x.w + ')' : '') + (x.n > 1 ? ' ×' + x.n : '') + ': ' + x.m);
	const d = kd();
	if (withSave && d) lines.push('', 'Сохранение:', d.K.bridge.save());
	return lines.join('\n');
}
function loadSaveText(t, label) {
	const d = kd();
	if (!d) { toast('Королевство не запущено', 'bad'); return; }
	t = String(t || '').trim();
	let rec = null;
	try {
		const j = JSON.parse(t);
		// Accepts the bare save, the browser record {v, save, offset, savedAt} and the cloud document.
		rec = j && typeof j.save === 'string' ? { v: 1, save: j.save, offset: +j.offset || 0, savedAt: Date.now() } : { v: 1, save: t, offset: d.clock.offset, savedAt: Date.now() };
		const s = parseSave(rec.save);
		// A save from a clock ahead of this page's would wait for real time: move the page clock up to it.
		const now = Date.now() / 1000;
		if (s.time > now + rec.offset) rec.offset = s.time - now + 1;
	} catch (e) { toast('Это не сохранение королевства: ' + (e.message || e), 'bad'); return; }
	keepBackup();
	if (!d.adopt(rec)) { toast('Игра не приняла сохранение: ' + d.K.bridge.error(), 'bad'); return; }
	d.persist();
	toast(label || 'Сохранение загружено', 'gold');
	render();
}

// ---------------------------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------------------------
const TABS = [['res', 'Ресурсы'], ['bld', 'Постройки'], ['army', 'Армия'], ['titans', 'Титаны'], ['time', 'Время'], ['lands', 'Земли'],
	['battle', 'Бои'], ['check', 'Проверки'], ['save', 'Сохранение'], ['log', 'Лог']];
const KINGDOM_TABS = new Set(['res', 'bld', 'army', 'titans', 'time', 'lands', 'check']);
const WRENCH = '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' +
	'<path d="M12.5 3.2a4 4 0 0 0-4.6 5.3l-5 5a1.6 1.6 0 0 0 2.3 2.3l5-5a4 4 0 0 0 5.3-4.6l-2.4 2.4-2.3-.6-.6-2.3z"/></svg>';
function attrs(o) { return o ? Object.keys(o).map((k) => ' data-' + k + '="' + esc(o[k]) + '"').join('') : ''; }
const b = (a, label, data, cls) => '<button class="btn' + (cls ? ' ' + cls : '') + '" data-a="' + a + '"' + attrs(data) + '>' + label + '</button>';
const row = (name, buttons, sub) => '<div class="admRow"><div class="nm">' + name + (sub ? '<small>' + sub + '</small>' : '') + '</div><div class="bs">' + buttons + '</div></div>';
const seg = (a, items, cur, data) => '<div class="seg admSeg">' + items.map(([v, l]) => '<button class="' + (String(v) === String(cur) ? 'on' : '') + '" data-a="' + a + '" data-v="' + esc(v) + '"' + attrs(data) + '>' + l + '</button>').join('') + '</div>';
function select(name, items, cur) {
	return '<select data-c="' + name + '">' + items.map(([v, l]) => '<option value="' + esc(v) + '"' + (String(v) === String(cur) ? ' selected' : '') + '>' + esc(l) + '</option>').join('') + '</select>';
}

function updateButton() {
	const btn = $('admBtn');
	if (!btn) return;
	const sheetOpen = ['kSheet', 'menu', 'results', 'help', 'loading'].some((id) => { const el = $(id); return el && !el.hidden; });
	btn.hidden = !admin.on || admin.open || sheetOpen;
	const n = newErrors();
	const badge = $('admBadge');
	if (badge) { badge.hidden = !n; badge.textContent = n > 99 ? '99+' : String(n); }
}
function openPanel(tab) {
	admin.open = true;
	if (typeof tab === 'string') admin.tab = tab;
	else if (inBattle() && KINGDOM_TABS.has(admin.tab)) admin.tab = 'battle';
	$('admSheet').hidden = false;
	render();
	updateButton();
}
function closePanel() {
	admin.open = false;
	admin.copyFallback = '';
	$('admSheet').hidden = true;
	updateButton();
}
function setOn(v) {
	admin.on = v;
	lsSet(ON_KEY, v ? '1' : null);
	if (!v) closePanel();
	updateButton();
	toast(v ? 'Режим админа включён: кнопка с ключом справа' : 'Режим админа выключен', 'gold');
}

function render() {
	if (!admin.open) return;
	const panel = $('admPanel');
	const top = panel.scrollTop;
	if (admin.tab === 'log') log.seen = log.items.length;
	let body = '';
	try {
		body = renderTab(admin.tab);
	} catch (e) {
		addLog('error', 'Админ: вкладка «' + admin.tab + '» не открылась: ' + text(e));
		body = '<p class="note">Вкладка не открылась: ' + esc(e.message || e) + '</p>';
	}
	const d = kd();
	const head = d ? 'Замок ' + d.K.st.castle + ' · хэш <code>' + esc(d.K.st.hash) + '</code>' : 'королевство не запущено';
	panel.innerHTML = '<h2 id="admTitle">' + WRENCH + '<span>Режим админа</span></h2><p class="fine">' + head + '</p>' +
		'<div class="shopTabs admTabs">' + TABS.map(([id, l]) => '<button class="' + (id === admin.tab ? 'on' : '') + '" data-a="tab" data-v="' + id + '">' + l +
			(id === 'log' && newErrors() ? ' <b class="admDot">' + newErrors() + '</b>' : '') + '</button>').join('') + '</div>' +
		'<p class="admMsg" id="admMsg">' + esc(admin.msg) + '</p>' + body +
		(admin.copyFallback ? '<h3>Текст для копирования</h3><textarea readonly class="admText">' + esc(admin.copyFallback) + '</textarea>' : '') +
		'<div class="actions">' + b('off', 'Выключить режим админа', null, 'danger') + b('close', 'Закрыть', null, 'primary') + '</div>';
	panel.scrollTop = top;
	updateButton();
}
function needKingdom() {
	const d = kd();
	if (!d) {
		const err = window.dotKingdom && window.dotKingdom.error;
		return '<p class="note">Королевство не запущено' + (err ? ': ' + esc(err) : '') + '. Работают вкладки «Бои», «Сохранение» и «Лог».</p>';
	}
	if (inBattle()) return '<p class="note">Идёт бой. Королевство меняется после выхода из боя; управление боем — на вкладке «Бои».</p>';
	return '';
}
function renderTab(tab) {
	if (KINGDOM_TABS.has(tab)) {
		const stop = needKingdom();
		if (stop) return stop;
	}
	const d = kd();
	const st = d ? d.K.st : null;
	switch (tab) {
	case 'res': {
		const R = [['gold', 'Золото', [1e3, 1e4, 1e5, 1e6]], ['food', 'Еда', [1e3, 1e4, 1e5, 1e6]], ['gems', 'Самоцветы', [100, 1e3, 1e4]],
			['stones', 'Камни портала', [3, 6, 30]], ['shards', 'Осколки', [10, 100, 1e3]], ['vp', 'Очки победы', [100, 1e3, 1e4]]];
		const short = (n) => (n >= 1e6 ? n / 1e6 + ' млн' : n >= 1e3 ? n / 1e3 + ' тыс.' : String(n));
		let h = '<h3>Ресурсы</h3><div class="admList">';
		R.forEach(([k, name, steps], i) => {
			const cap = i === 0 || i === 1 || i === 3 ? ' из ' + fmt(st.cap[i]) : '';
			h += row(name + ' <b>' + fmt(st.res[i]) + '</b>', steps.map((n) => b('res-add', '+' + short(n), { r: k, n })).join('') + b('res-add', '0', { r: k, n: -1e15 }) +
				'<input type="number" inputmode="numeric" min="0" placeholder="число" data-in="' + k + '"' + typedAttr(k) + '>' + b('res-set', '=', { r: k }), cap);
		});
		h += '</div><div class="actions">' + b('res-fill', 'Заполнить хранилища и камни') + b('res-rich', 'Богатое королевство') + '</div>';
		h += '<p class="fine">«Богатое королевство»: полные хранилища и камни, +100 000 самоцветов, +5 000 осколков. Первая правка сохраняет копию королевства (вкладка «Сохранение»).</p>';
		h += '<h3>Город</h3><div class="admList">' +
			row('Расширения города <b>' + st.expansions + ' из ' + rules().expansions.length + '</b>', b('exp', '−', { n: -1 }) + b('exp', '+', { n: 1 })) +
			row('Купленные места для земель <b>' + st.extraSlots + '</b>', b('slots', '−', { n: -1 }) + b('slots', '+', { n: 1 }), 'лимит земель ' + st.landCap) +
			row('Строители <b>' + st.busy + ' заняты из ' + st.builders + '</b>', b('finish', 'Достроить всё', { w: 'build' }), 'число строителей задаёт Двор строителей') + '</div>';
		return h;
	}
	case 'bld': {
		const castle = st.castle;
		let h = '<div class="actions">' + b('bld-castle', 'Замок −1', { n: -1 }) + b('bld-castle', 'Замок +1', { n: 1 }) + b('bld-allmax', 'Всё до максимума по Замку') + b('finish', 'Достроить всё', { w: 'build' }) + '</div>';
		h += '<h3>Постройки (' + st.b.length + ')</h3><div class="admList">';
		const list = st.b.slice().sort((x, y) => x.def - y.def || x.id - y.id);
		for (const x of list) {
			const df = defs()[x.def];
			const mx = maxForCastle(df, castle);
			h += row(esc(df.name) + ' <b>' + x.lv + '</b>', b('bld-lv', '−', { id: x.id, n: -1 }) + b('bld-lv', '+', { id: x.id, n: 1 }) + b('bld-lv', 'макс.', { id: x.id, n: 999 }) +
				(x.up ? b('bld-done', 'Достроить', { id: x.id }) : ''), '#' + x.id + ' · по Замку до ' + mx + ' из ' + df.levels.length + (x.up ? ' · строится' : ''));
		}
		h += '</div><h3>Поставить бесплатно</h3><div class="admList">';
		st.shop.forEach((c, i) => {
			const df = defs()[i];
			if (c.cnt < c.max) h += row(esc(df.name), b('bld-new', 'Поставить ур. 1', { def: i }), 'есть ' + c.cnt + ' из ' + c.max);
		});
		h += '</div><p class="fine">Новая постройка встаёт на первое свободное место у центра экрана. Лимиты по Замку соблюдаются; уровни выше разрешённых Замком можно задать кнопкой «макс.» — проверка покажет это как нарушение.</p>';
		return h;
	}
	case 'army': {
		const a = st.army;
		let h = '<div class="actions">' + b('troop-all', 'Все войска +5', { n: 5 }) + b('troop-cap', 'Уровни до потолка Арсенала') + b('finish', 'Закончить обучение и исследование', { w: 'army' }) + '</div>';
		h += '<h3>Войска · в лагере ' + a.total + ' из ' + a.cap + '</h3><div class="admList">';
		army().troops.forEach((t, i) => {
			h += row(esc(t.name) + ' <b>ур. ' + a.lv[i] + '</b> · ' + a.n[i] + ' отр.', b('troop-lv', '−', { i, n: -1 }) + b('troop-lv', '+', { i, n: 1 }) + b('troop-lv', 'макс.', { i, n: 99 }) +
				b('troop-n', '+1', { i, n: 1 }) + b('troop-n', '+5', { i, n: 5 }) + b('troop-n', '0', { i, n: -1e9 }), 'потолок Арсенала ' + a.lvCap[i] + ' · открывается на Замке ' + t.unlockCastle);
		});
		const sp = st.spells;
		h += '</div><h3>Заклинания · ' + sp.total + ' из ' + sp.cap + ', уровень до ' + sp.maxLv + '</h3><div class="admList">';
		army().spells.forEach((s, i) => {
			h += row(esc(s.name) + ' <b>×' + sp.stock[i] + '</b> · ур. ' + sp.lv[i], b('spell-n', '+1', { i, n: 1 }) + b('spell-n', '+5', { i, n: 5 }) + b('spell-n', '0', { i, n: -1e9 }) +
				b('spell-lv', '−', { i, n: -1 }) + b('spell-lv', '+', { i, n: 1 }));
		});
		h += '</div><div class="actions">' + b('finish', 'Закончить заклинания', { w: 'spells' }) + '</div>';
		return h;
	}
	case 'titans': {
		let h = '<div class="actions">' + b('finish', 'Все титаны отдохнули', { w: 'titans' }) + b('titan-allmax', 'Все титаны на макс. уровень') + '</div>';
		h += '<h3>Титаны · ' + st.titans.length + ' из ' + st.tcap + '</h3><div class="admList">';
		for (const t of st.titans) {
			const def = army().titans[t.def];
			const rest = t.rec > st.now ? ' · отдыхает ' + fmtDur(t.rec - st.now) : '';
			h += row(esc(def.name) + ' ' + '★'.repeat(def.stars) + ' <b>ур. ' + t.lv + '</b>', b('titan-lv', '−', { id: t.id, n: -1 }) + b('titan-lv', '+', { id: t.id, n: 1 }) +
				b('titan-lv', 'макс.', { id: t.id, n: 999 }) + b('titan-asc', t.asc ? 'Снять возвышение' : 'Возвысить', { id: t.id }) + b('titan-del', 'Убрать', { id: t.id }, 'danger'),
			'#' + t.id + ' · до ур. ' + t.max + ' · реликвий ' + t.relics.length + ' из ' + t.slots + rest);
		}
		h += '</div><h3>Получить титана</h3><div class="actions">' + army().titans.map((t, i) => b('titan-add', esc(t.name) + ' ' + '★'.repeat(t.stars), { i })).join('') + '</div>';
		const kinds = army().relics.kinds;
		const rl = admin.relic;
		h += '<h3>Реликвии · ' + st.relics.length + ' из ' + (army().relics.maxRelics || 200) + '</h3>' +
			'<div class="admForm">' + select('relic-kind', kinds.map((k, i) => [i, k.name + (k.type === 'economy' ? ' (постройки)' : '')]), rl.kind) +
			seg('relic-rar', [[1, '★'], [2, '★★'], [3, '★★★'], [4, '★4'], [5, '★5']], rl.rarity) +
			seg('relic-full', [[1, 'Все свойства макс.'], [0, 'Как из алтаря']], rl.full ? 1 : 0) + '</div>' +
			'<div class="actions">' + b('relic-add', 'Добавить реликвию') + b('relic-add10', '+10 случайных') + b('relic-fix', 'Починить все') + b('relic-clear', 'Убрать свободные', null, 'danger') +
			b('altar', 'Ежедневный алтарь сейчас') + '</div>';
		return h;
	}
	case 'time': {
		const ahead = d.clock.offset;
		const se = st.season;
		let h = '<h3>Время</h3><p class="fine">Время королевства ' + (ahead > 1 ? 'впереди реального на ' + fmtDur(ahead) : 'совпадает с реальным') +
			'. Назад время не идёт: вернуть прошлое можно только копией из вкладки «Сохранение».</p>' +
			seg('skip', [[60, '+1 мин'], [600, '+10 мин'], [3600, '+1 ч'], [21600, '+6 ч'], [86400, '+1 д'], [604800, '+7 д']], -1) +
			'<h3>Скорость времени</h3>' + seg('scale', [[1, '×1'], [10, '×10'], [100, '×100'], [1000, '×1000'], [10000, '×10000']], d.clock.scale) +
			'<h3>Таймеры</h3><div class="actions">' + b('finish', 'Завершить все таймеры', null, 'primary') + b('finish', 'Стройки', { w: 'build' }) + b('finish', 'Войска', { w: 'army' }) +
			b('finish', 'Заклинания', { w: 'spells' }) + b('finish', 'Отдых титанов', { w: 'titans' }) + b('stones', 'Камни портала до максимума') + b('altar', 'Ежедневный алтарь') + '</div>';
		const lg = se.league >= 0 ? rules().seasons.leagues[se.league] : null;
		h += '<h3>Сезон</h3>' + row('Сезон ' + se.no + ' · до конца ' + fmtDur(se.end - st.now), b('season-end', 'Закончить сезон сейчас', null, 'primary') + b('season-skip', '+4 сезона', { n: 4 }),
			'очков победы ' + fmt(st.res[5]) + (lg ? ' · ' + esc(lg.name) : ' · без лиги') + (se.last.no >= 0 ? ' · прошлый сезон ' + se.last.no + ': ' + fmt(se.last.vp) + ' ОП' : ''));
		const al = st.ally;
		const inAl = al && al.my >= 0;
		h += '<h3>Альянс</h3>' + row(inAl ? 'В альянсе «' + esc(al.name) + '»' : 'Не в альянсе',
			(inAl ? b('finish', 'Подкрепления прибыли', { w: 'ally' }) : b('res-add', '+1 000 ОП для вступления', { r: 'vp', n: 1000 })), inAl ? 'подкрепления: ' + (al.reinf ? al.reinf.length : 0) + ' в пути или в лагере' : 'вступление — с 1 000 очков победы');
		return h;
	}
	case 'lands': {
		const types = rules().lands.types;
		let h = '<h3>Земли мира</h3><div class="actions">' + b('world-new', 'Новые земли мира') + '</div>' + seg('world-lv', [[1, 'все ур. 1'], [3, 'ур. 3'], [5, 'ур. 5'], [7, 'ур. 7'], [9, 'ур. 9']], -1) + '<div class="admList">';
		for (const w of st.world) {
			h += row(esc(types[w.type].name) + ' <b>ур. ' + w.lv + '</b>', b('world-lvl', '−', { id: w.id, n: -1 }) + b('world-lvl', '+', { id: w.id, n: 1 }) + select('world-type:' + w.id, types.map((t, i) => [i, t.name]), w.type) +
				b('attack', 'Набег', { id: w.id, c: 0 }) + b('attack', 'Захват', { id: w.id, c: 1 }), '#' + w.id + ' · добыча ' + fmt(w.loot));
		}
		h += '</div><p class="fine">«Набег» и «Захват» открывают обычную подготовку к бою; камни портала докладываются, если их не хватает. Бой можно закончить сразу на вкладке «Бои».</p>';
		h += '<h3>Ваши земли · ' + st.lands.length + ' из ' + st.landCap + '</h3><div class="admList">';
		for (const l of st.lands) {
			const g = l.titan ? st.titans.find((t) => t.id === l.titan) : null;
			h += row(esc(types[l.type].name) + ' <b>ур. ' + l.lv + '</b>', b('land-lv', '−', { id: l.id, n: -1 }) + b('land-lv', '+', { id: l.id, n: 1 }) + b('land-fill', 'Заполнить', { id: l.id }) + b('land-del', 'Убрать', { id: l.id }, 'danger'),
				'#' + l.id + ' · ' + fmt(l.st) + ' из ' + fmt(l.cap) + (g ? ' · страж ' + esc(army().titans[g.def].name) : ''));
		}
		h += '</div><div class="admForm">' + select('land-type', types.map((t, i) => [i, t.name]), admin.land.type) +
			seg('land-newlv', [[1, 'ур. 1'], [3, 'ур. 3'], [5, 'ур. 5'], [9, 'ур. 9']], admin.land.lv) + '</div><div class="actions">' + b('land-add', 'Добавить землю') + b('land-fillall', 'Заполнить все') + '</div>';
		return h;
	}
	case 'battle': return renderBattle();
	case 'check': {
		let h = '<h3>Проверка королевства</h3><p class="fine">Сохранение без потерь, тот же хэш после загрузки, одно и то же королевство при разных шагах времени, ресурсы, лимиты, постройки на местах, реликвии, стражи и сезон. Королевство не меняется.</p>' +
			'<div class="actions">' + b('checks', 'Проверить королевство', null, 'primary') + '</div>';
		if (admin.checks) {
			h += '<div class="admList admChk">' + admin.checks.map(([l, t]) => '<div class="' + l + '">' + esc(t) + '</div>').join('') + '</div>';
		}
		const m = admin.monkey;
		h += '<h3>Случайная игра</h3><p class="fine">Сотни случайных действий подряд через обычные функции игры: стройки, войска, реликвии, слияния, бои за земли со случайным исходом, альянсы и прыжки времени. После каждого хода — сохранение, загрузка и хэш. Нарушения, которые были в королевстве до игры, не считаются. По умолчанию королевство потом возвращается как было.</p>' +
			'<div class="admForm"><label class="field">Ходов<input type="number" inputmode="numeric" min="10" max="5000" data-in="monkey-steps"' + typedAttr('monkey-steps', admin.monkeySteps) + '></label>' +
			'<label class="field">Зерно<input type="number" inputmode="numeric" min="1" data-in="monkey-seed"' + typedAttr('monkey-seed', admin.monkeySeed) + '></label></div>' +
			seg('monkey-keep', [[0, 'Вернуть королевство'], [1, 'Оставить результат']], admin.keepMonkey ? 1 : 0) +
			'<div class="actions">' + b('monkey', 'Сыграть случайно', null, 'primary') + b('monkey-next', 'Следующее зерно') + '</div>';
		if (m) {
			h += '<p class="' + (m.fails.length ? 'note' : 'fine') + '">Зерно ' + m.seed + ': ' + m.steps + ' ходов за ' + m.ms + ' мс, прошло игрового времени ' + m.played + ', сбоев ' + m.fails.length +
				(m.kept ? ' · результат оставлен' : ' · королевство возвращено') + '.</p>';
			if (m.fails.length) h += '<div class="admList admChk">' + m.fails.map((f) => '<div class="bad">Ход ' + f[0] + ' «' + esc(f[1]) + '»: ' + esc(f[2]) + '</div>').join('') + '</div>';
			h += '<p class="fine">' + Object.keys(m.tally).sort().map((k) => esc(k) + ' ' + m.tally[k]).join(' · ') + '</p>';
			if (m.fails.length) h += '<div class="actions">' + b('copy-report', 'Скопировать отчёт с королевством', { s: 1 }) + '</div>';
		}
		return h;
	}
	case 'save': return renderSave();
	case 'log': {
		const items = log.items.filter((x) => !admin.logErrorsOnly || x.l === 'error').slice().reverse();
		let h = '<div class="actions">' + b('copy-report', 'Скопировать отчёт для разработчика', { s: 1 }, 'primary') + b('copy-report', 'Отчёт без сохранения', { s: 0 }) +
			b('log-clear', 'Очистить лог', null, 'danger') + b('log-test', 'Тестовая ошибка') + '</div>' +
			'<p class="fine">Лог пишется всегда, даже когда режим админа выключен, и переживает перезагрузку (до ' + LOG_MAX + ' записей). Отчёт можно вставить в чат проекта: в нём браузер, экран, королевство, последние записи и сохранение.</p>' +
			seg('log-filter', [[0, 'Все записи (' + log.items.length + ')'], [1, 'Только ошибки']], admin.logErrorsOnly ? 1 : 0);
		if (!items.length) h += '<p class="fine">Записей нет.</p>';
		h += '<div class="admLog">' + items.slice(0, 150).map((x) => '<div class="' + x.l + '"><i>' + clockText(x.t) + (x.w ? ' · ' + x.w : '') + (x.n > 1 ? ' · ×' + x.n : '') + '</i>' + esc(x.m) + '</div>').join('') + '</div>';
		return h;
	}
	default: return '';
	}
}
function renderBattle() {
	const a = app();
	if (!a || !a.sim) return '<p class="note">Симуляция боя не загружена.</p>';
	let h = '';
	const g = demo() && demo().game;
	if (inBattle() && g && g.st) {
		const land = g.context && g.context.kind === 'land';
		const fr = g.fr || {};
		h += '<h3>Текущий бой' + (land ? ' за землю' : '') + '</h3><p class="fine">' + esc(g.st.name || '') + ' · фаза ' + (['расстановка', 'отсчёт', 'бой', 'окончен'][fr.phase] || fr.phase) +
			' · тик ' + (fr.tick || 0) + (g.paused ? ' · пауза' : '') + '</p>' +
			seg('bspeed', [[1, '×1'], [2, '×2'], [4, '×4'], [8, '×8']], g.speed) +
			'<div class="actions">' + b('bpause', g.paused ? 'Продолжить' : 'Пауза') + b('bauto', g.allAuto ? 'Автобой выкл.' : 'Автобой за меня') +
			b('bperf', document.body.classList.contains('adm-perf') ? 'Скрыть FPS' : 'Показать FPS') + '</div>';
		if (land) {
			h += '<div class="actions">' + b('bend', 'Победа ★★★', { n: 3 }, 'primary') + b('bend', '★★', { n: 2 }) + b('bend', '★', { n: 1 }) + b('bend', 'Поражение', { n: 0 }, 'danger') + '</div>' +
				'<p class="fine">Бой заканчивается сразу с выбранными звёздами; выжившие отряды и потраченные заклинания берутся из боя как он есть сейчас.</p>';
		} else if (a.kingdomReady) h += '<div class="actions">' + b('bexit', 'Выйти в королевство') + '</div>';
	}
	const list = scenarios();
	const bt = admin.battle;
	if (!bt.sc && list[0]) bt.sc = list[0].path;
	const base = list.find((x) => x.path === bt.sc) || list[0];
	const at = base && base.sc.sides.attacker.titan;
	const dt = base && base.sc.sides.defender.titan;
	const titanOpts = (cur) => [['', 'как в сценарии' + (cur ? ' (' + titanName(cur.id) + ', ур. ' + cur.level + ')' : ' (без титана)')], ['-', 'без титана']].concat(titanIds().map((id) => [id, titanName(id)]));
	h += '<h3>Любой бой</h3><div class="admForm">' +
		'<label class="field">Сценарий' + select('b-sc', list.map((x) => [x.path, x.name]), bt.sc) + '</label>' +
		'<label class="field">Ваш титан' + select('b-titan', titanOpts(at), bt.titan) + '</label>' +
		'<label class="field">Уровень<input type="number" inputmode="numeric" min="0" max="80" data-in="b-tlv"' + typedAttr('b-tlv', bt.tlv || '') + ' placeholder="как в сценарии"></label>' +
		'<label class="field">Звёзды<input type="number" inputmode="numeric" min="0" max="4" data-in="b-tst"' + typedAttr('b-tst', bt.tst || '') + ' placeholder="как в сценарии"></label>' +
		'<label class="field">Вражеский титан' + select('b-etitan', titanOpts(dt), bt.etitan) + '</label>' +
		'<label class="field">Уровень<input type="number" inputmode="numeric" min="0" max="80" data-in="b-elv"' + typedAttr('b-elv', bt.elv || '') + ' placeholder="как в сценарии"></label>' +
		'<label class="field">Звёзды<input type="number" inputmode="numeric" min="0" max="4" data-in="b-est"' + typedAttr('b-est', bt.est || '') + ' placeholder="как в сценарии"></label></div>' +
		seg('b-asc', [[0, 'Без возвышения'], [1, 'Ваш титан возвышен']], bt.asc ? 1 : 0) +
		'<h3>Противник</h3>' + seg('b-ai', AIS, bt.ai) +
		'<div class="actions">' + b('b-start', 'Начать бой', null, 'primary') + '</div>' +
		'<p class="fine">Бои за земли любого уровня — на вкладке «Земли»: там можно задать тип и уровень земли и атаковать её.</p>';
	return h;
}
function renderSave() {
	const d = kd();
	let h = '';
	if (d) {
		const t = d.K.bridge.save();
		let s = {};
		try { s = parseSave(t); } catch (e) { /* shown below */ }
		const cloud = d.cloud ? { on: 'есть, синхронизируется', error: 'недоступна', off: 'нет (вне claude.ai)' }[d.cloud.state] || d.cloud.state : '—';
		h += '<h3>Текущее королевство</h3><p class="fine">' + esc(kingdomSummary()) + '. Размер ' + fmt(t.length) + ' байт. Копия в аккаунте: ' + cloud + '.</p>' +
			'<div class="actions">' + b('save-copy', 'Скопировать сохранение') + b('save-dl', 'Скачать .json') + b('save-show', admin.showSave ? 'Скрыть' : 'Показать') + '</div>';
		if (admin.showSave) h += '<textarea readonly class="admText">' + esc(JSON.stringify(s, null, 1)) + '</textarea>';
		h += '<h3>Загрузить сохранение</h3><p class="fine">Вставьте сохранение королевства (JSON из «Скопировать» или из файла). Текущее королевство сначала уходит в копию ниже.</p>' +
			'<textarea class="admText small" data-in="save-in" placeholder="{&quot;formatVersion&quot;: 6, …}">' + esc(admin.typed['save-in'] || '') + '</textarea><div class="actions">' + b('save-load', 'Загрузить', null, 'primary') + '</div>';
		const bk = admin.backup;
		h += '<h3>Копия до правок админа</h3>' + (bk ? '<p class="fine">От ' + esc(dateText(bk.savedAt)) + '. Её можно вернуть в любой момент.</p><div class="actions">' + b('bk-restore', 'Вернуть королевство из копии', null, 'primary') +
			b('bk-new', 'Заменить копию текущим') + b('bk-drop', 'Удалить копию', null, 'danger') + '</div>' : '<p class="fine">Копии пока нет: она сохранится сама перед первой правкой.</p><div class="actions">' + b('bk-new', 'Сделать копию сейчас') + '</div>');
	} else h += needKingdom();
	const rej = lsGet(SAVE_KEY + '.rejected');
	if (rej) {
		let when = '';
		try { when = dateText(JSON.parse(rej).savedAt); } catch (e) { when = '?'; }
		h += '<h3>Отклонённое сохранение</h3><p class="note">При одном из запусков игра не приняла сохранение браузера (от ' + esc(when) + ') и начала новое королевство. Оно лежит здесь — это стоит прислать разработчику.</p>' +
			'<div class="actions">' + b('rej-copy', 'Скопировать') + (d ? b('rej-load', 'Попробовать загрузить') : '') + b('rej-drop', 'Удалить', null, 'danger') + '</div>';
	}
	return h;
}

// ---------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------
function inputVal(name) { const el = $('admPanel').querySelector('[data-in="' + name + '"]'); return el ? el.value : admin.typed[name] || ''; }
/** value="…" of a field: what the viewer typed, else the default. */
function typedAttr(name, def) { const v = name in admin.typed ? admin.typed[name] : def; return v === undefined || v === '' ? '' : ' value="' + esc(v) + '"'; }
const RES_KEYS = ['gold', 'food', 'gems', 'stones', 'shards', 'vp'];
function onClick(e) {
	const el = e.target.closest('[data-a]');
	if (!el || el.tagName === 'SELECT') return;
	const a = el.dataset.a;
	const n = +el.dataset.n || 0;
	const id = +el.dataset.id || 0;
	const i = +el.dataset.i || 0;
	try { act(a, el, n, id, i); } catch (err) {
		addLog('error', 'Админ: действие «' + a + '» упало: ' + text(err));
		toast('Действие не удалось: ' + (err.message || err), 'bad');
	}
}
function act(a, el, n, id, i) {
	const d = kd();
	switch (a) {
	case 'tab': admin.tab = el.dataset.v; lsSet(TAB_KEY, admin.tab); admin.copyFallback = ''; render(); break;
	case 'close': closePanel(); break;
	case 'off': setOn(false); break;
	// Resources and the city
	case 'res-add': edit((s) => { s.resources[el.dataset.r] = Math.max(0, (s.resources[el.dataset.r] || 0) + n); }, n < 0 ? 'Обнулено' : 'Добавлено'); break;
	case 'res-set': {
		const v = Math.floor(+inputVal(el.dataset.r));
		if (!Number.isFinite(v) || v < 0) { toast('Введите число от 0', 'bad'); break; }
		edit((s) => { s.resources[el.dataset.r] = v; }, 'Задано: ' + fmt(v));
		break;
	}
	case 'res-fill': edit((s, st) => { s.resources.gold = Math.max(s.resources.gold, st.cap[0]); s.resources.food = Math.max(s.resources.food, st.cap[1]); s.resources.stones = Math.max(s.resources.stones, st.cap[3]); }, 'Хранилища полны'); break;
	case 'res-rich': edit((s, st) => {
		s.resources.gold = Math.max(s.resources.gold, st.cap[0]);
		s.resources.food = Math.max(s.resources.food, st.cap[1]);
		s.resources.stones = Math.max(s.resources.stones, st.cap[3]);
		s.resources.gems += 100000;
		s.resources.shards += 5000;
	}, 'Королевство богато'); break;
	case 'exp': edit((s) => { s.expansions = clamp((s.expansions || 1) + n, 1, rules().expansions.length); }, 'Расширений: изменено'); break;
	case 'slots': edit((s) => { s.extraSlots = Math.max(0, (s.extraSlots || 0) + n); }, 'Мест для земель: изменено'); break;
	case 'finish': finishTimers(el.dataset.w); break;
	case 'stones': edit((s, st) => { s.resources.stones = Math.max(s.resources.stones, st.cap[3]); s.stonesAt = s.time; }, 'Камни портала полны'); break;
	case 'altar': edit((s) => { s.relicAt = s.time; }, 'Ежедневный алтарь открыт'); break;
	// Buildings
	case 'bld-castle': edit((s) => { const c = s.buildings.find((x) => x.def === 'castle'); if (!c) return false; c.level = clamp(c.level + n, 1, defs()[defIndex('castle')].levels.length); c.upgrading = false; }, 'Замок: уровень изменён'); break;
	case 'bld-lv': edit((s) => {
		const x = s.buildings.find((q) => q.id === id);
		if (!x) return false;
		const df = defs()[defIndex(x.def)];
		x.level = n === 999 ? maxForCastle(df, castleOf(s)) : clamp(x.level + n, 1, df.levels.length);
		x.upgrading = false;
	}, 'Уровень изменён'); break;
	case 'bld-allmax': edit((s) => {
		const castle = castleOf(s);
		for (const x of s.buildings) {
			if (x.def === 'castle') continue;
			const df = defs()[defIndex(x.def)];
			x.level = Math.max(x.level, maxForCastle(df, castle));
			x.upgrading = false;
		}
	}, 'Все постройки на максимуме для Замка'); break;
	case 'bld-done': edit((s) => { const x = s.buildings.find((q) => q.id === id); if (!x || !x.upgrading) return false; x.finishAt = s.time; }, 'Достроено'); break;
	case 'bld-new': {
		const def = +el.dataset.def;
		const p = d.findSpot(def);
		if (!p) { toast('Нет свободного места: расширьте город', 'bad'); break; }
		edit((s) => { s.buildings.push({ id: newId(s), def: defs()[def].id, level: 1, x: p.x, y: p.y, upgrading: false, startAt: s.time, finishAt: s.time, stored: 0, carry: 0, lastAt: s.time }); },
			'Поставлено: ' + defs()[def].name);
		break;
	}
	// Army and spells
	case 'troop-lv': edit((s) => { const t = army().troops[i].id; s.army.levels[t] = clamp((s.army.levels[t] || 1) + n, 1, army().troops[i].levels.length); }, 'Уровень войск изменён'); break;
	case 'troop-n': edit((s) => { const t = army().troops[i].id; s.army.units[t] = Math.max(0, (s.army.units[t] || 0) + n); }, 'Отряды изменены'); break;
	case 'troop-all': edit((s) => { for (const t of army().troops) s.army.units[t.id] = (s.army.units[t.id] || 0) + n; }, 'Каждого войска +' + n); break;
	case 'troop-cap': edit((s, st) => { army().troops.forEach((t, k) => { s.army.levels[t.id] = Math.max(s.army.levels[t.id] || 1, st.army.lvCap[k]); }); }, 'Уровни войск подняты до потолка'); break;
	case 'spell-n': edit((s) => { const k = army().spells[i].id; s.spells.stock[k] = Math.max(0, (s.spells.stock[k] || 0) + n); }, 'Заклинания изменены'); break;
	case 'spell-lv': edit((s, st) => { const k = army().spells[i].id; s.spells.levels[k] = clamp((s.spells.levels[k] || 1) + n, 1, Math.max(st.spells.maxLv, s.spells.levels[k] || 1)); }, 'Уровень заклинания изменён'); break;
	// Titans and relics
	case 'titan-lv': edit((s) => { const t = s.titans.find((q) => q.id === id); if (!t) return false; t.level = Math.max(1, t.level + n); t.xp = 0; }, 'Уровень титана изменён'); break;
	case 'titan-allmax': edit((s) => { for (const t of s.titans) { t.level = 999; t.xp = 0; } }, 'Все титаны на максимуме'); break;
	case 'titan-asc': edit((s) => { const t = s.titans.find((q) => q.id === id); if (!t) return false; t.ascended = !t.ascended; }, 'Возвышение изменено'); break;
	case 'titan-del': edit((s) => { s.titans = s.titans.filter((q) => q.id !== id); }, 'Титан убран'); break;
	case 'titan-add': edit((s) => { s.titans.push({ id: newId(s), def: army().titans[i].id, level: 1, xp: 0, recoverUntil: 0, ascended: false, relics: [] }); }, 'Новый титан: ' + army().titans[i].name); break;
	case 'relic-add': case 'relic-add10': {
		const kinds = army().relics.kinds;
		const many = a === 'relic-add10';
		const r = rng(Date.now() & 0xffffff);
		edit((s) => {
			for (let k = 0; k < (many ? 10 : 1); k++) {
				const kind = many ? Math.floor(r() * kinds.length) : admin.relic.kind;
				const rarity = many ? 1 + Math.floor(r() * 5) : admin.relic.rarity;
				const it = { id: newId(s), kind: kinds[kind].id, rarity, durability: 1e12 };
				if (admin.relic.full && !many) it.levels = [99, 99, 99];
				s.relics.push(it);
			}
		}, many ? 'Добавлено 10 реликвий' : 'Реликвия добавлена: ' + kinds[admin.relic.kind].name);
		break;
	}
	case 'relic-fix': edit((s) => { for (const r of s.relics) r.durability = 1e12; }, 'Все реликвии починены'); break;
	case 'relic-clear': {
		const st = d.K.st;
		const free = new Set(st.relics.filter((r) => !r.on && !r.b).map((r) => r.id));
		edit((s) => { s.relics = s.relics.filter((r) => !free.has(r.id)); }, 'Убрано свободных реликвий: ' + free.size);
		break;
	}
	case 'relic-rar': admin.relic.rarity = +el.dataset.v; render(); break;
	case 'relic-full': admin.relic.full = el.dataset.v === '1'; render(); break;
	// Time
	case 'skip': skipTime(+el.dataset.v); break;
	case 'scale': d.clock.scale = +el.dataset.v; toast(d.clock.scale > 1 ? 'Время идёт в ' + d.clock.scale + ' раз быстрее' : 'Обычное время', 'gold'); render(); break;
	case 'season-end': skipTime(d.K.st.season.end - d.K.st.now + 1); break;
	case 'season-skip': skipTime(d.K.st.season.end - d.K.st.now + 1 + (n - 1) * 7 * 86400); break;
	// Lands
	case 'world-new': edit((s) => { s.world = []; }, 'Земли мира созданы заново'); break;
	case 'world-lv': { const v = +el.dataset.v; edit((s) => { for (const w of s.world) w.level = v; }, 'Все земли мира уровня ' + v); break; }
	case 'world-lvl': edit((s) => { const w = s.world.find((q) => q.id === id); if (!w) return false; w.level = clamp(w.level + n, 1, rules().battles.length); }, 'Уровень земли изменён'); break;
	case 'attack': {
		const capture = el.dataset.c === '1';
		const need = capture ? rules().portal.captureCost : rules().portal.raidCost;
		if (capture && d.K.st.lands.length >= d.K.st.landCap) { toast('Нет места для новой земли: добавьте место на вкладке «Ресурсы»', 'bad'); break; }
		if (d.K.st.res[3] < need && !edit((s) => { s.resources.stones = need; }, '')) break;
		closePanel();
		d.openPrepare(id, capture);
		break;
	}
	case 'land-lv': edit((s) => { const l = s.lands.find((q) => q.id === id); if (!l) return false; l.level = clamp(l.level + n, 1, rules().lands.fillHours.length); }, 'Уровень земли изменён'); break;
	case 'land-fill': case 'land-fillall': {
		const caps = new Map(d.K.st.lands.map((l) => [l.id, l.cap]));
		edit((s) => { for (const l of s.lands) if (a === 'land-fillall' || l.id === id) { l.stored = caps.get(l.id) || 0; l.lastAt = s.time; l.carry = 0; } }, 'Хранилища земель полны');
		break;
	}
	case 'land-del': edit((s) => { s.lands = s.lands.filter((q) => q.id !== id); }, 'Земля убрана'); break;
	case 'land-newlv': admin.land.lv = +el.dataset.v; render(); break;
	case 'land-add': edit((s) => { s.lands.push({ id: newId(s), type: rules().lands.types[admin.land.type].id, level: admin.land.lv, stored: 0, carry: 0, lastAt: s.time, titan: 0 }); }, 'Земля добавлена'); break;
	// Battles
	case 'bspeed': demo().game.speed = +el.dataset.v; render(); break;
	case 'bpause': { const g = demo().game; g.paused = !g.paused; render(); break; }
	case 'bauto': { const t = $('tglAuto'); if (t) t.click(); render(); break; }
	case 'bperf': document.body.classList.toggle('adm-perf'); render(); break;
	case 'bend': endLandBattle(n); break;
	case 'bexit': closePanel(); app().exitToKingdom(0, false); break;
	case 'b-asc': admin.battle.asc = el.dataset.v === '1'; render(); break;
	case 'b-ai': admin.battle.ai = el.dataset.v; render(); break;
	case 'b-start': {
		const bt = admin.battle;
		bt.tlv = Math.max(0, Math.floor(+inputVal('b-tlv') || 0));
		bt.tst = clamp(Math.floor(+inputVal('b-tst') || 0), 0, 4);
		bt.elv = Math.max(0, Math.floor(+inputVal('b-elv') || 0));
		bt.est = clamp(Math.floor(+inputVal('b-est') || 0), 0, 4);
		startBattle();
		break;
	}
	// Checks and random play
	case 'checks':
		admin.checks = runChecks();
		d.refresh(true);
		if (admin.checks.some((x) => x[0] === 'bad')) addLog('error', 'Проверка королевства: ' + admin.checks.filter((x) => x[0] === 'bad').map((x) => x[1]).join('; '));
		toast(admin.checks.some((x) => x[0] === 'bad') ? 'Проверка нашла ошибки' : admin.checks.some((x) => x[0] === 'warn') ? 'Проверка: есть предупреждения' : 'Проверка: всё в порядке',
			admin.checks.some((x) => x[0] === 'bad') ? 'bad' : 'gold');
		render();
		break;
	case 'monkey': case 'monkey-next': {
		admin.monkeySteps = clamp(Math.floor(+inputVal('monkey-steps') || 200), 10, 5000);
		admin.monkeySeed = Math.max(1, Math.floor(+inputVal('monkey-seed') || 1)) + (a === 'monkey-next' ? 1 : 0);
		admin.typed['monkey-seed'] = String(admin.monkeySeed);
		runMonkey(admin.monkeySteps, admin.monkeySeed, admin.keepMonkey);
		const m = admin.monkey;
		toast(m.fails.length ? 'Случайная игра нашла сбои: ' + m.fails.length : 'Случайная игра: ' + m.steps + ' ходов без сбоев', m.fails.length ? 'bad' : 'gold');
		render();
		break;
	}
	case 'monkey-keep': admin.keepMonkey = el.dataset.v === '1'; render(); break;
	// Save and backup
	case 'save-copy': copyText(d.K.bridge.save(), 'Текст сохранения'); break;
	case 'save-dl': download('dawn-of-titans-kingdom-' + new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-') + '.json', d.K.bridge.save()); break;
	case 'save-show': admin.showSave = !admin.showSave; render(); break;
	case 'save-load': loadSaveText(inputVal('save-in')); admin.typed['save-in'] = ''; render(); break;
	case 'bk-restore': {
		const bk = admin.backup;
		if (!bk) break;
		if (!d.adopt(bk)) { toast('Копия не загрузилась: ' + d.K.bridge.error(), 'bad'); break; }
		d.persist();
		toast('Королевство возвращено из копии от ' + dateText(bk.savedAt), 'gold');
		render();
		break;
	}
	case 'bk-new': admin.backup = null; keepBackup(); toast('Копия сохранена', 'gold'); render(); break;
	case 'bk-drop': admin.backup = null; lsSet(BACKUP_KEY, null); toast('Копия удалена', ''); render(); break;
	case 'rej-copy': { let t = lsGet(SAVE_KEY + '.rejected') || ''; try { t = JSON.parse(t).save || t; } catch (e) { /* raw */ } copyText(t, 'Текст сохранения'); break; }
	case 'rej-load': loadSaveText(lsGet(SAVE_KEY + '.rejected'), 'Отклонённое сохранение загружено'); break;
	case 'rej-drop': lsSet(SAVE_KEY + '.rejected', null); render(); break;
	// Log
	case 'copy-report': copyText(report(el.dataset.s === '1'), 'Отчёт'); break;
	case 'log-clear': log.items = []; log.seen = 0; saveLog(); render(); updateButton(); break;
	case 'log-test': setTimeout(() => { throw new Error('Тестовая ошибка из режима админа'); }, 0); setTimeout(render, 50); break;
	case 'log-filter': admin.logErrorsOnly = el.dataset.v === '1'; render(); break;
	default: break;
	}
}
function onChange(e) {
	const el = e.target;
	const c = el.dataset && el.dataset.c;
	if (!c) return;
	const v = el.value;
	try {
		if (c === 'relic-kind') admin.relic.kind = +v;
		else if (c === 'land-type') admin.land.type = +v;
		else if (c === 'b-sc') { admin.battle.sc = v; render(); }
		else if (c === 'b-titan') admin.battle.titan = v;
		else if (c === 'b-etitan') admin.battle.etitan = v;
		else if (c.startsWith('world-type:')) {
			const wid = +c.slice(11);
			edit((s) => { const w = s.world.find((q) => q.id === wid); if (!w) return false; w.type = rules().lands.types[+v].id; }, 'Тип земли изменён');
		}
	} catch (err) {
		addLog('error', 'Админ: «' + c + '» не изменилось: ' + text(err));
	}
}

// ---------------------------------------------------------------------------------------------
// Switch: seven taps on the state hash (kingdom menu) or the engine line (battle menu)
// ---------------------------------------------------------------------------------------------
document.addEventListener('click', (e) => {
	const t = e.target;
	if (!t || !t.closest) return;
	const p = t.closest('p');
	if (!t.closest('#engineInfo') && !(p && p.querySelector('[data-live="hash"]'))) return;
	const now = performance.now();
	admin.taps = admin.taps.filter((x) => now - x < 6000);
	admin.taps.push(now);
	if (admin.taps.length >= TAPS) { admin.taps = []; setOn(!admin.on); } else if (admin.taps.length >= 4) toast('Ещё ' + (TAPS - admin.taps.length) + ' — и режим админа ' + (admin.on ? 'выключится' : 'включится'), '');
}, true);

function buildUi() {
	const game = $('game');
	if (!game || $('admBtn')) return;
	const btn = document.createElement('button');
	btn.id = 'admBtn';
	btn.className = 'chip icon';
	btn.setAttribute('aria-label', 'Режим админа');
	btn.innerHTML = WRENCH + '<b id="admBadge" hidden></b>';
	btn.hidden = true;
	btn.addEventListener('click', () => openPanel());
	const sheet = document.createElement('div');
	sheet.className = 'sheet';
	sheet.id = 'admSheet';
	sheet.hidden = true;
	sheet.innerHTML = '<div class="panel" role="dialog" aria-labelledby="admTitle" id="admPanel"></div>';
	sheet.addEventListener('click', (e) => { if (e.target === sheet) closePanel(); });
	game.appendChild(btn);
	game.appendChild(sheet);
	const panel = $('admPanel');
	panel.addEventListener('click', onClick);
	panel.addEventListener('change', onChange);
	panel.addEventListener('input', (e) => { const n = e.target.dataset && e.target.dataset.in; if (n) admin.typed[n] = e.target.value; });
	// Typing in the panel must not reach the battle's and kingdom's keyboard shortcuts.
	panel.addEventListener('keydown', (e) => { if (e.key === 'Escape') closePanel(); e.stopPropagation(); });
	setInterval(updateButton, 400);
	updateButton();
}
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', buildUi);
else buildUi();

// Hooks for the automated browser tests.
window.dotAdmin = { admin, log, addLog, openPanel, closePanel, setOn, runChecks, runMonkey, report, edit, render };
})();
