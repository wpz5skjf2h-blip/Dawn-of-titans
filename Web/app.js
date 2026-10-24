// Web/app.js - browser demo of the Dawn of Titans M0 battle.
// The battle itself runs in the deterministic C++ simulation (Source/DoTSim) compiled to
// WebAssembly; this file only renders it (Three.js), reads touch/mouse/keyboard input and
// sends the same commands the Unreal client sends.
(() => {
'use strict';

const THREE = window.THREE;
// Treat hex colors as sRGB and light in linear space, so the field does not look washed out.
THREE.ColorManagement.legacyMode = false;
const TPS = 30;
const DT = 1 / TPS;
const CMD = { Move: 0, Path: 1, Attack: 2, Charge: 3, Halt: 4, Retreat: 5, SetFormation: 6, SetStance: 7, SetAuto: 8, CastAbility: 9, CastSpell: 10,
	ClashInput: 11, DeployMove: 12, AutoDeploy: 13, StartBattle: 14, Surrender: 15 };
const EV = { SoldierDied: 0, TitanAttack: 1, TitanSmash: 2, TitanStomp: 3, TitanGrab: 4, TitanThrowImpact: 5, TitanFootstep: 6, TitanChargeStart: 7,
	TitanStaggered: 8, TitanDied: 9, AbilityCast: 10, AbilityImpact: 11, SpellCast: 12, SpellImpact: 13, ClashStarted: 14, ClashExchange: 15,
	ClashEnded: 16, SquadRouted: 17, SquadRallied: 18, SquadLeftField: 19, SquadDestroyed: 20, ChargeImpact: 21, StarEarned: 22, BattleStarted: 23, BattleEnded: 24, DeadRaised: 25,
	FirebombImpact: 26, ChainThrown: 27, TitanBound: 28 };
const PH = { Deploy: 0, Countdown: 1, Battle: 2, Finished: 3 };
const SS = { Idle: 0, Moving: 1, Fighting: 2, Airborne: 3, Down: 4, Dead: 5, Left: 6 };
const TS = { Idle: 0, Moving: 1, Attacking: 2, Stomping: 3, Grabbing: 4, Casting: 5, Leaping: 6, Clashing: 7, Staggered: 8, Dead: 9 };
const FT = { Hill: 0, Forest: 1, Shallow: 2, Deep: 3, Rock: 4, Road: 5, Ruins: 6 };
const CF = { Forest: 1, Ford: 2, Deep: 4, Road: 8, Ruins: 16, Rock: 32 };
const SHAPE = { Self: 0, Circle: 1, Cone: 2, Line: 3 };
const DELIV = { Instant: 0, Projectile: 1, Leap: 2, Zone: 3, Delayed: 4 };
const ORDER = { None: 0, Move: 1, Attack: 2, Retreat: 3, Withdraw: 4, Skirmish: 5 };
const ROUTING = 3;
const TITAN_BASE = 1000;
const SOLDIER_SCALE = 1.25;

const RU = {
	troop: { 'troop.militia': 'Ополчение', 'troop.swordsmen': 'Мечники', 'troop.spearmen': 'Копейщики', 'troop.archers': 'Лучники',
		'troop.outriders': 'Всадники', 'troop.knights': 'Рыцари', 'troop.skeleton': 'Скелеты', 'troop.grenadiers': 'Гренадеры',
		'troop.chainbreakers': 'Цепеломы', 'troop.priests': 'Боевые жрецы', 'troop.pikemen': 'Пикинёры', 'troop.goliaths': 'Голиафы', 'troop.panthers': 'Пантеры' },
	troopShort: { 'troop.militia': 'Ополч.', 'troop.swordsmen': 'Мечники', 'troop.spearmen': 'Копья', 'troop.archers': 'Лучники',
		'troop.outriders': 'Конница', 'troop.knights': 'Рыцари', 'troop.skeleton': 'Скелеты', 'troop.grenadiers': 'Гренад.',
		'troop.chainbreakers': 'Цепи', 'troop.priests': 'Жрецы', 'troop.pikemen': 'Пики', 'troop.goliaths': 'Голиафы', 'troop.panthers': 'Пантеры' },
	troopOf: { 'troop.militia': 'ополчение', 'troop.swordsmen': 'мечники', 'troop.spearmen': 'копейщики', 'troop.archers': 'лучники',
		'troop.outriders': 'всадники', 'troop.knights': 'рыцари', 'troop.skeleton': 'скелеты', 'troop.grenadiers': 'гренадеры',
		'troop.chainbreakers': 'цепеломы', 'troop.priests': 'жрецы', 'troop.pikemen': 'пикинёры', 'troop.goliaths': 'голиафы', 'troop.panthers': 'пантеры' },
	titan: { 'titan.thor': 'Тор', 'titan.talos': 'Талос', 'titan.minotaur': 'Минотавр', 'titan.cyclops': 'Циклоп', 'titan.perun': 'Перун',
		'titan.atlas': 'Атлант', 'titan.ymir': 'Имир', 'titan.surtr': 'Сурт', 'titan.hecate': 'Геката', 'titan.artemis': 'Артемида' },
	abil: { 'ability.talos.molten_ichor': 'Ихор', 'ability.talos.bronze_bulwark': 'Оплот', 'ability.thor.hammer_throw': 'Молот',
		'ability.thor.thunderclap': 'Гром', 'spell.meteor_strike': 'Метеор', 'spell.war_drums': 'Барабаны', 'spell.healing_spring': 'Источник',
		'spell.fireball': 'Огн. шар', 'spell.shield': 'Щит', 'spell.freeze': 'Заморозка', 'spell.lightning': 'Молния', 'spell.rage': 'Ярость',
		'spell.terror': 'Ужас', 'spell.acid_cloud': 'Кислота', 'spell.raise_dead': 'Мертвецы',
		'ability.minotaur.labyrinth_rush': 'Рывок', 'ability.minotaur.blood_frenzy': 'Бешенство',
		'ability.cyclops.forge_gaze': 'Взор', 'ability.cyclops.anvil_quake': 'Наковальня',
		'ability.perun.oak_splitter': 'Перуница', 'ability.perun.storm_herald': 'Гроза',
		'ability.atlas.falling_sky': 'Небосвод', 'ability.atlas.weight_of_heavens': 'Тяжесть',
		'ability.ymir.frost_breath': 'Стужа', 'ability.ymir.glacier_hide': 'Ледник',
		'ability.surtr.muspell_blade': 'Клинок', 'ability.surtr.world_fire': 'Пожар',
		'ability.hecate.crossroads_hex': 'Порча', 'ability.hecate.underworld_torches': 'Факелы',
		'ability.artemis.silver_volley': 'Залп', 'ability.artemis.moonshot': 'Луна' },
	abilFull: { 'spell.meteor_strike': 'Метеор', 'spell.war_drums': 'Боевые барабаны', 'spell.healing_spring': 'Целебный источник',
		'spell.fireball': 'Огненный шар', 'spell.shield': 'Щит', 'spell.freeze': 'Заморозка', 'spell.lightning': 'Молния', 'spell.rage': 'Ярость',
		'spell.terror': 'Ужас', 'spell.acid_cloud': 'Кислотное облако',
		'ability.talos.molten_ichor': 'Расплавленный ихор', 'ability.talos.bronze_bulwark': 'Бронзовый оплот',
		'ability.thor.hammer_throw': 'Бросок молота', 'ability.thor.thunderclap': 'Раскат грома',
		'spell.raise_dead': 'Поднять мёртвых',
		'ability.minotaur.labyrinth_rush': 'Рывок по лабиринту', 'ability.minotaur.blood_frenzy': 'Кровавое бешенство',
		'ability.cyclops.forge_gaze': 'Взор горна', 'ability.cyclops.anvil_quake': 'Удар о наковальню',
		'ability.perun.oak_splitter': 'Раскол дуба', 'ability.perun.storm_herald': 'Вестник грозы',
		'ability.atlas.falling_sky': 'Падающий небосвод', 'ability.atlas.weight_of_heavens': 'Тяжесть небес',
		'ability.ymir.frost_breath': 'Ледяное дыхание', 'ability.ymir.glacier_hide': 'Шкура ледника',
		'ability.surtr.muspell_blade': 'Клинок Муспелля', 'ability.surtr.world_fire': 'Мировой пожар',
		'ability.hecate.crossroads_hex': 'Порча перекрёстка', 'ability.hecate.underworld_torches': 'Факелы преисподней',
		'ability.artemis.silver_volley': 'Серебряный залп', 'ability.artemis.moonshot': 'Лунная стрела' },
	ai: { recruit: 'Новобранец', veteran: 'Ветеран', warlord: 'Полководец', nightmare: 'Кошмар' },
	formation: ['Линия', 'Колонна', 'Клин', 'Каре', 'Рассыпной'],
	stance: ['Натиск', 'Стоять', 'Застрельщики'],
	grade: { 3: 'идеально', 2: 'хорошо', 1: 'промах' },
	star: ['Разбита половина врага', 'Вражеский титан повержен', 'Полная победа'],
};
const SCENARIOS = [
	{ path: 'scenarios/m0_sandbox.json', name: 'Битва у брода', note: 'Тор и шесть отрядов против Талоса и его войска' },
	{ path: 'scenarios/m0_titan_duel.json', name: 'Дуэль титанов', note: 'Сразу в бой: тренировка схватки титанов' },
	{ path: 'scenarios/m0_cavalry_test.json', name: 'Копья против конницы', note: 'Удержите строй копейщиков против всадников' },
	{ path: 'scenarios/m1_raise_dead.json', name: 'Факелы Гекаты', note: 'Геката и «Поднять мёртвых» против огненного войска Сурта' },
	{ path: 'scenarios/m1_chains_and_fire.json', name: 'Цепи и огонь', note: 'Цепеломы, гренадеры и боевые жрецы против Атланта' },
];
const TITANS = ['titan.thor', 'titan.talos', 'titan.perun', 'titan.minotaur', 'titan.cyclops', 'titan.atlas', 'titan.ymir', 'titan.surtr', 'titan.hecate', 'titan.artemis'];
const AIS = ['recruit', 'veteran', 'warlord', 'nightmare'];
const QUALITY = [['auto', 'Авто'], ['high', 'Высокое'], ['low', 'Экономное']];
const SIDE_COLOR = [new THREE.Color(0x3d7bf2), new THREE.Color(0xd9442f)];
const SIDE_CSS = ['#4f8cff', '#e5533d'];

const $ = (id) => document.getElementById(id);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const lerp = (a, b, t) => a + (b - a) * t;
const smooth = (t) => { t = clamp(t, 0, 1); return t * t * (3 - 2 * t); };
const rad = THREE.MathUtils.degToRad;
const isTouch = window.matchMedia('(pointer: coarse)').matches;
const now = () => performance.now() / 1000;

const store = {
	get(k, d) { try { const v = localStorage.getItem('dot.' + k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
	set(k, v) { try { localStorage.setItem('dot.' + k, JSON.stringify(v)); } catch (e) { /* storage unavailable */ } },
};
const settings = {
	scenario: clamp(store.get('scenario', 0) | 0, 0, SCENARIOS.length - 1),
	ai: AIS.includes(store.get('ai', 'veteran')) ? store.get('ai', 'veteran') : 'veteran',
	quality: store.get('quality', 'auto'),
	titan: typeof store.get('titan', '') === 'string' ? store.get('titan', '') : '',
	sound: store.get('sound', true) !== false,
	follow: false,
};

// ---------------------------------------------------------------------------------------------
// Simulation bridge (Web/dot_web.cpp)
// ---------------------------------------------------------------------------------------------
class SimBridge {
	constructor(M, mode) { this.M = M; this.mode = mode; }
	str(p) { return this.M.UTF8ToString(p); }
	addFile(path, text) { this.M.ccall('dot_add_file', null, ['string', 'string'], [path, text]); }
	loadContent() { return this.M._dot_load_content() === 1; }
	error() { return this.str(this.M._dot_error()); }
	start(path, ai, playerAi) { return this.M.ccall('dot_start', 'number', ['string', 'string', 'number', 'string'], [path, ai, -1, playerAi]) === 1; }
	replay() { return this.M._dot_replay() === 1; }
	step() { return this.M._dot_step(); }
	cmd(type, unit, x, z, target, param) { this.M._dot_cmd(type, unit, x || 0, z || 0, target || 0, param || 0); }
	path(unit, x, z, target, pts) {
		const n = pts.length;
		const ptr = this.M._malloc(n * 16 + 16);
		const f = new Float64Array(n * 2);
		pts.forEach((p, i) => { f[i * 2] = p.x; f[i * 2 + 1] = p.z; });
		this.M.HEAPF64.set(f, ptr >> 3);
		this.M._dot_cmd_path(unit, x, z, target, ptr, n);
		this.M._free(ptr);
	}
	buffer(fnData, fnCount, stride, out) {
		const ptr = fnData() >> 2;
		const n = fnCount() * stride;
		if (!out || out.length !== n) out = new Float32Array(n);
		out.set(this.M.HEAPF32.subarray(ptr, ptr + n));
		return out;
	}
	soldiers(out) { return this.buffer(this.M._dot_soldiers, this.M._dot_soldier_count, 8, out); }
	titans(out) { return this.buffer(this.M._dot_titans, this.M._dot_titan_count, 4, out); }
	height(x, z) { return this.M._dot_height(x, z); }
	pick(x, z, enemy, radius) { return this.M._dot_pick(x, z, enemy ? 1 : 0, radius); }
	staticInfo() { return JSON.parse(this.str(this.M._dot_static())); }
	frame() { return JSON.parse(this.str(this.M._dot_frame())); }
	verify() { return JSON.parse(this.str(this.M._dot_verify())); }
}

function loadScript(src) {
	return new Promise((resolve, reject) => {
		const s = document.createElement('script');
		s.src = src;
		s.onload = resolve;
		s.onerror = () => reject(new Error('Не удалось загрузить ' + src));
		document.head.appendChild(s);
	});
}

function withTimeout(promise, ms) {
	return Promise.race([promise, new Promise((_, reject) => setTimeout(() => reject(new Error('timeout')), ms))]);
}

async function loadSimulation() {
	try {
		await loadScript('dotsim.js');
		const M = await withTimeout(window.createDotSim(), 15000);
		return new SimBridge(M, 'WebAssembly');
	} catch (e) {
		console.warn('WebAssembly build unavailable, falling back to the JavaScript build:', e);
	}
	$('loadMsg').textContent = 'WebAssembly недоступен, запускаю JS-версию симуляции…';
	window.createDotSim = undefined;
	await loadScript('dotsim-js.js');
	const M = await withTimeout(window.createDotSim(), 30000);
	return new SimBridge(M, 'JavaScript');
}

// ---------------------------------------------------------------------------------------------
// Renderer, scene, camera
// ---------------------------------------------------------------------------------------------
const glCanvas = $('gl');
const ovCanvas = $('ov');
const ov = ovCanvas.getContext('2d');
const renderer = new THREE.WebGLRenderer({ canvas: glCanvas, antialias: !isTouch || window.devicePixelRatio < 2, powerPreference: 'high-performance' });
renderer.outputEncoding = THREE.sRGBEncoding;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
const scene = new THREE.Scene();
const SKY = new THREE.Color(0xa9bfcf);
scene.background = SKY;
scene.fog = new THREE.Fog(SKY, 190, 560);
const camera = new THREE.PerspectiveCamera(48, 1, 1, 1500);
const hemi = new THREE.HemisphereLight(0xdfe9f2, 0x5b4a2e, 0.62);
scene.add(hemi);
const sun = new THREE.DirectionalLight(0xfff1d6, 1.45);
sun.position.set(-90, 160, 60);
sun.shadow.mapSize.set(2048, 2048);
const sc = sun.shadow.camera;
sc.left = -150; sc.right = 150; sc.top = 150; sc.bottom = -150; sc.near = 10; sc.far = 450;
sun.shadow.bias = -0.0008;
scene.add(sun);
scene.add(sun.target);

let viewW = 1;
let viewH = 1;
let ovScale = 1;

function qualityLevel() {
	return settings.quality === 'auto' ? (isTouch ? 'low' : 'high') : settings.quality;
}

function applyQuality() {
	const q = qualityLevel();
	const dpr = window.devicePixelRatio || 1;
	renderer.setPixelRatio(Math.min(dpr, q === 'high' ? 2 : 1.35));
	const shadows = q === 'high';
	if (renderer.shadowMap.enabled !== shadows) {
		renderer.shadowMap.enabled = shadows;
		sun.castShadow = shadows;
		scene.traverse((o) => { if (o.material) { (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => { m.needsUpdate = true; }); } });
	}
	resize();
}

function resize() {
	viewW = Math.max(1, window.innerWidth);
	viewH = Math.max(1, window.innerHeight);
	renderer.setSize(viewW, viewH, false);
	ovScale = Math.min(window.devicePixelRatio || 1, 2);
	ovCanvas.width = Math.round(viewW * ovScale);
	ovCanvas.height = Math.round(viewH * ovScale);
	camera.aspect = viewW / viewH;
	camera.fov = camera.aspect < 1 ? 66 : 48;
	camera.updateProjectionMatrix();
}
window.addEventListener('resize', resize);

// Camera rig: perspective, pitch eases 42..60 degrees with zoom, pan with inertia, shake.
const cam = { x: 120, z: -60, dist: 110, minD: 28, maxD: 185, vx: 0, vz: 0, shake: 0, shakeT: 0, follow: null, mapW: 240, mapD: 160 };

function clampCam() {
	cam.x = clamp(cam.x, 0, cam.mapW);
	cam.z = clamp(cam.z, -cam.mapD, 0);
	cam.dist = clamp(cam.dist, cam.minD, cam.maxD);
}

function placeCamera(dt) {
	if (cam.follow) {
		cam.x = lerp(cam.x, cam.follow.x, Math.min(1, dt * 3));
		cam.z = lerp(cam.z, cam.follow.z, Math.min(1, dt * 3));
	}
	if (!(input.press && input.press.mode === 'pan')) {
		cam.x += cam.vx * dt;
		cam.z += cam.vz * dt;
	}
	const decay = Math.exp(-dt * 5);
	cam.vx *= decay;
	cam.vz *= decay;
	clampCam();
	const t = (cam.dist - cam.minD) / (cam.maxD - cam.minD);
	const pitch = rad(42 + 18 * t);
	const ground = game.sim ? game.sim.height(cam.x, -cam.z) : 0;
	cam.shakeT += dt * 40;
	cam.shake *= Math.exp(-dt * 6);
	const sx = Math.sin(cam.shakeT * 1.7) * cam.shake;
	const sy = Math.cos(cam.shakeT * 2.3) * cam.shake * 0.6;
	camera.position.set(cam.x + sx, ground + Math.sin(pitch) * cam.dist + sy, cam.z + Math.cos(pitch) * cam.dist);
	camera.lookAt(cam.x, ground, cam.z);
	sun.position.set(cam.x - 90, 160, cam.z + 60);
	sun.target.position.set(cam.x, 0, cam.z);
}

function shakeAt(x, z, amount) {
	const d = Math.hypot(x - cam.x, -z - cam.z);
	const k = clamp(1 - d / 90, 0, 1) * (60 / Math.max(cam.dist, 40));
	cam.shake = Math.min(2.4, cam.shake + amount * k);
}

const raycaster = new THREE.Raycaster();
const ndc = new THREE.Vector2();
function screenRay(px, py) {
	ndc.set((px / viewW) * 2 - 1, -(py / viewH) * 2 + 1);
	raycaster.setFromCamera(ndc, camera);
	return raycaster.ray;
}
/** Ground point (sim coordinates) under a screen point. */
function groundAt(px, py) {
	const ray = screenRay(px, py);
	if (view.terrain) {
		const hit = raycaster.intersectObject(view.terrain, false)[0];
		if (hit) return { x: hit.point.x, z: -hit.point.z, y: hit.point.y };
	}
	return planeAt(px, py, 0, ray);
}
function planeAt(px, py, h, ray) {
	ray = ray || screenRay(px, py);
	if (Math.abs(ray.direction.y) < 1e-4) return null;
	const t = (h - ray.origin.y) / ray.direction.y;
	if (t <= 0) return null;
	return { x: ray.origin.x + ray.direction.x * t, z: -(ray.origin.z + ray.direction.z * t), y: h };
}
const projV = new THREE.Vector3();
/** Screen position (CSS px) of a sim point, or null when behind the camera. */
function toScreen(x, y, z) {
	projV.set(x, y, -z).project(camera);
	if (projV.z > 1) return null;
	return { x: (projV.x * 0.5 + 0.5) * viewW, y: (-projV.y * 0.5 + 0.5) * viewH };
}

// ---------------------------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------------------------
const GEO = {
	box: new THREE.BoxGeometry(1, 1, 1),
	sphere: new THREE.SphereGeometry(0.5, 16, 12),
	cyl: new THREE.CylinderGeometry(0.5, 0.5, 1, 12),
	cone: new THREE.ConeGeometry(0.5, 1, 12),
	ring: new THREE.RingGeometry(0.9, 1, 48).rotateX(-Math.PI / 2),
	disc: new THREE.CircleGeometry(1, 40).rotateX(-Math.PI / 2),
};

function part(geo, color, x, y, z, sx, sy, sz, rx, ry, rz) {
	const m = new THREE.Matrix4().compose(new THREE.Vector3(x, y, z), new THREE.Quaternion().setFromEuler(new THREE.Euler(rx || 0, ry || 0, rz || 0)),
		new THREE.Vector3(sx, sy, sz));
	return { geo, color: new THREE.Color(color), m };
}

/** Merges parts into one non-indexed geometry with baked vertex colors. */
function mergeParts(parts) {
	let total = 0;
	const items = parts.map((p) => {
		const g = (p.geo.index ? p.geo.toNonIndexed() : p.geo.clone());
		g.applyMatrix4(p.m);
		total += g.attributes.position.count;
		return { g, c: p.color };
	});
	const pos = new Float32Array(total * 3);
	const nor = new Float32Array(total * 3);
	const col = new Float32Array(total * 3);
	let o = 0;
	for (const { g, c } of items) {
		const n = g.attributes.position.count;
		pos.set(g.attributes.position.array, o * 3);
		nor.set(g.attributes.normal.array, o * 3);
		for (let i = 0; i < n; i++) { col[(o + i) * 3] = c.r; col[(o + i) * 3 + 1] = c.g; col[(o + i) * 3 + 2] = c.b; }
		o += n;
		g.dispose();
	}
	const out = new THREE.BufferGeometry();
	out.setAttribute('position', new THREE.BufferAttribute(pos, 3));
	out.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
	out.setAttribute('color', new THREE.BufferAttribute(col, 3));
	out.computeBoundingSphere();
	return out;
}

const C_SKIN = 0xd9a77e;
const C_STEEL = 0x9aa3ad;
const C_WOOD = 0x6b4a2b;
const C_HORSE = 0x6e4a2f;
const C_HORSE_GREY = 0xb9b3a8;
const C_LEATHER = 0x5a3d24;
const C_BONE = 0xe6dcc2;
const C_RUST = 0x8a5a3a;
const C_IRON = 0x4a4f57;
const C_ROBE = 0xeee6d2;
const C_GOLD = 0xd9b45a;
const C_FUR = 0xd6cfc0;
const C_ICE = 0x9fdcff;
const C_JADE = 0x2f9a72;
const C_GIANT = 0xb98a68;
const C_PANTHER = 0x1f1d24;
const C_VENOM = 0x9cff6a;
const lowCyl = new THREE.CylinderGeometry(0.5, 0.5, 1, 8);
const lowSph = new THREE.SphereGeometry(0.5, 8, 6);
const lowCone = new THREE.ConeGeometry(0.5, 1, 8);

/** Placeholder soldier models: "body" takes the team color, "gear" keeps natural colors. Forward is -Z. */
function troopModel(id) {
	const W = 0xffffff;
	const G = 0xcfcfcf;
	const body = [];
	const gear = [];
	const infantryHead = (helmet) => {
		gear.push(part(lowSph, C_SKIN, 0, 1.45, 0, 0.36, 0.38, 0.36));
		if (helmet) gear.push(part(lowSph, C_STEEL, 0, 1.53, 0, 0.4, 0.28, 0.4));
	};
	if (id === 'troop.swordsmen') {
		body.push(part(GEO.box, W, 0, 0.95, 0, 0.62, 0.7, 0.42));
		body.push(part(GEO.box, G, 0, 0.35, 0, 0.5, 0.7, 0.34));
		infantryHead(true);
		gear.push(part(GEO.box, C_WOOD, -0.38, 0.95, -0.12, 0.08, 0.9, 0.62));
		gear.push(part(GEO.box, C_STEEL, 0.4, 0.95, -0.35, 0.06, 0.06, 0.9, rad(-30), 0, 0));
	} else if (id === 'troop.spearmen') {
		body.push(part(lowCyl, W, 0, 0.62, 0, 0.56, 1.24, 0.56));
		infantryHead(true);
		gear.push(part(GEO.box, C_WOOD, 0.34, 1.55, -0.2, 0.06, 3.3, 0.06, rad(-12), 0, 0));
		gear.push(part(lowCone, C_STEEL, 0.34, 3.2, -0.55, 0.12, 0.35, 0.12, rad(-12), 0, 0));
		gear.push(part(lowCyl, C_WOOD, -0.36, 1.0, -0.1, 0.7, 0.07, 0.7, rad(90), 0, 0));
	} else if (id === 'troop.archers') {
		body.push(part(lowCone, W, 0, 0.66, 0, 0.78, 1.32, 0.78));
		gear.push(part(lowSph, C_SKIN, 0, 1.42, 0, 0.34, 0.36, 0.34));
		gear.push(part(lowCone, C_LEATHER, 0, 1.6, 0.02, 0.42, 0.34, 0.42));
		gear.push(part(GEO.box, C_WOOD, -0.36, 1.05, -0.12, 0.05, 1.25, 0.05));
		gear.push(part(lowCyl, C_LEATHER, 0.18, 1.1, 0.2, 0.16, 0.6, 0.16, rad(20), 0, 0));
	} else if (id === 'troop.outriders' || id === 'troop.knights') {
		const knight = id === 'troop.knights';
		const s = knight ? 1.12 : 1;
		const horse = knight ? C_HORSE_GREY : C_HORSE;
		gear.push(part(GEO.box, horse, 0, 1.15 * s, 0, 0.62 * s, 0.68 * s, 1.9 * s));
		gear.push(part(GEO.box, horse, 0, 1.6 * s, -1.05 * s, 0.34 * s, 0.9 * s, 0.4 * s, rad(-35), 0, 0));
		gear.push(part(GEO.box, horse, 0, 1.95 * s, -1.38 * s, 0.3 * s, 0.32 * s, 0.6 * s));
		for (const [lx, lz] of [[-0.22, -0.7], [0.22, -0.7], [-0.22, 0.7], [0.22, 0.7]]) gear.push(part(GEO.box, horse, lx * s, 0.45 * s, lz * s, 0.14 * s, 0.9 * s, 0.14 * s));
		body.push(part(GEO.box, W, 0, 1.3 * s, 0.1, 0.7 * s, 0.4 * s, 1.1 * s));
		body.push(part(lowCyl, W, 0, 2.05 * s, 0.15, 0.48 * s, 0.85 * s, 0.48 * s));
		gear.push(part(lowSph, knight ? C_STEEL : C_SKIN, 0, 2.62 * s, 0.15, 0.36 * s, 0.4 * s, 0.36 * s));
		gear.push(part(GEO.box, knight ? C_STEEL : C_WOOD, 0.36, 2.1 * s, -0.9, 0.06, 0.06, 2.6, rad(-6), 0, 0));
	} else if (id === 'troop.grenadiers') {
		// A leather apron, a satchel of clay firepots and one lit pot in the throwing hand.
		body.push(part(lowCyl, W, 0, 0.62, 0, 0.6, 1.24, 0.6));
		infantryHead(false);
		gear.push(part(lowCyl, C_LEATHER, 0, 1.68, 0, 0.44, 0.14, 0.44));
		gear.push(part(GEO.box, C_LEATHER, 0, 0.75, -0.3, 0.5, 0.8, 0.06));
		gear.push(part(GEO.box, C_LEATHER, -0.36, 0.9, 0.12, 0.2, 0.4, 0.36));
		gear.push(part(lowSph, C_RUST, 0.4, 1.55, -0.1, 0.3, 0.3, 0.3));
		gear.push(part(lowCone, 0xffa040, 0.4, 1.78, -0.1, 0.12, 0.22, 0.12));
	} else if (id === 'troop.chainbreakers') {
		// Stocky harpooners: an iron collar, a coil of chain and a barbed harpoon.
		body.push(part(GEO.box, W, 0, 0.9, 0, 0.66, 0.8, 0.46));
		body.push(part(GEO.box, G, 0, 0.32, 0, 0.5, 0.64, 0.34));
		infantryHead(true);
		gear.push(part(lowCyl, C_IRON, -0.38, 0.95, 0.05, 0.42, 0.22, 0.42, 0, 0, rad(90)));
		gear.push(part(lowCyl, C_IRON, 0, 1.28, 0, 0.62, 0.1, 0.5));
		gear.push(part(GEO.box, C_WOOD, 0.38, 1.25, -0.3, 0.07, 0.07, 1.8, rad(-25), 0, 0));
		gear.push(part(lowCone, C_IRON, 0.38, 1.7, -1.12, 0.16, 0.36, 0.16, rad(-115), 0, 0));
	} else if (id === 'troop.priests') {
		// Long pale robes with the house colour as a stole, a tall staff with a golden sun.
		gear.push(part(lowCone, C_ROBE, 0, 0.72, 0, 0.86, 1.44, 0.86));
		body.push(part(GEO.box, W, 0, 1.0, -0.2, 0.16, 0.9, 0.06));
		gear.push(part(lowSph, C_SKIN, 0, 1.5, 0, 0.34, 0.36, 0.34));
		gear.push(part(lowCone, C_ROBE, 0, 1.72, 0.04, 0.46, 0.46, 0.46));
		gear.push(part(GEO.box, C_WOOD, 0.38, 1.25, -0.1, 0.06, 2.4, 0.06));
		gear.push(part(lowSph, C_GOLD, 0.38, 2.5, -0.1, 0.3, 0.3, 0.3));
	} else if (id === 'troop.pikemen') {
		// Northern pikemen: a fur cloak over the house colour, a fur-trimmed helm and a long pike with an ice-blue head.
		body.push(part(lowCyl, W, 0, 0.62, 0, 0.6, 1.24, 0.6));
		infantryHead(true);
		gear.push(part(lowCyl, C_FUR, 0, 1.53, 0, 0.46, 0.08, 0.46));
		gear.push(part(GEO.box, C_FUR, 0, 0.9, 0.3, 0.5, 0.8, 0.08));
		gear.push(part(GEO.box, C_WOOD, 0.34, 1.35, -0.9, 0.06, 0.06, 4.4, rad(8), 0, 0));
		gear.push(part(lowCone, C_ICE, 0.34, 1.7, -3.32, 0.14, 0.5, 0.14, rad(-82), 0, 0));
	} else if (id === 'troop.goliaths') {
		// Hill giants about two and a half times a man's height: a kilt and shoulder strap in the house colour, a jade
		// pauldron and a stone-headed club. Built big here, the sim gives them a wide footprint too.
		for (const lx of [-0.3, 0.3]) gear.push(part(GEO.box, C_GIANT, lx, 0.6, 0, 0.38, 1.2, 0.42));
		body.push(part(lowCyl, W, 0, 1.35, 0, 1.15, 0.6, 0.8));
		gear.push(part(GEO.box, C_GIANT, 0, 2.1, 0, 1.15, 1.0, 0.72));
		body.push(part(GEO.box, W, 0, 2.15, 0, 1.18, 0.22, 0.75, 0, 0, rad(40)));
		gear.push(part(lowSph, C_GIANT, 0, 2.86, -0.05, 0.56, 0.6, 0.56));
		gear.push(part(lowSph, 0x4a3a2a, 0, 3.02, 0.06, 0.6, 0.36, 0.56));
		gear.push(part(lowSph, C_JADE, -0.62, 2.55, 0, 0.52, 0.34, 0.6));
		gear.push(part(GEO.box, C_GIANT, -0.72, 1.85, 0, 0.3, 1.15, 0.32));
		gear.push(part(GEO.box, C_GIANT, 0.72, 2.1, -0.3, 0.3, 0.32, 0.9));
		gear.push(part(lowCyl, C_WOOD, 0.75, 2.05, -1.1, 0.2, 1.5, 0.2, rad(-70), 0, 0));
		gear.push(part(lowSph, 0x8f8a80, 0.75, 2.35, -1.9, 0.62, 0.55, 0.62));
		const big = new THREE.Matrix4().makeScale(1.35, 1.35, 1.35);
		for (const q of body.concat(gear)) q.m.premultiply(big);
	} else if (id === 'troop.panthers') {
		// A black marsh panther with glowing eyes and a light rider in the house colour.
		gear.push(part(GEO.box, C_PANTHER, 0, 0.78, 0, 0.56, 0.5, 1.6));
		gear.push(part(GEO.box, C_PANTHER, 0, 0.95, -0.95, 0.46, 0.42, 0.5));
		gear.push(part(GEO.box, C_PANTHER, 0, 0.88, -1.25, 0.3, 0.22, 0.3));
		for (const ex of [-0.12, 0.12]) gear.push(part(GEO.box, C_VENOM, ex, 1.04, -1.21, 0.07, 0.05, 0.04));
		for (const ex of [-0.15, 0.15]) gear.push(part(lowCone, C_PANTHER, ex, 1.24, -0.88, 0.12, 0.18, 0.12));
		for (const [lx, lz] of [[-0.2, -0.6], [0.2, -0.6], [-0.2, 0.6], [0.2, 0.6]]) gear.push(part(GEO.box, C_PANTHER, lx, 0.3, lz, 0.14, 0.6, 0.16));
		gear.push(part(GEO.box, C_PANTHER, 0, 1.05, 1.15, 0.08, 0.08, 0.9, rad(-35), 0, 0));
		body.push(part(GEO.box, W, 0, 1.08, 0.15, 0.6, 0.12, 0.7));
		body.push(part(lowCyl, W, 0, 1.5, 0.2, 0.42, 0.7, 0.42));
		gear.push(part(lowSph, C_SKIN, 0, 1.98, 0.2, 0.32, 0.34, 0.32));
		gear.push(part(lowCone, 0x3b5a2e, 0, 2.18, 0.24, 0.42, 0.42, 0.42));
		gear.push(part(GEO.box, C_STEEL, 0.32, 1.55, -0.35, 0.05, 0.05, 1.2, rad(-25), 0, 0));
	} else if (id === 'troop.skeleton') {
		// Raised dead: a tattered tabard in the caster's colour over bare bones, a rusty blade.
		body.push(part(GEO.box, W, 0, 0.95, 0, 0.5, 0.55, 0.22));
		gear.push(part(GEO.box, C_BONE, 0, 1.0, 0, 0.42, 0.62, 0.3));
		for (const lx of [-0.13, 0.13]) gear.push(part(GEO.box, C_BONE, lx, 0.35, 0, 0.09, 0.7, 0.09));
		for (const ax of [-0.3, 0.3]) gear.push(part(GEO.box, C_BONE, ax, 0.95, -0.05, 0.07, 0.6, 0.07, rad(-15), 0, 0));
		gear.push(part(lowSph, C_BONE, 0, 1.48, 0, 0.32, 0.36, 0.34));
		gear.push(part(GEO.box, 0x2a2018, 0, 1.5, -0.25, 0.26, 0.08, 0.04));
		gear.push(part(GEO.box, C_RUST, 0.36, 1.0, -0.3, 0.06, 0.06, 1.0, rad(-35), 0, 0));
	} else {
		body.push(part(lowCyl, W, 0, 0.62, 0, 0.6, 1.24, 0.6));
		infantryHead(false);
		gear.push(part(GEO.box, C_WOOD, 0.36, 1.0, -0.25, 0.07, 0.07, 1.2, rad(-40), 0, 0));
	}
	return { body: mergeParts(body), gear: mergeParts(gear) };
}

function mulberry32(seed) {
	let a = seed >>> 0;
	return () => {
		a |= 0; a = (a + 0x6D2B79F5) | 0;
		let t = Math.imul(a ^ (a >>> 15), 1 | a);
		t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

// ---------------------------------------------------------------------------------------------
// Battle view (terrain, soldiers, titans, projectiles, effects)
// ---------------------------------------------------------------------------------------------
const view = {
	terrain: null,
	mapKey: '',
	terrainGroup: new THREE.Group(),
	units: new THREE.Group(),
	fxGroup: new THREE.Group(),
	groups: [],
	soldierGroup: null,
	soldierInst: null,
	titans: [],
	rings: [],
	deployZones: [],
	tmpObj: new THREE.Object3D(),
};
scene.add(view.terrainGroup, view.units, view.fxGroup);

function lambert(color, extra) { return new THREE.MeshLambertMaterial(Object.assign({ color }, extra || {})); }

function buildTerrain(st) {
	const key = st.map.name + ':' + st.grid.nx + 'x' + st.grid.nz;
	if (view.mapKey === key) return;
	view.mapKey = key;
	view.terrainGroup.clear();
	const g = st.grid;
	const cell = g.cell;
	const W = st.map.w;
	const D = st.map.d;
	cam.mapW = W;
	cam.mapD = D;
	const flagAt = (cx, cz) => g.f[clamp(cz, 0, g.nz - 1) * g.nx + clamp(cx, 0, g.nx - 1)];
	const heightAt = (cx, cz) => g.h[clamp(cz, 0, g.nz - 1) * g.nx + clamp(cx, 0, g.nx - 1)];
	const rnd = mulberry32(7);

	// Heightfield with vertex colors from the simulation's own nav-grid flags and heights.
	const vx = g.nx + 1;
	const vz = g.nz + 1;
	const pos = new Float32Array(vx * vz * 3);
	const col = new Float32Array(vx * vz * 3);
	const grass = new THREE.Color(0x6f8f3a);
	const grassDry = new THREE.Color(0x98a052);
	const forestFloor = new THREE.Color(0x3f5a24);
	const road = new THREE.Color(0xa88f62);
	const ford = new THREE.Color(0x8c8a6a);
	const bed = new THREE.Color(0x4a5a46);
	const ruins = new THREE.Color(0x8b8275);
	const rock = new THREE.Color(0x77736c);
	const c = new THREE.Color();
	for (let j = 0; j < vz; j++) {
		for (let i = 0; i < vx; i++) {
			let h = 0;
			let flags = 0;
			let deep = 0;
			let fordN = 0;
			for (const [di, dj] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
				const f = flagAt(i + di, j + dj);
				h += heightAt(i + di, j + dj);
				flags |= f;
				if (f & CF.Deep) deep++;
				if (f & CF.Ford) fordN++;
			}
			h /= 4;
			if (deep) h -= 0.35 * deep;
			else if (fordN) h -= 0.08 * fordN;
			const k = (j * vx + i) * 3;
			pos[k] = i * cell;
			pos[k + 1] = h;
			pos[k + 2] = -j * cell;
			c.copy(grass).lerp(grassDry, clamp(h / 7, 0, 1) * 0.8 + rnd() * 0.12);
			if (flags & CF.Forest) c.lerp(forestFloor, 0.65);
			if (flags & CF.Road) c.copy(road).offsetHSL(0, 0, (rnd() - 0.5) * 0.04);
			if (flags & CF.Ruins) c.lerp(ruins, 0.55);
			if (flags & CF.Rock) c.lerp(rock, 0.6);
			if (fordN) c.lerp(ford, 0.7);
			if (deep) c.copy(bed);
			c.offsetHSL(0, 0, (rnd() - 0.5) * 0.035);
			col[k] = c.r; col[k + 1] = c.g; col[k + 2] = c.b;
		}
	}
	const idx = [];
	for (let j = 0; j < g.nz; j++) {
		for (let i = 0; i < g.nx; i++) {
			const a = j * vx + i;
			const b = a + 1;
			const d = a + vx;
			const e = d + 1;
			idx.push(a, b, d, b, e, d);
		}
	}
	const geo = new THREE.BufferGeometry();
	geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
	geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
	geo.setIndex(idx);
	geo.computeVertexNormals();
	const terrain = new THREE.Mesh(geo, lambert(0xffffff, { vertexColors: true }));
	terrain.receiveShadow = true;
	view.terrain = terrain;
	view.terrainGroup.add(terrain);

	// Darker land beyond the battlefield.
	const outer = new THREE.Mesh(new THREE.PlaneGeometry(1400, 1400).rotateX(-Math.PI / 2), lambert(0x4f6a2c));
	outer.position.set(W / 2, -0.25, -D / 2);
	outer.receiveShadow = true;
	view.terrainGroup.add(outer);
	const edge = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0, 0.3, 0), new THREE.Vector3(W, 0.3, 0),
		new THREE.Vector3(W, 0.3, -D), new THREE.Vector3(0, 0.3, -D)]), new THREE.LineBasicMaterial({ color: 0x2a3418, transparent: true, opacity: 0.5 }));
	view.terrainGroup.add(edge);

	const water = new THREE.MeshPhongMaterial({ color: 0x2b6f93, specular: 0xbfe6ff, shininess: 90, transparent: true, opacity: 0.86 });
	const shallow = new THREE.MeshPhongMaterial({ color: 0x5fa3bf, specular: 0xffffff, shininess: 60, transparent: true, opacity: 0.5 });
	const trunks = [];
	const crowns = [];
	const rocks = [];
	const stones = [];
	for (const f of st.map.feat) {
		const cx = f.c ? f.x : (f.x0 + f.x1) / 2;
		const cz = f.c ? f.z : (f.z0 + f.z1) / 2;
		const sx = f.c ? f.r * 2 : f.x1 - f.x0;
		const sz = f.c ? f.r * 2 : f.z1 - f.z0;
		if (f.t === FT.Deep || f.t === FT.Shallow) {
			const geo = f.c ? new THREE.CircleGeometry(f.r, 48) : new THREE.PlaneGeometry(sx, sz); // round lakes and marshes
			const m = new THREE.Mesh(geo.rotateX(-Math.PI / 2), f.t === FT.Deep ? water : shallow);
			m.position.set(cx, f.t === FT.Deep ? -0.05 : 0.02, -cz);
			m.renderOrder = 1;
			view.terrainGroup.add(m);
			if (f.t === FT.Deep) view.water = m;
		} else if (f.t === FT.Forest) {
			const r = f.c ? f.r : Math.min(sx, sz) / 2;
			const n = clamp(Math.round((Math.PI * r * r) / 20), 4, 140);
			for (let k = 0; k < n; k++) {
				const a = rnd() * Math.PI * 2;
				const d = r * Math.sqrt(rnd());
				const px = cx + Math.cos(a) * d;
				const pz = cz + Math.sin(a) * d;
				const s = 0.8 + rnd() * 0.55;
				const y = heightAt(Math.floor(px / cell), Math.floor(pz / cell));
				trunks.push([px, y, pz, s]);
				crowns.push([px, y, pz, s, rnd()]);
			}
		} else if (f.t === FT.Rock) {
			const n = 3 + Math.floor(rnd() * 3);
			for (let k = 0; k < n; k++) rocks.push([cx + (rnd() - 0.5) * f.r, cz + (rnd() - 0.5) * f.r, f.r * (0.45 + rnd() * 0.4), rnd()]);
		} else if (f.t === FT.Ruins) {
			for (let k = 0; k < 9; k++) stones.push([cx + (rnd() - 0.5) * sx * 0.9, cz + (rnd() - 0.5) * sz * 0.9, rnd(), rnd()]);
		}
	}
	// Stone bridge where the road crosses deep water.
	const roads = st.map.feat.filter((f) => f.t === FT.Road && !f.c);
	const deeps = st.map.feat.filter((f) => f.t === FT.Deep && !f.c);
	const stoneMat = lambert(0x9a9286);
	for (const r of roads) {
		for (const d of deeps) {
			const x0 = Math.max(r.x0, d.x0);
			const x1 = Math.min(r.x1, d.x1);
			const z0 = Math.max(r.z0, d.z0);
			const z1 = Math.min(r.z1, d.z1);
			if (x1 <= x0 || z1 <= z0) continue;
			const deck = new THREE.Mesh(GEO.box, stoneMat);
			deck.scale.set(x1 - x0, 0.7, z1 - z0 + 3);
			deck.position.set((x0 + x1) / 2, 0.15, -(z0 + z1) / 2);
			deck.receiveShadow = true;
			deck.castShadow = true;
			view.terrainGroup.add(deck);
			for (const wx of [x0 + 0.4, x1 - 0.4]) {
				const wall = new THREE.Mesh(GEO.box, stoneMat);
				wall.scale.set(0.6, 1.1, z1 - z0 + 3);
				wall.position.set(wx, 0.9, -(z0 + z1) / 2);
				wall.castShadow = true;
				view.terrainGroup.add(wall);
			}
		}
	}
	const inst = (geo, mat, items, fill) => {
		if (!items.length) return;
		const m = new THREE.InstancedMesh(geo, mat, items.length);
		items.forEach((it, k) => { fill(view.tmpObj, it); view.tmpObj.updateMatrix(); m.setMatrixAt(k, view.tmpObj.matrix); });
		m.castShadow = true;
		m.receiveShadow = true;
		view.terrainGroup.add(m);
	};
	const resetObj = (o) => { o.rotation.set(0, 0, 0); o.scale.set(1, 1, 1); };
	inst(new THREE.CylinderGeometry(0.22, 0.32, 1, 6), lambert(0x5b3d22), trunks, (o, t) => { resetObj(o); o.position.set(t[0], t[1] + 1.4 * t[3], -t[2]); o.scale.set(t[3], 2.8 * t[3], t[3]); });
	const crownGeo = mergeParts([part(new THREE.ConeGeometry(1.7, 3.6, 7), 0x2f5a26, 0, 0, 0, 1, 1, 1), part(new THREE.ConeGeometry(1.3, 3.0, 7), 0x3a6b2c, 0, 1.9, 0, 1, 1, 1)]);
	inst(crownGeo, lambert(0xffffff, { vertexColors: true }), crowns, (o, t) => { resetObj(o); o.position.set(t[0], t[1] + 4.2 * t[3], -t[2]); o.scale.setScalar(t[3]); o.rotation.y = t[4] * 6; });
	inst(new THREE.DodecahedronGeometry(1, 0), lambert(0x86817a, { flatShading: true }), rocks, (o, t) => { resetObj(o); o.position.set(t[0], t[2] * 0.35, -t[1]); o.scale.set(t[2], t[2] * 0.75, t[2] * 0.9); o.rotation.set(t[3], t[3] * 4, 0); });
	inst(GEO.box, lambert(0xa79d8c), stones, (o, t) => {
		resetObj(o);
		const column = t[2] > 0.55;
		o.position.set(t[0], column ? 1.5 + t[3] : 0.6, -t[1]);
		o.scale.set(column ? 0.9 : 3.2, column ? 3 + t[3] * 2 : 1.2, column ? 0.9 : 0.7);
		o.rotation.y = t[3] * 3;
	});

	// Deploy zones (shown during deployment).
	view.deployZones = st.map.dep.map((z, side) => {
		const m = new THREE.Mesh(new THREE.PlaneGeometry(z[2] - z[0], z[3] - z[1]).rotateX(-Math.PI / 2),
			new THREE.MeshBasicMaterial({ color: SIDE_COLOR[side], transparent: true, opacity: side === 0 ? 0.16 : 0.08, depthWrite: false }));
		m.position.set((z[0] + z[2]) / 2, 0.4, -(z[1] + z[3]) / 2);
		m.renderOrder = 2;
		const border = new THREE.LineLoop(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(z[0], 0.45, -z[1]), new THREE.Vector3(z[2], 0.45, -z[1]),
			new THREE.Vector3(z[2], 0.45, -z[3]), new THREE.Vector3(z[0], 0.45, -z[3])]), new THREE.LineBasicMaterial({ color: SIDE_COLOR[side] }));
		const grp = new THREE.Group();
		grp.add(m, border);
		view.terrainGroup.add(grp);
		return grp;
	});
}

function disposeUnits() {
	view.units.traverse((o) => { if (o.isMesh && o.geometry && o.userData.ownGeo) o.geometry.dispose(); });
	view.units.clear();
	view.groups = [];
	view.titans = [];
	view.rings = [];
}

function buildUnits(st, soldiers) {
	disposeUnits();
	const n = soldiers.length / 8;
	const groupKey = new Map();
	view.soldierGroup = new Int16Array(n);
	view.soldierInst = new Int16Array(n);
	const counts = [];
	for (let i = 0; i < n; i++) {
		const sq = st.squads[soldiers[i * 8 + 7]];
		const key = sq.side + ':' + sq.troop;
		if (!groupKey.has(key)) { groupKey.set(key, counts.length); counts.push({ side: sq.side, troop: sq.troop, n: 0 }); }
		const gi = groupKey.get(key);
		view.soldierGroup[i] = gi;
		view.soldierInst[i] = counts[gi].n++;
	}
	view.groups = counts.map((g) => makeTroopGroup(st, g.side, g.troop, g.n));
	// Selection rings, one per squad.
	view.rings = st.squads.map(makeRing);
	view.titans = st.titans.map((t) => buildTitan(t));
}

const TROOP_TINT = { 'troop.militia': 1.15, 'troop.swordsmen': 0.85, 'troop.spearmen': 1.0, 'troop.archers': 1.3, 'troop.outriders': 0.95, 'troop.knights': 0.7, 'troop.skeleton': 0.55,
	'troop.grenadiers': 1.1, 'troop.chainbreakers': 0.8, 'troop.priests': 1.25, 'troop.pikemen': 1.05, 'troop.goliaths': 0.75, 'troop.panthers': 0.9 };
function makeTroopGroup(st, side, troop, n) {
	const id = st.troops[troop].id;
	const model = troopModel(id);
	const color = SIDE_COLOR[side].clone().multiplyScalar(TROOP_TINT[id] || 1);
	const body = new THREE.InstancedMesh(model.body, lambert(color, { vertexColors: true }), n);
	const gear = new THREE.InstancedMesh(model.gear, lambert(0xffffff, { vertexColors: true }), n);
	for (const m of [body, gear]) {
		m.userData.ownGeo = true;
		m.castShadow = true;
		m.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
		m.frustumCulled = false;
		view.units.add(m);
	}
	return { body, gear, cav: st.troops[troop].cav === 1 };
}
function makeRing(sq) {
	const m = new THREE.Mesh(GEO.ring, new THREE.MeshBasicMaterial({ color: SIDE_COLOR[sq.side].clone().lerp(new THREE.Color(0xffffff), 0.35), transparent: true, opacity: 0.9, depthWrite: false }));
	m.visible = false;
	m.renderOrder = 3;
	view.units.add(m);
	return m;
}

/** Raise Dead adds squads mid-battle: fetch their static info and give their soldiers meshes and rings. */
function growUnits() {
	const n = game.curS.length / 8;
	if (!view.soldierGroup || n <= view.soldierGroup.length) return;
	const st = game.st;
	const fresh = game.sim.staticInfo().squads;
	for (let i = st.squads.length; i < fresh.length; i++) st.squads.push(fresh[i]);
	const from = view.soldierGroup.length;
	const group = new Int16Array(n);
	const inst = new Int16Array(n);
	group.set(view.soldierGroup);
	inst.set(view.soldierInst);
	const bySquad = new Map();
	for (let i = from; i < n; i++) {
		const q = game.curS[i * 8 + 7];
		if (!bySquad.has(q)) bySquad.set(q, []);
		bySquad.get(q).push(i);
	}
	for (const [q, list] of bySquad) {
		const sq = st.squads[q];
		const gi = view.groups.length;
		view.groups.push(makeTroopGroup(st, sq.side, sq.troop, list.length));
		list.forEach((s, k) => { group[s] = gi; inst[s] = k; });
	}
	view.soldierGroup = group;
	view.soldierInst = inst;
	for (let i = view.rings.length; i < st.squads.length; i++) view.rings.push(makeRing(st.squads[i]));
}

// Look of each titan: colours and what it carries (the kingdom roster shares two body types for now).
const TITAN_LOOK = {
	'titan.thor': { skin: 0x8d9ab4, dark: 0x39404d, glow: 0xbfe6ff, metal: 0.35, rough: 0.55, kind: 'hammer' },
	'titan.perun': { skin: 0xa9b8c9, dark: 0x3b4a5c, glow: 0xd6f0ff, metal: 0.4, rough: 0.5, kind: 'hammer' },
	'titan.talos': { skin: 0xc27b2c, dark: 0x6a3f16, glow: 0xffa63a, metal: 0.75, rough: 0.32, kind: 'fists' },
	'titan.minotaur': { skin: 0x6b4a35, dark: 0x2e2018, glow: 0xff5a3a, metal: 0.1, rough: 0.8, kind: 'horns' },
	'titan.cyclops': { skin: 0x9a8f7a, dark: 0x4a4238, glow: 0xffe08a, metal: 0.2, rough: 0.7, kind: 'eye' },
	'titan.atlas': { skin: 0x8c7a5c, dark: 0x4c4030, glow: 0x9fd0ff, metal: 0.3, rough: 0.6, kind: 'fists' },
	'titan.ymir': { skin: 0xbfd6e6, dark: 0x5a7486, glow: 0xa8eaff, metal: 0.25, rough: 0.4, kind: 'fists' },
	'titan.surtr': { skin: 0x3a2a26, dark: 0x1c1412, glow: 0xff7a1a, metal: 0.4, rough: 0.5, kind: 'sword' },
	'titan.hecate': { skin: 0x5b4a7a, dark: 0x2a2238, glow: 0xc89bff, metal: 0.3, rough: 0.5, kind: 'staff' },
	'titan.artemis': { skin: 0x7a8c5a, dark: 0x3a4628, glow: 0xe8ffb0, metal: 0.2, rough: 0.6, kind: 'bow' },
};

function buildTitan(t) {
	const H = t.h;
	const R = t.r;
	const look = TITAN_LOOK[t.def] || TITAN_LOOK['titan.talos'];
	const thor = look.kind === 'hammer';
	const skin = new THREE.MeshStandardMaterial({ color: look.skin, metalness: look.metal, roughness: look.rough });
	const dark = new THREE.MeshStandardMaterial({ color: look.dark, metalness: 0.5, roughness: 0.5 });
	const accent = new THREE.MeshStandardMaterial({ color: SIDE_COLOR[t.side], metalness: 0.15, roughness: 0.7 });
	const glow = new THREE.MeshBasicMaterial({ color: look.glow });
	const root = new THREE.Group();
	const body = new THREE.Group();
	root.add(body);
	const add = (geo, mat, parent, x, y, z, sx, sy, sz) => {
		const m = new THREE.Mesh(geo, mat);
		m.position.set(x, y, z);
		m.scale.set(sx, sy, sz);
		m.castShadow = true;
		parent.add(m);
		return m;
	};
	const pivot = (parent, x, y, z) => { const p = new THREE.Group(); p.position.set(x, y, z); p.rotation.order = 'YXZ'; parent.add(p); return p; };
	body.rotation.order = 'YXZ';
	add(GEO.box, accent, body, 0, 0.47 * H, 0, 1.3 * R, 0.1 * H, 0.9 * R);
	add(GEO.box, skin, body, 0, 0.66 * H, 0, 1.5 * R, 0.3 * H, 0.9 * R);
	add(GEO.box, dark, body, 0, 0.8 * H, 0, 1.75 * R, 0.06 * H, 1.0 * R);
	add(GEO.sphere, skin, body, 0, 0.88 * H, 0, 0.75 * R, 0.8 * R, 0.75 * R);
	const hipL = pivot(body, -0.4 * R, 0.45 * H, 0);
	const hipR = pivot(body, 0.4 * R, 0.45 * H, 0);
	for (const hip of [hipL, hipR]) {
		add(GEO.cyl, skin, hip, 0, -0.225 * H, 0, 0.5 * R, 0.45 * H, 0.5 * R);
		add(GEO.box, dark, hip, 0, -0.44 * H, -0.12 * R, 0.56 * R, 0.05 * H, 0.85 * R);
	}
	const shL = pivot(body, -1.0 * R, 0.78 * H, 0);
	const shR = pivot(body, 1.0 * R, 0.78 * H, 0);
	for (const sh of [shL, shR]) {
		add(GEO.box, skin, sh, 0, -0.18 * H, 0, 0.38 * R, 0.38 * H, 0.38 * R);
		add(GEO.sphere, dark, sh, 0, 0, 0, 0.55 * R, 0.55 * R, 0.55 * R);
	}
	if (thor) {
		add(GEO.cyl, new THREE.MeshLambertMaterial({ color: 0x5a3b1e }), shR, 0, -0.4 * H, 0, 0.12 * R, 0.14 * H, 0.12 * R);
		add(GEO.box, new THREE.MeshStandardMaterial({ color: 0x737c88, metalness: 0.8, roughness: 0.3 }), shR, 0, -0.49 * H, 0, 0.5 * R, 0.5 * R, 0.85 * R);
		add(GEO.box, accent, body, 0, 0.6 * H, 0.5 * R, 1.35 * R, 0.4 * H, 0.06 * R);
		add(GEO.sphere, dark, body, 0, 0.91 * H, 0, 0.8 * R, 0.5 * R, 0.8 * R);
		add(GEO.box, glow, body, -0.42 * R, 0.95 * H, 0, 0.1 * R, 0.35 * R, 0.5 * R);
		add(GEO.box, glow, body, 0.42 * R, 0.95 * H, 0, 0.1 * R, 0.35 * R, 0.5 * R);
	} else {
		add(GEO.sphere, skin, shR, 0, -0.38 * H, 0, 0.62 * R, 0.62 * R, 0.62 * R);
		add(GEO.sphere, skin, shL, 0, -0.38 * H, 0, 0.62 * R, 0.62 * R, 0.62 * R);
		const steel = new THREE.MeshStandardMaterial({ color: 0x8a8f99, metalness: 0.8, roughness: 0.3 });
		if (look.kind === 'horns') {
			add(GEO.cone, dark, body, -0.45 * R, 0.99 * H, -0.05 * R, 0.2 * R, 0.5 * R, 0.2 * R).rotation.z = 0.7;
			add(GEO.cone, dark, body, 0.45 * R, 0.99 * H, -0.05 * R, 0.2 * R, 0.5 * R, 0.2 * R).rotation.z = -0.7;
		} else if (look.kind === 'eye') {
			add(GEO.sphere, glow, body, 0, 0.9 * H, -0.62 * R, 0.32 * R, 0.32 * R, 0.2 * R);
		} else if (look.kind === 'sword') {
			add(GEO.box, steel, shR, 0, -0.62 * H, -0.25 * R, 0.14 * R, 0.62 * H, 0.32 * R);
			add(GEO.box, glow, shR, 0, -0.62 * H, -0.25 * R, 0.05 * R, 0.6 * H, 0.34 * R);
		} else if (look.kind === 'staff') {
			add(GEO.cyl, dark, shR, 0, -0.42 * H, 0, 0.12 * R, 0.8 * H, 0.12 * R);
			add(GEO.sphere, glow, shR, 0, -0.02 * H, 0, 0.45 * R, 0.45 * R, 0.45 * R);
		} else if (look.kind === 'bow') {
			add(GEO.box, dark, shL, 0, -0.42 * H, -0.3 * R, 0.1 * R, 0.55 * H, 0.1 * R).rotation.x = 0.25;
			add(GEO.box, glow, shL, 0, -0.42 * H, -0.18 * R, 0.03 * R, 0.5 * H, 0.03 * R);
		}
		add(GEO.box, glow, body, -0.15 * R, 0.89 * H, -0.36 * R, 0.14 * R, 0.06 * R, 0.04 * R);
		add(GEO.box, glow, body, 0.15 * R, 0.89 * H, -0.36 * R, 0.14 * R, 0.06 * R, 0.04 * R);
		add(GEO.box, glow, body, 0, 0.66 * H, -0.46 * R, 0.08 * R, 0.24 * H, 0.03 * R);
		add(GEO.box, dark, body, 0, 0.93 * H, 0, 0.12 * R, 0.3 * R, 0.85 * R);
	}
	const ring = new THREE.Mesh(GEO.ring, new THREE.MeshBasicMaterial({ color: SIDE_COLOR[t.side], transparent: true, opacity: 0.8, depthWrite: false }));
	ring.scale.setScalar(1.45 * R);
	ring.position.y = 0.3;
	ring.renderOrder = 3;
	root.add(ring);
	view.units.add(root);
	return { root, body, hipL, hipR, shL, shR, H, R, ring, step: Math.max(t.step || 8, 2), walk: 0, lastX: null, lastZ: 0,
		attack: -9, combo: 0, stomp: -9, grab: -9, cast: -9, death: -1, side: t.side };
}

function setRot(o, pitch, yaw, roll) { o.rotation.set(rad(pitch), rad(-yaw), rad(roll || 0)); }

// Projectiles, telegraphs, zones and effects use small pools.
const pools = {};
function pool(name, make) {
	if (!pools[name]) pools[name] = { make, items: [], used: 0 };
	return pools[name];
}
function take(name, make) {
	const p = pool(name, make);
	if (p.used >= p.items.length) {
		const o = p.make();
		view.fxGroup.add(o);
		p.items.push(o);
	}
	const o = p.items[p.used++];
	o.visible = true;
	return o;
}
function poolsBegin() { for (const k in pools) pools[k].used = 0; }
function poolsEnd() { for (const k in pools) { const p = pools[k]; for (let i = p.used; i < p.items.length; i++) p.items[i].visible = false; } }

const MAT = {
	arrow: lambert(0xe6dcc2),
	boulder: lambert(0x8a5a2e),
	bolt: new THREE.MeshBasicMaterial({ color: 0xbfe6ff }),
	hammer: new THREE.MeshStandardMaterial({ color: 0x737c88, metalness: 0.8, roughness: 0.3 }),
	meteor: new THREE.MeshBasicMaterial({ color: 0xff8a2a }),
	tele: new THREE.MeshBasicMaterial({ color: 0xff4a1a, transparent: true, opacity: 0.7, depthWrite: false }),
	teleFill: new THREE.MeshBasicMaterial({ color: 0xff4a1a, transparent: true, opacity: 0.18, depthWrite: false }),
	heal: new THREE.MeshBasicMaterial({ color: 0x5fd38d, transparent: true, opacity: 0.28, depthWrite: false }),
	ichor: new THREE.MeshBasicMaterial({ color: 0xff8a2a, transparent: true, opacity: 0.3, depthWrite: false }),
	firePot: new THREE.MeshBasicMaterial({ color: 0xffa040 }),
	burn: new THREE.MeshBasicMaterial({ color: 0xff6a1a, transparent: true, opacity: 0.4, depthWrite: false }),
	chain: new THREE.MeshStandardMaterial({ color: 0x8d949e, metalness: 0.7, roughness: 0.4 }),
	reticle: new THREE.MeshBasicMaterial({ color: 0xf2c14e, transparent: true, opacity: 0.85, depthWrite: false }),
	reticleFill: new THREE.MeshBasicMaterial({ color: 0xf2c14e, transparent: true, opacity: 0.14, depthWrite: false }),
};
const arrowGeo = new THREE.BoxGeometry(0.06, 0.06, 1.1);

const effects = [];
const FX = {
	dust: { geo: GEO.ring, color: 0x9b8160, flat: true },
	shock: { geo: GEO.ring, color: 0xf4f1ff, flat: true },
	fire: { geo: GEO.sphere, color: 0xff7a2a },
	bolt: { geo: GEO.cyl, color: 0xbfe6ff, column: true },
	heal: { geo: GEO.cyl, color: 0x6ff0a0, column: true },
	stagger: { geo: GEO.sphere, color: 0xffe066 },
	order: { geo: GEO.ring, color: 0x9cc0ff, flat: true },
	attack: { geo: GEO.ring, color: 0xff8f7a, flat: true },
	necro: { geo: GEO.ring, color: 0x9dff8a, flat: true },
	necroCol: { geo: GEO.cyl, color: 0x7dffb0, column: true },
	frost: { geo: GEO.cyl, color: 0xa8eaff, column: true },
	flame: { geo: GEO.cyl, color: 0xff8a3a, column: true },
	moon: { geo: GEO.cyl, color: 0xf2ffd0, column: true },
};
/** Colour of a titan ability's impact column by its element. */
function abilityTint(A) {
	if (!A) return 'bolt';
	if (/ymir|atlas\.weight/.test(A.id)) return 'frost';
	if (/surtr|cyclops|talos\.molten/.test(A.id)) return 'flame';
	if (/hecate/.test(A.id)) return 'necroCol';
	if (/artemis/.test(A.id)) return 'moon';
	return 'bolt';
}
function spawnFx(kind, x, z, y, dur, s0, s1, h) {
	if (effects.length > 120) return;
	effects.push({ kind, x, z, y: y || 0, t0: now(), dur, s0, s1, h: h || 1 });
}
const fxMats = {};
function fxMat(kind) {
	if (!fxMats[kind]) fxMats[kind] = [];
	return fxMats[kind];
}
function updateEffects() {
	const t = now();
	const used = {};
	for (let i = effects.length - 1; i >= 0; i--) {
		const e = effects[i];
		const k = (t - e.t0) / e.dur;
		if (k >= 1) { effects.splice(i, 1); continue; }
		const def = FX[e.kind];
		const list = fxMat(e.kind);
		const u = (used[e.kind] = (used[e.kind] || 0) + 1) - 1;
		if (u >= list.length) {
			const m = new THREE.Mesh(def.geo, new THREE.MeshBasicMaterial({ color: def.color, transparent: true, depthWrite: false }));
			m.renderOrder = 4;
			view.fxGroup.add(m);
			list.push(m);
		}
		const m = list[u];
		m.visible = true;
		const s = lerp(e.s0, e.s1, smooth(k));
		m.material.opacity = (1 - k) * (def.flat ? 0.85 : 0.75);
		const ground = game.sim ? game.sim.height(e.x, e.z) : 0;
		if (def.flat) { m.scale.set(s, 1, s); m.position.set(e.x, ground + 0.5 + e.y, -e.z); }
		else if (def.column) { m.scale.set(s, e.h * (1 - k * 0.3), s); m.position.set(e.x, ground + e.y + e.h / 2, -e.z); }
		else { m.scale.setScalar(s * (1 - 0.4 * k)); m.position.set(e.x, ground + e.y, -e.z); }
	}
	for (const kind in fxMats) {
		const list = fxMats[kind];
		for (let i = used[kind] || 0; i < list.length; i++) list[i].visible = false;
	}
}

const tmpQ = new THREE.Quaternion();
const tmpE = new THREE.Euler(0, 0, 0, 'YXZ');
const tmpP = new THREE.Vector3();
const tmpS = new THREE.Vector3();
const tmpM = new THREE.Matrix4();
const ZERO_M = new THREE.Matrix4().makeScale(0, 0, 0);

function updateSoldiers(rt, alpha) {
	const prev = game.prevS;
	const b = game.curS;
	if (!b || !view.soldierGroup) return;
	const st = game.st;
	const fr = game.fr;
	const n = Math.min(b.length / 8, view.soldierGroup.length);
	const scale = SOLDIER_SCALE;
	for (let i = 0; i < n; i++) {
		const o = i * 8;
		// A soldier raised this tick has no previous snapshot yet.
		const a = o + 8 <= prev.length ? prev : b;
		const g = view.groups[view.soldierGroup[i]];
		const inst = view.soldierInst[i];
		const state = b[o + 4];
		const sqIdx = b[o + 7];
		const hidden = st.squads[sqIdx].side === 1 && fr && fr.sq[sqIdx] && !fr.sq[sqIdx].vis && state !== SS.Dead;
		if (state === SS.Left || hidden) { g.body.setMatrixAt(inst, ZERO_M); g.gear.setMatrixAt(inst, ZERO_M); continue; }
		const x = lerp(a[o], b[o], alpha);
		const z = lerp(a[o + 1], b[o + 1], alpha);
		let y = lerp(a[o + 2], b[o + 2], alpha);
		const yaw = b[o + 3];
		let pitch = 0;
		let roll = 0;
		let px = x;
		let pz = z;
		let s = scale;
		if (state === SS.Dead) {
			const since = (rt - b[o + 5]) / TPS;
			if (since > 17) { g.body.setMatrixAt(inst, ZERO_M); g.gear.setMatrixAt(inst, ZERO_M); continue; }
			roll = 90 * smooth(since / 0.4);
			y += 0.25 - Math.max(0, since - 14) * 0.4;
		} else if (state === SS.Airborne) {
			pitch = (rt * 23) % 360;
			roll = (rt * 17) % 360;
		} else if (state === SS.Down) {
			roll = 80;
		} else {
			const since = rt - b[o + 6];
			if (since >= 0 && since < 6) {
				const lunge = 0.35 * (1 - since / 6);
				px += Math.sin(yaw) * lunge;
				pz += Math.cos(yaw) * lunge;
			}
			if (state === SS.Moving && !g.cav) y += Math.abs(Math.sin(rt * 0.9 + i)) * 0.08;
		}
		tmpE.set(rad(pitch), -yaw, rad(roll), 'YXZ');
		tmpQ.setFromEuler(tmpE);
		tmpP.set(px, y, -pz);
		tmpS.set(s, s, s);
		tmpM.compose(tmpP, tmpQ, tmpS);
		g.body.setMatrixAt(inst, tmpM);
		g.gear.setMatrixAt(inst, tmpM);
	}
	for (const g of view.groups) { g.body.instanceMatrix.needsUpdate = true; g.gear.instanceMatrix.needsUpdate = true; }
}

function updateTitans(rt, alpha) {
	const a = game.prevT;
	const b = game.curT;
	const fr = game.fr;
	if (!b || !fr) return;
	const t = now();
	for (let i = 0; i < view.titans.length; i++) {
		const v = view.titans[i];
		const T = fr.ti[i];
		const o = i * 4;
		const x = lerp(a[o], b[o], alpha);
		const z = lerp(a[o + 1], b[o + 1], alpha);
		let y = lerp(a[o + 2], b[o + 2], alpha);
		let dyaw = b[o + 3] - a[o + 3];
		while (dyaw > Math.PI) dyaw -= Math.PI * 2;
		while (dyaw < -Math.PI) dyaw += Math.PI * 2;
		const yaw = a[o + 3] + dyaw * alpha;
		if (T.dead) {
			if (v.death < 0) v.death = t;
			const since = t - v.death;
			const fall = smooth(since / 1.4);
			if (since > 20) y -= (since - 20) * 1.2;
			v.root.visible = since < 26;
			v.ring.visible = false;
			v.root.position.set(x, y, -z);
			v.root.rotation.order = 'YXZ';
			v.root.rotation.set(rad(-88 * fall), -T.fall, 0);
			continue;
		}
		v.root.visible = true;
		v.ring.visible = true;
		if (T.s === TS.Leaping && T.le > T.ls) {
			const k = clamp((rt - T.ls) / (T.le - T.ls), 0, 1);
			// A rush (line-shaped leap) stays low; a leap arcs high.
			const LA = game.st.abil[T.la];
			y += Math.sin(k * Math.PI) * (LA && LA.shape === SHAPE.Line ? 1.2 : 9);
		}
		v.root.position.set(x, y, -z);
		v.root.rotation.set(0, -yaw, 0);
		if (v.lastX === null) { v.lastX = x; v.lastZ = z; }
		const moved = Math.hypot(x - v.lastX, z - v.lastZ);
		v.lastX = x;
		v.lastZ = z;
		v.walk += (moved / v.step) * Math.PI;
		const swing = moved > 0.005 ? Math.sin(v.walk) * 24 : 0;
		let hipL = [-swing, 0, 0];
		let hipR = [swing, 0, 0];
		let armL = [swing * 0.5, 0, 0];
		let armR = [-swing * 0.5, 0, 0];
		let bodyP = 0;
		let bodyR = 0;
		let bodyY = 0;
		const ta = (t - v.attack) / 0.55;
		if (ta >= 0 && ta <= 1) {
			if (v.combo === 0) armR = [90, lerp(70, -60, smooth(ta)), 0];
			else if (v.combo === 1) armL = [90, lerp(-70, 60, smooth(ta)), 0];
			else {
				const p = ta < 0.45 ? lerp(0, 170, smooth(ta / 0.45)) : lerp(170, 40, smooth((ta - 0.45) / 0.55));
				armL = [p, 0, 0];
				armR = [p, 0, 0];
				bodyP = ta > 0.45 ? -8 : 4;
			}
		}
		const ts = (t - v.stomp) / 0.6;
		if (ts >= 0 && ts <= 1) {
			hipR = [ts < 0.5 ? lerp(0, 45, ts * 2) : lerp(45, 0, (ts - 0.5) * 2), 0, 0];
			bodyY = ts > 0.5 ? -0.04 * v.H * (1 - ts) : 0;
		}
		const tg = (t - v.grab) / 1.0;
		if (tg >= 0 && tg <= 1) armR = [tg < 0.5 ? lerp(0, 100, tg * 2) : lerp(100, 175, (tg - 0.5) * 2), 0, 0];
		const tc = (t - v.cast) / 0.7;
		if (tc >= 0 && tc <= 1) { const p = 150 * Math.sin(tc * Math.PI); armL = [p, 0, 0]; armR = [p, 0, 0]; }
		if (T.s === TS.Clashing) {
			armL = [95, 0, 0];
			armR = [95, 0, 0];
			bodyP = -10 + Math.sin(t * 6) * 3;
		} else if (T.s === TS.Staggered) {
			bodyP = 12;
			bodyR = Math.sin(t * 9) * 7;
			armL = [-20, 0, -25];
			armR = [-20, 0, 25];
		}
		v.body.position.y = bodyY;
		setRot(v.body, bodyP, 0, bodyR);
		setRot(v.hipL, hipL[0], hipL[1], hipL[2]);
		setRot(v.hipR, hipR[0], hipR[1], hipR[2]);
		setRot(v.shL, armL[0], armL[1], armL[2]);
		setRot(v.shR, armR[0], armR[1], armR[2]);
		v.ring.material.opacity = game.selected === TITAN_BASE + i ? 0.95 : 0.45;
	}
}

function updateProjectiles(rt) {
	const fr = game.fr;
	if (!fr) return;
	const st = game.st;
	for (const p of fr.pr) {
		const [kind, fx, fz, tx, tz, h0, h1, t0, t1] = p;
		const span = Math.max(1, t1 - t0);
		const k = clamp((rt - t0) / span, 0, 1);
		const dist = Math.hypot(tx - fx, tz - fz);
		const thrown = kind === 2;
		const pot = kind === 4;
		const apex = thrown ? 12 : pot ? 2 + dist * 0.3 : 1 + dist * 0.12;
		const x = lerp(fx, tx, k);
		const z = lerp(fz, tz, k);
		const y = lerp(h0, h1 + 0.5, k) + 4 * apex * k * (1 - k);
		if (thrown) {
			const m = take('thrown', () => new THREE.Mesh(GEO.box, lambert(0xd9c7a8)));
			m.position.set(x, y, -z);
			m.scale.set(0.7, 1.8, 0.7);
			m.rotation.set(rt * 0.4, rt * 0.3, 0);
		} else if (pot) {
			const m = take('firePot', () => new THREE.Mesh(GEO.sphere, MAT.firePot));
			m.position.set(x, y, -z);
			m.scale.setScalar(0.55);
		} else if (kind === 3) {
			const m = take('bolt', () => new THREE.Mesh(GEO.sphere, MAT.bolt));
			m.position.set(x, y, -z);
			m.scale.setScalar(1.2);
		} else {
			const m = take('arrow', () => new THREE.Mesh(arrowGeo, MAT.arrow));
			m.position.set(x, y, -z);
			const k2 = Math.min(1, k + 0.02);
			const y2 = lerp(h0, h1 + 0.5, k2) + 4 * apex * k2 * (1 - k2);
			tmpP.set(lerp(fx, tx, k2), y2, -lerp(fz, tz, k2));
			if (tmpP.distanceToSquared(m.position) > 1e-6) m.lookAt(tmpP);
		}
	}
	for (const im of fr.im) {
		const [ab, ox, oz, px, pz, t0, t1] = im;
		const A = st.abil[ab];
		if (!A) continue;
		if (A.del === DELIV.Projectile) {
			const travel = Math.max(1, (A.range * TPS) / Math.max(A.speed, 1));
			const el = rt - t0;
			const back = t1 - t0 > travel + 1;
			let u = -1;
			if (!back && el <= travel) u = el / travel;
			else if (back && el > travel) u = 1 - (el - travel) / travel;
			if (u >= 0 && u <= 1) {
				const m = take('hammer', () => new THREE.Mesh(GEO.box, MAT.hammer));
				m.position.set(lerp(ox, px, u), 6 + game.sim.height(lerp(ox, px, u), lerp(oz, pz, u)), -lerp(oz, pz, u));
				m.scale.set(2.2, 1.4, 1.4);
				m.rotation.set(rt * 0.7, 0, 0);
			}
			continue;
		}
		const r = Math.max(A.radius, 2);
		const g = game.sim.height(px, pz);
		const ring = take('tele', () => new THREE.Mesh(GEO.ring, MAT.tele));
		ring.position.set(px, g + 0.45, -pz);
		ring.scale.setScalar(r);
		const span = Math.max(1, t1 - t0);
		const k = clamp((rt - t0) / span, 0, 1);
		const fill = take('teleFill', () => new THREE.Mesh(GEO.disc, MAT.teleFill));
		fill.position.set(px, g + 0.42, -pz);
		fill.scale.setScalar(Math.max(0.01, r * k));
		if (A.del === DELIV.Delayed) {
			const m = take('meteor', () => new THREE.Mesh(GEO.sphere, MAT.meteor));
			m.position.set(px + (1 - k) * 25, g + (1 - k) * 70 + 1, -pz);
			m.scale.setScalar(3.2);
		}
	}
	for (const fp of fr.fp || []) {
		const [x, z, r, end] = fp;
		const m = take('burn', () => new THREE.Mesh(GEO.disc, MAT.burn));
		const fade = clamp((end - rt) / 20, 0.2, 1);
		m.position.set(x, game.sim.height(x, z) + 0.45, -z);
		m.scale.setScalar(r * fade * (0.92 + 0.08 * Math.sin(rt * 1.7 + x)));
	}
	// Harpoon chains: a taut line from the throwing squad to the titan, briefly.
	const tNow = now();
	for (let i = game.chains.length - 1; i >= 0; i--) {
		const c = game.chains[i];
		const sq = fr.sq[c.squad];
		const ti = c.titan;
		if (tNow - c.t0 > 1.4 || !sq || !sq.act || !game.curT || ti * 4 + 1 >= game.curT.length) { game.chains.splice(i, 1); continue; }
		const sx = sq.c[0];
		const sz = sq.c[1];
		const tx = game.curT[ti * 4];
		const tz = game.curT[ti * 4 + 1];
		const vis = view.titans[ti];
		const a = tmpP.set(sx, game.sim.height(sx, sz) + 1.4, -sz);
		const b = tmpS.set(tx, game.sim.height(tx, tz) + (vis ? vis.H * 0.35 : 6), -tz);
		const m = take('chain', () => new THREE.Mesh(GEO.box, MAT.chain));
		m.position.copy(a).add(b).multiplyScalar(0.5);
		m.scale.set(0.16, 0.16, a.distanceTo(b));
		m.lookAt(b);
	}
	for (const zo of fr.zo) {
		const [ab, x, z, r] = zo;
		const A = st.abil[ab];
		const heal = A && A.id.indexOf('healing') >= 0;
		const m = take(heal ? 'zoneHeal' : 'zoneHot', () => new THREE.Mesh(GEO.disc, heal ? MAT.heal : MAT.ichor));
		const pulse = 1 + 0.05 * Math.sin(rt * 0.4);
		m.position.set(x, game.sim.height(x, z) + 0.5, -z);
		m.scale.setScalar(r * pulse);
	}
}

function updateRings() {
	const fr = game.fr;
	if (!fr) return;
	const sel = game.selected;
	for (let i = 0; i < view.rings.length; i++) {
		const m = view.rings[i];
		const s = fr.sq[i];
		const own = game.st.squads[i].side === 0;
		const on = s.act && own && (sel === 0 || sel === i + 1);
		const hover = s.act && !own && input.hoverEnemy === i + 1;
		m.visible = on || hover;
		if (!m.visible) continue;
		m.position.set(s.c[0], game.sim.height(s.c[0], s.c[1]) + 0.35, -s.c[1]);
		m.scale.setScalar(Math.max(3, s.r + 1.6));
	}
	for (const z of view.deployZones) z.visible = fr.phase === PH.Deploy;
}

function updateReticle() {
	const show = game.targeting && input.hover;
	if (!show) return;
	const A = targetingAbility();
	if (!A) return;
	const p = input.hover;
	const g = game.sim.height(p.x, p.z);
	const r = Math.max(A.radius || 3, 2);
	const ring = take('ret', () => new THREE.Mesh(GEO.ring, MAT.reticle));
	ring.position.set(p.x, g + 0.5, -p.z);
	ring.scale.setScalar(r);
	const fill = take('retFill', () => new THREE.Mesh(GEO.disc, MAT.reticleFill));
	fill.position.set(p.x, g + 0.48, -p.z);
	fill.scale.setScalar(r);
}

// ---------------------------------------------------------------------------------------------
// Sound (tiny synthesized cues; starts after the first interaction)
// ---------------------------------------------------------------------------------------------
const audio = { ctx: null };
function audioInit() {
	if (audio.ctx || !settings.sound) return;
	try { audio.ctx = new (window.AudioContext || window.webkitAudioContext)(); } catch (e) { audio.ctx = null; }
}
function tone(type, f0, f1, dur, gain) {
	const ctx = audio.ctx;
	if (!ctx || !settings.sound) return;
	const t = ctx.currentTime;
	const o = ctx.createOscillator();
	const g = ctx.createGain();
	o.type = type;
	o.frequency.setValueAtTime(f0, t);
	o.frequency.exponentialRampToValueAtTime(Math.max(f1, 1), t + dur);
	g.gain.setValueAtTime(gain, t);
	g.gain.exponentialRampToValueAtTime(0.0001, t + dur);
	o.connect(g).connect(ctx.destination);
	o.start(t);
	o.stop(t + dur + 0.02);
}
function soundAt(x, z, kind) {
	const d = Math.hypot(x - cam.x, -z - cam.z) + cam.dist * 0.4;
	const k = clamp(1 - d / 160, 0, 1);
	if (k <= 0) return;
	if (kind === 'step') tone('sine', 70, 40, 0.25, 0.12 * k);
	else if (kind === 'thud') tone('sine', 90, 38, 0.35, 0.35 * k);
	else if (kind === 'boom') { tone('sine', 70, 28, 0.9, 0.6 * k); tone('triangle', 160, 60, 0.5, 0.2 * k); }
	else if (kind === 'clang') { tone('square', 620, 300, 0.25, 0.12 * k); tone('triangle', 1240, 900, 0.4, 0.08 * k); }
}
function horn() { tone('sawtooth', 196, 196, 0.9, 0.06); tone('sawtooth', 294, 294, 0.9, 0.04); }

// ---------------------------------------------------------------------------------------------
// Game state and loop
// ---------------------------------------------------------------------------------------------
const game = {
	sim: null, st: null, fr: null, prevS: null, curS: null, prevT: null, curT: null,
	acc: 0, speed: 1, paused: false, selected: -1, targeting: 0, allAuto: false, simMs: 0, fps: 60,
	finishedAt: 0, resultsShown: false, lastPhase: -1, verify: null,
	chains: [],
	// What this battle is for: { kind: 'training' } or { kind: 'land', ... } from the kingdom.
	context: { kind: 'training' },
};
const app = { mode: 'battle' };

function ownTitanIndex() {
	if (!game.st) return -1;
	return game.st.titans.findIndex((t) => t.side === 0);
}
function unitPos(id) {
	const fr = game.fr;
	if (!fr) return null;
	if (id >= TITAN_BASE) { const i = id - TITAN_BASE; return game.curT ? { x: game.curT[i * 4], z: game.curT[i * 4 + 1] } : null; }
	if (id > 0) { const s = fr.sq[id - 1]; return s ? { x: s.c[0], z: s.c[1] } : null; }
	return null;
}
function unitAlive(id) {
	const fr = game.fr;
	if (!fr || id < 0) return false;
	if (id === 0) return true;
	if (id >= TITAN_BASE) { const t = fr.ti[id - TITAN_BASE]; return !!t && !t.dead; }
	const s = fr.sq[id - 1];
	return !!s && !!s.act;
}

/** Training battles can swap the player's titan for any titan of the roster; the scenario keeps its level and stars. */
function trainingPath(path) {
	if (!settings.titan) return path;
	try {
		const files = app.contentFiles();
		const sc = JSON.parse(files[path].replace(/^\s*\/\/.*$/gm, ''));
		if (sc.sides.attacker.titan.id === settings.titan) return path;
		sc.sides.attacker.titan.id = settings.titan;
		const custom = 'scenarios/_training_titan.json';
		game.sim.addFile(custom, JSON.stringify(sc));
		return custom;
	} catch (e) {
		return path;
	}
}
function startBattle(asReplay, override) {
	const sim = game.sim;
	if (override) game.context = override.context;
	else if (!asReplay) game.context = { kind: 'training' };
	const sc = SCENARIOS[settings.scenario];
	const path = override ? override.path : asReplay ? '' : trainingPath(sc.path);
	const ai = override ? override.ai : settings.ai;
	const ok = asReplay ? sim.replay() : sim.start(path, ai, '');
	if (!ok) { toast(sim.error(), 'bad'); return false; }
	game.st = sim.staticInfo();
	game.fr = sim.frame();
	game.curS = sim.soldiers();
	game.prevS = game.curS.slice();
	game.curT = sim.titans();
	game.prevT = game.curT.slice();
	game.acc = 0;
	game.selected = -1;
	game.targeting = 0;
	game.resultsShown = false;
	game.finishedAt = 0;
	game.verify = null;
	game.lastPhase = -1;
	if (!asReplay) game.allAuto = false;
	if (!asReplay) game.loaded = { scenario: settings.scenario, ai: settings.ai, titan: override ? '' : settings.titan };
	effects.length = 0;
	game.chains.length = 0;
	input.draw = null;
	closeRadial();
	buildTerrain(game.st);
	buildUnits(game.st, game.curS);
	buildHud(game.st);
	$('results').hidden = true;
	const ti = ownTitanIndex();
	const start = ti >= 0 ? { x: game.curT[ti * 4], z: game.curT[ti * 4 + 1] } : { x: game.st.map.w / 2, z: 40 };
	cam.x = start.x;
	cam.z = -(start.z + 30);
	cam.dist = game.fr.phase === PH.Deploy ? 120 : 95;
	cam.vx = cam.vz = 0;
	if (asReplay) toast('Реплей: бой воспроизводится по записанным командам', 'gold');
	return true;
}

function stepSim(dt) {
	const fr = game.fr;
	if (!game.sim || !fr || game.paused || fr.phase === PH.Finished) return;
	game.acc = Math.min(game.acc + dt * game.speed, 0.5);
	let steps = 0;
	const t0 = performance.now();
	while (game.acc >= DT && steps < 8) {
		// The previous "current" snapshot becomes "previous"; its old array is reused for the new one.
		const spareS = game.prevS;
		const spareT = game.prevT;
		game.prevS = game.curS;
		game.prevT = game.curT;
		const alive = game.sim.step();
		game.curS = game.sim.soldiers(spareS);
		game.curT = game.sim.titans(spareT);
		game.acc -= DT;
		steps++;
		if (!alive) break;
	}
	if (steps) {
		const ms = (performance.now() - t0) / steps;
		game.simMs = game.simMs ? game.simMs * 0.9 + ms * 0.1 : ms;
		game.fr = game.sim.frame();
		growUnits();
		handleEvents(game.fr.ev);
	}
}

let lastT = performance.now();
function loop(t) {
	const dt = Math.min(0.1, Math.max(0, (t - lastT) / 1000));
	lastT = t;
	if (dt > 0) game.fps = game.fps * 0.94 + (1 / dt) * 0.06;
	if (app.mode === 'kingdom' && window.dotKingdom) {
		window.dotKingdom.frame(dt, t);
	} else if (game.sim && game.fr) {
		stepSim(dt);
		const fr = game.fr;
		const alpha = fr.phase === PH.Finished ? 1 : clamp(game.acc * TPS, 0, 1);
		const rt = fr.tick - 1 + alpha;
		game.renderTick = rt;
		const ti = ownTitanIndex();
		cam.follow = settings.follow && ti >= 0 && !fr.ti[ti].dead ? { x: lerp(game.prevT[ti * 4], game.curT[ti * 4], alpha), z: -lerp(game.prevT[ti * 4 + 1], game.curT[ti * 4 + 1], alpha) } : null;
		if (fr.phase === PH.Finished && !game.finishedAt) game.finishedAt = t;
		if (game.finishedAt && !game.resultsShown && t - game.finishedAt > 2600) showResults();
		keyboardPan(dt);
		placeCamera(dt);
		poolsBegin();
		updateSoldiers(rt, alpha);
		updateTitans(rt, alpha);
		updateProjectiles(rt);
		updateRings();
		updateReticle();
		poolsEnd();
		updateEffects();
		if (view.water) view.water.material.color.setHSL(0.56, 0.55, 0.37 + Math.sin(t / 900) * 0.015);
		renderer.render(scene, camera);
		drawOverlay(rt);
		updateHud(rt);
	}
	requestAnimationFrame(loop);
}

// ---------------------------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------------------------
function send(type, unit, p, target, param) {
	if (!game.sim || (game.fr && game.fr.replay)) return;
	game.sim.cmd(type, unit, p ? p.x : 0, p ? p.z : 0, target || 0, param || 0);
}
function select(id) {
	game.selected = id;
	game.targeting = 0;
}
function targetingAbility() {
	const st = game.st;
	if (!st) return null;
	if (game.targeting >= 10) return st.abil[st.sides[0].spells[game.targeting - 10]];
	if (game.targeting >= 1) { const ti = ownTitanIndex(); return ti >= 0 ? st.abil[st.titans[ti].ab[game.targeting - 1]] : null; }
	return null;
}
function castSpell(slot, p) {
	send(CMD.CastSpell, 0, p, 0, slot);
	game.targeting = 0;
}
function castAbility(slot, p) {
	const ti = ownTitanIndex();
	if (ti >= 0) send(CMD.CastAbility, TITAN_BASE + ti, p, 0, slot);
	game.targeting = 0;
}
function pressAbility(slot) {
	const ti = ownTitanIndex();
	if (ti < 0 || game.fr.ti[ti].dead) return;
	const A = game.st.abil[game.st.titans[ti].ab[slot]];
	if (A && A.shape === SHAPE.Self) {
		castAbility(slot, { x: game.curT[ti * 4], z: game.curT[ti * 4 + 1] });
		return;
	}
	game.targeting = game.targeting === 1 + slot ? 0 : 1 + slot;
	if (game.targeting) toast('Выберите цель: «' + abilFull(A) + '»', 'gold');
}
function pressSpell(slot) {
	game.targeting = game.targeting === 10 + slot ? 0 : 10 + slot;
	if (game.targeting) toast('Выберите цель: «' + abilFull(targetingAbility()) + '»', 'gold');
}
function clashInput() {
	const fr = game.fr;
	if (!fr || !fr.cl.on) return;
	const ms = Math.round(((game.renderTick - fr.cl.mk) * 1000) / TPS);
	send(CMD.ClashInput, 0, null, 0, ms);
	game.clashPressed = fr.cl.ex;
	$('clashBtn').classList.remove('hot');
}
function setAllAuto(on) {
	game.allAuto = on;
	send(CMD.SetAuto, 0, null, 0, on ? 1 : 0);
	game.st.titans.forEach((t) => { if (t.side === 0) send(CMD.SetAuto, t.id, null, 0, on ? 1 : 0); });
	toast(on ? 'Автобой: армией командует ИИ' : 'Автобой выключен', 'gold');
}
function abilName(A) { return A ? (RU.abil[A.id] || A.name) : ''; }
function abilFull(A) { return A ? (RU.abilFull[A.id] || RU.abil[A.id] || A.name) : ''; }

function worldTap(p, double, right) {
	const fr = game.fr;
	if (!fr || !p) return;
	if (game.targeting >= 10) { castSpell(game.targeting - 10, p); return; }
	if (game.targeting >= 1) { castAbility(game.targeting - 1, p); return; }
	const enemy = pickUnit(p, true);
	const own = pickUnit(p, false);
	const sel = game.selected;
	if (fr.phase === PH.Deploy && !right) {
		if (own !== -1) select(own);
		else if (sel > 0) { send(CMD.DeployMove, sel, p); spawnFx('order', p.x, p.z, 0, 0.5, 1, 5); }
		return;
	}
	const order = () => {
		if (enemy !== -1) { send(double ? CMD.Charge : CMD.Attack, sel, p, enemy); spawnFx('attack', p.x, p.z, 0, 0.5, 1, 6); }
		else { send(double ? CMD.Charge : CMD.Move, sel, p); spawnFx('order', p.x, p.z, 0, 0.5, 1, 5); }
		if (double) toast('Атака с разгона!', 'own');
	};
	if (right) { if (sel !== -1) order(); return; }
	if (own !== -1 && enemy === -1) { select(own); return; }
	if (sel !== -1) { order(); return; }
	if (own !== -1) select(own);
}

function pickUnit(p, enemy) {
	if (!game.sim || !p) return -1;
	return game.sim.pick(p.x, p.z, enemy, 5.5);
}

// Path smoothing as in the Unreal controller: Chaikin x2, Ramer-Douglas-Peucker 0.75 m, max 24 points.
function chaikin(pts) {
	if (pts.length < 3) return pts;
	const out = [pts[0]];
	for (let i = 0; i + 1 < pts.length; i++) {
		const a = pts[i];
		const b = pts[i + 1];
		out.push({ x: a.x * 0.75 + b.x * 0.25, z: a.z * 0.75 + b.z * 0.25 }, { x: a.x * 0.25 + b.x * 0.75, z: a.z * 0.25 + b.z * 0.75 });
	}
	out.push(pts[pts.length - 1]);
	return out;
}
function rdp(pts, eps) {
	const keep = new Array(pts.length).fill(false);
	keep[0] = keep[pts.length - 1] = true;
	const rec = (a, b) => {
		if (b <= a + 1) return;
		let best = 0;
		let idx = a;
		const A = pts[a];
		const B = pts[b];
		const dx = B.x - A.x;
		const dz = B.z - A.z;
		const len = dx * dx + dz * dz;
		for (let i = a + 1; i < b; i++) {
			const P = pts[i];
			const t = len > 1e-9 ? clamp(((P.x - A.x) * dx + (P.z - A.z) * dz) / len, 0, 1) : 0;
			const d = Math.hypot(P.x - (A.x + dx * t), P.z - (A.z + dz * t));
			if (d > best) { best = d; idx = i; }
		}
		if (best > eps) { keep[idx] = true; rec(a, idx); rec(idx, b); }
	};
	rec(0, pts.length - 1);
	return pts.filter((_, i) => keep[i]);
}
function finalizePath(d) {
	if (!d || d.pts.length < 2) return;
	let pts = rdp(chaikin(chaikin(d.pts)), 0.75);
	if (pts.length > 1 && d.unit > 0) pts = pts.slice(1);
	const max = 24;
	if (pts.length > max) pts = Array.from({ length: max }, (_, i) => pts[Math.floor((i * (pts.length - 1)) / (max - 1))]);
	const end = d.pts[d.pts.length - 1];
	const enemy = pickUnit(end, true);
	game.sim.path(d.unit, end.x, end.z, enemy === -1 ? 0 : enemy, pts);
	select(d.unit);
	spawnFx(enemy === -1 ? 'order' : 'attack', end.x, end.z, 0, 0.5, 1, 5);
}

// ---------------------------------------------------------------------------------------------
// Input: one gesture engine for touch, pen and mouse
// ---------------------------------------------------------------------------------------------
const input = { pointers: new Map(), press: null, pinch: null, lastTap: { t: -10, x: 0, y: 0 }, draw: null, hover: null, hoverEnemy: -1, banners: [], longTimer: 0, deployGhost: null };

function hitBanner(x, y) {
	for (let i = input.banners.length - 1; i >= 0; i--) {
		const b = input.banners[i];
		if (x >= b.x0 - 6 && x <= b.x1 + 6 && y >= b.y0 - 6 && y <= b.y1 + 6) return b;
	}
	return null;
}

function cancelLongPress() { if (input.longTimer) { clearTimeout(input.longTimer); input.longTimer = 0; } }

function onPointerDown(e) {
	audioInit();
	const target = e.target;
	const card = target.closest ? target.closest('[data-unit]') : null;
	const spell = target.closest ? target.closest('[data-spell]') : null;
	const onField = target === ovCanvas;
	if (!onField && !card && !spell) return;
	if (target.closest && target.closest('[data-ab]')) return;
	if (!game.fr) return;
	e.preventDefault();
	input.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
	try { target.setPointerCapture(e.pointerId); } catch (err) { /* not capturable */ }
	if (onField && input.pointers.size === 2) {
		cancelLongPress();
		input.press = null;
		input.draw = null;
		const [p1, p2] = [...input.pointers.values()];
		const mx = (p1.x + p2.x) / 2;
		const my = (p1.y + p2.y) / 2;
		const anchor = planeAt(mx, my, game.sim.height(cam.x, -cam.z));
		input.pinch = { d0: Math.hypot(p1.x - p2.x, p1.y - p2.y) || 1, dist0: cam.dist, anchor };
		return;
	}
	if (input.pointers.size > 1) return;
	const press = { id: e.pointerId, x0: e.clientX, y0: e.clientY, x: e.clientX, y: e.clientY, t0: now(), right: e.button === 2, unit: -1, spell: -1,
		kind: spell ? 'spell' : card ? 'card' : 'field', moved: false, mode: null, long: false, el: card || spell };
	if (spell) press.spell = +spell.dataset.spell;
	else if (card) press.unit = +card.dataset.unit;
	else {
		if (radialOpen()) { closeRadial(); press.kind = 'swallow'; input.press = press; return; }
		const b = hitBanner(e.clientX, e.clientY);
		if (b && b.side === 0 && !game.targeting) press.unit = b.unit;
		else if (!game.targeting && !press.right) {
			const p = groundAt(e.clientX, e.clientY);
			press.unit = pickUnit(p, false);
		}
		if (press.right && game.selected !== -1) press.unit = game.selected;
	}
	input.press = press;
	if (press.unit > 0 && press.kind !== 'spell' && !press.right) {
		input.longTimer = setTimeout(() => {
			input.longTimer = 0;
			if (input.press === press && !press.moved) {
				press.long = true;
				select(press.unit);
				openRadial(press.unit, press.x0, press.y0);
				if (navigator.vibrate) { try { navigator.vibrate(12); } catch (err) { /* ignore */ } }
			}
		}, 320);
	}
}

function onPointerMove(e) {
	if (e.pointerType === 'mouse' && !input.press && e.target === ovCanvas) {
		input.hover = game.targeting ? groundAt(e.clientX, e.clientY) : null;
		const b = hitBanner(e.clientX, e.clientY);
		input.hoverEnemy = b && b.side === 1 ? b.unit : -1;
		ovCanvas.style.cursor = game.targeting ? 'crosshair' : b ? 'pointer' : 'default';
	}
	if (!input.pointers.has(e.pointerId)) return;
	input.pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
	if (input.pinch && input.pointers.size >= 2) {
		const [p1, p2] = [...input.pointers.values()];
		const d = Math.hypot(p1.x - p2.x, p1.y - p2.y) || 1;
		cam.dist = clamp((input.pinch.dist0 * input.pinch.d0) / d, cam.minD, cam.maxD);
		placeCamera(0);
		if (input.pinch.anchor) {
			const mx = (p1.x + p2.x) / 2;
			const my = (p1.y + p2.y) / 2;
			const p = planeAt(mx, my, input.pinch.anchor.y);
			if (p) { cam.x += input.pinch.anchor.x - p.x; cam.z -= input.pinch.anchor.z - p.z; clampCam(); placeCamera(0); }
		}
		return;
	}
	const press = input.press;
	if (!press || press.id !== e.pointerId || press.kind === 'swallow') return;
	const lastX = press.x;
	const lastY = press.y;
	press.x = e.clientX;
	press.y = e.clientY;
	if (!press.moved && Math.hypot(press.x - press.x0, press.y - press.y0) > (e.pointerType === 'mouse' ? 6 : 12) && !press.long) {
		press.moved = true;
		cancelLongPress();
		const phase = game.fr.phase;
		if (press.kind === 'spell') press.mode = 'spell';
		else if (press.unit > 0 && phase === PH.Deploy) press.mode = 'deploy';
		else if (press.unit !== -1 && !game.targeting && phase !== PH.Finished) {
			press.mode = 'draw';
			const start = unitPos(press.unit);
			input.draw = { unit: press.unit, pts: start && press.unit > 0 ? [start] : [] };
		} else if (press.kind === 'field') {
			press.mode = 'pan';
			cam.vx = cam.vz = 0;
			press.anchor = planeAt(press.x0, press.y0, game.sim.height(cam.x, -cam.z));
			press.lastT = now();
		}
	}
	if (press.mode === 'pan' && press.anchor) {
		const p = planeAt(press.x, press.y, press.anchor.y);
		if (p) {
			const dx = press.anchor.x - p.x;
			const dz = -(press.anchor.z - p.z);
			cam.x += dx;
			cam.z += dz;
			clampCam();
			placeCamera(0);
			const t = now();
			const dt = Math.max(1 / 120, t - press.lastT);
			press.lastT = t;
			press.vx = lerp(press.vx || 0, dx / dt, 0.35);
			press.vz = lerp(press.vz || 0, dz / dt, 0.35);
		}
	} else if (press.mode === 'draw' && input.draw) {
		if (!overDock(press.x, press.y)) {
			const p = groundAt(press.x, press.y);
			if (p) {
				const pts = input.draw.pts;
				let len = 0;
				for (let i = 1; i < pts.length; i++) len += Math.hypot(pts[i].x - pts[i - 1].x, pts[i].z - pts[i - 1].z);
				const last = pts[pts.length - 1];
				if ((!last || Math.hypot(p.x - last.x, p.z - last.z) >= 0.6) && len < 400) pts.push(p);
			}
		}
	} else if (press.mode === 'deploy' || press.mode === 'spell') {
		input.deployGhost = overDock(press.x, press.y) ? null : groundAt(press.x, press.y);
		if (press.mode === 'spell') {
			game.targeting = 10 + press.spell;
			input.hover = input.deployGhost;
		}
	}
	void lastX; void lastY;
}

function overDock(x, y) {
	const els = [$('dock'), $('top'), $('deploy'), $('clash')];
	for (const el of els) {
		if (!el || el.hidden) continue;
		for (const child of el.children) {
			const r = child.getBoundingClientRect();
			if (r.width && x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return true;
		}
	}
	return false;
}

function onPointerUp(e) {
	const wasPinch = !!input.pinch;
	input.pointers.delete(e.pointerId);
	if (input.pointers.size < 2) input.pinch = null;
	const press = input.press;
	if (!press || press.id !== e.pointerId) { if (wasPinch) input.press = null; return; }
	input.press = null;
	cancelLongPress();
	input.deployGhost = null;
	if (press.kind === 'swallow' || press.long || e.type === 'pointercancel') { input.draw = null; return; }
	const x = e.clientX;
	const y = e.clientY;
	if (press.kind === 'spell') {
		if (press.moved) {
			const p = overDock(x, y) ? null : groundAt(x, y);
			if (p) castSpell(press.spell, p);
			else game.targeting = 0;
			input.hover = null;
		} else pressSpell(press.spell);
		return;
	}
	if (press.mode === 'draw') { finalizePath(input.draw); input.draw = null; return; }
	if (press.mode === 'deploy') {
		const p = groundAt(x, y);
		if (p) { send(CMD.DeployMove, press.unit, p); select(press.unit); }
		return;
	}
	if (press.mode === 'pan') {
		const idle = now() - (press.lastT || 0) > 0.08;
		cam.vx = idle ? 0 : clamp(press.vx || 0, -250, 250);
		cam.vz = idle ? 0 : clamp(press.vz || 0, -250, 250);
		return;
	}
	if (press.kind === 'card') { cardTap(press.unit); return; }
	const t = now();
	const dbl = t - input.lastTap.t < 0.32 && Math.hypot(x - input.lastTap.x, y - input.lastTap.y) < 40;
	const b = hitBanner(x, y);
	if (b && !game.targeting && !press.right) {
		if (b.side === 0) {
			if (game.selected === b.unit && dbl) focusUnit(b.unit);
			select(b.unit);
		} else if (game.selected !== -1 && game.fr.phase !== PH.Deploy) {
			const p = unitPos(b.unit);
			send(dbl ? CMD.Charge : CMD.Attack, game.selected, p, b.unit);
			if (p) spawnFx('attack', p.x, p.z, 0, 0.5, 1, 6);
			if (dbl) toast('Атака с разгона!', 'own');
		}
	} else {
		worldTap(groundAt(x, y), dbl, press.right);
	}
	input.lastTap = dbl ? { t: -10, x: 0, y: 0 } : { t, x, y };
}

function cardTap(unit) {
	if (unit === game.selected && unit !== 0) focusUnit(unit);
	select(unit);
}
function focusUnit(unit) {
	const p = unitPos(unit);
	if (p) { cam.x = p.x; cam.z = -p.z - 12; cam.vx = cam.vz = 0; }
}

const gameEl = $('game');
gameEl.addEventListener('pointerdown', onPointerDown);
window.addEventListener('pointermove', onPointerMove, { passive: true });
window.addEventListener('pointerup', onPointerUp);
window.addEventListener('pointercancel', onPointerUp);
ovCanvas.addEventListener('contextmenu', (e) => e.preventDefault());
ovCanvas.addEventListener('wheel', (e) => {
	e.preventDefault();
	const before = planeAt(e.clientX, e.clientY, game.sim ? game.sim.height(cam.x, -cam.z) : 0);
	cam.dist = clamp(cam.dist * (e.deltaY > 0 ? 1.12 : 0.89), cam.minD, cam.maxD);
	placeCamera(0);
	const after = planeAt(e.clientX, e.clientY, before ? before.y : 0);
	if (before && after) { cam.x += before.x - after.x; cam.z -= before.z - after.z; clampCam(); }
}, { passive: false });
document.addEventListener('gesturestart', (e) => e.preventDefault());
document.addEventListener('dblclick', (e) => e.preventDefault());

const keys = new Set();
window.addEventListener('keydown', (e) => {
	if (app.mode !== 'battle' || !game.fr || e.target.tagName === 'INPUT') return;
	keys.add(e.code);
	const fr = game.fr;
	const st = game.st;
	const own = st.squads.filter((s) => s.side === 0 && !s.sum);
	const digit = /^Digit([1-6])$/.exec(e.code);
	if (digit) { const s = own[+digit[1] - 1]; if (s && unitAlive(s.id)) cardTap(s.id); return; }
	switch (e.code) {
	case 'Backquote': select(0); break;
	case 'KeyT': { const ti = ownTitanIndex(); if (ti >= 0) cardTap(TITAN_BASE + ti); break; }
	case 'KeyQ': pressAbility(0); break;
	case 'KeyE': pressAbility(1); break;
	case 'KeyZ': pressSpell(0); break;
	case 'KeyX': pressSpell(1); break;
	case 'KeyC': pressSpell(2); break;
	case 'KeyF': case 'KeyG': {
		const id = game.selected;
		if (id > 0 && id < TITAN_BASE) {
			const s = fr.sq[id - 1];
			if (e.code === 'KeyF') {
				const mask = st.troops[st.squads[id - 1].troop].forms;
				let f = s.f;
				for (let k = 0; k < 5; k++) { f = (f + 1) % 5; if (mask & (1 << f)) break; }
				send(CMD.SetFormation, id, null, 0, f);
				toast('Строй: ' + RU.formation[f], 'own');
			} else {
				const v = (s.st + 1) % 3;
				send(CMD.SetStance, id, null, 0, v);
				toast('Стойка: ' + RU.stance[v], 'own');
			}
		}
		break;
	}
	case 'KeyH': if (game.selected !== -1) send(CMD.Halt, game.selected); break;
	case 'KeyR': if (game.selected !== -1) send(CMD.Retreat, game.selected); break;
	case 'Space': e.preventDefault(); if (fr.cl.on) clashInput(); else togglePause(); break;
	case 'Tab': e.preventDefault(); cycleSpeed(); break;
	case 'Enter': if (fr.phase === PH.Deploy) send(CMD.StartBattle); break;
	case 'Escape': game.targeting = 0; closeRadial(); select(-1); break;
	case 'F5': e.preventDefault(); startBattle(false); break;
	case 'F6': e.preventDefault(); startBattle(true); break;
	default: break;
	}
});
window.addEventListener('keyup', (e) => keys.delete(e.code));
function keyboardPan(dt) {
	let dx = 0;
	let dz = 0;
	if (keys.has('KeyW') || keys.has('ArrowUp')) dz -= 1;
	if (keys.has('KeyS') || keys.has('ArrowDown')) dz += 1;
	if (keys.has('KeyD') || keys.has('ArrowRight')) dx += 1;
	if (keys.has('KeyA') || keys.has('ArrowLeft')) dx -= 1;
	if (dx || dz) { const v = cam.dist * 1.1 * dt; cam.x += dx * v; cam.z += dz * v; }
}
document.addEventListener('visibilitychange', () => { if (document.hidden) lastT = performance.now(); });

// ---------------------------------------------------------------------------------------------
// Radial command menu
// ---------------------------------------------------------------------------------------------
const radialEl = $('radial');
function radialOpen() { return radialEl.childElementCount > 0; }
function closeRadial() { radialEl.innerHTML = ''; }
function openRadial(unit, x, y) {
	closeRadial();
	const st = game.st;
	const fr = game.fr;
	const items = [];
	if (unit >= TITAN_BASE) {
		const T = fr.ti[unit - TITAN_BASE];
		items.push({ ring: 0, label: 'Стоп', fn: () => send(CMD.Halt, unit) });
		items.push({ ring: 0, label: 'Отступить', fn: () => send(CMD.Retreat, unit) });
		items.push({ ring: 0, label: T.au ? 'Авто: вкл' : 'Авто', on: !!T.au, fn: () => send(CMD.SetAuto, unit, null, 0, T.au ? 0 : 1) });
		items.push({ ring: 0, label: settings.follow ? 'Камера: за ним' : 'Следить', on: settings.follow, fn: () => { settings.follow = !settings.follow; } });
	} else if (unit > 0) {
		const s = fr.sq[unit - 1];
		const mask = st.troops[st.squads[unit - 1].troop].forms;
		RU.formation.forEach((name, f) => { if (mask & (1 << f)) items.push({ ring: 0, label: name, on: s.f === f, fn: () => send(CMD.SetFormation, unit, null, 0, f) }); });
		RU.stance.forEach((name, v) => items.push({ ring: 1, label: name, on: s.st === v, fn: () => send(CMD.SetStance, unit, null, 0, v) }));
		items.push({ ring: 1, label: 'Стоп', fn: () => send(CMD.Halt, unit) });
		items.push({ ring: 1, label: 'Отступить', fn: () => send(CMD.Retreat, unit) });
		items.push({ ring: 1, label: s.au ? 'Авто: вкл' : 'Авто', on: !!s.au, fn: () => send(CMD.SetAuto, unit, null, 0, s.au ? 0 : 1) });
	}
	const rings = [items.filter((i) => i.ring === 0), items.filter((i) => i.ring === 1)];
	const radii = [rings[1].length ? 74 : 86, 140];
	const maxR = rings[1].length ? 140 : 86;
	const cx = clamp(x, maxR + 40, viewW - maxR - 40);
	const cy = clamp(y, maxR + 30, viewH - maxR - 30);
	radialEl.style.left = cx + 'px';
	radialEl.style.top = cy + 'px';
	rings.forEach((list, r) => {
		list.forEach((it, k) => {
			const a = -Math.PI / 2 + (k / list.length) * Math.PI * 2 + (r ? Math.PI / list.length : 0);
			const b = document.createElement('button');
			b.textContent = it.label;
			b.style.left = Math.cos(a) * radii[r] + 'px';
			b.style.top = Math.sin(a) * radii[r] + 'px';
			b.style.animationDelay = (k * 0.015) + 's';
			if (it.on) b.classList.add('on');
			b.addEventListener('click', (ev) => { ev.stopPropagation(); it.fn(); closeRadial(); });
			radialEl.appendChild(b);
		});
	});
}

// ---------------------------------------------------------------------------------------------
// Overlay (banners, bars, paths, clash ring)
// ---------------------------------------------------------------------------------------------
function rrect(x, y, w, h, r) {
	ov.beginPath();
	ov.moveTo(x + r, y);
	ov.arcTo(x + w, y, x + w, y + h, r);
	ov.arcTo(x + w, y + h, x, y + h, r);
	ov.arcTo(x, y + h, x, y, r);
	ov.arcTo(x, y, x + w, y, r);
	ov.closePath();
}
function drawPathLine(points, color, width, dash) {
	if (points.length < 2) return;
	ov.save();
	ov.strokeStyle = color;
	ov.lineWidth = width;
	ov.lineJoin = 'round';
	ov.lineCap = 'round';
	if (dash) ov.setLineDash(dash);
	ov.beginPath();
	let started = false;
	for (const p of points) {
		const s = toScreen(p.x, game.sim.height(p.x, p.z) + 0.6, p.z);
		if (!s) { started = false; continue; }
		if (!started) { ov.moveTo(s.x, s.y); started = true; } else ov.lineTo(s.x, s.y);
	}
	ov.stroke();
	ov.restore();
}

function drawOverlay(rt) {
	const st = game.st;
	const fr = game.fr;
	ov.setTransform(ovScale, 0, 0, ovScale, 0, 0);
	ov.clearRect(0, 0, viewW, viewH);
	input.banners = [];
	const small = Math.min(viewW, viewH) < 500;
	const font = small ? 11 : 12;
	ov.font = '600 ' + font + 'px "Fira Sans Condensed", "Roboto Condensed", sans-serif-condensed, "Arial Narrow", sans-serif';
	ov.textBaseline = 'middle';
	const sel = game.selected;

	// Orders of selected own units.
	if (fr.phase !== PH.Finished) {
		for (let i = 0; i < st.squads.length; i++) {
			const s = fr.sq[i];
			const sq = st.squads[i];
			if (sq.side !== 0 || !s.act || s.o === ORDER.None) continue;
			const chosen = sel === 0 || sel === i + 1;
			if (!chosen) continue;
			const pts = [{ x: s.c[0], z: s.c[1] }];
			if (s.p) for (const w of s.p) pts.push({ x: w[0], z: w[1] });
			if (s.o === ORDER.Attack && s.ot > 0) {
				const tp = unitPos(s.ot);
				if (tp) { drawPathLine([pts[0], tp], 'rgba(255, 140, 120, 0.9)', 2.5, [8, 6]); continue; }
			}
			if (pts.length < 2 && s.op) pts.push({ x: s.op[0], z: s.op[1] });
			drawPathLine(pts, 'rgba(160, 196, 255, 0.85)', 2.5, [8, 6]);
		}
		const ti = ownTitanIndex();
		if (ti >= 0 && sel === TITAN_BASE + ti) {
			const T = fr.ti[ti];
			if (T.o !== ORDER.None && T.op) {
				const pts = [{ x: game.curT[ti * 4], z: game.curT[ti * 4 + 1] }];
				if (T.p) for (const w of T.p) pts.push({ x: w[0], z: w[1] });
				if (pts.length < 2) pts.push({ x: T.op[0], z: T.op[1] });
				drawPathLine(pts, 'rgba(242, 193, 78, 0.85)', 3, [10, 6]);
			}
		}
	}
	if (input.draw && input.draw.pts.length > 1) {
		drawPathLine(input.draw.pts, 'rgba(0, 0, 0, 0.35)', 7);
		drawPathLine(input.draw.pts, '#f2c14e', 4);
	}
	if (input.deployGhost && input.press && input.press.mode === 'deploy') {
		const g = input.deployGhost;
		const s = toScreen(g.x, g.y + 0.5, g.z);
		if (s) { ov.strokeStyle = '#9cc0ff'; ov.lineWidth = 2; ov.beginPath(); ov.arc(s.x, s.y, 18, 0, Math.PI * 2); ov.stroke(); }
	}

	// Titan bars are placed first so squad banners move out of their way.
	const placed = [];
	const titanBars = [];
	for (let i = 0; i < st.titans.length; i++) {
		const t = st.titans[i];
		if (fr.ti[i].dead) continue;
		const al = clamp(game.acc * TPS, 0, 1);
		const scr = toScreen(lerp(game.prevT[i * 4], game.curT[i * 4], al), game.curT[i * 4 + 2] + t.h + 3, lerp(game.prevT[i * 4 + 1], game.curT[i * 4 + 1], al));
		if (!scr) continue;
		const w = small ? 96 : 130;
		const rect = { x0: scr.x - w / 2 - 4, y0: scr.y - 38, x1: scr.x + w / 2 + 4, y1: scr.y - 38 + (t.side === 0 ? 32 : 28) };
		placed.push(rect);
		titanBars.push({ i, scr, w, rect });
	}
	const compact = small || cam.dist > 135;
	const banners = [];
	for (let i = 0; i < st.squads.length; i++) {
		const s = fr.sq[i];
		const sq = st.squads[i];
		if (!s.act || (sq.side === 1 && !s.vis)) continue;
		const scr = toScreen(s.c[0], game.sim.height(s.c[0], s.c[1]) + 5.5, s.c[1]);
		if (!scr || scr.x < -60 || scr.x > viewW + 60 || scr.y < -30 || scr.y > viewH + 30) continue;
		const T = st.troops[sq.troop];
		const label = (compact ? RU.troopShort[T.id] : RU.troop[T.id]) + ' ' + s.a;
		const w = ov.measureText(label).width + 16;
		const h = font + 9;
		banners.push({ i, s, sq, label, w, h, x: scr.x - w / 2, y: scr.y - h });
	}
	// Simple declutter: from the bottom of the screen up, lift a banner above anything it overlaps.
	banners.sort((p, q) => q.y - p.y);
	for (const bn of banners) {
		for (let tries = 0; tries < 6; tries++) {
			const hit = placed.find((r) => bn.x < r.x1 + 2 && bn.x + bn.w > r.x0 - 2 && bn.y < r.y1 + 2 && bn.y + bn.h + 6 > r.y0 - 2);
			if (!hit) break;
			bn.y = hit.y0 - bn.h - 8;
		}
		placed.push({ x0: bn.x, y0: bn.y, x1: bn.x + bn.w, y1: bn.y + bn.h + 6 });
	}
	for (const bn of banners) {
		const { i, s, sq, label, w, h, x, y } = bn;
		const own = sq.side === 0;
		const chosen = own && (sel === 0 || sel === i + 1);
		const routing = s.ms === ROUTING;
		ov.fillStyle = chosen ? 'rgba(30, 52, 92, 0.92)' : 'rgba(17, 22, 27, 0.78)';
		rrect(x, y, w, h, 6);
		ov.fill();
		ov.lineWidth = chosen ? 2 : 1.2;
		ov.strokeStyle = routing && Math.floor(rt / 8) % 2 ? '#f2c14e' : SIDE_CSS[sq.side];
		ov.stroke();
		ov.fillStyle = '#efe7d6';
		ov.textAlign = 'center';
		ov.fillText(label, x + w / 2, y + h / 2 + 0.5);
		const mw = w - 10;
		ov.fillStyle = 'rgba(255, 255, 255, 0.16)';
		ov.fillRect(x + 5, y + h + 2, mw, 3);
		ov.fillStyle = routing ? '#f2c14e' : s.m < 40 ? '#ff9b6b' : '#5fd38d';
		ov.fillRect(x + 5, y + h + 2, mw * clamp(s.m / 100, 0, 1), 3);
		if (s.au && own) { ov.fillStyle = '#f2c14e'; ov.fillText('A', x - 7, y + h / 2); }
		input.banners.push({ x0: x, y0: y, x1: x + w, y1: y + h + 6, unit: sq.id, side: sq.side });
	}

	// Titan bars.
	for (const tb of titanBars) {
		const { i, scr, w } = tb;
		const t = st.titans[i];
		const T = fr.ti[i];
		const bx = scr.x - w / 2;
		const by = scr.y - 22;
		ov.fillStyle = 'rgba(17, 22, 27, 0.8)';
		rrect(bx - 4, by - 16, w + 8, t.side === 0 ? 32 : 28, 7);
		ov.fill();
		ov.strokeStyle = SIDE_CSS[t.side];
		ov.lineWidth = sel === t.id ? 2 : 1;
		ov.stroke();
		ov.fillStyle = '#efe7d6';
		ov.textAlign = 'left';
		const chains = T.ch || 0;
		ov.fillText((RU.titan[t.def] || t.name) + '  ур. ' + t.lvl + (chains ? '  ⛓' + chains : ''), bx, by - 6);
		ov.fillStyle = 'rgba(255, 255, 255, 0.15)';
		ov.fillRect(bx, by + 2, w, 6);
		ov.fillStyle = t.side === 0 ? '#5fd38d' : '#e5533d';
		ov.fillRect(bx, by + 2, w * clamp(T.hp / t.mhp, 0, 1), 6);
		if (t.side === 0) {
			ov.fillStyle = 'rgba(255, 122, 47, 0.25)';
			ov.fillRect(bx, by + 10, w, 3);
			ov.fillStyle = '#ff7a2f';
			ov.fillRect(bx, by + 10, w * clamp(T.rage / 100, 0, 1), 3);
		}
		if (T.stg > fr.tick) {
			ov.fillStyle = '#f2c14e';
			ov.textAlign = 'center';
			ov.fillText('Оглушён', scr.x, by - 24);
		}
		input.banners.push({ x0: bx - 4, y0: by - 16, x1: bx + w + 4, y1: by + 16, unit: t.id, side: t.side });
	}

	// Clash timing ring.
	const cl = fr.cl;
	if (cl.on) {
		const cx = viewW / 2;
		const cy = viewH * (viewH < 450 ? 0.42 : 0.4);
		const base = Math.min(viewW, viewH) * 0.08 + 16;
		const win = Math.max(1, cl.mk - cl.ws);
		const k = clamp((rt - cl.ws) / win, 0, 1.3);
		const r = base * Math.max(0.3, 3 - 2 * k);
		ov.lineWidth = 5;
		ov.strokeStyle = '#f2c14e';
		ov.beginPath(); ov.arc(cx, cy, base, 0, Math.PI * 2); ov.stroke();
		ov.lineWidth = 12;
		ov.strokeStyle = 'rgba(242, 193, 78, 0.18)';
		ov.beginPath(); ov.arc(cx, cy, base, 0, Math.PI * 2); ov.stroke();
		ov.lineWidth = 3;
		ov.strokeStyle = game.clashPressed === cl.ex ? 'rgba(239, 231, 214, 0.35)' : '#efe7d6';
		ov.beginPath(); ov.arc(cx, cy, r, 0, Math.PI * 2); ov.stroke();
		setText($('clashHint'), 'Схватка ' + (cl.ex + 1) + ' из 3 · жмите, когда кольцо сойдётся с меткой');
		$('clashBtn').classList.toggle('hot', Math.abs(k - 1) < 0.1 && game.clashPressed !== cl.ex);
	}
}

// ---------------------------------------------------------------------------------------------
// HUD (DOM)
// ---------------------------------------------------------------------------------------------
const VALOR_SVG = '<svg class="vg" viewBox="0 0 12 12" aria-hidden="true"><path d="M6 0.5L11 6 6 11.5 1 6z"/></svg>';
const STAR_SVG = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 2.6l2.8 6.1 6.6.6-5 4.4 1.5 6.5L12 16.8 6.1 20.2l1.5-6.5-5-4.4 6.6-.6z"/></svg>';
const hud = { text: new Map(), style: new Map(), squadEls: [], abilEls: [], spellEls: [], pips: [] };
function setText(el, v) { if (el && hud.text.get(el) !== v) { hud.text.set(el, v); el.textContent = v; } }
function setStyle(el, prop, v) {
	let m = hud.style.get(el);
	if (!m) { m = {}; hud.style.set(el, m); }
	if (m[prop] !== v) { m[prop] = v; el.style[prop] = v; }
}
function setClass(el, cls, on) { if (el.classList.contains(cls) !== !!on) el.classList.toggle(cls, !!on); }

function buildHud(st) {
	hud.text = new Map();
	hud.style = new Map();
	const squads = $('squads');
	squads.innerHTML = '';
	const all = document.createElement('button');
	all.className = 'card all';
	all.dataset.unit = '0';
	all.textContent = 'Все';
	all.setAttribute('aria-label', 'Все отряды');
	squads.appendChild(all);
	hud.allEl = all;
	hud.squadEls = [];
	st.squads.forEach((sq, i) => {
		if (sq.side !== 0) return;
		const T = st.troops[sq.troop];
		const b = document.createElement('button');
		b.className = 'card sq';
		b.dataset.unit = String(sq.id);
		b.innerHTML = '<span class="top">' + DotIcons.svg(T.id) + '<span class="nm"></span></span><span class="row2"><span class="ct"></span><span class="tag"></span></span><span class="mor"><i></i></span>';
		b.querySelector('.nm').textContent = RU.troopShort[T.id] || T.name;
		b.title = (RU.troop[T.id] || T.name) + ', ряд ' + sq.tier;
		b.setAttribute('aria-label', (RU.troop[T.id] || T.name) + ', ряд ' + sq.tier);
		squads.appendChild(b);
		hud.squadEls.push({ el: b, i, nm: b.querySelector('.nm'), tag: b.querySelector('.tag'), ct: b.querySelector('.ct'), mor: b.querySelector('.mor i'), start: sq.start, tier: sq.tier });
	});
	const tc = $('titanCard');
	const ti = ownTitanIndex();
	tc.hidden = ti < 0;
	hud.abilEls = [];
	if (ti >= 0) {
		const t = st.titans[ti];
		tc.dataset.unit = String(t.id);
		$('tName').textContent = RU.titan[t.def] || t.name;
		$('tLv').textContent = 'ур. ' + t.lvl + ' ' + '★'.repeat(t.stars);
		const ab = $('abil');
		ab.innerHTML = '';
		t.ab.forEach((ai, slot) => {
			const A = st.abil[ai];
			const b = document.createElement('button');
			b.className = 'ab';
			b.dataset.ab = String(slot);
			b.innerHTML = '<span class="lbl"></span><span class="cd" hidden></span>';
			b.querySelector('.lbl').innerHTML = '<kbd>' + (slot === 0 ? 'Q' : 'E') + '</kbd>';
			b.querySelector('.lbl').append(abilName(A));
			b.title = abilFull(A) + ': ' + A.rage + ' ярости';
			b.setAttribute('aria-label', b.title);
			b.addEventListener('click', (e) => { e.stopPropagation(); pressAbility(slot); });
			ab.appendChild(b);
			hud.abilEls.push({ el: b, cd: b.querySelector('.cd'), A });
		});
	}
	const spells = $('spells');
	spells.innerHTML = '';
	hud.spellEls = [];
	st.sides[0].spells.forEach((ai, slot) => {
		const A = st.abil[ai];
		const b = document.createElement('button');
		b.className = 'card sp';
		b.dataset.spell = String(slot);
		b.innerHTML = '<span class="top">' + DotIcons.svg(A.id) + '<span class="nm"></span></span><span class="cost">' + VALOR_SVG + '<span></span><b class="ch" hidden></b><kbd></kbd></span><span class="cd" hidden></span>';
		b.querySelector('.nm').textContent = abilName(A);
		b.querySelector('.cost span').textContent = String(A.valor);
		b.querySelector('.cost kbd').textContent = ['Z', 'X', 'C'][slot] || '';
		b.title = abilFull(A) + ': ' + A.valor + ' доблести';
		b.setAttribute('aria-label', b.title);
		spells.appendChild(b);
		hud.spellEls.push({ el: b, cd: b.querySelector('.cd'), ch: b.querySelector('.ch'), A });
	});
	$('spellBox').hidden = st.sides[0].spells.length === 0;
	const pips = $('valorPips');
	pips.innerHTML = '';
	hud.pips = [];
	const vmax = Math.round(st.rules.valorMax) || 10;
	pips.style.gridTemplateColumns = 'repeat(' + vmax + ', 1fr)';
	for (let k = 0; k < vmax; k++) { const i = document.createElement('i'); const bb = document.createElement('b'); i.appendChild(bb); pips.appendChild(i); hud.pips.push(bb); }
	$('stars').innerHTML = STAR_SVG.repeat(3);
	$('replayTag').hidden = true;
}

/** Russian plural form: plural(5, 'хэш', 'хэша', 'хэшей'). */
function plural(n, one, few, many) {
	const m10 = n % 10;
	const m100 = n % 100;
	if (m10 === 1 && m100 !== 11) return one;
	if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) return few;
	return many;
}

function fmtTime(ticks) {
	const s = Math.max(0, Math.ceil(ticks / TPS));
	return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
}

function updateHud(rt) {
	const st = game.st;
	const fr = game.fr;
	const sel = game.selected;
	const tick = fr.tick;
	setText($('timer'), fr.phase === PH.Deploy ? 'Расст.' : fr.phase === PH.Countdown ? String(Math.ceil(fr.countdown / TPS)) : fmtTime(fr.timer));
	const stars = $('stars').children;
	for (let k = 0; k < 3; k++) if (stars[k]) setClass(stars[k], 'lit', fr.obj[k]);
	const ownPct = st.sides[0].power ? Math.round((fr.sides[0].power / st.sides[0].power) * 100) : 0;
	const enPct = st.sides[1].power ? Math.round((fr.sides[1].power / st.sides[1].power) * 100) : 0;
	setText($('ownPct'), ownPct + '%');
	setText($('enPct'), enPct + '%');
	setStyle($('ownBar').firstElementChild, 'width', ownPct + '%');
	setStyle($('enBar').firstElementChild, 'width', enPct + '%');
	setText($('btnSpeed'), game.speed + '×');
	setClass($('btnSpeed'), 'on', game.speed > 1);
	setClass($('btnPause'), 'on', game.paused);
	$('replayTag').hidden = !fr.replay;

	setClass(hud.allEl, 'sel', sel === 0);
	for (const s of hud.squadEls) {
		const q = fr.sq[s.i];
		setClass(s.el, 'sel', sel === s.i + 1 || (sel === 0 && q.act));
		setClass(s.el, 'dead', !q.act);
		setText(s.ct, q.a + '/' + s.start);
		setStyle(s.mor, 'width', clamp(q.m, 0, 100) + '%');
		setStyle(s.mor, 'background', q.ms === ROUTING ? 'var(--valor)' : q.m < 40 ? '#ff9b6b' : 'var(--good)');
		const tag = !q.act ? 'Разбит' : q.ms === ROUTING ? 'Бегут' : q.au ? 'Авто' : q.chg ? 'Разгон' : q.mel ? 'Бой' : '';
		setText(s.tag, tag);
		setClass(s.tag, 'bad', q.ms === ROUTING || !q.act);
		const state = !q.act ? '' : q.ms === ROUTING ? 'rout' : q.chg ? 'charge' : q.mel ? 'melee' : q.au ? 'auto' : '';
		if (s.state !== state) { s.state = state; if (state) s.el.dataset.state = state; else delete s.el.dataset.state; }
	}
	const ti = ownTitanIndex();
	if (ti >= 0) {
		const t = st.titans[ti];
		const T = fr.ti[ti];
		setClass($('titanCard'), 'sel', sel === t.id);
		setClass($('titanCard'), 'dead', !!T.dead);
		setStyle($('tHp').firstElementChild, 'width', clamp((T.hp / t.mhp) * 100, 0, 100) + '%');
		setStyle($('tRage').firstElementChild, 'width', clamp(T.rage, 0, 100) + '%');
		hud.abilEls.forEach((a, slot) => {
			const cd = Math.max(0, T.ar[slot] - tick);
			const ready = !T.dead && cd === 0 && T.rage >= a.A.rage && fr.phase === PH.Battle;
			setClass(a.el, 'ready', ready);
			setClass(a.el, 'arm', game.targeting === 1 + slot);
			a.cd.hidden = !(cd > 0 && !T.dead);
			if (cd > 0) setText(a.cd, (cd / TPS).toFixed(cd > TPS * 10 ? 0 : 1) + ' с');
		});
	}
	const valor = fr.sides[0].valor;
	setText($('valorNum'), String(Math.floor(valor)));
	hud.pips.forEach((b, k) => setStyle(b, 'transform', 'scaleX(' + clamp(valor - k, 0, 1).toFixed(2) + ')'));
	hud.spellEls.forEach((s, slot) => {
		const cd = Math.max(0, (fr.sides[0].ready[slot] || 0) - tick);
		// Charges come from the kingdom's spell stock (-1 = no limit in training battles).
		const ch = fr.sides[0].ch ? fr.sides[0].ch[slot] : -1;
		s.ch.hidden = ch < 0;
		if (ch >= 0) setText(s.ch, '×' + ch);
		const ok = cd === 0 && valor >= s.A.valor && ch !== 0 && fr.phase === PH.Battle && !fr.replay;
		setClass(s.el, 'off', !ok);
		setClass(s.el, 'arm', game.targeting === 10 + slot);
		s.cd.hidden = cd === 0;
		if (cd > 0) setText(s.cd, Math.ceil(cd / TPS) + ' с');
	});
	$('deploy').hidden = fr.phase !== PH.Deploy || !!fr.replay;
	$('clash').hidden = !fr.cl.on;
	const big = $('bigMsg');
	if (fr.phase === PH.Countdown) { big.hidden = false; setText(big, String(Math.ceil(fr.countdown / TPS))); }
	else if (game.bigUntil && performance.now() < game.bigUntil) { big.hidden = false; }
	else big.hidden = true;
	if (Math.floor(rt) % 6 === 0) {
		let alive = 0;
		for (let i = 0; i < game.curS.length; i += 8) if (game.curS[i + 4] < SS.Dead) alive++;
		setText($('perf'), Math.round(game.fps) + ' FPS · симуляция ' + game.simMs.toFixed(2) + ' мс/тик · ' + alive + ' солдат · ' + game.sim.mode);
	}
}

function bigMessage(text, ms) {
	const big = $('bigMsg');
	big.textContent = text;
	hud.text.delete(big);
	big.hidden = false;
	game.bigUntil = performance.now() + ms;
}

function toast(text, kind) {
	const box = $('toasts');
	const el = document.createElement('div');
	el.className = 'toast' + (kind ? ' ' + kind : '');
	el.textContent = text;
	box.appendChild(el);
	const max = Math.min(viewW, viewH) < 500 ? 2 : 3;
	while (box.children.length > max) box.removeChild(box.firstChild);
	setTimeout(() => { if (el.parentNode) el.parentNode.removeChild(el); }, 3200);
}

function handleEvents(events) {
	const st = game.st;
	for (const ev of events) {
		const [type, tick, side, a, b, x, z, value] = ev;
		const own = side === 0;
		const titanVis = a >= TITAN_BASE ? view.titans[a - TITAN_BASE] : null;
		const t = now();
		switch (type) {
		case EV.TitanAttack: if (titanVis) { titanVis.attack = t; titanVis.combo = value; } break;
		case EV.TitanSmash: spawnFx('dust', x, z, 0, 0.6, 2, 12); shakeAt(x, z, 0.5); soundAt(x, z, 'thud'); break;
		case EV.TitanStomp: if (titanVis) titanVis.stomp = t - 0.25; spawnFx('dust', x, z, 0, 0.8, 3, 18); spawnFx('shock', x, z, 0, 0.5, 2, 14); shakeAt(x, z, 0.8); soundAt(x, z, 'boom'); break;
		case EV.TitanFootstep: spawnFx('dust', x, z, 0, 0.4, 1, 5); shakeAt(x, z, 0.12); soundAt(x, z, 'step'); break;
		case EV.TitanGrab: if (titanVis) titanVis.grab = t; break;
		case EV.TitanThrowImpact: spawnFx('dust', x, z, 0, 0.6, 2, 9); shakeAt(x, z, 0.3); soundAt(x, z, 'thud'); break;
		case EV.TitanChargeStart: if (!own) toast('Вражеский титан идёт на таран!', 'bad'); break;
		case EV.AbilityCast: if (titanVis) titanVis.cast = t; if (!own && st.abil[value]) toast('Враг: ' + abilFull(st.abil[value]), 'bad'); break;
		case EV.AbilityImpact: {
			const tint = abilityTint(st.abil[b]);
			spawnFx(tint === 'bolt' ? 'bolt' : tint, x, z, 0, 0.45, 2.5, 6, 30); spawnFx('shock', x, z, 0, 0.5, 2, 16); shakeAt(x, z, 0.45); soundAt(x, z, 'boom');
			break;
		}
		case EV.DeadRaised:
			spawnFx('necro', x, z, 0, 0.9, 2, 14); spawnFx('necroCol', x, z, 0, 1.1, 2, 7, 18); soundAt(x, z, 'thud');
			toast(own ? 'Мёртвые восстали: ' + b + ' скелетов бьются за вас' : 'Враг поднял мёртвых: ' + b + ' скелетов', own ? 'own' : 'bad');
			break;
		case EV.SpellCast: spawnFx('heal', x, z, 0, 0.6, 1, 10, 8); if (!own && st.abil[a]) toast('Враг применяет: ' + abilFull(st.abil[a]), 'bad'); break;
		case EV.SpellImpact: spawnFx('fire', x, z, 2, 0.7, 3, 13); spawnFx('dust', x, z, 0, 0.9, 2, 16); shakeAt(x, z, 0.75); soundAt(x, z, 'boom'); break;
		case EV.ClashStarted:
			spawnFx('shock', x, z, 8, 0.5, 3, 22); spawnFx('bolt', x, z, 0, 0.3, 2, 5, 14); shakeAt(x, z, 0.6); soundAt(x, z, 'clang');
			toast('Схватка титанов! Жмите «Удар!», когда кольцо сойдётся с меткой', 'gold');
			game.clashPressed = -1;
			break;
		case EV.ClashExchange: {
			spawnFx('shock', x, z, 8, 0.5, 3, 22); shakeAt(x, z, 0.6); soundAt(x, z, 'clang');
			const win = value === 0;
			toast('Удар: вы — ' + (RU.grade[a] || '?') + ', враг — ' + (RU.grade[b] || '?'), value === -1 ? '' : win ? 'gold' : 'bad');
			if (navigator.vibrate) { try { navigator.vibrate(win ? [20, 40, 20] : 40); } catch (err) { /* ignore */ } }
			break;
		}
		case EV.TitanStaggered:
			spawnFx('stagger', x, z, (titanVis ? titanVis.H : 18) + 2, 1.2, 2, 4);
			toast(own ? 'Ваш титан оглушён!' : 'Вражеский титан оглушён! Бейте всем, что есть!', own ? 'bad' : 'gold');
			break;
		case EV.TitanDied:
			spawnFx('dust', x, z, 0, 1.6, 4, 40); shakeAt(x, z, 1.6); soundAt(x, z, 'boom');
			toast(own ? 'Ваш титан пал!' : 'Вражеский титан повержен!', own ? 'bad' : 'gold');
			break;
		case EV.ChargeImpact: spawnFx('dust', x, z, 0, 0.5, 1, 6); break;
		case EV.FirebombImpact: spawnFx('fire', x, z, 0.6, 0.45, 1, 4.5); spawnFx('dust', x, z, 0, 0.5, 1, 5); soundAt(x, z, 'thud'); break;
		case EV.ChainThrown:
			game.chains.push({ squad: a - 1, titan: b - TITAN_BASE, t0: t });
			soundAt(x, z, 'clang');
			break;
		case EV.TitanBound: {
			const vis = view.titans[a - TITAN_BASE];
			spawnFx('stagger', x, z, (vis ? vis.H : 18) + 2, 1.4, 2, 5);
			spawnFx('shock', x, z, 0, 0.6, 3, 14);
			toast(own ? 'Ваш титан скован цепями!' : 'Цепеломы сковали вражеского титана! Бейте всем, что есть!', own ? 'bad' : 'gold');
			break;
		}
		case EV.SquadRouted: {
			const sq = st.squads[a - 1];
			if (sq) { const nm = RU.troopOf[st.troops[sq.troop].id] || ''; toast(own ? 'Ваши ' + nm + ' бегут!' : 'Враг бежит: ' + nm, own ? 'bad' : 'own'); }
			break;
		}
		case EV.StarEarned: toast('Звезда: ' + (RU.star[a] || ''), 'gold'); break;
		case EV.BattleStarted: bigMessage('В бой!', 1100); horn(); break;
		default: break;
		}
		void tick;
	}
}

function showResults() {
	game.resultsShown = true;
	const fr = game.fr;
	const r = fr.res;
	if (!r) return;
	const title = $('resTitle');
	title.textContent = r.win ? 'Победа' : 'Поражение';
	title.className = r.win ? 'win' : 'loss';
	$('resStars').innerHTML = STAR_SVG.repeat(3);
	[...$('resStars').children].forEach((s, k) => setClass(s, 'lit', k < r.stars));
	const stat = (v, l) => '<div class="stat"><b>' + v + '</b><span>' + l + '</span></div>';
	$('resStats').innerHTML = stat(fmtTime(r.dur), 'длительность боя') + stat(r.destr + '%', 'войска врага разбито') +
		stat(r.as + ' / ' + (r.as + r.al), 'ваших солдат выжило') + stat(r.score, 'очки боя');
	if (!game.verify) game.verify = game.sim.verify();
	const v = game.verify;
	const box = $('resVerify');
	box.className = 'verify' + (v.ok ? '' : ' bad');
	box.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="' + (v.ok ? '#5fd38d' : '#e5533d') + '" stroke-width="2.4"><path d="' + (v.ok ? 'M4 12l5 5L20 6' : 'M6 6l12 12M18 6L6 18') + '"/></svg><p class="fine"></p>';
	box.querySelector('p').innerHTML = v.ok
		? 'Бой проверен как на сервере: запись из ' + v.commands + ' ' + plural(v.commands, 'команды', 'команд', 'команд') + ' (' + Math.max(1, Math.round(v.bytes / 1024)) +
			' КБ) пересчитана заново, ' + (v.hashes === 1 ? 'контрольный хэш совпал' : 'все ' + v.hashes + ' ' + plural(v.hashes, 'контрольный хэш', 'контрольных хэша', 'контрольных хэшей') + ' совпали') +
			'. Итоговый хэш <code>' + v.hash + '</code>.'
		: 'Проверка не прошла: ' + v.error;
	const land = game.context.kind === 'land';
	$('btnResKingdom').hidden = !land && !app.kingdomReady;
	$('btnResAgain').hidden = land;
	$('btnResMenu').hidden = land;
	$('btnResHarder').hidden = land || AIS.indexOf(settings.ai) >= AIS.length - 1;
	$('btnResHarder').textContent = 'Сложнее: ' + (RU.ai[AIS[Math.min(AIS.length - 1, AIS.indexOf(settings.ai) + 1)]] || '');
	$('results').hidden = false;
}

// ---------------------------------------------------------------------------------------------
// Menu and buttons
// ---------------------------------------------------------------------------------------------
function togglePause() { game.paused = !game.paused; toast(game.paused ? 'Пауза' : 'Продолжаем', ''); }
function cycleSpeed() {
	const speeds = game.sim && game.sim.mode === 'JavaScript' ? [1, 2] : [1, 2, 4];
	game.speed = speeds[(speeds.indexOf(game.speed) + 1) % speeds.length] || 1;
}
function buildMenu() {
	const so = $('scenarioOpts');
	so.innerHTML = '';
	SCENARIOS.forEach((s, i) => {
		const b = document.createElement('button');
		b.className = 'opt' + (i === settings.scenario ? ' on' : '');
		b.innerHTML = '<b></b><span></span>';
		b.querySelector('b').textContent = s.name;
		b.querySelector('span').textContent = s.note;
		b.addEventListener('click', () => { settings.scenario = i; store.set('scenario', i); buildMenu(); });
		so.appendChild(b);
	});
	const ao = $('aiOpts');
	ao.innerHTML = '';
	AIS.forEach((id) => {
		const b = document.createElement('button');
		b.textContent = RU.ai[id];
		if (id === settings.ai) b.className = 'on';
		b.addEventListener('click', () => { settings.ai = id; store.set('ai', id); buildMenu(); });
		ao.appendChild(b);
	});
	const to = $('titanOpts');
	to.innerHTML = '';
	[''].concat(TITANS).forEach((id) => {
		const b = document.createElement('button');
		b.textContent = id ? RU.titan[id] || id : 'По сценарию';
		if (id) b.title = app.titanAbilities(id).join(', ');
		if (id === settings.titan) b.className = 'on';
		b.addEventListener('click', () => { settings.titan = id; store.set('titan', id); buildMenu(); });
		to.appendChild(b);
	});
	const qo = $('qualityOpts');
	qo.innerHTML = '';
	QUALITY.forEach(([id, label]) => {
		const b = document.createElement('button');
		b.textContent = label;
		if (id === settings.quality) b.className = 'on';
		b.addEventListener('click', () => { settings.quality = id; store.set('quality', id); applyQuality(); buildMenu(); });
		qo.appendChild(b);
	});
	const changed = game.loaded && (game.loaded.scenario !== settings.scenario || game.loaded.ai !== settings.ai || game.loaded.titan !== settings.titan);
	$('btnClose').textContent = changed ? 'Начать этот бой' : game.firstMenu ? 'Играть' : 'Продолжить';
	setClass($('btnClose'), 'primary', changed || game.firstMenu);
	const landBattle = game.context.kind === 'land' && !(game.fr && game.fr.phase === PH.Finished);
	$('btnRestart').textContent = landBattle ? 'Отступить' : 'Начать заново';
	$('btnToKingdom').hidden = !app.kingdomReady || landBattle;
	$('scenarioOpts').hidden = landBattle;
	$('aiOpts').hidden = landBattle;
	$('titanOpts').hidden = landBattle;
	$('titanOpts').previousElementSibling.hidden = landBattle;
	$('scenarioOpts').previousElementSibling.hidden = landBattle;
	$('aiOpts').previousElementSibling.hidden = landBattle;
	setClass($('tglAuto'), 'on', game.allAuto);
	setClass($('tglFollow'), 'on', settings.follow);
	setClass($('tglSound'), 'on', settings.sound);
	if (game.st) {
		$('engineInfo').innerHTML = 'Бой считает та же детерминированная C++-симуляция, что и в Unreal-проекте, собранная в ' + game.sim.mode +
			'. 30 тиков в секунду, фиксированная точка Q32.32. Контент <code>' + game.st.version + '</code>, хэш <code>' + game.st.contentHash + '</code>.';
	}
}
function openMenu() {
	if ($('menu').hidden) { game.pausedBeforeMenu = game.paused; game.paused = true; }
	buildMenu();
	$('menu').hidden = false;
}
function closeMenu() {
	if ($('menu').hidden) return;
	$('menu').hidden = true;
	game.paused = !!game.pausedBeforeMenu;
	game.firstMenu = false;
	if (game.loaded && (game.loaded.scenario !== settings.scenario || game.loaded.ai !== settings.ai || game.loaded.titan !== settings.titan)) startBattle(false);
}

$('btnMenu').addEventListener('click', openMenu);
$('btnClose').addEventListener('click', closeMenu);
$('menu').addEventListener('click', (e) => { if (e.target.id === 'menu') closeMenu(); });
$('btnRestart').addEventListener('click', () => {
	if (game.context.kind === 'land' && !(game.fr && game.fr.phase === PH.Finished)) {
		$('menu').hidden = true;
		exitToKingdom(0, true);
		return;
	}
	game.loaded = null;
	closeMenu();
	startBattle(false);
});
$('btnToKingdom').addEventListener('click', () => { $('menu').hidden = true; exitToKingdom(0, false); });
$('btnResKingdom').addEventListener('click', () => {
	const stars = game.fr && game.fr.res && !game.fr.replay ? game.fr.res.stars : 0;
	exitToKingdom(game.context.kind === 'land' ? stars : 0, false);
});
$('btnReplay').addEventListener('click', () => { const keep = game.loaded; game.loaded = null; closeMenu(); game.loaded = keep; startBattle(true); });
$('btnHelp').addEventListener('click', () => { $('help').hidden = false; });
$('btnHelpClose').addEventListener('click', () => { $('help').hidden = true; });
$('help').addEventListener('click', (e) => { if (e.target.id === 'help') $('help').hidden = true; });
$('tglAuto').addEventListener('click', () => { if (game.st) setAllAuto(!game.allAuto); buildMenu(); });
$('tglFollow').addEventListener('click', () => { settings.follow = !settings.follow; buildMenu(); });
$('tglSound').addEventListener('click', () => { settings.sound = !settings.sound; store.set('sound', settings.sound); if (settings.sound) audioInit(); buildMenu(); });
$('btnPause').addEventListener('click', togglePause);
$('btnSpeed').addEventListener('click', cycleSpeed);
$('btnFight').addEventListener('click', () => send(CMD.StartBattle));
$('btnAutoDeploy').addEventListener('click', () => { send(CMD.AutoDeploy); toast('Войска расставлены', 'own'); });
$('clashBtn').addEventListener('pointerdown', (e) => { e.preventDefault(); e.stopPropagation(); audioInit(); clashInput(); });
$('btnResAgain').addEventListener('click', () => startBattle(false));
$('btnResReplay').addEventListener('click', () => startBattle(true));
$('btnResHarder').addEventListener('click', () => {
	const i = AIS.indexOf(settings.ai);
	settings.ai = AIS[Math.min(AIS.length - 1, i + 1)];
	store.set('ai', settings.ai);
	startBattle(false);
});
$('btnResMenu').addEventListener('click', () => { $('results').hidden = true; openMenu(); });
// Keyboard-activated cards (Enter/Space on a focused card).
gameEl.addEventListener('click', (e) => {
	if (e.detail !== 0) return;
	const card = e.target.closest && e.target.closest('[data-unit]');
	const spell = e.target.closest && e.target.closest('[data-spell]');
	if (card) cardTap(+card.dataset.unit);
	else if (spell) pressSpell(+spell.dataset.spell);
});

// ---------------------------------------------------------------------------------------------
// Modes: the kingdom (start screen) and battles started from it
// ---------------------------------------------------------------------------------------------
function setMode(mode) {
	app.mode = mode;
	document.body.classList.toggle('mode-kingdom', mode === 'kingdom');
	$('results').hidden = true;
	closeRadial();
	lastT = performance.now();
}

/** Leaves the battle screen. For a land battle the kingdom applies the result (0 stars = retreat or defeat). */
function exitToKingdom(stars, retreat) {
	const ctx = game.context;
	game.context = { kind: 'training' };
	game.paused = false;
	game.firstMenu = false;
	setMode('kingdom');
	if (window.dotKingdom) window.dotKingdom.onBattleEnd(ctx, stars, retreat);
}

// defineProperties keeps the getters live (Object.assign would copy their current values).
Object.defineProperties(app, Object.getOwnPropertyDescriptors({
	setMode,
	exitToKingdom,
	toast,
	renderer,
	isTouch,
	settings,
	get sim() { return game.sim; },
	get viewW() { return viewW; },
	get viewH() { return viewH; },
	get ovScale() { return ovScale; },
	contentFiles: () => JSON.parse($('content-data').textContent),
	/** Russian names of a titan's two abilities, read from Content/Data/titans.json (whole-line comments stripped). */
	titanAbilities(id) {
		try {
			const t = JSON.parse(app.contentFiles()['titans.json'].replace(/^\s*\/\/.*$/gm, '')).titans.find((x) => x.id === id);
			return t ? t.abilities.map((a) => RU.abilFull[a] || a) : [];
		} catch (e) {
			return [];
		}
	},
	/** Starts a battle for a kingdom land; path is a scenario file already added to the simulation. */
	startLandBattle(path, ai, context) {
		setMode('battle');
		$('menu').hidden = true;
		game.speed = 1;
		game.paused = false;
		if (!startBattle(false, { path, ai, context })) { exitToKingdom(0, true); return false; }
		if (isTouch && game.fr.phase === PH.Deploy) toast('Расстановка: перетащите отряды внутри синей зоны, затем «В бой!»', '');
		return true;
	},
	openTrainingMenu() {
		setMode('battle');
		startBattle(false);
		game.firstMenu = true;
		openMenu();
	},
}));
window.dotApp = app;

// ---------------------------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------------------------
async function boot() {
	applyQuality();
	try {
		const sim = await loadSimulation();
		$('loadMsg').textContent = 'Загружаю войска, титанов и карту…';
		const content = JSON.parse($('content-data').textContent);
		for (const path of Object.keys(content)) sim.addFile(path, content[path]);
		if (!sim.loadContent()) throw new Error(sim.error());
		game.sim = sim;
		if (!startBattle(false)) throw new Error(sim.error());
		$('loading').hidden = true;
		app.kingdomReady = !!(window.dotKingdom && window.dotKingdom.init(app));
		setMode(app.kingdomReady ? 'kingdom' : 'battle');
		// Without the kingdom, start on the battle menu: it explains the screen and lets the player pick a battle.
		if (!app.kingdomReady) {
			game.firstMenu = true;
			openMenu();
		}
		$('btnClose').addEventListener('click', () => {
			if (isTouch && window.innerHeight > window.innerWidth) toast('Совет: поверните телефон горизонтально, так поле видно лучше', '');
			else if (isTouch && game.fr.phase === PH.Deploy) toast('Расстановка: перетащите отряды внутри синей зоны, затем «В бой!»', '');
		}, { once: true });
		// Debug and test hooks (used by the automated browser tests).
		window.dotDemo = { game, settings, startBattle, send, CMD, cam, camera, banners: () => input.banners, input };
	} catch (e) {
		console.error(e);
		$('loadMsg').textContent = 'Не удалось запустить симуляцию.';
		const err = $('loadErr');
		err.hidden = false;
		err.textContent = String(e && e.message ? e.message : e);
	}
	requestAnimationFrame(loop);
}
boot();
})();
