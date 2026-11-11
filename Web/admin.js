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
	relic: { kind: 0, rarity: 3, full: true }, land: { type: 0, lv: 1 }, goal: null, monkeySteps: 200, monkeySeed: 1, keepMonkey: false,
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
const goalText = (g) => g.name.replace('{n}', fmt(g.n || 1));
/** The Conquest map as the kingdom lays it (kingdom.json "conquest" rings): kinds 0 fortress, 1 plains, 2 fort, 3 shrine,
 *  4 sanctum, and each tile's neighbours. */
function conqHexes() {
	const R = rules().conquest;
	const dirs = [[1, 0], [1, -1], [0, -1], [-1, 0], [-1, 1], [0, 1]];
	const tiles = [];
	(R && R.rings || []).forEach((kinds, ring) => {
		let q = -ring;
		let r = ring;
		for (let i = 0; i < kinds.length; i++) {
			tiles.push({ kind: 'HPFSC'.indexOf(kinds[i]), q, r, adj: [] });
			if (ring > 0) { const d = dirs[Math.floor(i / ring)]; q += d[0]; r += d[1]; }
		}
	});
	tiles.forEach((a) => tiles.forEach((b, j) => { if (a !== b && Math.abs(b.q - a.q) <= 1 && Math.abs(b.r - a.r) <= 1 && Math.abs(b.q - a.q + b.r - a.r) <= 1) a.adj.push(j); }));
	return tiles;
}
const CONQ_KEYS = ['home', 'plains', 'fort', 'shrine', 'sanctum'];
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
/** Gold, food and portal stones from one state to the next: rewards (goals, quests, chests, calendars) are paid on
 *  top of full vaults, everything else stops at the cap, so above it they grow only through a reward. */
function resourceCaps(prev, st, add, reward) {
	if (reward) return;
	[[0, 'Золото выросло'], [1, 'Еда выросла'], [3, 'Камни портала выросли']].forEach(([k, what]) => {
		const v = st.res[k];
		if (v > st.cap[k] && v > prev.res[k] && v > prev.cap[k]) add('bad', what + ' сверх хранилищ без награды: ' + fmt(prev.res[k]) + ' → ' + fmt(v) + ' из ' + fmt(st.cap[k]));
	});
}
/** Raids won on lands between two states: land id → what they took. Null when the raid log (it keeps the last few
 *  reports) may have dropped some of them. */
function raidsTook(prev, st) {
	const key = (r) => r.at + ':' + r.land + ':' + r.lv + ':' + r.rp;
	const was = new Set(((prev.raids && prev.raids.log) || []).map(key));
	const log = (st.raids && st.raids.log) || [];
	const cap = (rules().raids && rules().raids.log) || Infinity;
	if (log.length >= cap && !was.has(key(log[0]))) return null;
	const took = new Map();
	for (const r of log) if (r.land && !was.has(key(r))) took.set(r.land, (took.get(r.land) || 0) + r.lost.reduce((x, y) => x + y, 0));
	return took;
}
/** Land stores from one state to the next: only collecting and raids empty them, and a store a guard filled above
 *  the plain cap keeps what it holds once the guard leaves but grows no more. */
