'use strict';
// Web/titans3d.js - the kingdom titans as 3D figures for the battle (Three.js), drawn after their portraits in titans.js.
// A figure is a recipe: a body from the titan's class proportions plus its own parts (helmets, beards, capes, weapons, glowing
// bits), each part a simple solid on a bone. Units are the titan's height (1 = H), ground at y = 0, the titan looks along -Z
// and its right hand is +X. The battle turns the same bones as before (body, hips, shoulders); pose() adds knees, elbows,
// head, cape, breathing and the flames, sparks and frost that the titan gives off.
//   DotTitans3D.has(id)
//   DotTitans3D.build(THREE, id, { H, team, glow, emit }) -> { root, body, hipL, hipR, shL, shR, pose(t, v), ... }
//       team: THREE.Color of the side; glow(hex) -> texture for halo sprites (optional); emit(kind, Vector3, size) (optional)
//   DotTitans3D.recipe(id) -> the titan's recipe as plain JSON (for the art export)

const DotTitans3D = (() => {
	// --- Class proportions -----------------------------------------------------------------------------------
	// S chest half-width, sh shoulder joint x, hip joint x, limb radii (thigh, shin, upper arm, forearm), fist and head size,
	// chest depth (z scale of the round torso), waist half-width.
	const BUILD = {
		Juggernaut: { S: 0.215, sh: 0.24, hip: 0.08, thigh: 0.072, shin: 0.058, arm: 0.064, fore: 0.056, fist: 0.092, head: 0.14, depth: 0.66, waist: 0.15 },
		Ravager: { S: 0.185, sh: 0.205, hip: 0.07, thigh: 0.062, shin: 0.05, arm: 0.054, fore: 0.047, fist: 0.078, head: 0.132, depth: 0.62, waist: 0.12 },
		Warden: { S: 0.2, sh: 0.222, hip: 0.075, thigh: 0.067, shin: 0.054, arm: 0.059, fore: 0.052, fist: 0.085, head: 0.138, depth: 0.64, waist: 0.135 },
		Mystic: { S: 0.14, sh: 0.152, hip: 0.06, thigh: 0.05, shin: 0.04, arm: 0.04, fore: 0.034, fist: 0.055, head: 0.112, depth: 0.6, waist: 0.085 },
		Hunter: { S: 0.135, sh: 0.148, hip: 0.058, thigh: 0.05, shin: 0.04, arm: 0.04, fore: 0.034, fist: 0.055, head: 0.112, depth: 0.6, waist: 0.085 },
	};
	const HIP_Y = 0.48;
	const SH_Y = 0.775;
	const NECK_Y = 0.835;
	const THIGH = 0.235;
	const UPPER = 0.19;
	const LOWER = 0.17;

	/** The bones of a titan and where they sit on their parent (body is the root's child at the feet). */
	function bones(b) {
		return {
			body: [null, 0, 0, 0],
			head: ['body', 0, NECK_Y, 0],
			cape: ['body', 0, SH_Y + 0.02, b.S * b.depth * 0.95],
			hipL: ['body', -b.hip, HIP_Y, 0], hipR: ['body', b.hip, HIP_Y, 0],
			knL: ['hipL', 0, -THIGH, 0], knR: ['hipR', 0, -THIGH, 0],
			shL: ['body', -b.sh, SH_Y, 0], shR: ['body', b.sh, SH_Y, 0],
			elL: ['shL', -b.arm * 0.3, -UPPER, 0], elR: ['shR', b.arm * 0.3, -UPPER, 0],
			hdL: ['elL', 0, -LOWER, 0], hdR: ['elR', 0, -LOWER, 0],
		};
	}

	// Part: { s: shape, b: bone, m: material, p: [x, y, z], d: dims, r: [rx, ry, rz] degrees, x: 1 = also mirrored to the other side }
	//   sph d [w, h, depth] ellipsoid      cap d [r0, r1, len] tapered capsule hanging down -Y from p
	//   cyl d [rTop, rBot, h, startDeg, lenDeg] (a shell when the angles are given)   box d [w, h, depth]   cone d [r, h]
	//   tor d [R, tube, arcDeg]   tube pts [[x, y, z]...] d [r0, r1]   ext pts [[x, y]...] d [depth] flat outline
	//   lathe pts [[r, y]...] d [sx, sz]   ico d [r, detail] (s [sx, sy, sz])   halo d [size] (a glow sprite)   emit k kind d [size, rate]
	/** The body every titan starts from; `o` names the material of each region, a missing name drops the region. */
	function baseBody(b, o) {
		const P = [];
		const add = (part) => { if (part.m) P.push(part); };
		const S = b.S;
		// Torso: a lathe with a broad chest and a narrower waist, flattened front to back.
		const torso = [[0.0, HIP_Y - 0.02], [b.waist * 0.9, HIP_Y - 0.01], [b.waist, HIP_Y + 0.04], [b.waist * 1.02, HIP_Y + 0.11], [S * 0.86, 0.66], [S, 0.72], [S * 0.98, 0.76],
			[S * 0.82, 0.8], [S * 0.5, 0.828], [0.0, 0.84]];
		add({ s: 'lathe', b: 'body', m: o.torso, pts: torso, d: [1, b.depth] });
		if (o.belly) add({ s: 'sph', b: 'body', m: o.belly, p: [0, 0.6, -S * b.depth * 0.35], d: [b.waist * 1.7, 0.14, S * b.depth * 1.1] });
		add({ s: 'sph', b: 'body', m: o.hips, p: [0, HIP_Y, 0], d: [b.waist * 2.1, 0.13, b.waist * 1.5] });
		add({ s: 'tor', b: 'body', m: o.belt, p: [0, HIP_Y + 0.045, 0], d: [b.waist * 1.0, 0.018, 360], r: [90, 0, 0], sc: [1, b.depth * 1.15, 1] });
		add({ s: 'box', b: 'body', m: o.buckle, p: [0, HIP_Y + 0.045, -b.waist * b.depth * 1.18 - 0.01], d: [0.045, 0.04, 0.015] });
		// Loincloth in the side's colour, front and back: a tapering cloth with a notched hem.
		const w = b.waist * 0.5;
		const cloth = (h) => [[-w, 0], [w, 0], [w * 0.78, -h], [w * 0.25, -h * 0.9], [0, -h * 1.06], [-w * 0.25, -h * 0.9], [-w * 0.78, -h]];
		add({ s: 'ext', b: 'body', m: o.loin, p: [0, HIP_Y + 0.02, -b.waist * 0.8], pts: cloth(0.17), d: [0.006], r: [-7, 0, 0] });
		add({ s: 'ext', b: 'body', m: o.loin, p: [0, HIP_Y + 0.02, b.waist * 0.78], pts: cloth(0.14), d: [0.006], r: [7, 0, 0] });
		add({ s: 'cyl', b: 'body', m: o.neck, p: [0, NECK_Y + 0.005, 0], d: [0.04, 0.05, 0.05] });
		// Head (on the head bone at the base of the neck).
		add({ s: 'sph', b: 'head', m: o.head, p: [0, 0.07, -0.005], d: [b.head * 0.88, b.head, b.head * 0.94] });
		if (o.eyes) for (const s of [-1, 1]) add({ s: 'sph', b: 'head', m: o.eyes, p: [s * 0.022, 0.078, -b.head * 0.44], d: [0.018, 0.013, 0.01] });
		// Legs: thigh, knee, shin and boot.
		for (const k of ['L', 'R']) {
			add({ s: 'cap', b: 'hip' + k, m: o.legs, p: [0, 0, 0], d: [b.thigh, b.thigh * 0.8, THIGH - 0.02] });
			add({ s: 'sph', b: 'kn' + k, m: o.knee, p: [0, 0.005, -b.shin * 0.5], d: [b.shin * 1.6, b.shin * 1.5, b.shin * 1.1] });
			add({ s: 'cap', b: 'kn' + k, m: o.shins, p: [0, 0, 0], d: [b.shin, b.shin * 0.78, 0.2] });
			const tall = 0.07 + (o.bootTall || 0);
			add({ s: 'cyl', b: 'kn' + k, m: o.boots, p: [0, -0.215 + tall / 2, 0], d: [b.shin * 0.98 + (o.bootTall || 0) * 0.1, b.shin * 1.05, tall] });
			add({ s: 'sph', b: 'kn' + k, m: o.feet || o.boots, p: [0, -0.222, -0.03], d: [b.shin * 1.9, 0.05, b.shin * 3.2] });
		}
		// Arms: pauldron, upper arm, elbow, forearm with bracer, fist.
		for (const k of ['L', 'R']) {
			const s = k === 'L' ? -1 : 1;
			if (o.pauldron) add({ s: 'sph', b: 'sh' + k, m: o.pauldron, p: [s * 0.012, 0.012, 0], d: [b.arm * 2.6, b.arm * 1.9, b.arm * 2.5], r: [0, 0, s * -18] });
			add({ s: 'sph', b: 'sh' + k, m: o.arms, p: [0, -0.01, 0], d: [b.arm * 2.1, b.arm * 2.1, b.arm * 2.0] });
			add({ s: 'cap', b: 'sh' + k, m: o.arms, p: [0, -0.01, 0], d: [b.arm, b.arm * 0.82, UPPER - 0.02], r: [0, 0, (s * Math.asin((b.arm * 0.3) / UPPER) * 180) / Math.PI] });
			add({ s: 'cap', b: 'el' + k, m: o.fore, p: [0, 0, 0], d: [b.fore, b.fore * 0.78, LOWER - 0.03] });
			if (o.bracer) add({ s: 'cyl', b: 'el' + k, m: o.bracer, p: [0, -0.1, 0], d: [b.fore * 1.12, b.fore * 1.02, 0.075] });
			add({ s: 'sph', b: 'hd' + k, m: o.hands, p: [0, -0.01, -0.005], d: [b.fist * 0.95, b.fist, b.fist * 0.95] });
		}
		return P;
	}

	// --- Materials ------------------------------------------------------------------------------------------
	// name: [hex, metalness, roughness, emissive (0..), flags] ; flags: 'flat' faceted, 'glass' see-through ice, 'cloth' two-sided
	const COMMON = {
		team: ['team', 0.05, 0.75, 0, 'cloth'], teamD: ['teamD', 0.05, 0.8], leather: ['#6a4226', 0, 0.8], wood: ['#6b4222', 0, 0.85], gold: ['#f2b23a', 0.9, 0.3, 0.12],
		ink: ['#1a120c', 0, 0.9], white: ['#f4efe6', 0, 0.7], iron: ['#5a5e66', 0.85, 0.42], steel: ['#b8c2d0', 0.9, 0.28],
	};

	// --- The titans -----------------------------------------------------------------------------------------
	const R = {};

	R['titan.thor'] = {
		cls: 'Ravager',
		mat: { skin: ['#e8b48a', 0, 0.62], mail: ['#9aa8c2', 0.85, 0.32], mailD: ['#5d6a80', 0.8, 0.4], beard: ['#c4502a', 0, 0.85], cape: ['#9b2a22', 0, 0.8, 0, 'cloth'],
			pants: ['#4a5160', 0, 0.8], boot: ['#8a5a32', 0, 0.75], wing: ['#f2f2ee', 0, 0.6], rune: ['#bfe6ff', 0, 0.4, 2.2] },
		body: { torso: 'mail', hips: 'pants', belt: 'leather', buckle: 'gold', loin: 'team', neck: 'skin', head: 'skin', eyes: 'ink', legs: 'pants', knee: 'mailD', shins: 'pants',
			boots: 'boot', bootTall: 0.07, arms: 'skin', fore: 'skin', bracer: 'mailD', hands: 'skin', pauldron: 'mail' },
		parts: [
			// Chest plates and the golden emblem.
			{ s: 'tor', b: 'body', m: 'mailD', p: [0, 0.69, 0], d: [0.178, 0.008, 360], r: [90, 0, 0], sc: [1, 0.63, 1] },
			{ s: 'tor', b: 'body', m: 'mailD', p: [0, 0.62, 0], d: [0.152, 0.008, 360], r: [90, 0, 0], sc: [1, 0.66, 1] },
			{ s: 'box', b: 'body', m: 'gold', p: [0, 0.715, -0.118], d: [0.05, 0.05, 0.02], r: [0, 0, 45] },
			// Winged helmet, red beard and moustache, braided ends.
			{ s: 'sph', b: 'head', m: 'mail', p: [0, 0.095, -0.004], d: [0.118, 0.082, 0.124] },
			{ s: 'tor', b: 'head', m: 'mailD', p: [0, 0.083, -0.004], d: [0.058, 0.007, 360], r: [90, 0, 0] },
			{ s: 'box', b: 'head', m: 'mailD', p: [0, 0.07, -0.06], d: [0.01, 0.045, 0.012] },
			{ s: 'ext', b: 'head', m: 'wing', p: [0.056, 0.1, 0.01], pts: [[0, 0], [0.03, 0.03], [0.06, 0.075], [0.065, 0.1], [0.045, 0.085], [0.04, 0.06], [0.02, 0.05], [0.005, 0.025]], d: [0.008], r: [0, -80, -10], x: 1 },
			{ s: 'cone', b: 'head', m: 'beard', p: [0, 0.005, -0.04], d: [0.05, 0.11], r: [190, 0, 0] },
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.035, -0.045], d: [0.1, 0.06, 0.06] },
			{ s: 'cap', b: 'head', m: 'beard', p: [0.018, 0.0, -0.06], d: [0.009, 0.006, 0.07], r: [12, 0, 0], x: 1 },
			{ s: 'sph', b: 'head', m: 'gold', p: [0.018, -0.075, -0.075], d: [0.014, 0.014, 0.014], x: 1 },
			{ s: 'box', b: 'head', m: 'beard', p: [0, 0.05, -0.062], d: [0.06, 0.012, 0.012] },
			// Cape over the back, held by two golden clasps.
			{ s: 'cape', b: 'cape', m: 'cape', p: [0, -0.23, -0.12], d: [0.13, 0.175, 0.46, -60, 120, 3.5, 0.016] },
			{ s: 'sph', b: 'body', m: 'gold', p: [0.13, 0.785, -0.06], d: [0.03, 0.03, 0.02], x: 1 },
			// Mjolnir in the right fist: short leather-wrapped handle, the head hanging below it with a glowing rune.
			{ s: 'cyl', b: 'hdR', m: 'leather', p: [0, -0.06, 0], d: [0.014, 0.016, 0.13] },
			{ s: 'sph', b: 'hdR', m: 'gold', p: [0, 0.02, 0], d: [0.026, 0.02, 0.026] },
			{ s: 'box', b: 'hdR', m: 'mail', p: [0, -0.15, 0], d: [0.085, 0.072, 0.15] },
			{ s: 'box', b: 'hdR', m: 'mailD', p: [0, -0.15, 0], d: [0.09, 0.05, 0.155] },
			{ s: 'box', b: 'hdR', m: 'rune', p: [0.046, -0.15, 0], d: [0.004, 0.03, 0.06] },
			{ s: 'box', b: 'hdR', m: 'rune', p: [-0.046, -0.15, 0], d: [0.004, 0.03, 0.06] },
			{ s: 'halo', b: 'hdR', c: '#9fd0ff', p: [0, -0.15, 0], d: [0.3] },
			{ s: 'emit', b: 'hdR', k: 'spark', p: [0, -0.15, 0], d: [0.5, 5] },
		],
	};

	R['titan.talos'] = {
		cls: 'Juggernaut',
		mat: { bronze: ['#c98a3a', 0.9, 0.34], bronzeD: ['#8a5520', 0.88, 0.42], crest: ['#b8402a', 0, 0.85], ichor: ['#ffa63a', 0, 0.4, 2.4], rivet: ['#f4c44f', 0.9, 0.3, 0.2] },
		body: { torso: 'bronze', hips: 'bronzeD', belt: 'bronzeD', buckle: 'rivet', loin: 'team', neck: 'bronzeD', head: 'bronze', legs: 'bronze', knee: 'bronzeD', shins: 'bronze',
			boots: 'bronzeD', bootTall: 0.05, arms: 'bronze', fore: 'bronze', bracer: 'bronzeD', hands: 'bronze', pauldron: 'bronzeD' },
		parts: [
			// Sculpted chest and abdomen plates.
			{ s: 'sph', b: 'body', m: 'bronze', p: [0.07, 0.69, -0.1], d: [0.15, 0.1, 0.06], x: 1 },
			{ s: 'box', b: 'body', m: 'bronzeD', p: [0, 0.6, -0.095], d: [0.12, 0.008, 0.02] },
			{ s: 'box', b: 'body', m: 'bronzeD', p: [0, 0.56, -0.09], d: [0.11, 0.008, 0.02] },
			{ s: 'box', b: 'body', m: 'bronzeD', p: [0, 0.6, -0.1], d: [0.008, 0.12, 0.02] },
			{ s: 'sph', b: 'shL', m: 'rivet', p: [-0.03, 0.06, -0.06], d: [0.02, 0.02, 0.02], x: 1 },
			{ s: 'sph', b: 'shL', m: 'rivet', p: [-0.075, 0.03, 0], d: [0.02, 0.02, 0.02], x: 1 },
			// Pteruges: hanging strips over the loincloth.
			{ s: 'cyl', b: 'body', m: 'bronzeD', p: [0, HIP_Y - 0.05, 0], d: [0.152, 0.17, 0.1, 110, 140] },
			{ s: 'cyl', b: 'body', m: 'bronzeD', p: [0, HIP_Y - 0.05, 0], d: [0.152, 0.17, 0.1, -70, 140] },
			// Corinthian helmet with a glowing eye slit and the tall crest.
			{ s: 'sph', b: 'head', m: 'bronze', p: [0, 0.075, -0.004], d: [0.12, 0.135, 0.128] },
			{ s: 'box', b: 'head', m: 'ichor', p: [0, 0.082, -0.063], d: [0.075, 0.01, 0.006] },
			{ s: 'box', b: 'head', m: 'bronzeD', p: [0, 0.06, -0.064], d: [0.012, 0.045, 0.008] },
			{ s: 'cyl', b: 'head', m: 'bronzeD', p: [0, 0.145, 0.0], d: [0.012, 0.02, 0.02] },
			{ s: 'ext', b: 'head', m: 'crest', p: [-0.009, 0.15, 0], pts: [[-0.08, 0], [-0.07, 0.04], [-0.035, 0.068], [0.01, 0.075], [0.05, 0.065], [0.085, 0.035], [0.1, -0.01], [0.085, -0.04], [0.05, -0.005], [0, 0.01]], d: [0.018], r: [0, -90, 0] },
			// Ichor: the single vein from neck to ankle, sealed by a bronze nail.
			{ s: 'cyl', b: 'body', m: 'ichor', p: [0.035, 0.66, -0.11], d: [0.005, 0.005, 0.32], r: [-8, 0, -6] },
			{ s: 'cyl', b: 'hipR', m: 'ichor', p: [0.01, -0.11, -0.065], d: [0.005, 0.005, 0.2], r: [-6, 0, 0] },
			{ s: 'cyl', b: 'knR', m: 'ichor', p: [0.0, -0.1, -0.05], d: [0.005, 0.005, 0.18] },
			{ s: 'sph', b: 'knR', m: 'rivet', p: [0, -0.19, -0.045], d: [0.022, 0.022, 0.022] },
			{ s: 'halo', b: 'head', c: '#ffa63a', p: [0, 0.082, -0.07], d: [0.14] },
			{ s: 'emit', b: 'knR', k: 'ember', p: [0, -0.19, -0.05], d: [0.35, 2] },
		],
	};

	R['titan.minotaur'] = {
		cls: 'Ravager',
		mat: { fur: ['#7a5238', 0, 0.95], furD: ['#4e3222', 0, 0.95], muzzle: ['#9a6c4c', 0, 0.85], horn: ['#efe2c4', 0, 0.55], hoof: ['#2e2018', 0, 0.7],
			strap: ['#5a3a24', 0, 0.8], eye: ['#ff5a3a', 0, 0.4, 2.5], blade: ['#c9d1da', 0.9, 0.25] },
		body: { torso: 'fur', belly: 'muzzle', hips: 'furD', belt: 'strap', buckle: 'gold', loin: 'team', neck: 'fur', head: 'fur', eyes: 'eye', legs: 'fur', knee: 'furD', shins: 'furD',
			boots: 'hoof', bootTall: 0.0, feet: 'hoof', arms: 'fur', fore: 'fur', bracer: 'strap', hands: 'furD' },
		parts: [
			// Thick neck hump and leather harness across the chest.
			{ s: 'sph', b: 'body', m: 'furD', p: [0, 0.805, 0.03], d: [0.22, 0.11, 0.16] },
			{ s: 'box', b: 'body', m: 'strap', p: [0, 0.7, -0.112], d: [0.032, 0.28, 0.012], r: [0, 0, 38] },
			{ s: 'box', b: 'body', m: 'strap', p: [0, 0.7, 0.112], d: [0.032, 0.28, 0.012], r: [0, 0, -38] },
			{ s: 'sph', b: 'body', m: 'gold', p: [0, 0.7, -0.118], d: [0.03, 0.03, 0.015] },
			// Bull head: long snout, nose ring, ears and the great curved horns.
			{ s: 'sph', b: 'head', m: 'furD', p: [0, 0.08, 0.0], d: [0.12, 0.11, 0.12] },
			{ s: 'sph', b: 'head', m: 'muzzle', p: [0, 0.045, -0.06], d: [0.085, 0.07, 0.085] },
			{ s: 'sph', b: 'head', m: 'ink', p: [0.017, 0.045, -0.1], d: [0.012, 0.012, 0.008], x: 1 },
			{ s: 'tor', b: 'head', m: 'gold', p: [0, 0.022, -0.1], d: [0.016, 0.004, 360] },
			{ s: 'cone', b: 'head', m: 'fur', p: [0.065, 0.09, 0.01], d: [0.016, 0.045], r: [0, 0, -100], x: 1 },
			{ s: 'tube', b: 'head', m: 'horn', pts: [[0.045, 0.115, -0.005], [0.1, 0.13, -0.02], [0.14, 0.17, -0.04], [0.15, 0.22, -0.065]], d: [0.022, 0.004], x: 1 },
			// Great axe held low and forward, blade towards the enemy.
			{ s: 'cyl', b: 'hdR', m: 'wood', p: [0, -0.169, -0.061], d: [0.011, 0.012, 0.52], r: [200, 0, 0] },
			{ s: 'ext', b: 'hdR', m: 'blade', p: [0, -0.357, -0.129], pts: [[0, -0.04], [0.04, -0.075], [0.11, -0.085], [0.13, -0.02], [0.13, 0.04], [0.11, 0.1], [0.04, 0.085], [0, 0.05]], d: [0.01], r: [200, -90, 0] },
			{ s: 'ext', b: 'hdR', m: 'blade', p: [0, -0.357, -0.129], pts: [[0, -0.03], [-0.03, -0.05], [-0.06, -0.04], [-0.06, 0.03], [-0.03, 0.045], [0, 0.03]], d: [0.01], r: [200, -90, 0] },
			{ s: 'cone', b: 'hdR', m: 'iron', p: [0, -0.43, -0.156], d: [0.014, 0.04], r: [200, 0, 0] },
			{ s: 'cyl', b: 'hdR', m: 'strap', p: [0, 0.0, 0], d: [0.015, 0.015, 0.04], r: [200, 0, 0] },
			{ s: 'halo', b: 'head', c: '#ff5a3a', p: [0, 0.078, -0.06], d: [0.12] },
		],
	};

	R['titan.cyclops'] = {
		cls: 'Warden',
		mat: { skin: ['#b3a684', 0, 0.8], skinD: ['#7f7458', 0, 0.85], apron: ['#7d4f27', 0, 0.85], pants: ['#6b5a44', 0, 0.9], boot: ['#4a3828', 0, 0.8],
			eyeW: ['#f6ecd6', 0, 0.3], eye: ['#ffd84f', 0, 0.3, 2.6], shield: ['#c48a3a', 0.85, 0.38], forge: ['#ff8c2a', 0, 0.5, 1.8] },
		body: { torso: 'skin', belly: 'skin', hips: 'pants', belt: 'apron', buckle: 'iron', loin: 'team', neck: 'skin', head: 'skin', legs: 'pants', knee: 'apron', shins: 'skin',
			boots: 'boot', bootTall: 0.03, arms: 'skin', fore: 'skin', bracer: 'apron', hands: 'skin' },
		parts: [
			// Smith's apron and shoulder strap.
			{ s: 'box', b: 'body', m: 'apron', p: [0, 0.6, -0.1], d: [0.2, 0.3, 0.012], r: [-4, 0, 0] },
			{ s: 'box', b: 'body', m: 'apron', p: [0, 0.71, 0], d: [0.03, 0.012, 0.29], r: [0, 0, -30] },
			{ s: 'sph', b: 'shL', m: 'apron', p: [-0.01, 0.015, 0], d: [0.13, 0.08, 0.13] },
			// Bald head, heavy brow, the one great eye.
			{ s: 'box', b: 'head', m: 'skinD', p: [0, 0.115, -0.054], d: [0.09, 0.02, 0.035], r: [-14, 0, 0] },
			{ s: 'sph', b: 'head', m: 'eyeW', p: [0, 0.083, -0.054], d: [0.066, 0.056, 0.036] },
			{ s: 'sph', b: 'head', m: 'eye', p: [0, 0.083, -0.07], d: [0.034, 0.034, 0.014] },
			{ s: 'sph', b: 'head', m: 'ink', p: [0, 0.083, -0.076], d: [0.014, 0.016, 0.006] },
			{ s: 'sph', b: 'head', m: 'skin', p: [0.06, 0.07, 0.005], d: [0.02, 0.035, 0.025], x: 1 },
			{ s: 'box', b: 'head', m: 'skinD', p: [0, 0.035, -0.055], d: [0.04, 0.008, 0.01] },
			{ s: 'halo', b: 'head', c: '#ffe08a', p: [0, 0.083, -0.07], d: [0.16] },
			// Round bronze shield with an anvil on the left forearm.
			{ s: 'cyl', b: 'elL', m: 'shield', p: [-0.06, -0.08, -0.01], d: [0.13, 0.13, 0.022], r: [0, 0, 90] },
			{ s: 'tor', b: 'elL', m: 'iron', p: [-0.072, -0.08, -0.01], d: [0.13, 0.009, 360], r: [0, 90, 0] },
			{ s: 'box', b: 'elL', m: 'iron', p: [-0.074, -0.075, -0.01], d: [0.006, 0.035, 0.09] },
			{ s: 'box', b: 'elL', m: 'iron', p: [-0.074, -0.105, -0.01], d: [0.006, 0.035, 0.04] },
			// Forge hammer: long handle, heavy square head with a hot face.
			{ s: 'cyl', b: 'hdR', m: 'wood', p: [0, -0.1, 0], d: [0.011, 0.013, 0.24] },
			{ s: 'box', b: 'hdR', m: 'iron', p: [0, -0.23, -0.01], d: [0.07, 0.065, 0.12] },
			{ s: 'box', b: 'hdR', m: 'forge', p: [0, -0.23, -0.072], d: [0.06, 0.055, 0.006] },
			{ s: 'emit', b: 'hdR', k: 'ember', p: [0, -0.23, -0.075], d: [0.35, 3] },
		],
	};

	R['titan.perun'] = {
		cls: 'Ravager',
		mat: { skin: ['#e0b08a', 0, 0.62], scale: ['#a9b8c9', 0.85, 0.32], scaleD: ['#6f8296', 0.8, 0.4], beard: ['#eef3f8', 0, 0.75], cloak: ['#2f4a78', 0, 0.8, 0, 'cloth'],
			pants: ['#a83a2a', 0, 0.8], boot: ['#6b4222', 0, 0.75], pauldron: ['#8a6a3a', 0.7, 0.45], oak: ['#4a8a3a', 0, 0.8], bolt: ['#bfe6ff', 0, 0.4, 2.4] },
		body: { torso: 'scale', hips: 'pants', belt: 'leather', buckle: 'gold', loin: 'team', neck: 'skin', head: 'skin', eyes: 'ink', legs: 'pants', knee: 'scaleD', shins: 'pants',
			boots: 'boot', bootTall: 0.09, arms: 'skin', fore: 'skin', bracer: 'scaleD', hands: 'skin', pauldron: 'pauldron' },
		parts: [
			{ s: 'tor', b: 'body', m: 'scaleD', p: [0, 0.66, 0], d: [0.176, 0.006, 360], r: [90, 0, 0], sc: [1, 0.63, 1] },
			{ s: 'tor', b: 'body', m: 'scaleD', p: [0, 0.6, 0], d: [0.15, 0.006, 360], r: [90, 0, 0], sc: [1, 0.66, 1] },
			// White hair, long beard to the chest, oak wreath.
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.085, 0.012], d: [0.112, 0.105, 0.11] },
			{ s: 'cap', b: 'head', m: 'beard', p: [0, 0.07, 0.04], d: [0.045, 0.035, 0.09] },
			{ s: 'cone', b: 'head', m: 'beard', p: [0, -0.03, -0.045], d: [0.052, 0.17], r: [188, 0, 0] },
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.035, -0.047], d: [0.095, 0.065, 0.055] },
			{ s: 'tor', b: 'head', m: 'oak', p: [0, 0.115, 0], d: [0.056, 0.011, 360], r: [90, 0, 0] },
			{ s: 'sph', b: 'head', m: 'oak', p: [0.045, 0.123, -0.03], d: [0.03, 0.016, 0.022], r: [0, 30, 20], x: 1 },
			{ s: 'sph', b: 'head', m: 'oak', p: [0.058, 0.12, 0.015], d: [0.024, 0.016, 0.03], r: [0, -20, 20], x: 1 },
			{ s: 'sph', b: 'head', m: 'oak', p: [0, 0.126, -0.058], d: [0.03, 0.018, 0.02] },
			// Blue cloak.
			{ s: 'cape', b: 'cape', m: 'cloak', p: [0, -0.24, -0.12], d: [0.13, 0.18, 0.5, -66, 132, 4.5, 0.015] },
			// Axe with a lightning rune: long haft, bearded blade, held low and forward.
			{ s: 'cyl', b: 'hdR', m: 'wood', p: [0, -0.141, -0.051], d: [0.01, 0.011, 0.44], r: [200, 0, 0] },
			{ s: 'ext', b: 'hdR', m: 'scale', p: [0, -0.31, -0.112], pts: [[0, -0.02], [0.03, -0.06], [0.08, -0.075], [0.105, -0.03], [0.11, 0.03], [0.095, 0.07], [0.04, 0.05], [0, 0.03]], d: [0.009], r: [200, -90, 0] },
			{ s: 'box', b: 'hdR', m: 'bolt', p: [0, -0.29, -0.17], d: [0.018, 0.05, 0.01], r: [200, 0, 25] },
			{ s: 'halo', b: 'hdR', c: '#9fd0ff', p: [0, -0.29, -0.17], d: [0.22] },
			{ s: 'emit', b: 'hdR', k: 'spark', p: [0, -0.29, -0.17], d: [0.45, 4] },
		],
	};

	R['titan.atlas'] = {
		cls: 'Warden',
		mat: { skin: ['#b88f62', 0, 0.6], skinD: ['#86643e', 0, 0.7], beard: ['#3c2a1c', 0, 0.9], toga: ['#f0e6d0', 0, 0.85, 0, 'cloth'], sandal: ['#7a4e2a', 0, 0.8],
			sky: ['#24356e', 0.2, 0.45, 0.25], star: ['#fff3c0', 0, 0.3, 2.4], meridian: ['#f4c44f', 0.9, 0.3, 0.5] },
		body: { torso: 'skin', hips: 'toga', belt: 'leather', buckle: 'gold', loin: 'team', neck: 'skin', head: 'skin', eyes: 'ink', legs: 'skin', knee: 'skin', shins: 'skin',
			boots: 'sandal', bootTall: 0.0, arms: 'skin', fore: 'skin', bracer: 'gold', hands: 'skin' },
		parts: [
			// Toga strap over the left shoulder and a muscled chest.
			{ s: 'box', b: 'body', m: 'toga', p: [-0.02, 0.67, 0], d: [0.05, 0.36, 0.25], r: [0, 0, -32] },
			{ s: 'sph', b: 'body', m: 'skin', p: [0.065, 0.69, -0.085], d: [0.13, 0.09, 0.06], x: 1 },
			{ s: 'box', b: 'body', m: 'skinD', p: [0, 0.6, -0.1], d: [0.006, 0.1, 0.01] },
			// Curly dark beard and hair, head bowed under the weight.
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.095, 0.01], d: [0.11, 0.08, 0.115] },
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.03, -0.035], d: [0.095, 0.08, 0.075] },
			{ s: 'box', b: 'head', m: 'skinD', p: [0, 0.095, -0.055], d: [0.07, 0.012, 0.012] },
			// The sky on his shoulders: a dark globe with golden meridians and stars, resting behind the head.
			{ s: 'sph', b: 'body', m: 'sky', p: [0, 0.99, 0.2], d: [0.3, 0.3, 0.3] },
			{ s: 'tor', b: 'body', m: 'meridian', p: [0, 0.99, 0.2], d: [0.152, 0.006, 360] },
			{ s: 'tor', b: 'body', m: 'meridian', p: [0, 0.99, 0.2], d: [0.152, 0.006, 360], r: [0, 60, 0] },
			{ s: 'tor', b: 'body', m: 'meridian', p: [0, 0.99, 0.2], d: [0.152, 0.006, 360], r: [0, -60, 0] },
			{ s: 'tor', b: 'body', m: 'meridian', p: [0, 0.99, 0.2], d: [0.152, 0.007, 360], r: [90, 0, 23] },
			{ s: 'sph', b: 'body', m: 'star', p: [0.09, 1.08, -0.08], d: [0.018, 0.018, 0.018] },
			{ s: 'sph', b: 'body', m: 'star', p: [-0.12, 0.96, -0.06], d: [0.014, 0.014, 0.014] },
			{ s: 'sph', b: 'body', m: 'star', p: [0.03, 1.15, 0.02], d: [0.016, 0.016, 0.016] },
			{ s: 'sph', b: 'body', m: 'star', p: [-0.06, 1.1, 0.17], d: [0.014, 0.014, 0.014] },
			{ s: 'sph', b: 'body', m: 'star', p: [0.15, 0.97, 0.12], d: [0.016, 0.016, 0.016] },
			{ s: 'sph', b: 'body', m: 'skinD', p: [0.1, 0.83, 0.05], d: [0.09, 0.05, 0.12], x: 1 },
			{ s: 'halo', b: 'body', c: '#9fd0ff', p: [0, 0.99, 0.2], d: [0.5] },
			{ s: 'emit', b: 'body', k: 'star', p: [0, 0.99, 0.2], d: [0.4, 3] },
		],
	};

	R['titan.ymir'] = {
		cls: 'Juggernaut',
		mat: { skin: ['#bcd4e6', 0.1, 0.45], skinD: ['#7f9cb4', 0.1, 0.5], hand: ['#d6ecf8', 0.1, 0.4], beard: ['#f4fbff', 0, 0.6], pelt: ['#8a7a6a', 0, 0.95],
			boot: ['#6e6052', 0, 0.9], ice: ['#a8eaff', 0.2, 0.15, 0.55, 'flat'], iceD: ['#6fc6ee', 0.2, 0.2, 0.35, 'flat'] },
		body: { torso: 'skin', belly: 'skin', hips: 'pelt', belt: 'pelt', buckle: 'iceD', loin: 'team', neck: 'skin', head: 'skin', eyes: 'iceD', legs: 'pelt', knee: 'iceD', shins: 'skin',
			boots: 'boot', bootTall: 0.06, arms: 'skin', fore: 'skin', bracer: 'ice', hands: 'hand' },
		parts: [
			// Fur mantle over the shoulders.
			{ s: 'tor', b: 'body', m: 'pelt', p: [0, 0.79, 0.005], d: [0.15, 0.04, 360], r: [90, 0, 0], sc: [1, 0.7, 1] },
			// Ice pauldrons: clusters of crystals.
			{ s: 'ico', b: 'shL', m: 'ice', p: [-0.02, 0.03, 0], d: [0.06, 0], sc: [1, 1.6, 1], r: [0, 20, 25], x: 1 },
			{ s: 'ico', b: 'shL', m: 'iceD', p: [-0.05, 0.05, 0.035], d: [0.04, 0], sc: [1, 1.8, 1], r: [10, 0, 35], x: 1 },
			{ s: 'ico', b: 'shL', m: 'ice', p: [-0.01, 0.06, -0.04], d: [0.035, 0], sc: [1, 2, 1], r: [-15, 0, 10], x: 1 },
			// White beard and hair, a crown of ice spikes.
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.085, 0.012], d: [0.12, 0.1, 0.115] },
			{ s: 'cone', b: 'head', m: 'beard', p: [0, -0.04, -0.05], d: [0.06, 0.19], r: [186, 0, 0] },
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.035, -0.05], d: [0.105, 0.07, 0.06] },
			{ s: 'cone', b: 'head', m: 'ice', p: [0, 0.165, -0.05], d: [0.016, 0.09], r: [-12, 0, 0] },
			{ s: 'cone', b: 'head', m: 'ice', p: [0.035, 0.155, -0.04], d: [0.012, 0.065], r: [-10, 0, -18], x: 1 },
			{ s: 'cone', b: 'head', m: 'iceD', p: [0.055, 0.14, -0.012], d: [0.011, 0.05], r: [0, 0, -30], x: 1 },
			{ s: 'tor', b: 'head', m: 'iceD', p: [0, 0.122, -0.004], d: [0.056, 0.008, 360], r: [90, 0, 0] },
			// Icicles on the forearms and knuckles of frost.
			{ s: 'cone', b: 'elL', m: 'ice', p: [-0.045, -0.08, 0.02], d: [0.013, 0.06], r: [0, 0, 70], x: 1 },
			{ s: 'ico', b: 'hdL', m: 'ice', p: [0, -0.03, -0.02], d: [0.035, 0], x: 1 },
			{ s: 'emit', b: 'head', k: 'frost', p: [0, 0.02, -0.08], d: [0.5, 3] },
			{ s: 'emit', b: 'shR', k: 'frost', p: [0.02, 0.06, 0], d: [0.4, 2] },
		],
	};

	R['titan.surtr'] = {
		cls: 'Ravager',
		mat: { skin: ['#3a2a26', 0.2, 0.6], skinD: ['#241816', 0.2, 0.65], lava: ['#ff7a1a', 0, 0.4, 2.6], iron: ['#4a4448', 0.85, 0.45], ironD: ['#2e2a2c', 0.8, 0.5],
			fire: ['#ff8a2a', 0, 0.3, 1.7], horn: ['#1c1412', 0.3, 0.5] },
		body: { torso: 'skin', hips: 'ironD', belt: 'iron', buckle: 'lava', loin: 'team', neck: 'skin', head: 'skin', eyes: 'lava', legs: 'skin', knee: 'iron', shins: 'skin',
			boots: 'iron', bootTall: 0.07, arms: 'skin', fore: 'skin', bracer: 'iron', hands: 'skinD', pauldron: 'iron' },
		parts: [
			// Lava seams on chest and arms.
			{ s: 'box', b: 'body', m: 'lava', p: [0.04, 0.68, -0.114], d: [0.006, 0.09, 0.006], r: [0, 0, 25] },
			{ s: 'box', b: 'body', m: 'lava', p: [-0.05, 0.64, -0.112], d: [0.006, 0.08, 0.006], r: [0, 0, -30] },
			{ s: 'box', b: 'body', m: 'lava', p: [0.0, 0.57, -0.104], d: [0.07, 0.006, 0.006], r: [0, 0, 8] },
			{ s: 'sph', b: 'body', m: 'lava', p: [0, 0.71, -0.118], d: [0.03, 0.03, 0.012] },
			{ s: 'box', b: 'shL', m: 'lava', p: [-0.0, -0.1, -0.05], d: [0.005, 0.08, 0.005], r: [0, 0, 15], x: 1 },
			// Spiked iron pauldrons.
			{ s: 'cone', b: 'shL', m: 'ironD', p: [-0.04, 0.055, 0], d: [0.016, 0.06], r: [0, 0, 30], x: 1 },
			{ s: 'cone', b: 'shL', m: 'ironD', p: [-0.02, 0.06, -0.035], d: [0.013, 0.05], r: [-20, 0, 15], x: 1 },
			// Horned head under a crown of fire.
			{ s: 'tube', b: 'head', m: 'horn', pts: [[0.04, 0.11, -0.01], [0.075, 0.13, 0.0], [0.09, 0.17, 0.015], [0.08, 0.21, 0.03]], d: [0.016, 0.003], x: 1 },
			{ s: 'box', b: 'head', m: 'lava', p: [0, 0.035, -0.058], d: [0.035, 0.006, 0.006] },
			{ s: 'cone', b: 'head', m: 'fire', p: [0, 0.155, 0], d: [0.04, 0.09] },
			{ s: 'cone', b: 'head', m: 'lava', p: [0.03, 0.145, 0.005], d: [0.025, 0.06], r: [0, 0, -15], x: 1 },
			{ s: 'halo', b: 'head', c: '#ff8c2a', p: [0, 0.15, 0], d: [0.3] },
			{ s: 'emit', b: 'head', k: 'flame', p: [0, 0.16, 0], d: [0.55, 14] },
			// The Muspell blade: a long burning sword held point down and forward.
			{ s: 'box', b: 'hdR', m: 'ironD', p: [0, 0, 0], d: [0.1, 0.016, 0.024] },
			{ s: 'cyl', b: 'hdR', m: 'ironD', p: [0, 0.035, 0.012], d: [0.012, 0.012, 0.07], r: [20, 0, 0] },
			{ s: 'sph', b: 'hdR', m: 'lava', p: [0, 0.072, 0.026], d: [0.022, 0.022, 0.022] },
			{ s: 'ext', b: 'hdR', m: 'fire', p: [0, 0, 0], pts: [[-0.02, 0.01], [0.02, 0.01], [0.024, 0.3], [0, 0.37], [-0.024, 0.3]], d: [0.008], r: [200, 0, 0] },
			{ s: 'emit', b: 'hdR', k: 'flame', p: [0, -0.141, -0.051], d: [0.35, 8] },
			{ s: 'emit', b: 'hdR', k: 'flame', p: [0, -0.263, -0.095], d: [0.4, 10] },
			{ s: 'halo', b: 'hdR', c: '#ff7a1a', p: [0, -0.188, -0.068], d: [0.34] },
		],
	};

	R['titan.hecate'] = {
		cls: 'Mystic',
		mat: { skin: ['#e6d6f0', 0, 0.55], robe: ['#4a3a6a', 0, 0.85, 0, 'cloth'], robeL: ['#6a568e', 0, 0.8, 0, 'cloth'], gold: ['#e8b85c', 0.9, 0.3, 0.15],
			hair: ['#2a2238', 0, 0.8], flame: ['#c89bff', 0, 0.3, 2.8], eye: ['#c89bff', 0, 0.3, 2] },
		body: { torso: 'robeL', hips: 'robe', belt: 'gold', buckle: 'gold', neck: 'skin', head: 'skin', eyes: 'eye', arms: 'robeL', fore: 'robeL', bracer: 'gold', hands: 'skin' },
		parts: [
			// Robe to the ground (the legs walk inside it), team-coloured panel down the front.
			{ s: 'cyl', b: 'body', m: 'robe', p: [0, 0.245, 0], d: [0.1, 0.2, 0.49] },
			{ s: 'box', b: 'body', m: 'team', p: [0, 0.25, -0.15], d: [0.06, 0.44, 0.01], r: [11.5, 0, 0] },
			{ s: 'tor', b: 'body', m: 'gold', p: [0, 0.01, 0], d: [0.2, 0.008, 360], r: [90, 0, 0] },
			{ s: 'tor', b: 'body', m: 'gold', p: [0, 0.795, 0], d: [0.075, 0.012, 360], r: [90, 0, 0], sc: [1, 0.8, 1] },
			// Wide sleeves.
			{ s: 'cyl', b: 'elL', m: 'robe', p: [0, -0.09, 0], d: [0.04, 0.07, 0.12], x: 1 },
			// Hair in a high knot, a golden diadem with a crescent.
			{ s: 'sph', b: 'head', m: 'hair', p: [0, 0.088, 0.012], d: [0.102, 0.1, 0.105] },
			{ s: 'sph', b: 'head', m: 'hair', p: [0, 0.14, 0.03], d: [0.05, 0.045, 0.05] },
			{ s: 'cap', b: 'head', m: 'hair', p: [0.045, 0.08, 0.02], d: [0.02, 0.015, 0.1], x: 1 },
			{ s: 'tor', b: 'head', m: 'gold', p: [0, 0.112, -0.005], d: [0.05, 0.006, 360], r: [90, 0, 0] },
			{ s: 'tor', b: 'head', m: 'gold', p: [0, 0.14, -0.05], d: [0.018, 0.005, 220], r: [0, 0, 160] },
			// Two torches of underworld fire.
			{ s: 'cyl', b: 'hdL', m: 'wood', p: [0, 0.06, 0], d: [0.008, 0.01, 0.22], x: 1 },
			{ s: 'cyl', b: 'hdL', m: 'gold', p: [0, 0.18, 0], d: [0.03, 0.014, 0.035], x: 1 },
			{ s: 'cone', b: 'hdL', m: 'flame', p: [0, 0.225, 0], d: [0.024, 0.07], x: 1 },
			{ s: 'halo', b: 'hdL', c: '#c89bff', p: [0, 0.22, 0], d: [0.26], x: 1 },
			{ s: 'emit', b: 'hdL', k: 'witchfire', p: [0, 0.23, 0], d: [0.45, 12], x: 1 },
		],
	};

	R['titan.artemis'] = {
		cls: 'Hunter',
		mat: { skin: ['#e6b98f', 0, 0.6], tunic: ['#7a9a5a', 0, 0.85, 0, 'cloth'], tunicD: ['#4a5e34', 0, 0.85], hair: ['#7a3a22', 0, 0.8], silver: ['#e0e8f4', 0.9, 0.22, 0.35],
			boot: ['#a8693a', 0, 0.75], quiver: ['#6b4222', 0, 0.8], fletch: ['#f2f6e0', 0, 0.7], moon: ['#e8ffb0', 0, 0.3, 2.2] },
		body: { torso: 'tunicD', hips: 'tunic', belt: 'leather', buckle: 'silver', loin: 'team', neck: 'skin', head: 'skin', eyes: 'ink', legs: 'skin', knee: 'skin', shins: 'skin',
			boots: 'boot', bootTall: 0.1, arms: 'skin', fore: 'skin', bracer: 'boot', hands: 'skin' },
		parts: [
			// Short tunic skirt and a sash in the side's colour.
			{ s: 'cyl', b: 'body', m: 'tunic', p: [0, HIP_Y - 0.04, 0], d: [0.088, 0.12, 0.13] },
			{ s: 'box', b: 'body', m: 'team', p: [0, 0.66, 0], d: [0.03, 0.32, 0.16], r: [0, 0, 34] },
			{ s: 'sph', b: 'body', m: 'tunic', p: [0, 0.71, 0], d: [0.25, 0.12, 0.165] },
			// Auburn hair, ponytail, silver crescent.
			{ s: 'sph', b: 'head', m: 'hair', p: [0, 0.088, 0.01], d: [0.102, 0.098, 0.105] },
			{ s: 'tube', b: 'head', m: 'hair', pts: [[0, 0.12, 0.04], [0, 0.11, 0.08], [0, 0.04, 0.09], [0, -0.06, 0.07]], d: [0.022, 0.008] },
			{ s: 'tor', b: 'head', m: 'silver', p: [0, 0.13, -0.045], d: [0.016, 0.004, 220], r: [0, 0, 160] },
			// Quiver on the back with silver-tipped arrows.
			{ s: 'cyl', b: 'body', m: 'quiver', p: [0.03, 0.72, 0.075], d: [0.03, 0.026, 0.2], r: [0, 0, -18] },
			{ s: 'cyl', b: 'body', m: 'fletch', p: [0.065, 0.84, 0.075], d: [0.004, 0.004, 0.08], r: [0, 0, -14] },
			{ s: 'cyl', b: 'body', m: 'fletch', p: [0.055, 0.845, 0.065], d: [0.004, 0.004, 0.08], r: [6, 0, -22] },
			{ s: 'cyl', b: 'body', m: 'fletch', p: [0.075, 0.84, 0.085], d: [0.004, 0.004, 0.08], r: [-6, 0, -10] },
			// The silver bow in the left hand, string drawn tight.
			{ s: 'tor', b: 'hdL', m: 'silver', p: [0, 0, 0.21], d: [0.21, 0.007, 140], r: [0, 90, -70] },
			{ s: 'cyl', b: 'hdL', m: 'fletch', p: [0, 0, 0.138], d: [0.0015, 0.0015, 0.394] },
			{ s: 'sph', b: 'hdL', m: 'moon', p: [0, 0.197, 0.138], d: [0.016, 0.016, 0.016] },
			{ s: 'sph', b: 'hdL', m: 'moon', p: [0, -0.197, 0.138], d: [0.016, 0.016, 0.016] },
			{ s: 'emit', b: 'hdL', k: 'moon', p: [0, 0, 0], d: [0.3, 2] },
		],
	};

	R['titan.loki'] = {
		cls: 'Ravager',
		mat: { skin: ['#dcc4a2', 0, 0.62], jerkin: ['#3d5a34', 0, 0.8], jerkinD: ['#24361f', 0, 0.85], cloak: ['#2c4428', 0, 0.85, 0, 'cloth'], pants: ['#4a6a3e', 0, 0.8],
			boot: ['#2a2018', 0, 0.8], blade: ['#cfdcc6', 0.9, 0.25], venom: ['#9dff6a', 0, 0.35, 2.2] },
		body: { torso: 'jerkin', hips: 'jerkinD', belt: 'jerkinD', buckle: 'gold', loin: 'team', neck: 'skin', head: 'skin', legs: 'pants', knee: 'gold', shins: 'jerkinD',
			boots: 'boot', bootTall: 0.09, arms: 'skin', fore: 'skin', bracer: 'gold', hands: 'skin' },
		parts: [
			// Leather jerkin, golden straps crossed over chest and back, a green stone where they meet.
			{ s: 'box', b: 'body', m: 'gold', p: [0, 0.655, 0], d: [0.016, 0.3, 0.232], r: [0, 0, 30], x: 1 },
			{ s: 'sph', b: 'body', m: 'gold', p: [0, 0.68, -0.106], d: [0.034, 0.034, 0.018] },
			{ s: 'sph', b: 'body', m: 'venom', p: [0, 0.68, -0.114], d: [0.015, 0.015, 0.008] },
			// The trickster's cowl: a hood open at the face, its two long tips curling up like horns, gold bells on the ends.
			{ s: 'cyl', b: 'head', m: 'cloak', p: [0, 0.06, 0.008], d: [0.068, 0.078, 0.11, -125, 250] },
			{ s: 'sph', b: 'head', m: 'cloak', p: [0, 0.11, 0.012], d: [0.142, 0.1, 0.14] },
			{ s: 'tube', b: 'head', m: 'cloak', pts: [[0.035, 0.14, 0.01], [0.07, 0.18, 0.02], [0.085, 0.24, 0.035], [0.11, 0.27, 0.03], [0.125, 0.255, 0.015]], d: [0.03, 0.007], x: 1 },
			{ s: 'sph', b: 'head', m: 'gold', p: [0.127, 0.25, 0.012], d: [0.022, 0.022, 0.022], x: 1 },
			// Sly green eyes on the face (the base body's eyes would sit inside this head).
			{ s: 'sph', b: 'head', m: 'venom', p: [0.022, 0.078, -0.064], d: [0.02, 0.01, 0.01], x: 1 },
			// Ragged cloak: his shadow, trailing dark wisps.
			{ s: 'cape', b: 'cape', m: 'cloak', p: [0, -0.25, -0.12], d: [0.13, 0.2, 0.52, -72, 144, 7, 0.022] },
			{ s: 'sph', b: 'body', m: 'gold', p: [0.125, 0.785, -0.06], d: [0.026, 0.026, 0.018], x: 1 },
			{ s: 'emit', b: 'body', k: 'shade', p: [0, 0.12, 0.12], d: [0.5, 4] },
			// A dagger in each fist, held in a reverse grip, with a venom-green line along the blade.
			{ s: 'sph', b: 'hdL', m: 'gold', p: [0, 0.075, 0.03], d: [0.024, 0.024, 0.024], x: 1 },
			{ s: 'cyl', b: 'hdL', m: 'jerkinD', p: [0, 0.035, 0.012], d: [0.011, 0.011, 0.07], r: [20, 0, 0], x: 1 },
			{ s: 'box', b: 'hdL', m: 'gold', p: [0, 0, 0], d: [0.075, 0.014, 0.02], x: 1 },
			{ s: 'ext', b: 'hdL', m: 'blade', p: [0, 0, 0], pts: [[-0.016, 0.008], [0.016, 0.008], [0.02, 0.08], [0.008, 0.16], [0, 0.18], [-0.012, 0.1]], d: [0.006], r: [200, 0, 0], x: 1 },
			{ s: 'box', b: 'hdL', m: 'venom', p: [0, -0.08, -0.029], d: [0.004, 0.1, 0.01], r: [200, 0, 0], x: 1 },
			{ s: 'halo', b: 'hdL', c: '#9dff6a', p: [0, -0.08, -0.029], d: [0.16], x: 1 },
		],
	};

	R['titan.tyr'] = {
		cls: 'Warden',
		mat: { skin: ['#e0b48a', 0, 0.62], steel: ['#c9d1da', 0.9, 0.26], steelD: ['#8e9aa6', 0.85, 0.36], mail: ['#9aa4ae', 0.8, 0.4], beard: ['#b0703a', 0, 0.85],
			cape: ['#efe6cf', 0, 0.8, 0, 'cloth'], face: ['#f6f1e2', 0.2, 0.5], blade: ['#eef2f6', 0.95, 0.18], holy: ['#fff0a0', 0, 0.35, 2.2] },
		body: { torso: 'steel', hips: 'mail', belt: 'leather', buckle: 'gold', loin: 'team', neck: 'skin', head: 'skin', eyes: 'holy', legs: 'mail', knee: 'gold', shins: 'steel',
			boots: 'steelD', bootTall: 0.1, arms: 'mail', fore: 'mail', bracer: 'steel', hands: 'steelD', pauldron: 'steel' },
		parts: [
			// Polished breastplate: golden rim at the collar, a plate edge, and Tyr's rune (an upward arrow) on the chest.
			{ s: 'tor', b: 'body', m: 'gold', p: [0, 0.765, 0], d: [0.196, 0.009, 360], r: [90, 0, 0], sc: [1, 0.65, 1] },
			{ s: 'tor', b: 'body', m: 'steelD', p: [0, 0.62, 0], d: [0.156, 0.007, 360], r: [90, 0, 0], sc: [1, 0.66, 1] },
			{ s: 'box', b: 'body', m: 'gold', p: [0, 0.676, -0.121], d: [0.01, 0.072, 0.008] },
			{ s: 'box', b: 'body', m: 'gold', p: [0.0135, 0.6985, -0.12], d: [0.009, 0.038, 0.008], r: [0, 0, 45], x: 1 },
			// Steel helm with a nose guard and a golden crest; braided golden-brown beard with a gold bead.
			{ s: 'sph', b: 'head', m: 'steel', p: [0, 0.1, -0.002], d: [0.132, 0.1, 0.138] },
			{ s: 'tor', b: 'head', m: 'steelD', p: [0, 0.075, -0.002], d: [0.064, 0.007, 360], r: [90, 0, 0] },
			{ s: 'box', b: 'head', m: 'steelD', p: [0, 0.06, -0.068], d: [0.012, 0.05, 0.01] },
			{ s: 'box', b: 'head', m: 'gold', p: [0, 0.152, 0], d: [0.012, 0.032, 0.11] },
			{ s: 'cone', b: 'head', m: 'beard', p: [0, -0.005, -0.045], d: [0.05, 0.13], r: [188, 0, 0] },
			{ s: 'sph', b: 'head', m: 'beard', p: [0, 0.032, -0.047], d: [0.1, 0.065, 0.06] },
			{ s: 'sph', b: 'head', m: 'gold', p: [0, -0.073, -0.055], d: [0.017, 0.017, 0.017] },
			{ s: 'box', b: 'head', m: 'beard', p: [0, 0.052, -0.063], d: [0.056, 0.012, 0.012] },
			// White cape over both shoulders, golden clasps.
			{ s: 'cape', b: 'cape', m: 'cape', p: [0, -0.25, -0.12], d: [0.14, 0.2, 0.52, -75, 150, 4, 0.014] },
			{ s: 'sph', b: 'body', m: 'gold', p: [0.14, 0.785, -0.065], d: [0.03, 0.03, 0.02], x: 1 },
			// The sword in his left hand, point down and forward, a warm light along the blade.
			{ s: 'box', b: 'hdL', m: 'gold', p: [0, 0, 0], d: [0.11, 0.016, 0.026] },
			{ s: 'cyl', b: 'hdL', m: 'leather', p: [0, 0.035, 0.012], d: [0.012, 0.012, 0.07], r: [20, 0, 0] },
			{ s: 'sph', b: 'hdL', m: 'gold', p: [0, 0.072, 0.026], d: [0.024, 0.024, 0.024] },
			{ s: 'ext', b: 'hdL', m: 'blade', p: [0, 0, 0], pts: [[-0.022, 0.01], [0.022, 0.01], [0.02, 0.3], [0, 0.36], [-0.02, 0.3]], d: [0.008], r: [200, 0, 0] },
			{ s: 'halo', b: 'hdL', c: '#fff0a0', p: [0, -0.188, -0.068], d: [0.3] },
			{ s: 'emit', b: 'hdL', k: 'star', p: [0, -0.2, -0.073], d: [0.2, 2] },
			// A round shield strapped to the right forearm (the hand Fenrir took is behind it), Tyr's rune in gold.
			{ s: 'cyl', b: 'elR', m: 'steelD', p: [0.065, -0.1, -0.01], d: [0.13, 0.13, 0.022], r: [0, 0, 90] },
			{ s: 'cyl', b: 'elR', m: 'face', p: [0.065, -0.1, -0.01], d: [0.112, 0.112, 0.026], r: [0, 0, 90] },
			{ s: 'tor', b: 'elR', m: 'gold', p: [0.078, -0.1, -0.01], d: [0.112, 0.006, 360], r: [0, 90, 0] },
			{ s: 'box', b: 'elR', m: 'gold', p: [0.079, -0.1, -0.01], d: [0.006, 0.12, 0.012] },
			{ s: 'box', b: 'elR', m: 'gold', p: [0.079, -0.061, 0.011], d: [0.006, 0.06, 0.012], r: [-45, 0, 0] },
			{ s: 'box', b: 'elR', m: 'gold', p: [0.079, -0.061, -0.031], d: [0.006, 0.06, 0.012], r: [45, 0, 0] },
		],
	};

	// --- Geometry -------------------------------------------------------------------------------------------
	function shape(T, part) {
		const d = part.d || [];
		switch (part.s) {
		case 'sph': return new T.SphereGeometry(0.5, 18, 12);
		case 'cap': {
			// Tapered capsule from (0, 0) down to (0, -len): a lathe of two arcs and a straight side.
			const [r0, r1, len] = d;
			const pts = [];
			for (let i = 0; i <= 5; i++) { const a = -Math.PI / 2 + (i / 5) * (Math.PI / 2); pts.push(new T.Vector2(Math.cos(a) * r1, -len + Math.sin(a) * r1)); }
			for (let i = 0; i <= 5; i++) { const a = (i / 5) * (Math.PI / 2); pts.push(new T.Vector2(Math.cos(a) * r0, Math.sin(a) * r0)); }
			return new T.LatheGeometry(pts, 14);
		}
		case 'cyl': return d.length > 3 ? new T.CylinderGeometry(d[0], d[1], d[2], 18, 1, true, (d[3] * Math.PI) / 180, (d[4] * Math.PI) / 180)
			: new T.CylinderGeometry(d[0], d[1], d[2], 16, 1);
		case 'cape': {
			// Open shell like 'cyl' (radius d[0] at the top to d[1] at the hem, length d[2], arc d[3]..d[3]+d[4]),
			// with d[5] folds of depth d[6] that deepen towards a ragged hem.
			const [r0, r1, len, a0, al, folds, deep] = d;
			const NU = 28;
			const NV = 8;
			const pos = [];
			const uv = [];
			const idx = [];
			for (let j = 0; j <= NV; j++) {
				const t = j / NV;
				for (let i = 0; i <= NU; i++) {
					const u = i / NU;
					const a = ((a0 + al * u) * Math.PI) / 180;
					const wave = Math.sin(u * folds * Math.PI * 2);
					const r = r0 + (r1 - r0) * t + deep * (0.2 + 0.8 * t) * wave;
					const hem = t === 1 ? len * 0.05 * (0.5 + 0.5 * Math.sin(u * folds * Math.PI * 2 + 1.2)) : 0;
					pos.push(Math.sin(a) * r, len / 2 - len * t - hem, Math.cos(a) * r);
					uv.push(u, 1 - t);
				}
			}
			for (let j = 0; j < NV; j++) {
				for (let i = 0; i < NU; i++) {
					const a = j * (NU + 1) + i;
					const b = a + NU + 1;
					idx.push(a, b, a + 1, b, b + 1, a + 1);
				}
			}
			const g = new T.BufferGeometry();
			g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
			g.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));
			g.setIndex(idx);
			g.computeVertexNormals();
			return g;
		}
		case 'box': return new T.BoxGeometry(d[0], d[1], d[2]);
		case 'cone': return new T.ConeGeometry(d[0], d[1], 12);
		case 'tor': return new T.TorusGeometry(d[0], d[1], 8, 32, ((d[2] || 360) * Math.PI) / 180);
		case 'tube': {
			const curve = new T.CatmullRomCurve3(part.pts.map((p) => new T.Vector3(p[0], p[1], p[2])));
			const g = new T.TubeGeometry(curve, 16, 1, 8, false);
			// Taper the tube from r0 to r1 along its length.
			const pos = g.attributes.position;
			const v = new T.Vector3();
			for (let i = 0; i < pos.count; i++) {
				const seg = Math.floor(i / 9);
				const k = seg / 16;
				const c = curve.getPointAt(k);
				v.fromBufferAttribute(pos, i).sub(c).multiplyScalar(d[0] + (d[1] - d[0]) * k).add(c);
				pos.setXYZ(i, v.x, v.y, v.z);
			}
			g.computeVertexNormals();
			return g;
		}
		case 'ext': {
			const sh = new T.Shape(part.pts.map((p) => new T.Vector2(p[0], p[1])));
			const g = new T.ExtrudeGeometry(sh, { depth: d[0], bevelEnabled: true, bevelThickness: d[0] * 0.3, bevelSize: d[0] * 0.3, bevelSegments: 1 });
			g.translate(0, 0, -d[0] / 2);
			return g;
		}
		case 'lathe': return new T.LatheGeometry(part.pts.map((p) => new T.Vector2(p[0], p[1])), 24);
		case 'ico': return new T.IcosahedronGeometry(d[0], d[1] || 0);
		default: return null;
		}
	}
	/** Placement of a part on its bone: position, rotation, and the size for shapes built at unit size. */
	function placement(T, part) {
		const d = part.d || [];
		const r = part.r || [0, 0, 0];
		const s = part.sc ? part.sc.slice() : [1, 1, 1];
		if (part.s === 'sph') { s[0] *= d[0]; s[1] *= d[1]; s[2] *= d[2]; }
		if (part.s === 'lathe') { s[0] *= d[0]; s[2] *= d[1]; }
		const p = part.p || [0, 0, 0];
		return new T.Matrix4().compose(new T.Vector3(p[0], p[1], p[2]),
			new T.Quaternion().setFromEuler(new T.Euler((r[0] * Math.PI) / 180, (r[1] * Math.PI) / 180, (r[2] * Math.PI) / 180, 'XYZ')), new T.Vector3(s[0], s[1], s[2]));
	}
	const otherSide = (b) => (/L$/.test(b) ? b.slice(0, -1) + 'R' : /R$/.test(b) ? b.slice(0, -1) + 'L' : b);
	/** The part on the other side of the body: bone swapped, x and the y/z turns negated. */
	function mirrored(part) {
		const m = JSON.parse(JSON.stringify(part));
		m.b = otherSide(part.b);
		if (m.p) m.p[0] = -m.p[0];
		if (m.pts && part.s === 'tube') m.pts = m.pts.map((q) => [-q[0], q[1], q[2]]);
		if (m.r) { m.r[1] = -m.r[1]; m.r[2] = -m.r[2]; }
		if (part.s === 'ext' && part.r) { m.r[1] = 180 - part.r[1]; m.r[2] = part.r[2]; }
		delete m.x;
		return m;
	}
	function allParts(id) {
		const rec = R[id];
		const b = BUILD[rec.cls];
		const out = [];
		for (const part of baseBody(b, rec.body).concat(rec.parts)) {
			out.push(part);
			if (part.x) out.push(mirrored(part));
		}
		return out;
	}

	/** Merges non-indexed copies of geometries placed by their matrices into one geometry (position + normal). */
	function merge(T, items) {
		let n = 0;
		const parts = items.map(([g, m]) => {
			const q = (g.index ? g.toNonIndexed() : g).applyMatrix4(m);
			n += q.attributes.position.count;
			return q;
		});
		const pos = new Float32Array(n * 3);
		const nor = new Float32Array(n * 3);
		let o = 0;
		for (const q of parts) {
			pos.set(q.attributes.position.array, o * 3);
			nor.set(q.attributes.normal.array, o * 3);
			o += q.attributes.position.count;
			q.dispose();
		}
		const out = new T.BufferGeometry();
		out.setAttribute('position', new T.BufferAttribute(pos, 3));
		out.setAttribute('normal', new T.BufferAttribute(nor, 3));
		out.computeBoundingSphere();
		return out;
	}

	function material(T, spec, team) {
		const [hex, metal, rough, emi, flag] = spec;
		const c = hex === 'team' ? team.clone() : hex === 'teamD' ? team.clone().multiplyScalar(0.55) : new T.Color(hex);
		const m = new T.MeshStandardMaterial({ color: c, metalness: metal || 0, roughness: rough === undefined ? 0.7 : rough, flatShading: flag === 'flat' });
		if (emi) { m.emissive = c.clone(); m.emissiveIntensity = emi; }
		if (flag === 'cloth') m.side = T.DoubleSide;
		if (flag === 'flat') { m.transparent = true; m.opacity = 0.92; }
		return m;
	}

	// --- Building and posing --------------------------------------------------------------------------------
	function build(T, id, opts) {
		const rec = R[id];
		const H = opts.H;
		const team = opts.team || new T.Color(0x3a8dde);
		const b = BUILD[rec.cls];
		const root = new T.Group();
		const scaler = new T.Group();
		scaler.scale.setScalar(H);
		root.add(scaler);
		const bone = {};
		const B = bones(b);
		for (const name of Object.keys(B)) {
			const [parent, x, y, z] = B[name];
			const g = new T.Group();
			g.position.set(x, y, z);
			g.rotation.order = 'YXZ';
			(parent ? bone[parent] : scaler).add(g);
			bone[name] = g;
		}
		const mats = {};
		const matOf = (name) => {
			if (!mats[name]) {
				const spec = rec.mat[name] || COMMON[name];
				mats[name] = material(T, spec || ['#ff00ff'], team);
			}
			return mats[name];
		};
		const groups = new Map();
		const emitters = [];
		const halos = [];
		const geoCache = {};
		for (const part of allParts(id)) {
			if (part.s === 'emit') {
				const o = new T.Object3D();
				o.position.fromArray(part.p);
				bone[part.b].add(o);
				emitters.push({ o, kind: part.k, size: part.d[0], rate: part.d[1], acc: Math.random() });
				continue;
			}
			if (part.s === 'halo') {
				if (!opts.glow) continue;
				const s = new T.Sprite(new T.SpriteMaterial({ map: opts.glow(part.c), color: new T.Color(part.c), blending: T.AdditiveBlending, transparent: true, depthWrite: false, opacity: 0.8 }));
				s.position.fromArray(part.p);
				s.scale.setScalar(part.d[0]);
				s.renderOrder = 5;
				bone[part.b].add(s);
				halos.push(s);
				continue;
			}
			const key = part.s === 'sph' || part.s === 'box' || part.s === 'ico' ? part.s + (part.d && part.s !== 'sph' ? part.d.join(',') : '') : null;
			const g = key && geoCache[key] ? geoCache[key] : shape(T, part);
			if (!g) continue;
			if (key) geoCache[key] = g;
			const k = part.b + '|' + part.m;
			if (!groups.has(k)) groups.set(k, []);
			groups.get(k).push([key ? g.clone() : g, placement(T, part)]);
		}
		for (const [k, items] of groups) {
			const [bn, mn] = k.split('|');
			const mesh = new T.Mesh(merge(T, items), matOf(mn));
			mesh.castShadow = true;
			mesh.userData.ownGeo = true;
			bone[bn].add(mesh);
		}
		for (const key in geoCache) geoCache[key].dispose();
		const rig = {
			root, body: bone.body, hipL: bone.hipL, hipR: bone.hipR, shL: bone.shL, shR: bone.shR, bone, emitters, halos, H,
			robe: rec.cls === 'Mystic', last: 0, lastWalk: 0, phase: Math.random() * 10,
			pose(t, v) { pose(rig, t, v, opts.emit); },
		};
		return rig;
	}

	const tmpV = { x: 0, y: 0, z: 0 };
	/** Secondary motion over the battle's own pose of body, hips and shoulders: knees, elbows, head, cape, breath, emitters. */
	function pose(rig, t, v, emit) {
		const dt = rig.last ? Math.min(0.1, Math.max(0, t - rig.last)) : 0;
		rig.last = t;
		const B = rig.bone;
		const walk = v ? v.walk : 0;
		const moving = Math.abs(walk - rig.lastWalk) > 1e-4;
		rig.lastWalk = walk;
		const ph = t + rig.phase;
		// Knees bend on the leg that swings forward, more when the hip lifts high (stomps, leaps).
		for (const k of ['L', 'R']) {
			const hip = B['hip' + k].rotation.x;
			const swingFwd = moving ? Math.max(0, (k === 'L' ? -1 : 1) * Math.cos(walk)) : 0;
			B['kn' + k].rotation.x = -(swingFwd * 0.75 + Math.max(0, hip - 0.15) * 0.9 + 0.04);
			// Elbows: relaxed bend, a little more while the arm is raised.
			const sh = B['sh' + k].rotation.x;
			B['el' + k].rotation.x = 0.22 + Math.max(0, Math.sin(sh)) * 0.15 + Math.sin(ph * 1.7) * 0.02;
		}
		// Head looks a little down at the field and sways; the cape trails when moving.
		B.head.rotation.x = 0.1 + Math.sin(ph * 0.9) * 0.03;
		B.head.rotation.y = Math.sin(ph * 0.4) * 0.12;
		B.cape.rotation.x = -(moving ? 0.2 + Math.sin(ph * 6) * 0.05 : 0.05 + Math.sin(ph * 1.3) * 0.025);
		rig.body.position.y += Math.sin(ph * 1.8) * 0.0025 * rig.H;
		for (const h of rig.halos) h.material.opacity = 0.65 + Math.sin(ph * 5 + h.id) * 0.15;
		if (!emit || !dt) return;
		for (const e of rig.emitters) {
			e.acc += e.rate * dt;
			while (e.acc >= 1) {
				e.acc -= 1;
				e.o.getWorldPosition(e.w || (e.w = new rig.root.position.constructor()));
				emit(e.kind, e.w, e.size * rig.H * 0.2);
			}
		}
		void tmpV;
	}

	return {
		has: (id) => !!R[id],
		ids: () => Object.keys(R),
		build,
		/** The full recipe of a titan (base body and its own parts, mirrored parts written out) as plain JSON. */
		recipe(id) {
			const rec = R[id];
			return { id, cls: rec.cls, units: 'titan height', build: BUILD[rec.cls], bones: bones(BUILD[rec.cls]), materials: Object.assign({}, COMMON, rec.mat), parts: allParts(id) };
		},
	};
})();
if (typeof module !== 'undefined') module.exports = DotTitans3D;