function landStores(prev, st, add, collected) {
	const before = new Map(prev.lands.map((l) => [l.id, l]));
	const took = raidsTook(prev, st);
	for (const l of st.lands) {
		const p = before.get(l.id);
		if (!p) continue;
		// A raid takes a share of what the store held then, which is at least what it held before.
		const raid = took ? took.get(l.id) || 0 : 0;
		if (!collected && took && l.st < p.st - raid) add('bad', 'Земля #' + l.id + ': хранилище уменьшилось без сбора' + (raid ? ' больше, чем забрал налёт (' + raid + ')' : '') + ', ' + p.st + ' → ' + l.st + ' из ' + l.cap);
		if (l.st > l.cap && l.st > p.st && l.st > p.cap) add('bad', 'Земля #' + l.id + ': хранилище растёт сверх вместимости, ' + p.st + ' → ' + l.st + ' из ' + l.cap);
	}
}
/** Rules a kingdom state must keep whatever was played: add(level, text) for each broken one. */
function invariants(st, add) {
	const d = kd();
	const K = d.K;
	const B = K.bridge;
	const res = ['золото', 'еда', 'самоцветы', 'камни портала', 'осколки', 'очки победы'];
	st.res.forEach((v, i) => { if (!Number.isFinite(v) || v < 0) add('bad', 'Ресурс «' + res[i] + '» = ' + v); });
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
	}
	const goals = rules().goals;
	if (st.goal && goals) {
		if (!(st.goal.i >= 0 && st.goal.i <= goals.length)) add('bad', 'Цель ' + (st.goal.i + 1) + ' вне линии из ' + goals.length + ' целей');
		else if (st.goal.i === goals.length && st.goal.done) add('bad', 'Линия целей пройдена, а цель отмечена выполненной');
		if (!Number.isFinite(st.goal.v) || st.goal.v < 0) add('bad', 'Счёт цели ' + (st.goal.i + 1) + ' = ' + st.goal.v);
	}
	if (st.world.length !== rules().lands.worldSize) add('bad', 'Земель мира ' + st.world.length + ', а должно быть ' + rules().lands.worldSize);
	if (st.spells.total > st.spells.cap) add('warn', 'Заклинаний больше вместимости Кузницы: ' + st.spells.total + ' из ' + st.spells.cap);
	if (!(st.season.end > st.now)) add('bad', 'Сезон ' + st.season.no + ' должен был закончиться');
	if (st.pending && !inBattle()) add('warn', 'Висит незавершённый бой за землю #' + st.pending);
	const asr = rules().assault;
	const as = st.assault;
	if (asr && as && as.no >= 0) {
		if (as.stage < 0 || as.stage > asr.stages.length) add('bad', 'Штурм: этап ' + as.stage + ' из ' + asr.stages.length);
		if (as.tries < 0 || as.tries > asr.retries) add('bad', 'Штурм: попыток ' + as.tries + ' из ' + asr.retries);
		if (as.squads.some((q) => q[2] < 0 || q[2] > 100)) add('bad', 'Штурм: доля отряда вне 0–100%: ' + as.squads.map((q) => q[2]).join(', '));
		if (as.squads.length > st.army.slots) add('warn', 'Штурм: отрядов ' + as.squads.length + ' при ' + st.army.slots + ' местах в бою');
		if (as.offer.length && (!as.run || as.over)) add('bad', 'Штурм окончен или не начат, а благословения предложены');
		if (as.offer.length > asr.boonChoices || new Set(as.offer).size !== as.offer.length) add('bad', 'Штурм: предложено ' + as.offer.join(', '));
		if (as.boons.length > as.stage) add('bad', 'Штурм: благословений ' + as.boons.length + ' за ' + as.stage + ' этапов');
		if (as.run && !as.over && as.stage >= asr.stages.length) add('bad', 'Штурм пройден целиком, но не окончен');
	}
	// Raid Boss: the pool holds the members' damage and the player's; outside an alliance there is none.
	const bsr = rules().raidBoss;
	const bs = st.boss;
	if (bsr && bs && bs.i >= 0) {
		if (bs.i >= bsr.bosses.length) add('bad', 'Босс ' + bs.i + ' вне списка из ' + bsr.bosses.length);
		if (st.ally.my < 0) {
			if (bs.pool || bs.dmg || bs.tier || bs.members.length) add('bad', 'Вне альянса у рейдового босса запас ' + bs.pool + ', урон ' + bs.dmg + ', сложность ' + bs.tier);
		} else {
			const n = bsr.tiers.length;
			if (!(bs.tier >= 1 && bs.tier <= n && bs.home >= 1 && bs.home <= n && bs.next >= 1 && bs.next <= n)) add('bad', 'Рейдовый босс: сложность ' + bs.tier + ', своя ' + bs.home + ', следующая ' + bs.next + ' из ' + n);
			if (!(bs.pool > 0) || bs.dmg < 0 || bs.dmg > bs.pool) add('bad', 'Рейдовый босс: урон ' + bs.dmg + ' при запасе ' + bs.pool);
			const sum = bs.members.reduce((x, m) => x + m[1], bs.mine);
			if (bs.fell ? bs.dmg !== bs.pool || sum < bs.pool : sum !== bs.dmg) add('bad', 'Рейдовый босс: урон участников ' + sum + ', а в запасе ' + bs.dmg + ' из ' + bs.pool + (bs.fell ? ' (повержен)' : ''));
			if (bs.members.some((m) => m[1] < 0 || m[2] < 0 || m[2] > bsr.attempts * bsr.days)) add('bad', 'Рейдовый босс: у участника боёв или урона вне границ');
			if (bs.best > 1000 || bs.best > bs.mine || (bs.mb === 0 && (bs.mine || bs.best))) add('bad', 'Рейдовый босс: ваш урон ' + bs.mine + ', лучший бой ' + bs.best + ', боёв ' + bs.mb);
		}
		if (bs.left < 0 || bs.left > bsr.attempts) add('bad', 'Рейдовый босс: боёв сегодня ' + bs.left + ' из ' + bsr.attempts);
		if (st.pendingBoss && !inBattle()) add('warn', 'Висит незавершённый бой с рейдовым боссом');
	}
	// Daily Trials: open on their day (all on Sunday), attempts within the day's, stars 0-3 won in order, and the
	// highest open tier the one the Castle and the stars allow.
	const dtr = rules().dailyTrials;
	const dl = st.daily;
	if (dtr && dl) {
		if (dl.list.length !== dtr.list.length || dl.wd < 0 || dl.wd > 6) add('bad', 'Ежедневные испытания: ' + dl.list.length + ' из ' + dtr.list.length + ', день недели ' + dl.wd);
		dl.list.forEach((x, i) => {
			const d = dtr.list[i];
			if (!d) return;
			const nm = 'Испытание «' + d.name + '»: ';
			if (x.left < 0 || x.left > dtr.attempts) add('bad', nm + 'попыток ' + x.left + ' из ' + dtr.attempts);
			if (!!x.open !== (dl.wd === 6 || dl.wd === d.day)) add('bad', nm + (x.open ? 'открыто' : 'закрыто') + ' в день ' + dl.wd + ', а его день ' + d.day);
			if (x.stars.length !== dtr.tiers.length || x.stars.some((v) => !(v >= 0 && v <= 3))) add('bad', nm + 'звёзды уровней ' + x.stars.join(''));
			const gap = x.stars.indexOf(0);
			if (gap >= 0 && x.stars.slice(gap).some((v) => v > 0)) add('bad', nm + 'пройден уровень после непройденного: ' + x.stars.join(''));
			let top = 0;
			while (st.castle >= dtr.castle && top < dtr.tiers.length && st.castle >= dtr.tiers[top].castle && (top === 0 || x.stars[top - 1] > 0)) top++;
			if (x.top !== top) add('bad', nm + 'открыт уровень ' + x.top + ', а по Замку и звёздам ' + top);
		});
		if (st.pendingDaily && st.pendingDaily[0] >= 0 && !inBattle()) add('warn', 'Висит незавершённый бой ежедневного испытания');
	}
	// Alliance War: rosters of the same size, tiers by place, stars 0-3 a base taken by someone of the other side, the
	// scores the bases' sums, attacks within the rules, the result by the scores.
	const wr = rules().allianceWar;
	const wa = st.war;
	if (wr && wa) {
		if (!(wa.ph >= 0 && wa.ph <= 2) || wa.battle !== wa.prep + 86400 || wa.end !== wa.battle + wr.battleDays * 86400 || wa.next !== wa.prep + 7 * 86400) add('bad', 'Война альянсов: фаза ' + wa.ph + ', дни ' + [wa.prep, wa.battle, wa.end, wa.next].join(' / '));
		if (wa.left < 0 || wa.left > wr.attacks) add('bad', 'Война альянсов: атак осталось ' + wa.left + ' из ' + wr.attacks);
		if (st.ally.my < 0 && (wa.on || wa.members)) add('bad', 'Вне альянса идёт война альянсов');
		if (wa.on) {
			const n = wa.roster;
			if (n < wr.minMembers || n > wr.rosterMax || n % wr.rosterStep || wa.us.length !== n || wa.them.length !== n || wa.members < n || wa.me < 0 || wa.me >= n) add('bad', 'Война альянсов: состав ' + n + ', баз ' + wa.us.length + ' и ' + wa.them.length + ', участников ' + wa.members + ', ваше место ' + wa.me);
			if (wa.opp < 0 || wa.opp === st.ally.my) add('bad', 'Война альянсов: соперник ' + wa.opp);
			let ours = 0;
			let theirs = 0;
			[[wa.us, wa.them], [wa.them, wa.us]].forEach(([side, other], s) => side.forEach((x, i) => {
				const nm = 'Война альянсов, ' + (s ? 'база соперника ' : 'наша база ') + (i + 1) + ': ';
				if (!(x[1] >= 1 && x[1] <= wr.tiers.length)) add('bad', nm + 'ступень ' + x[1]);
				if (i > 0 && x[1] > side[i - 1][1]) add('bad', nm + 'сильнее предыдущей');
				if (!(x[2] >= 0 && x[2] <= 3) || (x[2] > 0) !== (x[3] >= 0) || x[3] >= n) add('bad', nm + 'звёзд ' + x[2] + ', взял ' + x[3]);
				if (x[4] < 0 || x[4] > wr.attacks || x[5] < 0 || x[5] > 3 * x[4]) add('bad', nm + 'атак ' + x[4] + ', звёзд ' + x[5]);
				if (wa.ph === 0 && (x[2] || x[4])) add('bad', nm + 'в день подготовки уже есть атаки');
				if (s) ours += x[2];
				else theirs += x[2];
			}));
			if (ours !== wa.ours || theirs !== wa.theirs) add('bad', 'Война альянсов: счёт ' + wa.ours + ' : ' + wa.theirs + ', а по базам ' + ours + ' : ' + theirs);
			const won = wa.them.reduce((x, y) => x + y[5], 0);
			if (wa.us.reduce((x, y) => x + y[5], 0) !== wa.ours || won !== wa.theirs) add('bad', 'Война альянсов: звёзды атак не сходятся со счётом');
			if (wa.ph === 2 && wa.res !== Math.sign(wa.ours - wa.theirs)) add('bad', 'Война альянсов: итог ' + wa.res + ' при счёте ' + wa.ours + ' : ' + wa.theirs);
			if (wa.mine < 0 || wa.mine > 3 * wr.attacks || wa.mine < wa.us[wa.me][5]) add('bad', 'Война альянсов: ваших звёзд ' + wa.mine + ', в счёт ' + wa.us[wa.me][5]);
		}
		if (st.pendingWar >= 0 && !inBattle()) add('warn', 'Висит незавершённая атака на войне альянсов');
	}
	// Conquest: its days, the tokens within the stock, a map only in an alliance; on it every fortress its slot's, Control
	// within the kind's, a place for each alliance, the shrines' attack as held, the player's stars within the attacks.
	const qr = rules().conquest;
	const cq = st.conq;
	if (qr && cq) {
		if (!(cq.ph >= 0 && cq.ph <= 2) || cq.end !== cq.start + qr.days * 86400 || (cq.ph > 0 && cq.next !== cq.start + qr.weeks * 7 * 86400)) add('bad', 'Завоевание: фаза ' + cq.ph + ', дни ' + [cq.start, cq.end, cq.next].join(' / '));
		if (cq.tok < 0 || cq.tok > qr.tokens || cq.tokIn < 0 || cq.tokIn > qr.tokenSec || (cq.tok < qr.tokens) !== (cq.tokIn > 0)) add('bad', 'Завоевание: жетонов ' + cq.tok + ' из ' + qr.tokens + ', следующий через ' + cq.tokIn);
		if (st.ally.my < 0 && (cq.on || cq.al.length || cq.tiles.length)) add('bad', 'Вне альянса есть карта Завоевания');
		if (cq.attacks < 0 || cq.stars < 0 || cq.stars > 3 * cq.attacks || cq.taken < 0 || cq.taken > cq.attacks) add('bad', 'Завоевание: атак ' + cq.attacks + ', звёзд ' + cq.stars + ', взято ' + cq.taken);
		if (cq.chest && cq.ph !== 2) add('bad', 'Завоевание: сундук открыт до конца');
		if (cq.on) {
			const map = conqHexes();
			const n = cq.al.length;
			const homes = map.map((h, t) => (h.kind === 0 ? t : -1)).filter((t) => t >= 0);
			if (n < 2 || n > homes.length || cq.al[0][0] !== st.ally.my || new Set(cq.al.map((a) => a[0])).size !== n) add('bad', 'Завоевание: альянсы ' + cq.al.map((a) => a[0]).join(', ') + ' (ваш ' + st.ally.my + ')');
			if (cq.tiles.length !== map.length) add('bad', 'Завоевание: клеток ' + cq.tiles.length + ' из ' + map.length);
			const places = cq.al.map((a) => a[4]).sort((x, y) => x - y);
			if (places.some((p, i) => p !== i + 1) || cq.al.some((a) => a[3] < 0)) add('bad', 'Завоевание: места ' + places.join(', '));
			let atk = 0;
			cq.tiles.forEach((x, t) => {
				const h = map[t];
				if (!h) return;
				const kd0 = qr.tiles[CONQ_KEYS[h.kind]] || {};
				const nm = 'Завоевание, клетка ' + (t + 1) + ': ';
				if (x[0] < -1 || x[0] >= n) add('bad', nm + 'хозяин ' + x[0]);
				if (h.kind === 0 && (x[0] !== (homes.indexOf(t) < n ? homes.indexOf(t) : -1) || x[1] !== 0)) add('bad', nm + 'крепость у ' + x[0] + ', контроль ' + x[1]);
				if (h.kind !== 0 && !(x[1] > 0 && x[1] <= kd0.control)) add('bad', nm + 'контроль ' + x[1] + ' из ' + kd0.control);
				if (x[2] > cq.end + qr.truceSec) add('bad', nm + 'перемирие после конца');
				if (x[0] === 0) atk += kd0.atkPct || 0;
			});
			if (atk !== cq.atk) add('bad', 'Завоевание: атака святилищ ' + cq.atk + '%, а по клеткам ' + atk + '%');
		}
		if (cq.at >= 0 && !inBattle()) add('warn', 'Висит незавершённая атака в Завоевании');
	}
	// Season events: their days, points only in the player's event and within what the lords allow, the place as the
	// bracket gives it, milestones paid only once reached, the rank reward only after the week.
	const svr = rules().seasonEvents;
	if (svr && st.sev) {
		if (st.sev.length !== svr.list.length) add('bad', 'Событий сезона ' + st.sev.length + ' из ' + svr.list.length);
		st.sev.forEach((v, e) => {
			const d = svr.list[e];
			if (!d) return;
			const nm = 'Событие «' + d.name + '»: ';
			const pars = d.par.map((x) => x[1]);
			if (v.end !== v.start + svr.days * 86400 || v.next !== v.start + svr.weeks * 7 * 86400 || (v.on === 1) !== (st.now >= v.start && st.now < v.end) || v.start > st.now) {
				add('bad', nm + 'дни ' + [v.start, v.end, v.next].join(' / ') + ', идёт ' + v.on + ', сейчас ' + st.now);
			}
			if (v.mine > v.no) add('bad', nm + 'очки события ' + v.mine + ' при событии ' + v.no);
			if (v.pts < 0 || (v.mine !== v.no && v.pts !== 0)) add('bad', nm + 'очков ' + v.pts + ' (событие ' + v.mine + ' из ' + v.no + ')');
			if (!pars.includes(v.par)) add('bad', nm + 'норма ' + v.par + ' не из таблицы');
			if (v.lords.length !== svr.bracket - 1 || v.lords.some((x) => x < 0 || x > Math.floor((v.par * 26) / 10))) add('bad', nm + 'очки лордов ' + v.lords.length + ' шт., до ' + Math.max(0, ...v.lords));
			const place = 1 + v.lords.filter((x) => x > v.pts).length;
			if (v.place !== place) add('bad', nm + 'место ' + v.place + ', а по таблице ' + place);
			d.milestones.forEach((m, i) => {
				if ((v.ms >> i) & 1 && v.pts < Math.max(1, Math.floor((v.par * m.pct) / 100))) add('bad', nm + 'рубеж ' + (i + 1) + ' получен при ' + v.pts + ' очках');
			});
			if (v.ms >> d.milestones.length) add('bad', nm + 'лишние рубежи ' + v.ms);
			if (v.rk && (v.on || v.pts <= 0)) add('bad', nm + 'награда за место получена до конца или без очков');
			if (v.mine === v.no && v.no >= 0 && st.castle < svr.castle) add('warn', nm + 'есть очки ниже Замка ' + svr.castle);
		});
	}
	// Alliance help: helps a timer within the rules, asks only of running timers with their ends, the time taken within the
	// helps, no help late, the members' asks open, today's helps within the cap; nothing outside an alliance.
	const hr = rules().allianceHelp;
	const hp = st.help;
	if (hr && hp) {
		if (hp.lim < 0 || hp.lim > hr.max || (st.ally.my < 0 && hp.lim)) add('bad', 'Помощь альянса: помощей на таймер ' + hp.lim);
		if (st.ally.my < 0 && (hp.asks.length || hp.log.length || hp.ally.length)) add('bad', 'Вне альянса есть помощь: просьб ' + hp.asks.length + ', в журнале ' + hp.log.length + ', просьб союзников ' + hp.ally.length);
		if (hp.today < 0 || hp.today > hr.dailyCap) add('bad', 'Помощь альянса: сегодня помогли ' + hp.today + ' из ' + hr.dailyCap);
		for (const a of hp.asks) {
			const nm = 'Помощь альянса (вид ' + a[0] + ', #' + a[1] + '): ';
			const cut = Math.max(hr.minCutSec, Math.floor((a[4] * hr.cutPct) / 100));
			if (a[5] < 0 || a[5] > a[6] || a[6] < 1 || a[6] > hr.max) add('bad', nm + 'помощей ' + a[5] + ' из ' + a[6]);
			if (a[3] <= st.now) add('bad', nm + 'таймер кончился, а просьба осталась');
			if (a[8] < 0 || a[8] > a[5] * cut) add('bad', nm + 'снято ' + a[8] + ' с за ' + a[5] + ' помощей по ' + cut + ' с');
			if (a[5] < a[6] && a[7] <= st.now) add('bad', nm + 'помощь опаздывает: ждали в ' + a[7] + ', сейчас ' + st.now);
			const c = hp.can.find((x) => x[0] === a[0] && x[1] === a[1]);
			if (!c || c[2] !== a[3]) add('bad', nm + 'конец ' + a[3] + ', а у таймера ' + (c ? c[2] : 'его нет'));
			else if (c[3] === 0) add('bad', nm + 'о помощи можно попросить снова');
		}
		for (const g of hp.log) if (g[4] <= 0 || g[0] > st.now) add('bad', 'Помощь альянса в журнале: ' + g.join(', '));
		for (const a of hp.ally) {
			if (a[3] > st.now || a[4] <= st.now || a[4] - a[3] !== hr.openHours * 3600) add('bad', 'Просьба союзника ' + a[0] + ': открыта ' + a[3] + '–' + a[4] + ', сейчас ' + st.now);
		}
	}
	// Campaign sweeps: War Scrolls within the most a kingdom holds; only a mission won on 3 stars can be swept, with a scroll.
	const cr = rules().campaigns;
	const her = cr && cr.heroic && cr.heroic.castle > 0 ? cr.heroic : null;
	const mLevel = (a, m, h) => (h ? Math.min(rules().battles.length, cr.acts[a].missions[m].level + her.level) : cr.acts[a].missions[m].level);
	if (cr && cr.sweep && st.campaign) {
		if (!(st.scrolls >= 0 && st.scrolls <= cr.sweep.max)) add('bad', 'Боевых свитков ' + st.scrolls + ' из ' + cr.sweep.max);
		st.campaign.forEach((c0, a) => [c0, c0.heroic].forEach((c, h) => c && (c.sw || []).forEach((e, m) => {
			if (e === 0 && (c.stars[m] < 3 || st.scrolls < 1 || st.res[1] < cr.sweep.food * mLevel(a, m, h))) {
				add('bad', (h ? 'Героическая з' : 'З') + 'ачистка миссии ' + (a + 1) + '.' + (m + 1) + ' открыта: звёзд ' + c.stars[m] + ', свитков ' + st.scrolls + ', еды ' + st.res[1]);
			}
		})));
	}
	// Heroic difficulty: open only for an act won to its end and from its Castle, in order; its stars count apart.
	if (her && st.campaign) {
		st.campaign.forEach((c, a) => {
			const h = c.heroic;
			const nm = 'Героическая сложность, акт ' + (a + 1) + ': ';
			if (!h) { add('bad', nm + 'нет в состоянии'); return; }
			if (h.total !== h.stars.reduce((x, y) => x + y, 0)) add('bad', nm + 'звёзд ' + h.total + ', а по миссиям ' + h.stars.join(''));
			if (h.stars.length !== c.stars.length || h.chests.length !== c.chests.length) add('bad', nm + 'миссий или сундуков не столько, сколько в обычной');
			const actDone = c.stars[c.stars.length - 1] > 0;
			const castle = Math.max(cr.acts[a].castle, her.castle);
			h.err.forEach((e, m) => {
				const open = e === 0 || e === 15; // None, NeedStones
				if (open && (!actDone || st.castle < castle || (m > 0 && h.stars[m - 1] === 0))) add('bad', nm + 'миссия ' + (m + 1) + ' открыта: акт ' + (actDone ? 'пройден' : 'не пройден') + ', Замок ' + st.castle + ', прошлая ★' + (m ? h.stars[m - 1] : '-'));
				if (h.stars[m] > 0 && m > 0 && h.stars[m - 1] === 0) add('bad', nm + 'миссия ' + (m + 1) + ' пройдена раньше ' + m + '-й');
			});
			if (h.total > 0 && !actDone) add('bad', nm + 'есть звёзды, а акт не пройден');
			cr.acts[a].chests.forEach((ch, i) => { if (h.chests[i] && h.total < ch.stars) add('bad', nm + 'сундук за ' + ch.stars + ' ★ открыт при ' + h.total + ' ★'); });
		});
	}
	// Army loadouts: within the most a kingdom keeps, known troops and spells, a spell once, no more than a battle takes.
	const lr = rules().loadouts;
	if (lr && st.loadouts) {
		if (st.loadouts.length > lr.max) add('bad', 'Наборов армии ' + st.loadouts.length + ' из ' + lr.max);
		st.loadouts.forEach((lo, i) => {
			const nm = 'Набор армии ' + (i + 1) + ': ';
			if (lo.s.some((t) => !(t >= 0 && t < st.army.n.length)) || lo.p.some((x) => !(x >= 0 && x < st.spells.stock.length))) add('bad', nm + 'неизвестные отряды или заклинания');
			if (new Set(lo.p).size !== lo.p.length || lo.p.length > st.spells.kinds) add('bad', nm + 'заклинания ' + lo.p.join(','));
			if (lo.s.length > st.army.slots) add('bad', nm + 'отрядов ' + lo.s.length + ' при ' + st.army.slots + ' местах в бою');
			if (new TextEncoder().encode(lo.n).length > 48 || lo.n !== lo.n.trim()) add('bad', nm + 'имя «' + lo.n + '»');
		});
	}
	// Divine gems: none below zero; the shrine keeps up to its cap, nothing without a shrine; an altar is not open
	// for fewer gems than its price.
	const dr = rules().divine;
	if (dr && st.divine) {
		const dv = st.divine;
		const built = st.b.some((x) => defs()[x.def].id === dr.building && x.lv > 0);
		if (!(Number.isInteger(dv.g) && dv.g >= 0)) add('bad', 'Божественных самоцветов ' + dv.g);
		if (!(dv.st >= 0 && dv.st <= dr.cap)) add('bad', 'В святилище ' + dv.st + ' из ' + dr.cap);
		if (!built && (dv.st !== 0 || dv.nx !== 0)) add('bad', 'Святилища нет, а в нём ' + dv.st + ' (следующий в ' + dv.nx + ')');
		if (built && (dv.st >= dr.cap ? dv.nx !== 0 : !(dv.nx > st.now && dv.nx <= st.now + Math.ceil(86400 / dr.perDay)))) {
			add('bad', 'Святилище: ' + dv.st + ' из ' + dr.cap + ', следующий через ' + (dv.nx - st.now) + ' с');
		}
		dr.altars.forEach((a, i) => { if (dv.alt[i] === 0 && dv.g < a.price) add('bad', 'Алтарь «' + a.name + '» открыт при ' + dv.g + ' из ' + a.price); });
	}
	// Titan Spire: the floor reached within the spire and the next one right after it (none only at the top); the lords'
	// floors within the spire and the place from them; the top floors closing within a week; the next floor shut only
	// below its Castle, at the top or for want of relic room; a battle only on the next floor.
	const spr = rules().spire;
	if (spr && st.spire) {
		const sp = st.spire;
		const N = spr.perBand * spr.bands.length;
		if (!(Number.isInteger(sp.top) && sp.top >= 0 && sp.top <= N)) add('bad', 'Башня: взято этажей ' + sp.top + ' из ' + N);
		if (sp.next !== (sp.top < N ? sp.top + 1 : 0)) add('bad', 'Башня: взято ' + sp.top + ', а следующий этаж ' + sp.next);
		if (sp.fixed !== N - spr.weekly) add('bad', 'Башня: постоянных этажей ' + sp.fixed + ' из ' + N);
		if (sp.lords.length !== spr.bracket - 1 || sp.lords.some((f) => !(Number.isInteger(f) && f >= 1 && f <= N))) add('bad', 'Башня: этажи лордов ' + sp.lords.join());
		const place = 1 + sp.lords.filter((f) => f > sp.top).length;
		if (sp.rank !== place) add('bad', 'Башня: место ' + sp.rank + ', а по таблице ' + place);
		if (!(sp.reset > st.now && sp.reset <= st.now + 7 * 86400)) add('bad', 'Башня: верхние этажи закроются через ' + (sp.reset - st.now) + ' с');
		const want = st.castle < spr.castle ? [6] : !sp.next ? [13] : [0, 28];
		if (!want.includes(sp.ck.err)) add('bad', 'Башня: Замок ' + st.castle + ', следующий этаж ' + sp.next + ', а отказ ' + sp.ck.err);
		if (sp.at && sp.at !== sp.next) add('warn', 'Башня: бой на этаже ' + sp.at + ', а следующий ' + sp.next + ' (неделя сменилась во время боя?)');
		if (sp.at && !inBattle()) add('warn', 'Висит незавершённый бой в башне');
	}
	// Codex: every kind in the Hall has its entry open; a page full or paid only with all its entries open; Resonance by
	// the kinds of the lineage at the Codex level, its tier by the content's counts.
	const cxr = rules().codex;
	const lines = rules().lineages || [];
	if (cxr && st.codex && st.codex.on) {
		const cx = st.codex;
		const tix = (id) => army().titans.findIndex((x) => x.id === id);
		for (const t of st.titans) if (!(cx.e[t.def] > 0)) add('bad', 'Кодекс: титан #' + t.id + ' в Зале, а его запись закрыта');
		if (cx.e.some((v) => v !== 0 && v !== 1 && v !== 2) || cx.e.length !== army().titans.length) add('bad', 'Кодекс: записи ' + cx.e.join(''));
		lines.forEach((L, k) => {
			const [page, cnt, tier] = cx.p[k] || [];
			const full = L.titans.every((id) => cx.e[tix(id)] > 0);
			if ((page > 0) !== full) add('bad', 'Кодекс: страница «' + L.name + '» в состоянии ' + page + ', а записи ' + (full ? 'все открыты' : 'не все'));
			const want = L.titans.filter((id) => st.titans.some((t) => t.def === tix(id) && t.lv >= cxr.level)).length;
			if (cnt !== want) add('bad', 'Кодекс: резонанс «' + L.name + '» считает ' + cnt + ' титанов, а их ' + want);
			const wantTier = (cxr.resonance || []).reduce((n, r, i) => (want >= r.count ? i + 1 : n), 0);
			if (tier !== wantTier) add('bad', 'Кодекс: резонанс «' + L.name + '» ступени ' + tier + ', а по счёту ' + wantTier);
		});
	}
	// Honor: never below zero; today's donations and this week's buys within their limits; outside an alliance (or below
	// the Castle) neither donations nor the shop; a buy offered only with the Honor for it.
	const honr = rules().honor;
	if (honr && st.honor && st.honor.on) {
		const h = st.honor;
		if (!(Number.isInteger(h.pts) && h.pts >= 0)) add('bad', 'Почёт: ' + h.pts);
		if (!(h.don >= 0 && h.don <= honr.donate.perDay)) add('bad', 'Почёт: пожертвований сегодня ' + h.don + ' из ' + honr.donate.perDay);
		if (h.shop.length !== honr.shop.length) add('bad', 'Лавка альянса: товаров ' + h.shop.length + ', а в правилах ' + honr.shop.length);
		h.shop.forEach(([n, err], i) => {
			const d = honr.shop[i];
			if (!d) return;
			if (!(n >= 0 && n <= d.weekly)) add('bad', 'Лавка альянса: «' + d.name + '» куплено ' + n + ' из ' + d.weekly);
			if (st.ally.my < 0 && err !== 32) add('bad', 'Лавка альянса: вне альянса «' + d.name + '» даёт отказ ' + err);
			if (err === 0 && (h.pts < d.price || n >= d.weekly || st.castle < Math.max(honr.castle, d.castle || 1))) add('bad', 'Лавка альянса: «' + d.name + '» продаётся при почёте ' + h.pts + ', куплено ' + n + ', Замок ' + st.castle);
		});
		for (const [ck, k] of [[h.dg, 0], [h.df, 1]]) {
			if (st.ally.my < 0 && ck.err !== 32) add('bad', 'Пожертвование вне альянса: отказ ' + ck.err);
			if (ck.err === 0 && (h.don >= honr.donate.perDay || st.res[k] < ck.cost[k] || ck.cost[k] !== honr.donate[k ? 'food' : 'gold'] * st.castle)) add('bad', 'Пожертвование открыто: сегодня ' + h.don + ', цена ' + ck.cost[k] + ', есть ' + st.res[k]);
		}
	}
	// War Banners: silk never below zero; a banner within its levels, its bonus and next cost as the rules say; a level
	// offered only from the Castle, below the top and with the silk and gold for it.
	const bnr = rules().banners;
	if (bnr && st.banners && st.banners.on) {
		const bn = st.banners;
		const max = bnr.silk.length;
		if (!(Number.isInteger(bn.silk) && bn.silk >= 0)) add('bad', 'Знамённый шёлк: ' + bn.silk);
		if (bn.list.length !== army().troops.length) add('bad', 'Знамёна: ' + bn.list.length + ' при ' + army().troops.length + ' войсках');
		bn.list.forEach((x, t) => {
			const name = (army().troops[t] || {}).name || t;
			const want = x.lv * bnr.pct + (bnr.perkLevels || []).filter((p) => x.lv >= p).length * (bnr.perkPct || 0);
			if (!(x.lv >= 0 && x.lv <= max)) add('bad', 'Знамя «' + name + '»: уровень ' + x.lv + ' из ' + max);
			if (x.pct !== want) add('bad', 'Знамя «' + name + '»: прибавка ' + x.pct + '%, а по правилам ' + want + '%');
			if (x.silk !== (x.lv < max ? bnr.silk[x.lv] : 0)) add('bad', 'Знамя «' + name + '»: шёлка на следующий уровень ' + x.silk);
			const ok = st.castle >= bnr.castle && x.lv < max && bn.silk >= x.silk && st.res[0] >= bnr.gold[x.lv];
			if ((x.ck.err === 0) !== ok) add('bad', 'Знамя «' + name + '»: проверка ' + x.ck.err + ' при Замке ' + st.castle + ', ур. ' + x.lv + ', шёлке ' + bn.silk + ', золоте ' + st.res[0]);
			if (st.castle < bnr.castle && x.ck.err !== 6) add('bad', 'Знамя «' + name + '» ниже Замка ' + bnr.castle + ': отказ ' + x.ck.err);
		});
	}
	// Alliance Tech: a level as its points say (within the top), the next level's total, the player's points within the
	// alliance's; outside an alliance nothing; the tech donations go to is the first of the lowest; the war and boss
	// effects are their level times the effect a level.
	const tcr = rules().allianceTech;
	if (tcr && st.tech && st.tech.on) {
		const tc = st.tech;
		const max = tcr.costs.length;
		if (tc.list.length !== tcr.list.length) add('bad', 'Технологии альянса: ' + tc.list.length + ', а в правилах ' + tcr.list.length);
		let low = -1;
		tc.list.forEach((x, i) => {
			const d = tcr.list[i];
			if (!d) return;
			let lv = 0;
			let sum = 0;
			while (lv < max && x.pts >= sum + tcr.costs[lv]) sum += tcr.costs[lv++];
			if (x.lv !== lv) add('bad', 'Технология «' + d.name + '»: уровень ' + x.lv + ' при ' + x.pts + ' очках, а по правилам ' + lv);
			if (x.need !== (st.ally.my < 0 || lv >= max ? 0 : sum + tcr.costs[lv])) add('bad', 'Технология «' + d.name + '»: до следующего уровня ' + x.need);
			if (!(x.mine >= 0 && x.mine <= x.pts)) add('bad', 'Технология «' + d.name + '»: ваших очков ' + x.mine + ' из ' + x.pts);
			if (st.ally.my < 0 && (x.lv || x.pts || x.mine)) add('bad', 'Технология «' + d.name + '» вне альянса: ур. ' + x.lv + ', очков ' + x.pts);
			if (x.lv < max && (low < 0 || x.lv < tc.list[low].lv)) low = i;
		});
		if (tc.auto !== (st.ally.my < 0 ? -1 : low)) add('bad', 'Технологии альянса: пожертвования идут в ' + tc.auto + ', а должны в ' + low);
		for (const k of ['war', 'boss']) {
			const i = tcr.list.findIndex((d) => d.kind === k);
			const want = i >= 0 && tc.list[i] ? tc.list[i].lv * tcr.list[i].per : 0;
			if (tc[k] !== want) add('bad', 'Технологии альянса: «' + k + '» даёт ' + tc[k] + '%, а по уровню ' + want + '%');
		}
	}
	// Comeback gift: only for a week or more away, from its Castle, and the absence ended no later than now.
	const cbr = rules().comeback;
	if (cbr && st.comeback) {
		const cb = st.comeback;
		if (cb.away === 0 ? cb.since !== 0 : !(cb.away >= cbr.days * 86400 && cb.since >= 0 && cb.since + cb.away <= st.now)) add('bad', 'Подарок за возвращение: отсутствие ' + cb.away + ' с, с ' + cb.since + ', сейчас ' + st.now);
		if (cb.away && !cbr.days) add('bad', 'Подарок за возвращение ждёт, а он выключен');
	}
	// Kingdom Power: the parts add up; the strongest titans count, strongest first, with their own Power.
	const pw = st.power;
	if (pw) {
		const most = (rules().power || {}).titans || 5;
		const pc = pw.c || 0;
		if (![pw.b, pw.t, pw.r, pc, pw.total].every((v) => Number.isFinite(v) && v >= 0) || pw.b + pw.t + pw.r + pc !== pw.total) add('bad', 'Сила королевства: ' + pw.b + ' + ' + pw.t + ' + ' + pw.r + ' + ' + pc + ' ≠ ' + pw.total);
		const cx = rules().codex;
		if (st.codex && st.codex.on && cx && pc !== cx.power * st.codex.e.filter((x) => x > 0).length) add('bad', 'Сила королевства: Кодекс даёт ' + pc + ', а записей ' + st.codex.e.filter((x) => x > 0).length);
		if (pw.top.length > most || pw.top.some((x, i) => i > 0 && x[1] > pw.top[i - 1][1])) add('bad', 'Сила королевства: титаны не по убыванию или их больше ' + most);
		if (pw.top.reduce((s, x) => s + x[1], 0) !== pw.t) add('bad', 'Сила королевства: титаны дают ' + pw.t + ', а их сумма ' + pw.top.reduce((s, x) => s + x[1], 0));
		for (const [id, p] of pw.top) {
			const t = st.titans.find((x) => x.id === id);
			if (!t || t.pw !== p) add('bad', 'Сила королевства: титан #' + id + ' учтён с силой ' + p + ', а у него ' + (t ? t.pw : 'его нет'));
		}
		const cut = pw.top.length ? pw.top[pw.top.length - 1][1] : 0;
		const left = st.titans.filter((t) => !pw.top.some((x) => x[0] === t.id));
		if (pw.top.length && (left.some((t) => t.pw > cut) || (pw.top.length < most && left.length))) add('bad', 'Сила королевства: не учтён титан посильнее');
	}
	// The Marshal of the open prepare screen (for the camp as it is): the plan fits the camp and the slots and scores at least the army taken.
	const mr = K.prep && K.scoutRes;
	const prepOpen = !!document.querySelector('#kSheet:not([hidden]) [data-act="prep-go"]');
	if (prepOpen && mr && mr.plan && K.scoutText && K.scoutText.includes('|' + st.army.n.join() + '|') && !(K.prep.assault && st.assault.run)) {
		const n = mr.plan.reduce((x, y) => x + y, 0);
		if (n > st.army.slots || mr.plan.some((x, t) => x < 0 || x > st.army.n[t])) add('bad', 'План маршала не помещается в лагерь или в места: ' + mr.plan.join(','));
		if (mr.mt.length > 3) add('bad', 'Советов маршала ' + mr.mt.length + ', а их не больше трёх');
		const taken = K.prep.counts.reduce((x, y) => x + y, 0);
		if (taken <= st.army.slots && K.prep.counts.every((x, t) => x <= st.army.n[t]) && mr.ps < mr.ts) add('bad', 'План маршала (' + mr.ps + ') слабее взятого войска (' + mr.ts + ')');
	}
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
		const stA = B.state();
		const hA = stA.hash;
		const sA = B.save();
		landStores(st, stA, (l, txt) => add(l, txt + ' (за 6 часов)'), false);
		resourceCaps(st, stA, (l, txt) => add(l, txt + ' (за 6 часов)'), false);
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
const loKeyOf = (st) => (st.loadouts || []).map((lo) => [lo.n, lo.t, lo.s.join('.'), lo.p.join('.')].join(':')).join('/');
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
	// Actions that pay a reward, which may go on top of full vaults.
	const REWARDS = new Set(['цель', 'задание', 'сундук заданий', 'достижение', 'календарь новичка', 'календарь месяца', 'недельное задание', 'недельный сундук', 'награда испытания', 'место в испытании', 'путь войны', 'сундук пути', 'штурм', 'рейдовый босс', 'сундук альянса', 'ежедневное испытание', 'зачистка испытания', 'атака на войне', 'сундук войны', 'атака в Завоевании', 'сундук Завоевания', 'награда события', 'помочь союзникам', 'башня титанов', 'подарок за возвращение', 'лавка альянса']);
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
		['цель', 3, () => (B.goalClaim ? B.goalClaim().err : -1)],
		['задание', 3, (st) => (B.questClaim && st.quests && st.quests.list.length ? B.questClaim(Math.floor(r() * st.quests.list.length)).err : -1)],
		['замена задания', 1, (st) => (B.questReroll && st.quests && st.quests.list.length ? B.questReroll(Math.floor(r() * st.quests.list.length)) : -1)],
		['сундук заданий', 1, () => (B.questChest ? B.questChest().err : -1)],
		['достижение', 2, (st) => (B.achClaim && st.ach && st.ach.v.length ? B.achClaim(Math.floor(r() * st.ach.v.length)).err : -1)],
		['календарь новичка', 1, () => {
			const days = (rules().calendar && rules().calendar.newcomer) || [];
			return B.calNewcomer && days.length ? B.calNewcomer(Math.floor(r() * days.length), Math.floor(r() * 3)).err : -1;
		}],
		['календарь месяца', 1, () => (B.calMonth ? B.calMonth().err : -1)],
		['недельное задание', 2, (st) => (B.weekClaim && st.week && st.week.list.length ? B.weekClaim(Math.floor(r() * st.week.list.length)).err : -1)],
		['замена недельного задания', 1, (st) => (B.weekReroll && st.week && st.week.list.length ? B.weekReroll(Math.floor(r() * st.week.list.length)) : -1)],
		['недельный сундук', 1, () => (B.weekChest ? B.weekChest().err : -1)],
		['испытание', 3, (st) => {
			if (!B.trialStart || !st.trial || st.trial.i < 0) return -1;
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = pick(st.titans.filter((x) => x.rec <= st.now));
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.trialStart(squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			const err = B.trialFinish(Math.floor(r() * 4), Math.floor(r() * 7000)).err;
			// Nothing is lost in a trial.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('испытание забрало отряды или заклинания');
			if (st2.trial.best > 5500 || st2.trial.best < st.trial.best) throw new Error('лучший счёт испытания ' + st2.trial.best + ' после ' + st.trial.best);
			return err;
		}],
		['награда испытания', 1, (st) => (B.trialMilestone && st.trial && st.trial.i >= 0 ? B.trialMilestone(Math.floor(r() * 3)).err : -1)],
		['место в испытании', 1, (st) => (B.trialRank && st.trial && st.trial.i >= 0 ? B.trialRank().err : -1)],
		['путь войны', 2, (st) => {
			const n = (rules().warpath && rules().warpath.tiers && rules().warpath.tiers.length) || 0;
			return B.wpClaim && st.wp && st.wp.no >= 0 && n ? B.wpClaim(Math.floor(r() * (n + 1))).err : -1;
		}],
		['уровень пути', 1, (st) => {
			if (!B.wpSkip || !st.wp || st.wp.no < 0) return -1;
			const code = B.wpSkip();
			const st2 = B.state();
			if (code === 0 && (st2.res[2] !== st.res[2] - rules().warpath.skipGems || st2.wp.tier !== st.wp.tier + 1)) throw new Error('уровень пути: самоцветов ' + st.res[2] + ' → ' + st2.res[2] + ', уровень ' + st.wp.tier + ' → ' + st2.wp.tier);
			return code;
		}],
		['сундук пути', 1, (st) => (B.wpBonus && st.wp && st.wp.no >= 0 ? B.wpBonus().err : -1)],
		['штурм', 4, (st) => {
			if (!B.asStart || !st.assault || st.assault.no < 0) return -1;
			const as = st.assault;
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			let code;
			let kind = 'stage';
			if (!as.run) {
				kind = 'start';
				const squads = [];
				st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
				const ti = pick(st.titans.filter((x) => x.rec <= st.now));
				code = B.asStart(squads, ti ? ti.id : 0);
			} else if (as.offer.length) {
				kind = 'boon';
				code = B.asBoon(Math.floor(r() * 4));
			} else {
				const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
				code = B.asStage(spells);
				if (code === 0) {
					const alive = as.squads.filter((q) => q[2] > 0);
					code = B.asFinish(Math.floor(r() * 4), alive.map(() => Math.floor(r() * 101))).err;
				}
			}
			// The run's army is a copy: the camp and the spells stay; the run never goes back.
			const st2 = B.state();
			const a2 = st2.assault;
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('штурм забрал отряды или заклинания лагеря');
			if (a2.no === as.no && (a2.stage < as.stage || a2.tries > as.tries || (as.over && !a2.over))) throw new Error('штурм откатился: этап ' + as.stage + ' → ' + a2.stage + ', попыток ' + as.tries + ' → ' + a2.tries);
			if (a2.no === as.no && kind === 'stage' && a2.squads.some((q, k) => q[2] > as.squads[k][2])) throw new Error('в бою штурма отряд прибавил бойцов: ' + as.squads.map((q) => q[2]).join() + ' → ' + a2.squads.map((q) => q[2]).join());
			if (st2.pendingAssault) throw new Error('бой штурма остался незавершённым');
			return code;
		}],
		['конец штурма', 1, () => (B.asEnd ? B.asEnd() : -1)],
		['рейдовый босс', 3, (st) => {
			if (!B.bossStart || !st.boss || st.boss.i < 0) return -1;
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = pick(st.titans.filter((x) => x.rec <= st.now));
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.bossStart(squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			if (!B.state().pendingBoss) throw new Error('бой с боссом начался, но не висит');
			const err = B.bossFinish(Math.floor(r() * 2), Math.floor(r() * 1200)).err;
			// Nothing is lost against the boss; the player's damage only grows within an event.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('бой с боссом забрал отряды или заклинания');
			if (st2.pendingBoss) throw new Error('бой с боссом остался незавершённым');
			if (st2.boss.no === st.boss.no && st2.ally.my === st.ally.my && (st2.boss.mine < st.boss.mine || st2.boss.mb !== st.boss.mb + 1)) throw new Error('урон по боссу ' + st.boss.mine + ' → ' + st2.boss.mine + ', боёв ' + st.boss.mb + ' → ' + st2.boss.mb);
			return err;
		}],
		['сундук альянса', 1, () => (B.bossChest ? B.bossChest().err : -1)],
		['ежедневное испытание', 3, (st) => {
			if (!B.dailyStart || !st.daily || !st.daily.list.length) return -1;
			const i = Math.floor(r() * st.daily.list.length);
			const x = st.daily.list[i];
			// Now and then the tier past the open ones, which must be refused.
			const tier = 1 + Math.floor(r() * (x.top + 1));
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = r() < 0.85 ? pick(st.titans.filter((y) => y.rec <= st.now)) : null;
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.dailyStart(i, tier, squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			if (tier > x.top) throw new Error('бой испытания начался на закрытом уровне ' + tier + ' (открыт до ' + x.top + ')');
			const st1 = B.state();
			if (st1.pendingDaily[0] !== i || st1.pendingDaily[1] !== tier || st1.daily.list[i].left !== x.left - 1) throw new Error('бой испытания: висит ' + st1.pendingDaily.join(':') + ', попыток ' + x.left + ' → ' + st1.daily.list[i].left);
			const stars = Math.floor(r() * 4);
			const err = B.dailyFinish(stars).err;
			// Nothing is lost in a trial; a win keeps its best stars.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('бой испытания забрал отряды или заклинания');
			if (st2.pendingDaily[0] >= 0) throw new Error('бой испытания остался незавершённым');
			if (st2.daily.list[i].stars[tier - 1] !== Math.max(x.stars[tier - 1], stars)) throw new Error('звёзды уровня ' + tier + ': ' + x.stars[tier - 1] + ' и бой на ' + stars + ' → ' + st2.daily.list[i].stars[tier - 1]);
			return err;
		}],
		['зачистка испытания', 2, (st) => {
			if (!B.dailySweep || !st.daily || !st.daily.list.length) return -1;
			const i = Math.floor(r() * st.daily.list.length);
			const x = st.daily.list[i];
			const tier = 1 + Math.floor(r() * Math.max(1, x.top));
			const ti = r() < 0.8 ? pick(st.titans) : null;
			const res = B.dailySweep(i, tier, ti ? ti.id : 0);
			if (res.err === 0) {
				const st2 = B.state();
				if (x.stars[tier - 1] < 3 || tier > x.top) throw new Error('зачистка уровня ' + tier + ' без трёх звёзд (' + x.stars[tier - 1] + ') или закрытого');
				if (st2.daily.list[i].left !== x.left - 1) throw new Error('зачистка: попыток ' + x.left + ' → ' + st2.daily.list[i].left);
			}
			return res.err;
		}],
		['сложность босса', 1, () => (B.bossTier ? B.bossTier(Math.floor(r() * 12) - 1) : -1)],
		['атака на войне', 3, (st) => {
			if (!B.warStart || !st.war) return -1;
			// Now and then a base past the roster, which must be refused.
			const i = Math.floor(r() * (st.war.roster + 1));
			const base = st.war.them[i];
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = r() < 0.85 ? pick(st.titans.filter((y) => y.rec <= st.now)) : null;
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.warStart(i, squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			if (!base || base[2] >= 3 || st.war.ph !== 1 || st.war.left <= 0) throw new Error('атака на войне началась: база ' + i + (base ? ' (' + base[2] + ' ★)' : ' вне состава') + ', фаза ' + st.war.ph + ', атак ' + st.war.left);
			const st1 = B.state();
			if (st1.pendingWar !== i || st1.war.left !== st.war.left - 1) throw new Error('атака на войне: висит ' + st1.pendingWar + ', атак ' + st.war.left + ' → ' + st1.war.left);
			const stars = Math.floor(r() * 4);
			const err = B.warFinish(stars).err;
			// Nothing is lost at war; the base keeps its best stars.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('атака на войне забрала отряды или заклинания');
			if (st2.pendingWar >= 0) throw new Error('атака на войне осталась незавершённой');
			if (st2.war.no === st.war.no && st2.war.on && st2.war.them[i][2] < Math.max(base[2], stars)) throw new Error('звёзды базы ' + i + ': ' + base[2] + ' и атака на ' + stars + ' → ' + st2.war.them[i][2]);
			return err;
		}],
		['сундук войны', 1, () => (B.warChest ? B.warChest().err : -1)],
		['атака в Завоевании', 3, (st) => {
			if (!B.conqStart || !st.conq) return -1;
			const c = st.conq;
			const map = conqHexes();
			// Mostly a tile next to the alliance's land; now and then any tile, one past the map or none, which may be refused.
			const near = c.on ? map.map((h, k) => k).filter((k) => map[k].kind !== 0 && c.tiles[k][0] !== 0 && map[k].adj.some((j) => c.tiles[j][0] === 0)) : [];
			const t = near.length && r() < 0.75 ? pick(near) : Math.floor(r() * (map.length + 2)) - 1;
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = r() < 0.85 ? pick(st.titans.filter((y) => y.rec <= st.now)) : null;
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.conqStart(t, squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			const x = c.tiles[t];
			const legal = c.on && c.ph === 1 && c.tok > 0 && st.castle >= rules().conquest.castle && x && map[t].kind !== 0 && x[0] !== 0 && x[2] <= st.now && map[t].adj.some((j) => c.tiles[j][0] === 0);
			if (!legal) throw new Error('атака в Завоевании началась: клетка ' + t + (x ? ' у ' + x[0] + ', перемирие до ' + x[2] : ' вне карты') + ', фаза ' + c.ph + ', жетонов ' + c.tok);
			const st1 = B.state();
			if (st1.conq.at !== t || st1.conq.tok !== c.tok - 1) throw new Error('атака в Завоевании: висит ' + st1.conq.at + ', жетонов ' + c.tok + ' → ' + st1.conq.tok);
			const stars = Math.floor(r() * 4);
			const err = B.conqFinish(stars).err;
			// Nothing is lost; the stars count for the player.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('атака в Завоевании забрала отряды или заклинания');
			if (st2.conq.at >= 0) throw new Error('атака в Завоевании осталась незавершённой');
			if (st2.conq.no === c.no && st2.conq.on && (st2.conq.attacks !== c.attacks + 1 || st2.conq.stars !== c.stars + stars)) throw new Error('Завоевание: атак ' + c.attacks + ' → ' + st2.conq.attacks + ', звёзд ' + c.stars + ' + ' + stars + ' → ' + st2.conq.stars);
			return err;
		}],
		['сундук Завоевания', 1, () => (B.conqChest ? B.conqChest().err : -1)],
		['награда события', 2, (st) => {
			if (!B.seasonMs || !st.sev || !st.sev.length) return -1;
			// A milestone or the rank of an event, now and then a wrong one.
			const e = Math.floor(r() * (st.sev.length + 1)) - (r() < 0.1 ? 1 : 0);
			const v = st.sev[e];
			const ms = rules().seasonEvents.list[e] ? rules().seasonEvents.list[e].milestones.length : 1;
			const res = r() < 0.3 ? B.seasonRank(e) : B.seasonMs(e, Math.floor(r() * (ms + 1)));
			if (res.err === 0 && v) {
				const st2 = B.state();
				if (st2.sev[e].ms === v.ms && st2.sev[e].rk === v.rk) throw new Error('награда события ' + e + ' выдана, а отметки нет');
			}
			return res.err;
		}],
		['просьба о помощи', 3, (st) => {
			if (!B.helpAsk || !st.help) return -1;
			// A running timer, now and then a wrong kind or one that is not running.
			const c = r() < 0.85 ? pick(st.help.can) : null;
			const kind = c ? c[0] : Math.floor(r() * 7) - 1;
			const id = c ? c[1] : Math.floor(r() * 50);
			const code = B.helpAsk(kind, id);
			if (code === 0) {
				const st2 = B.state();
				const a = st2.help.asks.find((x) => x[0] === kind && x[1] === id);
				if (!c || c[3] !== 0 || !a || a[5] !== 0 || a[6] !== st.help.lim) throw new Error('просьба о помощи (вид ' + kind + ', #' + id + ') принята: ' + (c ? 'проверка ' + c[3] : 'таймера нет') + ', просьба ' + (a ? a.join(',') : 'не появилась'));
			}
			return code;
		}],
		['помочь союзникам', 2, (st) => {
			if (!B.helpGive || !st.help) return -1;
			const open = st.help.ally.filter((a) => !a[5]).length;
			const res = B.helpGive();
			if (res.err === 0) {
				const st2 = B.state();
				const n = st2.help.today - (st.help.today > st2.help.today ? 0 : st.help.today);
				if (n < 1 || n > open || st2.help.ally.some((a) => !a[5] && st2.help.today < rules().allianceHelp.dailyCap)) throw new Error('помощь союзникам: открытых было ' + open + ', помогли ' + n + ', осталось ' + st2.help.ally.filter((a) => !a[5]).length);
			}
			return res.err;
		}],
		['бой миссии', 2, (st) => {
			if (!st.campaign || !st.campaign.length) return -1;
			const a = Math.floor(r() * st.campaign.length);
			// Now and then on Heroic (mission numbers from 50 there).
			const h = !!st.campaign[a].heroic && r() < 0.4;
			const c = h ? st.campaign[a].heroic : st.campaign[a];
			const base = h ? 50 : 0;
			const nm = (h ? 'героической ' : '') + 'миссии ' + (a + 1) + '.';
			// The next mission or one won before, now and then one past it.
			const m = Math.min(c.stars.length - 1, Math.floor(r() * (c.next + 2)));
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = r() < 0.85 ? pick(st.titans.filter((y) => y.rec <= st.now)) : null;
			const code = B.missionStart(a, base + m, squads, ti ? ti.id : 0, []);
			if (code !== 0) return code;
			if (c.err[m] !== 0 && c.err[m] !== undefined) throw new Error('бой ' + nm + (m + 1) + ' начался при проверке ' + c.err[m]);
			const stars = Math.floor(r() * 4);
			const res = B.missionFinish(a, base + m, stars, squads.map(() => Math.floor(r() * 101)), []);
			const st2 = B.state();
			const c2 = h ? st2.campaign[a].heroic : st2.campaign[a];
			if (c2.stars[m] !== Math.max(c.stars[m], stars)) throw new Error('звёзды ' + nm + (m + 1) + ': ' + c.stars[m] + ' и бой на ' + stars + ' → ' + c2.stars[m]);
			if (!!res.first !== (stars > 0 && c.stars[m] === 0)) throw new Error(nm + (m + 1) + ': награда за первую победу ' + res.first + ' при звёздах ' + c.stars[m] + ' → ' + stars);
			if (h && st2.campaign[a].stars.join('') !== st.campaign[a].stars.join('')) throw new Error('героический бой изменил обычные звёзды акта ' + (a + 1));
			return res.err;
		}],
		['зачистка миссии', 3, (st) => {
			if (!B.missionSweep || !st.campaign || !st.campaign.length) return -1;
			const a = Math.floor(r() * st.campaign.length);
			const h = !!st.campaign[a].heroic && r() < 0.4;
			const c = h ? st.campaign[a].heroic : st.campaign[a];
			// Mostly a mission won on 3 stars.
			const won = c.stars.map((x, k) => k).filter((k) => c.stars[k] >= 3);
			const m = won.length && r() < 0.85 ? pick(won) : Math.floor(r() * c.stars.length);
			const ti = r() < 0.8 ? pick(st.titans) : null;
			const army = st.army.n.join();
			const res = B.missionSweep(a, (h ? 50 : 0) + m, ti ? ti.id : 0);
			if (res.err === 0) {
				const st2 = B.state();
				const cr = rules().campaigns;
				const level = cr.acts[a].missions[m].level;
				const food = cr.sweep.food * (h ? Math.min(rules().battles.length, level + cr.heroic.level) : level);
				if (c.sw[m] !== 0 || c.stars[m] < 3) throw new Error('зачистка миссии ' + (a + 1) + '.' + (m + 1) + ' при проверке ' + c.sw[m] + ' и звёздах ' + c.stars[m]);
				if (st2.scrolls !== st.scrolls - 1) throw new Error('зачистка: свитков ' + st.scrolls + ' → ' + st2.scrolls);
				if (st2.army.n.join() !== army || res.lost) throw new Error('зачистка забрала отряды');
				if (st2.res[1] !== st.res[1] - food + res.food) throw new Error('зачистка: еды ' + st.res[1] + ' − ' + food + ' + ' + res.food + ' → ' + st2.res[1]);
				if (ti && (ti.rec > st.now || res.titan !== ti.id)) throw new Error('зачистка с титаном #' + ti.id + (ti.rec > st.now ? ', который отдыхал' : '') + ': опыт получил #' + res.titan);
				if (st2.res[3] < st.res[3]) throw new Error('зачистка потратила камни портала');
			}
			return res.err;
		}],
		['набор армии', 2, (st) => {
			if (!B.loadoutSave || !st.loadouts) return -1;
			const n = st.loadouts.length;
			// A slot that is there or the next one, now and then one past it; now and then a bad troop, spell or titan.
			const slot = Math.floor(r() * (n + 2));
			const squads = [];
			const k = Math.floor(r() * (st.army.slots + (r() < 0.1 ? 2 : 1)));
			for (let j = 0; j < k; j++) squads.push(r() < 0.03 ? st.army.n.length : Math.floor(r() * st.army.n.length));
			const spells = [];
			for (let j = 0; j < st.spells.stock.length && spells.length < st.spells.kinds + (r() < 0.1 ? 1 : 0); j++) if (r() < 0.4) spells.push(j);
			const ti = r() < 0.7 ? pick(st.titans) : null;
			const titan = ti ? ti.id : r() < 0.05 ? 999999 : 0;
			const name = r() < 0.5 ? '' : pick(['Штурм', '  Лучники  ', 'Против конницы', 'x'.repeat(30), 'Ночной\nнабег']);
			const code = B.loadoutSave(slot, name, titan, squads, spells);
			const st2 = B.state();
			if (code === 0) {
				const lo = st2.loadouts[slot];
				if (slot > n || !lo || lo.t !== titan || lo.s.join() !== squads.join() || lo.p.join() !== spells.join() || lo.n !== name.trim()) throw new Error('набор армии ' + slot + ' сохранён не так: ' + JSON.stringify(lo));
				if (st2.loadouts.length !== (slot === n ? n + 1 : n)) throw new Error('наборов армии ' + n + ' → ' + st2.loadouts.length);
			} else if (st2.loadouts.length !== n || loKeyOf(st2) !== loKeyOf(st)) throw new Error('отказ ' + code + ' всё же изменил наборы армии');
			return code;
		}],
		['удалить набор', 1, (st) => {
			if (!B.loadoutDelete || !st.loadouts) return -1;
			const slot = Math.floor(r() * (st.loadouts.length + 1));
			const code = B.loadoutDelete(slot);
			const st2 = B.state();
			if (code === 0 && (st2.loadouts.length !== st.loadouts.length - 1 || loKeyOf({ loadouts: st.loadouts.filter((x, j) => j !== slot) }) !== loKeyOf(st2))) throw new Error('удаление набора ' + slot + ' сдвинуло не те');
			return code;
		}],
		['собрать святилище', 2, (st) => {
			if (!B.shrineCollect || !st.divine) return -1;
			const res = B.shrineCollect();
			const st2 = B.state();
			if (res.err === 0 && (res.gems !== st.divine.st || st2.divine.g !== st.divine.g + res.gems || st2.divine.st !== 0)) {
				throw new Error('святилище: было ' + st.divine.st + ', собрано ' + res.gems + ', божественных ' + st.divine.g + ' → ' + st2.divine.g + ', осталось ' + st2.divine.st);
			}
			if (res.err !== 0 && st2.divine.g !== st.divine.g) throw new Error('отказ ' + res.err + ' всё же изменил божественные самоцветы');
			return res.err;
		}],
		['божественный алтарь', 1, (st) => {
			const dr = rules().divine;
			if (!B.divineBuy || !st.divine || !dr) return -1;
			const ai = Math.floor(r() * dr.altars.length);
			const a = dr.altars[ai];
			const titans = d.K.army.titans;
			const ok = a.titan ? titans.map((x, i) => i).filter((i) => titans[i].stars === a.titan) : d.K.army.relics.kinds.map((x, i) => i);
			const p = r() < 0.1 ? Math.floor(r() * (titans.length + 2)) - 1 : pick(ok);
			const res = B.divineBuy(ai, p);
			const st2 = B.state();
			if (res.err === 0) {
				if (!ok.includes(p) || st2.divine.g !== st.divine.g - a.price) throw new Error('алтарь «' + a.name + '»: выбор ' + p + ', божественных ' + st.divine.g + ' → ' + st2.divine.g);
				const t = a.titan ? st2.titans.find((x) => x.id === res.id) : null;
				const rl = a.titan ? null : st2.relics.find((x) => x.id === res.id);
				if (a.titan ? !t || t.def !== p || st2.titans.length !== st.titans.length + 1 : !rl || rl.kind !== p || rl.r !== a.relic || st2.relics.length !== st.relics.length + 1) {
					throw new Error('алтарь «' + a.name + '» дал не то: ' + JSON.stringify(t || rl || res));
				}
			} else if (st2.divine.g !== st.divine.g || st2.titans.length !== st.titans.length || st2.relics.length !== st.relics.length) throw new Error('отказ алтаря ' + res.err + ' всё же что-то изменил');
			return res.err;
		}],
		['башня титанов', 3, (st) => {
			if (!B.spireStart || !st.spire) return -1;
			const sp = st.spire;
			const squads = [];
			st.army.n.forEach((n, k) => { for (let j = 0; j < n && squads.length < st.army.slots; j++) if (r() < 0.6) squads.push(k); });
			const ti = r() < 0.85 ? pick(st.titans.filter((y) => y.rec <= st.now)) : null;
			const spells = st.spells.stock.map((n, k) => k).filter((k) => st.spells.stock[k] > 0 && r() < 0.5).slice(0, st.spells.kinds);
			const army = st.army.n.join();
			const stock = st.spells.stock.join();
			const code = B.spireStart(squads, ti ? ti.id : 0, spells);
			if (code !== 0) return code;
			if (sp.ck.err !== 0) throw new Error('бой в башне начался при отказе ' + sp.ck.err);
			const st1 = B.state();
			if (st1.spire.at !== sp.next) throw new Error('бой в башне: висит этаж ' + st1.spire.at + ', а следующим был ' + sp.next);
			// Mostly wins, to climb.
			const stars = r() < 0.7 ? 1 + Math.floor(r() * 3) : 0;
			const res = B.spireFinish(stars);
			// Nothing is lost in the spire; a win takes the floor and pays it, a loss changes nothing.
			const st2 = B.state();
			if (st2.army.n.join() !== army || st2.spells.stock.join() !== stock) throw new Error('бой в башне забрал отряды или заклинания');
			if (st2.spire.at) throw new Error('бой в башне остался незавершённым');
			if (st2.spire.top !== (stars > 0 ? sp.next : sp.top)) throw new Error('башня: этаж ' + sp.next + ' на ' + stars + ' ★, взято ' + sp.top + ' → ' + st2.spire.top);
			const band = rules().spire.bands[Math.floor((sp.next - 1) / rules().spire.perBand)];
			if (res.err === 0 && (res.gold + res.food) !== (stars > 0 ? band.pay * Math.max(1, st.castle) : 0)) throw new Error('башня: этаж ' + sp.next + ' заплатил ' + res.gold + ' золота и ' + res.food + ' еды');
			return res.err;
		}],
		['кодекс', 2, (st) => {
			if (!B.codexClaim || !st.codex || !st.codex.on) return -1;
			const cx = st.codex;
			// Mostly something waiting; now and then a page or an entry not ready, to see the refusal.
			const ready = cx.e.map((v, i) => [v, i]).filter((x) => x[0] === 1).map((x) => x[1]);
			const pages = cx.p.map((v, i) => [v[0], i]).filter((x) => x[0] === 1).map((x) => x[1]);
			const page = pages.length && r() < 0.5 ? true : !ready.length ? r() < 0.5 : false;
			const i = page ? (pages.length ? pick(pages) : Math.floor(r() * cx.p.length)) : (ready.length && r() < 0.8 ? pick(ready) : Math.floor(r() * cx.e.length));
			const gems = st.res[2];
			const res = B.codexClaim(i, page);
			const st2 = B.state();
			const was = page ? cx.p[i][0] : cx.e[i];
			const want = was === 1 ? 0 : was === 2 ? 13 : page ? 23 : 6;
			if (res.err !== want && !(page && was === 1 && res.err === 28)) throw new Error('кодекс: ' + (page ? 'страница ' : 'запись ') + i + ' в состоянии ' + was + ', отказ ' + res.err + ' вместо ' + want);
			if (res.err !== 0) {
				if (st2.res.join() !== st.res.join() || JSON.stringify(st2.codex) !== JSON.stringify(cx)) throw new Error('отказ кодекса ' + res.err + ' всё же что-то изменил');
				return res.err;
			}
			const now = page ? st2.codex.p[i][0] : st2.codex.e[i];
			if (now !== 2) throw new Error('кодекс: после награды ' + (page ? 'страница ' : 'запись ') + i + ' в состоянии ' + now);
			if (!page && st2.res[2] !== gems + rules().codex.entryGems[army().titans[i].stars - 1]) throw new Error('кодекс: запись ' + i + ' дала ' + (st2.res[2] - gems) + ' самоцветов');
			return 0;
		}],
		['пожертвование', 2, (st) => {
			if (!B.donate || !st.honor || !st.honor.on) return -1;
			const food = r() < 0.5;
			const k = food ? 1 : 0;
			const ck = food ? st.honor.df : st.honor.dg;
			// Half of them to an Alliance Tech picked at random (one at the top is refused).
			const tr = rules().allianceTech;
			const tc = tr && st.tech && st.tech.on ? st.tech : null;
			const tech = tc && r() < 0.5 ? Math.floor(r() * tc.list.length) : -1;
			const top = tech >= 0 && tc.list[tech].lv >= tr.costs.length;
			const want = top && ck.err !== 32 && ck.err !== 6 && ck.err !== 7 ? 2 : ck.err;
			const res = B.donate(food, tech);
			const st2 = B.state();
			if (res.err !== want) throw new Error('пожертвование ' + (food ? 'едой' : 'золотом') + (tech >= 0 ? ' в технологию ' + tech : '') + ': отказ ' + res.err + ', а проверка ' + want);
			if (res.err !== 0) {
				if (st2.res.join() !== st.res.join() || st2.honor.pts !== st.honor.pts || st2.honor.don !== st.honor.don || (tc && st2.tech.list.map((x) => x.mine).join() !== tc.list.map((x) => x.mine).join())) throw new Error('отказ пожертвования ' + res.err + ' всё же что-то изменил');
				return res.err;
			}
			if (tc) {
				const to = tech >= 0 ? tech : tc.auto;
				if (res.tech !== to || res.techPts !== (to >= 0 ? tr.points : 0)) throw new Error('пожертвование: в технологию ' + res.tech + ' (+' + res.techPts + '), а должно в ' + to);
				const got = st2.tech.list.map((x, i) => x.mine - tc.list[i].mine);
				if (got.some((g, i) => g !== (i === to ? tr.points : 0))) throw new Error('пожертвование: очки технологий изменились на ' + got.join());
				if (to >= 0 && res.techLv !== st2.tech.list[to].lv) throw new Error('пожертвование: уровень ' + res.techLv + ', а в королевстве ' + st2.tech.list[to].lv);
			}
			const R = rules().honor;
			if (st2.honor.pts !== st.honor.pts + R.donate.honor || res.honor !== R.donate.honor || st2.honor.don !== st.honor.don + 1) throw new Error('пожертвование: почёт ' + st.honor.pts + ' → ' + st2.honor.pts + ', за день ' + st.honor.don + ' → ' + st2.honor.don);
			if (st2.res[k] !== st.res[k] - ck.cost[k] || ck.cost[k] !== R.donate[food ? 'food' : 'gold'] * st.castle) throw new Error('пожертвование: ' + (food ? 'еды' : 'золота') + ' ' + st.res[k] + ' → ' + st2.res[k] + ' при цене ' + ck.cost[k]);
			return 0;
		}],
		['лавка альянса', 2, (st) => {
			if (!B.shopBuy || !st.honor || !st.honor.on) return -1;
			const R = rules().honor;
			// Mostly something the kingdom can buy; now and then anything, to see the refusal.
			const can = st.honor.shop.map((x, i) => [x[1], i]).filter((x) => x[0] === 0).map((x) => x[1]);
			const i = can.length && r() < 0.8 ? pick(can) : Math.floor(r() * R.shop.length);
			const d = R.shop[i];
			const want = st.honor.shop[i][1];
			const res = B.shopBuy(i);
			const st2 = B.state();
			if (res.err !== want) throw new Error('лавка: «' + d.name + '» отказ ' + res.err + ', а проверка ' + want);
			if (res.err !== 0) {
				if (st2.res.join() !== st.res.join() || st2.honor.pts !== st.honor.pts || st2.relics.length !== st.relics.length || st2.scrolls !== st.scrolls) throw new Error('отказ лавки ' + res.err + ' всё же что-то изменил');
				return res.err;
			}
			const p = d.pay || {};
			if (st2.honor.pts !== st.honor.pts - d.price || st2.honor.shop[i][0] !== st.honor.shop[i][0] + 1) throw new Error('лавка: «' + d.name + '» почёт ' + st.honor.pts + ' → ' + st2.honor.pts + ', куплено ' + st.honor.shop[i][0] + ' → ' + st2.honor.shop[i][0]);
			if ((p.stones || 0) !== st2.res[3] - st.res[3] || (p.shards || 0) !== st2.res[4] - st.res[4]) throw new Error('лавка: «' + d.name + '» дала камней ' + (st2.res[3] - st.res[3]) + ', осколков ' + (st2.res[4] - st.res[4]));
			if (p.scrolls && st2.scrolls !== Math.min(rules().campaigns.sweep.max, st.scrolls + p.scrolls)) throw new Error('лавка: свитков ' + st.scrolls + ' → ' + st2.scrolls);
			if (p.relic && st2.relics.length !== st.relics.length + 1) throw new Error('лавка: реликвий ' + st.relics.length + ' → ' + st2.relics.length);
			if (p.divine && st2.divine.g !== st.divine.g + p.divine) throw new Error('лавка: божественных ' + st.divine.g + ' → ' + st2.divine.g);
			return 0;
		}],
		['знамя', 2, (st) => {
			if (!B.bannerUp || !st.banners || !st.banners.on) return -1;
			const R = rules().banners;
			// Mostly a banner the kingdom can raise; now and then any, to see the refusal.
			const can = st.banners.list.map((x, i) => [x.ck.err, i]).filter((x) => x[0] === 0).map((x) => x[1]);
			const t = can.length && r() < 0.8 ? pick(can) : Math.floor(r() * st.banners.list.length);
			const x = st.banners.list[t];
			const code = B.bannerUp(t);
			const st2 = B.state();
			if (code !== x.ck.err) throw new Error('знамя ' + t + ': отказ ' + code + ', а проверка ' + x.ck.err);
			const y = st2.banners.list[t];
			if (code !== 0) {
				if (st2.res.join() !== st.res.join() || st2.banners.silk !== st.banners.silk || y.lv !== x.lv) throw new Error('отказ знамени ' + code + ' всё же что-то изменил');
				return code;
			}
			if (y.lv !== x.lv + 1 || st2.banners.silk !== st.banners.silk - x.silk || st2.res[0] !== st.res[0] - R.gold[x.lv]) throw new Error('знамя ' + t + ': ур. ' + x.lv + ' → ' + y.lv + ', шёлк ' + st.banners.silk + ' → ' + st2.banners.silk + ', золото ' + st.res[0] + ' → ' + st2.res[0]);
			if (st2.power.r !== st.power.r + (rules().power ? rules().power.research : 25)) throw new Error('знамя: мощь исследований ' + st.power.r + ' → ' + st2.power.r);
			return 0;
		}],
		['подарок за возвращение', 2, (st) => {
			if (!B.comebackClaim || !st.comeback) return -1;
			// A visit after a week or more away earns the gift (sometimes the week passes here); then mostly claim it.
			if (r() < 0.3) { t += Math.floor(86400 * (7 + r() * 3)); B.advance(t); }
			B.visit();
			const st1 = B.state();
			const R = rules().comeback;
			if (st.comeback.away === 0 && st1.comeback.away !== 0 && st1.castle < R.castle) throw new Error('подарок за возвращение до Замка ' + R.castle);
			if (r() < 0.2) return -1;
			const res = B.comebackClaim();
			const st2 = B.state();
			if (!st1.comeback.away) {
				if (res.err !== 13 || st2.res.join() !== st1.res.join() || st2.scrolls !== st1.scrolls) throw new Error('подарок без отсутствия: отказ ' + res.err + ', ресурсы ' + st1.res.join() + ' → ' + st2.res.join());
				return res.err;
			}
			if (res.err !== 0) throw new Error('подарок за возвращение не выдан: отказ ' + res.err);
			if (st2.comeback.away !== 0) throw new Error('подарок за возвращение остался после получения');
			if (st2.res[2] !== st1.res[2] + R.gems) throw new Error('подарок за возвращение: самоцветы ' + st1.res[2] + ' → ' + st2.res[2]);
			if (st2.relics.length + (res.relicsLost || 0) !== st1.relics.length + (R.relic ? 1 : 0)) throw new Error('подарок за возвращение: реликвий ' + st1.relics.length + ' → ' + st2.relics.length);
			return 0;
		}],
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
				const note = (l, txt) => {
					const kind = kindOf(txt);
					if (!seen.has(kind)) { seen.add(kind); fails.push([i, act[0], (l === 'warn' ? 'предупреждение: ' : '') + txt]); }
				};
				invariants(st2, note);
				landStores(st, st2, note, act[0] === 'сбор' || act[0] === 'сбор с земли');
				// The end of a Warpath pass pays what was reached and not claimed, on top of full vaults too.
				const wpPaid = !!(st.wp && st2.wp && st2.wp.paid.no !== st.wp.paid.no);
				// A season event beginning pays what the one before of its kind left unclaimed.
				const sevPaid = !!(st.sev && st2.sev && st2.sev.some((v, e) => st.sev[e] && v.no !== st.sev[e].no && st.sev[e].mine >= 0));
				resourceCaps(st, st2, note, REWARDS.has(act[0]) || wpPaid || sevPaid);
				if (st.wp && st2.wp && st2.wp.no === st.wp.no && (st2.wp.xp < st.wp.xp || st2.wp.got.replace(/0/g, '').length < st.wp.got.replace(/0/g, '').length)) {
					note('bad', 'Путь войны откатился: опыт ' + st.wp.xp + ' → ' + st2.wp.xp);
				}
				if (st2.wp && rules().warpath && st2.wp.day > rules().warpath.xp.battleCap) note('bad', 'Опыт Пути войны за бои за день ' + st2.wp.day + ' больше предела');
				// Only claiming the reward moves the main line on, one goal at a time.
				if (st.goal && st2.goal && st2.goal.i !== st.goal.i && (act[0] !== 'цель' || st2.goal.i !== st.goal.i + 1)) {
					note('bad', 'Цель сменилась с ' + (st.goal.i + 1) + ' на ' + (st2.goal.i + 1));
				}
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
/** Offers a file: through claude.ai's `downloads` capability inside the artifact viewer, else a plain browser download. */
async function download(name, t) {
	toast('Готовлю файл ' + name + '…', '');
	let dl = null;
	try { dl = window.claude && typeof window.claude.use === 'function' ? await window.claude.use('downloads') : null; } catch (e) { dl = null; }
	if (dl) {
		try {
			await dl.save({ filename: name, data: t });
			toast('Файл ' + name + ' сохранён', 'gold');
		} catch (e) {
			const code = e && e.code;
			if (code === 'declined') toast('Сохранение файла отменено', '');
			else if (code === 'rate_limited') toast('Окно сохранения уже открыто, попробуйте чуть позже', 'bad');
			else toast('Файл здесь не сохраняется (' + (code || e.message || e) + '): скопируйте текст', 'bad');
		}
		return;
	}
	try {
		const url = URL.createObjectURL(new Blob([t], { type: name.endsWith('.json') ? 'application/json' : 'text/plain' }));
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
const stamp = () => new Date().toISOString().slice(0, 16).replace(/[:T]/g, '-');
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
		if (st.divine) {
			h += row('Божественные самоцветы <b>' + fmt(st.divine.g) + '</b>', [100, 1000, 10000].map((n) => b('div-add', '+' + short(n), { n })).join('') + b('div-add', '0', { n: -1e15 }) +
				'<input type="number" inputmode="numeric" min="0" placeholder="число" data-in="divine"' + typedAttr('divine') + '>' + b('div-set', '='), 'в святилище ' + st.divine.st);
		}
		if (st.spire) {
			// Floors won: below the weekly ones they stay, the weekly ones count for this week.
			h += row('Башня титанов: взято <b>' + st.spire.top + '</b>', [0, 9, 49, st.spire.fixed - 1, st.spire.fixed + 5].map((n) => b('spire-set', String(n), { n })).join(''),
				'следующий этаж ' + (st.spire.next || '—'));
		}
		if (st.codex && st.codex.on) {
			// The Codex: every entry open (nothing paid), or back to the titans in the Hall.
			const open = st.codex.e.filter((x) => x > 0).length;
			h += row('Кодекс титанов <b>' + open + ' из ' + st.codex.e.length + '</b>', b('codex-set', 'Открыть все', { n: 1 }) + b('codex-set', 'Сбросить', { n: 0 }),
				'наград ждёт ' + (st.codex.e.filter((x) => x === 1).length + st.codex.p.filter((x) => x[0] === 1).length));
		}
		if (st.honor && st.honor.on) {
			// Honor: a thousand to spend in the Alliance Shop, or none (today's donations and the week's buys start over).
			h += row('Почёт <b>' + fmt(st.honor.pts) + '</b>', b('honor-set', '+1000', { n: 1 }) + b('honor-set', 'Обнулить', { n: 0 }),
				'пожертвований сегодня ' + st.honor.don + (st.ally.my < 0 ? ', вне альянса' : ''));
		}
		if (st.banners && st.banners.on) {
			// War Banners: a thousand silk to raise them, or none (and every banner back to 0).
			h += row('Знамённый шёлк <b>' + fmt(st.banners.silk) + '</b>', b('silk-set', '+1000', { n: 1 }) + b('silk-set', 'Обнулить', { n: 0 }),
				'знамён поднято ' + st.banners.list.reduce((a, x) => a + x.lv, 0) + (st.castle < rules().banners.castle ? ', откроются на Замке ' + rules().banners.castle : ''));
		}
		if (st.tech && st.tech.on && rules().allianceTech) {
			// Alliance Tech: the player's points for every tech of the current alliance to the top, or none anywhere.
			h += row('Технологии альянса <b>' + st.tech.list.reduce((a, x) => a + x.lv, 0) + ' из ' + rules().allianceTech.costs.length * st.tech.list.length + '</b>',
				b('tech-set', 'Изучить все', { n: 1 }) + b('tech-set', 'Сбросить', { n: 0 }), st.ally.my < 0 ? 'вне альянса: только сброс' : 'ваших очков ' + fmt(st.tech.list.reduce((a, x) => a + x.mine, 0)));
		}
		if (st.conq && rules().conquest) {
			// Conquest: March Tokens for this Conquest, all of them or none (the clock starts now).
			const cr = rules().conquest;
			h += row('Жетоны похода <b>' + st.conq.tok + ' из ' + cr.tokens + '</b>', b('conq-tok', 'Полный запас', { n: cr.tokens }) + b('conq-tok', 'Ни одного', { n: 0 }),
				st.conq.ph === 1 ? 'Завоевание идёт, конец через ' + fmtDur(st.conq.end - st.now) : 'следующее через ' + fmtDur((st.conq.ph === 0 ? st.conq.start : st.conq.next) - st.now));
		}
		if (st.sev && st.sev.length && rules().seasonEvents) {
			// Season events: points for the event that runs (or ran last), a par more or none.
			const svr = rules().seasonEvents;
			st.sev.forEach((v, e) => {
				const d = svr.list[e];
				const mine = v.mine === v.no ? v.pts : 0;
				h += row('«' + d.name + '» <b>' + fmt(mine) + ' очков</b>', b('sev-pts', '+ норма', { n: e * 10 + 1 }) + b('sev-pts', 'Обнулить', { n: e * 10 }),
					(v.on ? 'идёт, конец через ' + fmtDur(v.end - st.now) : 'следующее через ' + fmtDur(v.next - st.now)) + ', норма ' + fmt(v.par) + ', место ' + v.place +
					(st.castle < svr.castle ? ', очки с Замка ' + svr.castle : ''));
			});
		}
		if (st.comeback && rules().comeback && rules().comeback.days) {
			// The comeback gift: give one as after a week away (plus a day), or take back the one waiting.
			const cbd = rules().comeback.days;
			h += row('Подарок за возвращение <b>' + (st.comeback.away ? 'ждёт' : 'нет') + '</b>', b('cb-set', 'Выдать', { n: 1 }) + b('cb-set', 'Убрать', { n: 0 }),
				st.comeback.away ? 'за ' + Math.floor(st.comeback.away / 86400) + ' дн. отсутствия' : 'даётся после ' + cbd + ' дн. отсутствия, с Замка ' + rules().comeback.castle);
		}
		h += '</div><div class="actions">' + b('res-fill', 'Заполнить хранилища и камни') + b('res-rich', 'Богатое королевство') + '</div>';
		h += '<p class="fine">«Богатое королевство»: полные хранилища и камни, +100 000 самоцветов, +5 000 осколков. Первая правка сохраняет копию королевства (вкладка «Сохранение»).</p>';
		h += '<h3>Город</h3><div class="admList">' +
			row('Расширения города <b>' + st.expansions + ' из ' + rules().expansions.length + '</b>', b('exp', '−', { n: -1 }) + b('exp', '+', { n: 1 })) +
			row('Купленные места для земель <b>' + st.extraSlots + '</b>', b('slots', '−', { n: -1 }) + b('slots', '+', { n: 1 }), 'лимит земель ' + st.landCap) +
			row('Строители <b>' + st.busy + ' заняты из ' + st.builders + '</b>', b('finish', 'Достроить всё', { w: 'build' }), 'число строителей задаёт Двор строителей') + '</div>';
		const goals = rules().goals;
		if (st.goal && goals && goals.length) {
			const g = goals[st.goal.i];
			const pick = clamp(admin.goal === null ? st.goal.i : admin.goal, 0, goals.length);
			h += '<h3>Цели</h3><div class="admList">' +
				row(g ? 'Цель <b>' + (st.goal.i + 1) + ' из ' + goals.length + '</b> ' + esc(goalText(g)) : 'Линия целей пройдена <b>' + goals.length + ' из ' + goals.length + '</b>',
					b('goal-to', '◀', { i: st.goal.i - 1 }) + b('goal-to', '▶', { i: st.goal.i + 1 }) + (st.goal.done ? b('goal-claim', 'Забрать награду') : ''),
					g ? 'счёт ' + fmt(st.goal.v) + ' из ' + fmt(g.n || 1) + (st.goal.done ? ' · выполнена' : '') : '') +
				row('Перейти к цели', select('goal-pick', goals.map((x, k) => [k, k + 1 + '. ' + goalText(x)]).concat([[goals.length, 'Вся линия пройдена']]), pick) +
					b('goal-to', 'Перейти', { i: 'pick' })) + '</div>';
		}
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
		let h = '<div class="actions">' + b('copy-report', 'Скопировать отчёт для разработчика', { s: 1 }, 'primary') + b('copy-report', 'Отчёт без сохранения', { s: 0 }) + b('dl-report', 'Скачать отчёт .txt') +
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
	case 'div-add': edit((s) => { s.divine = s.divine || { gems: 0, from: 0 }; s.divine.gems = Math.max(0, (s.divine.gems || 0) + n); }, n < 0 ? 'Обнулено' : 'Добавлено'); break;
	case 'spire-set': edit((s, st) => {
		const fixed = st.spire.fixed;
		const week = Math.floor((Math.floor(Math.max(0, st.now) / 86400) + 3) / 7);
		s.spire = { top: Math.min(n, fixed), week: n > fixed ? week : -1, weekTop: n > fixed ? n : 0, at: 0 };
	}, 'Башня: взято ' + n); break;
	case 'codex-set': edit((s) => { s.codex = { seen: n ? army().titans.map((t) => t.id) : [], paid: [], pages: [] }; }, n ? 'Кодекс: все записи открыты' : 'Кодекс сброшен'); break;
	case 'honor-set': edit((s) => {
		if (n) s.honor = Object.assign({ day: 0, don: 0, week: 0, bought: {} }, s.honor || {}, { pts: ((s.honor && s.honor.pts) || 0) + 1000 });
		else delete s.honor;
	}, n ? 'Почёт +1000' : 'Почёт обнулён'); break;
	case 'silk-set': edit((s) => {
		if (n) s.banners = Object.assign({ lv: {} }, s.banners || {}, { silk: ((s.banners && s.banners.silk) || 0) + 1000 });
		else delete s.banners;
	}, n ? 'Шёлк +1000' : 'Шёлк и знамёна обнулены'); break;
	case 'tech-set': {
		edit((s, st) => {
			const my = st.ally.my;
			if (n && my < 0) { toast('Сначала вступите в альянс', 'bad'); return false; }
			const tr = rules().allianceTech;
			const all = tr.costs.reduce((a, c) => a + c, 0);
			const rest = (s.allianceTech || []).filter((e) => n && e.a !== my);
			if (n) rest.push({ a: my, pts: Object.fromEntries(tr.list.map((x) => [x.id, all])) });
			if (rest.length) s.allianceTech = rest;
			else delete s.allianceTech;
		}, n ? 'Технологии альянса изучены' : 'Технологии альянса сброшены');
		break;
	}
	case 'conq-tok': edit((s, st) => {
		const c = s.conquest || { no: -1, key: -1, league: 0, ids: [], mem: [], hits: [], chest: false, at: -1 };
		c.tokNo = st.conq.no;
		c.tok = n;
		c.tokAt = st.now;
		s.conquest = c;
	}, n ? 'Жетонов похода: ' + n : 'Жетоны похода кончились'); break;
	case 'sev-pts': {
		const e = Math.floor(n / 10);
		const d = rules().seasonEvents.list[e];
		const par = kd() ? kd().K.bridge.state().sev[e].par : 0;
		edit((s, st) => {
			const v = st.sev[e];
			const list = (s.seasonEvents || []).filter((x) => x.id !== d.id);
			const old = (s.seasonEvents || []).find((x) => x.id === d.id && x.no === v.no);
			const x = old || { id: d.id, no: v.no, castle: st.castle, points: 0, ms: 0, rank: false };
			x.points = n % 10 ? x.points + v.par : 0;
			if (!(n % 10)) { x.ms = 0; x.rank = false; }
			list.push(x);
			s.seasonEvents = list;
		}, n % 10 ? '«' + d.name + '»: +' + fmt(par) + ' очков' : '«' + d.name + '»: очки обнулены');
		break;
	}
	case 'cb-set': edit((s, st) => {
		if (n) s.comeback = { away: (rules().comeback.days + 1) * 86400, at: st.now };
		else delete s.comeback;
	}, n ? 'Подарок за возвращение ждёт' : 'Подарок убран'); break;
	case 'div-set': {
		const v = Math.floor(+inputVal('divine'));
		if (!Number.isFinite(v) || v < 0) { toast('Введите число от 0', 'bad'); break; }
		edit((s) => { s.divine = s.divine || { gems: 0, from: 0 }; s.divine.gems = v; }, 'Задано: ' + fmt(v));
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
	case 'goal-to': {
		const goals = rules().goals || [];
		const at = clamp(el.dataset.i === 'pick' ? admin.goal === null ? d.K.st.goal.i : admin.goal : i, 0, goals.length);
		admin.goal = null;
		edit((s) => { s.goal = { at, id: at < goals.length ? goals[at].id : '', events: 0 }; }, at < goals.length ? 'Цель ' + (at + 1) + ': ' + goalText(goals[at]) : 'Линия целей пройдена');
		break;
	}
	case 'goal-claim': {
		const r = d.K.bridge.goalClaim();
		if (r.err) toast('Награда не выдана, код ' + r.err, 'bad');
		applied(r.err ? '' : 'Награда за цель получена');
		break;
	}
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
	case 'save-dl': download('dawn-of-titans-kingdom-' + stamp() + '.json', d.K.bridge.save()); break;
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
	case 'dl-report': download('dawn-of-titans-report-' + stamp() + '.txt', report(true)); break;
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
		else if (c === 'goal-pick') admin.goal = +v;
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
