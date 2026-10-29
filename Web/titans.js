'use strict';
// Titan art: one hand-drawn figure per kingdom titan in the icons.js ink style (flat fills, dark outline, white highlights),
// rigged into groups (root, legs, upper body, head, arms, forearms, weapons, effects) that CSS keyframes animate.
// Canvas 200 x 240, ground at y = 226. Each titan has four clips: idle (loops), atk (basic attack), a1 and a2 (its two abilities in abilities.json order).
// Silhouettes follow the master prompt (16.2): one feature per titan that reads at any distance, class language for the body.
//   DotTitans.svg(id, { view: 'bust' })  inline figure; 'bust' crops to head and shoulders for 44 px icon tiles.
//   DotTitans.play(svgEl, clip)          plays atk / a1 / a2 once on an inline figure, then returns to idle.
//   DotTitans.standalone(id, { loop })   self-contained SVG file: a portrait card, or a looping showcase of every clip.

const DotTitans = (() => {
	const INK = '#26180c';
	const WHITE = '#ffffff';
	const TEAM = '#3a8dde';
	const GROUND = 226;
	const n1 = (v) => Math.round(v * 10) / 10;
	const xy = (pt) => n1(pt[0]) + ' ' + n1(pt[1]);
	const attr = (extra) => (extra ? ' ' + extra : '');
	const P = (d, fill, extra) => '<path d="' + d + '" fill="' + fill + '"' + attr(extra) + '/>';
	const C = (x, y, r, fill, extra) => '<circle cx="' + n1(x) + '" cy="' + n1(y) + '" r="' + n1(r) + '" fill="' + fill + '"' + attr(extra) + '/>';
	const E = (x, y, rx, ry, fill, extra) => '<ellipse cx="' + n1(x) + '" cy="' + n1(y) + '" rx="' + n1(rx) + '" ry="' + n1(ry) + '" fill="' + fill + '"' + attr(extra) + '/>';
	const L = (d, col, w, extra) => '<path d="' + d + '" fill="none" stroke="' + col + '" stroke-width="' + w + '"' + attr(extra) + '/>';
	const HI = (d, w) => L(d, WHITE, w || 2.2, 'stroke-opacity=".6"');
	const SHADE = (d) => P(d, '#000', 'stroke="none" fill-opacity=".17"');
	const GLOW = (x, y, r, col, op) => C(x, y, r, col, 'stroke="none" fill-opacity="' + (op || 0.35) + '"');
	/** An outlined rod: handles, shafts, staves. */
	const ROD = (d, col, w) => L(d, INK, w + 3) + L(d, col, w);
	const TEAMFILL = (d) => P(d, TEAM, 'class="tm"');
	/** Group; `o` is the pivot the CSS rotation turns around. */
	const G = (cls, o, body) => '<g class="' + cls + '"' + (o ? ' style="transform-origin:' + n1(o[0]) + 'px ' + n1(o[1]) + 'px"' : '') + '>' + body + '</g>';

	/** Tapered capsule from (x1, y1) with radius ra to (x2, y2) with radius rb. */
	function cap(x1, y1, ra, x2, y2, rb) {
		const len = Math.hypot(x2 - x1, y2 - y1) || 1;
		const nx = -(y2 - y1) / len;
		const ny = (x2 - x1) / len;
		const pt = (x, y, r, s) => n1(x + nx * r * s) + ' ' + n1(y + ny * r * s);
		return 'M' + pt(x1, y1, ra, 1) + 'L' + pt(x2, y2, rb, 1) + 'A' + n1(rb) + ' ' + n1(rb) + ' 0 0 0 ' + pt(x2, y2, rb, -1) +
			'L' + pt(x1, y1, ra, -1) + 'A' + n1(ra) + ' ' + n1(ra) + ' 0 0 0 ' + pt(x1, y1, ra, 1) + 'Z';
	}
	/** Outlined limb with a shade band on its far side and a highlight on the near one. */
	function limb(a, ra, b, rb, fill, noHi) {
		const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
		const nx = -(b[1] - a[1]) / len;
		const ny = (b[0] - a[0]) / len;
		const off = (p, r, k) => [p[0] - nx * r * k, p[1] - ny * r * k];
		const sa = off(a, ra, 0.45);
		const sb = off(b, rb, 0.45);
		const ha = off(a, ra, -0.55);
		const hb = off(b, rb, -0.55);
		return P(cap(a[0], a[1], ra, b[0], b[1], rb), fill) + SHADE(cap(sa[0], sa[1], ra * 0.55, sb[0], sb[1], rb * 0.55)) +
			(noHi ? '' : HI('M' + xy([ha[0] + (hb[0] - ha[0]) * 0.15, ha[1] + (hb[1] - ha[1]) * 0.15]) + 'L' + xy([ha[0] + (hb[0] - ha[0]) * 0.7, ha[1] + (hb[1] - ha[1]) * 0.7]), 1.8));
	}
	const fist = (x, y, r, fill) => C(x, y, r, fill) + SHADE('M' + n1(x + r * 0.2) + ' ' + n1(y - r * 0.9) + 'A' + n1(r) + ' ' + n1(r) + ' 0 0 1 ' + n1(x + r * 0.2) + ' ' + n1(y + r * 0.95) + 'Z') +
		L('M' + n1(x - r * 0.6) + ' ' + n1(y - r * 0.15) + 'H' + n1(x + r * 0.6), INK, 1.4);
	/** Four-point sparkle. */
	const spark = (x, y, s, fill) => P('M' + n1(x) + ' ' + n1(y - s) + 'Q' + n1(x) + ' ' + n1(y) + ' ' + n1(x + s) + ' ' + n1(y) + 'Q' + n1(x) + ' ' + n1(y) + ' ' + n1(x) + ' ' + n1(y + s) +
		'Q' + n1(x) + ' ' + n1(y) + ' ' + n1(x - s) + ' ' + n1(y) + 'Q' + n1(x) + ' ' + n1(y) + ' ' + n1(x) + ' ' + n1(y - s) + 'Z', fill, 'stroke-width="1.2"');
	/** Flame tongue standing on (x, y). */
	const flame = (x, y, w, h, outer, inner) => P('M' + n1(x - w) + ' ' + n1(y) + 'C' + n1(x - w * 1.2) + ' ' + n1(y - h * 0.5) + ' ' + n1(x - w * 0.2) + ' ' + n1(y - h * 0.6) + ' ' + n1(x) + ' ' + n1(y - h) +
		'C' + n1(x + w * 0.3) + ' ' + n1(y - h * 0.55) + ' ' + n1(x + w * 1.2) + ' ' + n1(y - h * 0.45) + ' ' + n1(x + w) + ' ' + n1(y) + 'Z', outer) +
		(inner ? P('M' + n1(x - w * 0.5) + ' ' + n1(y) + 'C' + n1(x - w * 0.6) + ' ' + n1(y - h * 0.3) + ' ' + n1(x - w * 0.1) + ' ' + n1(y - h * 0.35) + ' ' + n1(x) + ' ' + n1(y - h * 0.62) +
			'C' + n1(x + w * 0.2) + ' ' + n1(y - h * 0.3) + ' ' + n1(x + w * 0.6) + ' ' + n1(y - h * 0.25) + ' ' + n1(x + w * 0.5) + ' ' + n1(y) + 'Z', inner, 'stroke="none"') : '');
	/** Jagged lightning bolt through the given points. */
	const bolt = (pts, w) => {
		const d = 'M' + pts.map(xy).join('L');
		return L(d, '#3a6bd8', w + 4, 'stroke-opacity=".45"') + L(d, '#bfe6ff', w + 1.5) + L(d, WHITE, w * 0.5);
	};
	const ring = (x, y, rx, ry, col, w) => E(x, y, rx, ry, 'none', 'stroke="' + col + '" stroke-width="' + w + '"');
	/** Stun sparkles over the enemies at both edges of a shockwave. */
	const stunStars = (y) => [[22, 0], [36, -8], [164, -8], [178, 0]].map(([x, dy]) => spark(x, y + dy, 5, '#ffe066')).join('');
	/** Point p turned by deg (CSS sense, clockwise on screen) around c. */
	const rot = (p, c, deg) => {
		const a = (deg * Math.PI) / 180;
		const dx = p[0] - c[0];
		const dy = p[1] - c[1];
		return [c[0] + dx * Math.cos(a) - dy * Math.sin(a), c[1] + dx * Math.sin(a) + dy * Math.cos(a)];
	};
	/** Where a point held in hand k ends up when the arm, forearm and weapon turn by the given angles. */
	const posed = (g, k, p, arm, fore, wpn) => rot(rot(rot(p, g['hd' + k], wpn || 0), g['el' + k], fore || 0), g['sh' + k], arm || 0);
	const dust = (x, y, s) => P('M' + n1(x - 20 * s) + ' ' + y + 'C' + n1(x - 24 * s) + ' ' + n1(y - 10 * s) + ' ' + n1(x - 10 * s) + ' ' + n1(y - 16 * s) + ' ' + n1(x - 4 * s) + ' ' + n1(y - 10 * s) +
		'C' + n1(x) + ' ' + n1(y - 20 * s) + ' ' + n1(x + 16 * s) + ' ' + n1(y - 16 * s) + ' ' + n1(x + 14 * s) + ' ' + n1(y - 6 * s) + 'C' + n1(x + 24 * s) + ' ' + n1(y - 8 * s) + ' ' + n1(x + 24 * s) + ' ' + y + ' ' + n1(x + 18 * s) + ' ' + y + 'Z', '#cbb08a', 'stroke-width="2"');

	// --- Body ------------------------------------------------------------------------------------------
	// S shoulder half-width, W waist half-width, hip joint offset, leg and arm radii (top, middle, bottom), fist radius, neck radius.
	const BUILD = {
		Juggernaut: { S: 46, W: 31, hip: 16, leg: [14, 10.5], arm: [13.5, 11.5, 10], hand: 11.5, neck: 12, headR: 19 },
		Ravager: { S: 40, W: 25, hip: 13, leg: [12, 9], arm: [11, 9.5, 8.5], hand: 9.5, neck: 10, headR: 18 },
		Warden: { S: 43, W: 28, hip: 14.5, leg: [13, 9.5], arm: [12, 10.5, 9], hand: 10.5, neck: 11, headR: 19 },
		Mystic: { S: 30, W: 19, hip: 11, leg: [0, 0], arm: [8, 7, 6], hand: 6.5, neck: 7, headR: 16 },
		Hunter: { S: 29, W: 18, hip: 10, leg: [9.5, 7], arm: [8, 6.8, 5.8], hand: 6.5, neck: 7, headR: 16 },
	};
	function rig(cls) {
		const b = BUILD[cls];
		const sh = 92;
		const waist = 138;
		const g = Object.assign({ shY: sh, waistY: waist, head: [100, 54] }, b);
		for (const s of [-1, 1]) {
			const k = s < 0 ? 'L' : 'R';
			g['sh' + k] = [100 + s * (b.S - 9), sh];
			g['el' + k] = [100 + s * (b.S + 1), sh + 29];
			g['wr' + k] = [100 + s * (b.S + 3), sh + 54];
			g['hd' + k] = [100 + s * (b.S + 3), sh + 59];
			g['hip' + k] = [100 + s * b.hip, waist + 8];
			g['kn' + k] = [100 + s * (b.hip + 3), 181];
			g['an' + k] = [100 + s * (b.hip + 4), 209];
		}
		return g;
	}
	function torsoPath(g) {
		const { S, W, shY, waistY } = g;
		const top = shY - 11;
		return 'M' + (100 - S) + ' ' + (shY - 1) + 'Q' + (100 - S) + ' ' + top + ' ' + (100 - S + 15) + ' ' + top + 'H' + (100 + S - 15) + 'Q' + (100 + S) + ' ' + top + ' ' + (100 + S) + ' ' + (shY - 1) +
			'C' + (100 + S + 1) + ' ' + (shY + 22) + ' ' + (100 + W + 5) + ' ' + (waistY - 24) + ' ' + (100 + W) + ' ' + waistY + 'H' + (100 - W) +
			'C' + (100 - W - 5) + ' ' + (waistY - 24) + ' ' + (100 - S - 1) + ' ' + (shY + 22) + ' ' + (100 - S) + ' ' + (shY - 1) + 'Z';
	}
	const torsoShade = (g) => SHADE('M' + (100 + g.S * 0.35) + ' ' + (g.shY - 10) + 'H' + (100 + g.S - 15) + 'Q' + (100 + g.S) + ' ' + (g.shY - 11) + ' ' + (100 + g.S) + ' ' + (g.shY - 1) +
		'C' + (100 + g.S + 1) + ' ' + (g.shY + 22) + ' ' + (100 + g.W + 5) + ' ' + (g.waistY - 24) + ' ' + (100 + g.W) + ' ' + g.waistY + 'H' + (100 + g.W * 0.45) +
		'C' + (100 + g.W * 0.7) + ' ' + (g.waistY - 30) + ' ' + (100 + g.S * 0.55) + ' ' + (g.shY + 10) + ' ' + (100 + g.S * 0.35) + ' ' + (g.shY - 10) + 'Z');
	/** Muscle lines on a bare or cuirassed chest. */
	const pecs = (g, col) => L('M' + (100 - g.S * 0.62) + ' ' + (g.shY + 4) + 'C' + (100 - g.S * 0.5) + ' ' + (g.shY + 20) + ' ' + (100 - 6) + ' ' + (g.shY + 20) + ' 100 ' + (g.shY + 12) +
		'C' + (100 + 6) + ' ' + (g.shY + 20) + ' ' + (100 + g.S * 0.5) + ' ' + (g.shY + 20) + ' ' + (100 + g.S * 0.62) + ' ' + (g.shY + 4) +
		'M100 ' + (g.shY + 12) + 'V' + (g.waistY - 8) + 'M' + (100 - g.W * 0.55) + ' ' + (g.shY + 32) + 'H' + (100 + g.W * 0.55) + 'M' + (100 - g.W * 0.5) + ' ' + (g.shY + 44) + 'H' + (100 + g.W * 0.5), col || INK, 1.6, 'stroke-opacity=".55"');
	const belt = (g, col, buckle) => P('M' + (100 - g.W - 4) + ' ' + (g.waistY - 7) + 'H' + (100 + g.W + 4) + 'V' + (g.waistY + 5) + 'H' + (100 - g.W - 4) + 'Z', col) +
		(buckle ? P('M93 ' + (g.waistY - 9) + 'H107V' + (g.waistY + 7) + 'H93Z', buckle) + P('M97 ' + (g.waistY - 4) + 'H103V' + (g.waistY + 2) + 'H97Z', INK, 'stroke="none"') : '');
	/** Team-coloured cloth hanging from the belt (master prompt 16.2: team colour on tabards). */
	const tabard = (g, len) => {
		const y = g.waistY + 5;
		const w = Math.max(g.W * 0.55, 10);
		return TEAMFILL('M' + (100 - w) + ' ' + y + 'H' + (100 + w) + 'L' + (100 + w * 0.8) + ' ' + (y + len) + 'Q100 ' + (y + len + 6) + ' ' + (100 - w * 0.8) + ' ' + (y + len) + 'Z') +
			L('M' + (100 - w * 0.6) + ' ' + (y + 5) + 'L' + (100 - w * 0.5) + ' ' + (y + len - 4), WHITE, 1.6, 'stroke-opacity=".35"') +
			SHADE('M100 ' + y + 'H' + (100 + w) + 'L' + (100 + w * 0.8) + ' ' + (y + len) + 'Q100 ' + (y + len + 6) + ' 100 ' + (y + len + 6) + 'Z');
	};
	/** Boot or sandal around the ankle; s = -1 left, 1 right. */
	function boot(g, s, fill, tall) {
		const a = g['an' + (s < 0 ? 'L' : 'R')];
		const w = g.leg[1] + 2;
		const top = a[1] - (tall || 6);
		const x = a[0];
		const toe = x + s * 4;
		return P('M' + n1(x - w) + ' ' + top + 'H' + n1(x + w) + 'L' + n1(x + w + 1) + ' ' + (GROUND - 9) + 'Q' + n1(toe + w + 4) + ' ' + (GROUND - 7) + ' ' + n1(toe + w + 3) + ' ' + (GROUND - 1) +
			'H' + n1(toe - w - 3) + 'Q' + n1(toe - w - 4) + ' ' + (GROUND - 7) + ' ' + n1(x - w - 1) + ' ' + (GROUND - 9) + 'Z', fill) +
			HI('M' + n1(x - w + 3) + ' ' + (top + 3) + 'V' + (GROUND - 10), 1.6);
	}
	function leg(g, s, o) {
		const k = s < 0 ? 'L' : 'R';
		const hip = g['hip' + k];
		const kn = g['kn' + k];
		const an = g['an' + k];
		return limb(hip, g.leg[0], kn, g.leg[0] * 0.8, o.leg) + limb(kn, g.leg[0] * 0.78, an, g.leg[1], o.shin || o.leg) +
			(o.knee ? C(kn[0], kn[1], g.leg[0] * 0.55, o.knee) + HI('M' + n1(kn[0] - 3) + ' ' + n1(kn[1] - 3) + 'l2 -2') : '') +
			boot(g, s, o.boot, o.bootTall) + (o.legExtra ? o.legExtra(s, hip, kn, an) : '');
	}
	/** One arm: upper arm, forearm group (forearm, item, fist), pauldron. */
	function arm(g, s, o, item, itemFront) {
		const k = s < 0 ? 'L' : 'R';
		const sh = o['sh' + k] || g['sh' + k];
		const el = o['el' + k] || g['el' + k];
		const wr = o['wr' + k] || g['wr' + k];
		const hd = o['hd' + k] || g['hd' + k];
		const fore = limb(el, g.arm[1], wr, g.arm[2], o.arm) +
			(o.bracer ? P(cap(el[0] + (wr[0] - el[0]) * 0.45, el[1] + (wr[1] - el[1]) * 0.45, g.arm[1] * 1.05, wr[0], wr[1], g.arm[2] * 1.12), o.bracer) +
				HI('M' + n1(el[0] + (wr[0] - el[0]) * 0.5 - s * g.arm[2] * 0.5) + ' ' + n1(el[1] + (wr[1] - el[1]) * 0.55) + 'l0 8', 1.5) : '');
		const weapon = item ? G('p-wpn' + k, hd, item + g.fxAt('wpn' + k)) : '';
		const forearm = G('p-fore' + k, el, fore + (itemFront ? '' : weapon) + fist(hd[0], hd[1], g.hand, o.hand || o.arm) + (itemFront ? weapon : '') + g.fxAt('fore' + k));
		const pad = o.pauldron ? P('M' + n1(sh[0] - g.arm[0] * 1.35) + ' ' + n1(sh[1] + 6) + 'C' + n1(sh[0] - g.arm[0] * 1.4) + ' ' + n1(sh[1] - 16) + ' ' + n1(sh[0] + g.arm[0] * 1.4) + ' ' + n1(sh[1] - 16) + ' ' +
			n1(sh[0] + g.arm[0] * 1.35) + ' ' + n1(sh[1] + 6) + 'Q' + n1(sh[0]) + ' ' + n1(sh[1] + 1) + ' ' + n1(sh[0] - g.arm[0] * 1.35) + ' ' + n1(sh[1] + 6) + 'Z', o.pauldron) +
			HI('M' + n1(sh[0] - g.arm[0] * 0.9) + ' ' + n1(sh[1] - 3) + 'Q' + n1(sh[0] - g.arm[0] * 0.4) + ' ' + n1(sh[1] - 10) + ' ' + n1(sh[0] + g.arm[0] * 0.2) + ' ' + n1(sh[1] - 10.5)) : '';
		return G('p-arm' + k, sh, limb(sh, g.arm[0], el, g.arm[1], o.arm) + forearm + pad + (o.armExtra ? o.armExtra(s, sh, el) : ''));
	}
	/** Rounded head outline centred on (x, y). */
	const skull = (x, y, r, jaw) => {
		const j = jaw || 1;
		return 'M' + n1(x - r) + ' ' + n1(y - 2) + 'C' + n1(x - r) + ' ' + n1(y - r * 1.3) + ' ' + n1(x + r) + ' ' + n1(y - r * 1.3) + ' ' + n1(x + r) + ' ' + n1(y - 2) +
			'C' + n1(x + r) + ' ' + n1(y + r * 0.7 * j) + ' ' + n1(x + r * 0.5) + ' ' + n1(y + r * 1.05 * j) + ' ' + n1(x) + ' ' + n1(y + r * 1.1 * j) +
			'C' + n1(x - r * 0.5) + ' ' + n1(y + r * 1.05 * j) + ' ' + n1(x - r) + ' ' + n1(y + r * 0.7 * j) + ' ' + n1(x - r) + ' ' + n1(y - 2) + 'Z';
	};
	const eyes = (x, y, dx, col, w) => L('M' + n1(x - dx - 4) + ' ' + n1(y) + 'l7 1.5M' + n1(x + dx + 4) + ' ' + n1(y) + 'l-7 1.5', col, w || 3.2) +
		L('M' + n1(x - dx - 5) + ' ' + n1(y - 4) + 'l9 2.5M' + n1(x + dx + 5) + ' ' + n1(y - 4) + 'l-9 2.5', INK, 2.4);

	// --- Titans -----------------------------------------------------------------------------------------
	// Pose keyframes: [t (0..1 of the clip), {x, y, r, s, sx, sy}, opacity, easing into the next frame]. Parts not listed stay at rest.
	const R = (r, more) => Object.assign({ r }, more);
	const EASE_HIT = 'cubic-bezier(.55,0,1,.6)';
	const EASE_OUT = 'cubic-bezier(.2,.7,.3,1)';

	const T = {};

	T['titan.talos'] = {
		name: 'Талос', cls: 'Juggernaut', classRu: 'Чемпион', stars: 1,
		abil: ['Расплавленный ихор', 'Бронзовый оплот'],
		crop: [56, 2, 88, 88],
		draw(g) {
			const bronze = '#c98a3a';
			const bronzeD = '#8a5520';
			const ichor = '#ffa63a';
			const o = { leg: bronze, shin: bronze, knee: bronzeD, boot: bronzeD, arm: bronze, bracer: bronzeD, pauldron: bronzeD, hand: bronze };
			const rivets = (pts) => pts.map(([x, y]) => C(x, y, 1.6, '#f4c44f', 'stroke-width="1"')).join('');
			const torso = P(torsoPath(g), bronze) + torsoShade(g) + pecs(g, bronzeD) +
				rivets([[100 - g.S + 12, g.shY + 2], [100 + g.S - 12, g.shY + 2], [100 - g.W + 4, g.waistY - 14], [100 + g.W - 4, g.waistY - 14]]) +
				HI('M' + (100 - g.S + 8) + ' ' + (g.shY + 6) + 'C' + (100 - g.S + 6) + ' ' + (g.shY + 20) + ' ' + (100 - g.W) + ' ' + (g.waistY - 26) + ' ' + (100 - g.W + 2) + ' ' + (g.waistY - 14));
			// The single vein of ichor from the neck to the right ankle, sealed by a bronze nail (the myth's weak spot).
			const vein = 'M104 76C110 96 118 110 112 ' + (g.waistY - 4);
			const veinLeg = 'M' + (g.hipR[0] + 2) + ' ' + (g.hipR[1] + 6) + 'C' + (g.hipR[0] + 6) + ' 170 ' + (g.anR[0] - 2) + ' 190 ' + (g.anR[0] + 2) + ' ' + (g.anR[1] - 4);
			const glowLine = (d) => L(d, '#ff6a1a', 5, 'stroke-opacity=".45" class="p-vein"') + L(d, ichor, 2.2) + L(d, '#fff3c0', 0.9);
			const head = P('M' + 60 + ' ' + 20 + 'C70 2 130 2 140 20C134 26 124 26 118 28L82 28C76 26 66 26 60 20Z', '#a22d22') +
				L('M70 14L74 24M80 9L83 25M90 6L91 26M100 5V27M110 6L109 26M120 9L117 25M130 14L126 24', '#6e1a14', 1.6) +
				P(skull(100, 52, g.headR, 1.05), bronze) +
				P('M' + (100 - g.headR + 3) + ' 54C' + (100 - g.headR + 2) + ' 70 ' + (100 - 8) + ' 76 ' + (100 - 6) + ' 76L94 58H106L106 76C108 76 ' + (100 + g.headR - 2) + ' 70 ' + (100 + g.headR - 3) + ' 54Z', bronzeD, 'fill-opacity=".55" stroke="none"') +
				P('M84 44H116L113 51H103V73H97V51H87Z', '#2a1608') + L('M88 47.5h8M104 47.5h8', ichor, 2.6) + C(100, 33, 2, '#f4c44f', 'stroke-width="1"') +
				HI('M86 36C90 32 96 31 100 31');
			const tunic = P('M' + (100 - g.W - 4) + ' ' + (g.waistY + 3) + 'L' + (100 - g.W - 8) + ' ' + (g.waistY + 20) + 'H' + (100 + g.W + 8) + 'L' + (100 + g.W + 4) + ' ' + (g.waistY + 3) + 'Z', bronzeD) +
				L('M' + (100 - g.W) + ' ' + (g.waistY + 6) + 'v12M' + (100 - g.W / 2) + ' ' + (g.waistY + 6) + 'v13M' + (100 + g.W / 2) + ' ' + (g.waistY + 6) + 'v13M' + (100 + g.W) + ' ' + (g.waistY + 6) + 'v12', INK, 1.4);
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o) + glowLine(veinLeg) + C(g.anR[0] + 2, g.anR[1] - 4, 3, '#f4c44f')),
				torso: tunic + P('M' + (100 - g.neck) + ' 82V66H' + (100 + g.neck) + 'V82Z', bronzeD) + torso + glowLine(vein) + belt(g, bronzeD, '#f4c44f') + tabard(g, 34),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o),
			};
		},
		fx(g) {
			const ichor = '#ffa63a';
			const stream = (s) => {
				const k = s < 0 ? 'L' : 'R';
				const h = posed(g, k, g['hd' + k], s * -20, s * 70);
				const d = 'M' + xy([h[0], h[1] + 6]) + 'C' + xy([h[0] - s * 4, h[1] + 40]) + ' ' + xy([h[0] - s * 22, h[1] + 50]) + ' ' + xy([h[0] - s * 30, GROUND - 4]);
				return L(d, '#ff6a1a', 12, 'stroke-opacity=".5"') + L(d, ichor, 7) + L(d, '#fff3c0', 2.2) + C(h[0], h[1] + 4, 8, '#ffd84f', 'stroke="none" fill-opacity=".8"');
			};
			const hl = posed(g, 'L', g.hdL, 20, -70);
			const pool = (x) => E(x, GROUND - 2, 22, 5, '#ff7a1a', 'fill-opacity=".85"') + flame(x - 8, GROUND - 3, 5, 16, '#ff8c2a', '#ffd84f') + flame(x + 7, GROUND - 3, 4, 12, '#ff8c2a', '#ffd84f');
			const hexes = [];
			for (let i = 0; i < 6; i++) {
				const a = (i * Math.PI) / 3;
				hexes.push(xy([100 + Math.cos(a) * 78, 132 + Math.sin(a) * 92]));
			}
			return {
				shock: [[100, GROUND], ring(100, GROUND, 70, 12, '#f4f1ff', 4) + dust(46, GROUND, 1.1) + dust(154, GROUND, 1.1) + spark(100, 168, 14, '#fff3c0')],
				pourL: [[hl[0], hl[1]], stream(-1)],
				pourR: [[200 - hl[0], hl[1]], stream(1)],
				burn: [[100, GROUND], pool(hl[0] + 30) + pool(170 - hl[0])],
				chest: [[100, 108], GLOW(100, 108, 26, '#ffa63a', 0.55) + GLOW(100, 108, 14, '#fff3c0', 0.8)],
				dome: [[100, 140], P('M' + hexes.join('L') + 'Z', '#f4c44f', 'fill-opacity=".18" stroke="#f4c44f" stroke-width="3"') +
					L('M100 40V224M33 86L167 178M33 178L167 86', '#f4c44f', 1.6, 'stroke-opacity=".6"') + spark(46, 70, 7, WHITE) + spark(160, 196, 6, WHITE)],
			};
		},
		clips: {
			idle: { dur: 1.8, k: {
				upper: [[0, {}], [0.5, { y: 1.8 }], [1, {}]],
				head: [[0, {}], [0.5, R(-1.5, { y: 0.6 })], [1, {}]],
				armL: [[0, {}], [0.5, R(2)], [1, {}]], armR: [[0, {}], [0.5, R(-2)], [1, {}]],
				vein: [[0, {}, 0.35], [0.5, {}, 1], [1, {}, 0.35]],
			} },
			atk: { dur: 1.4, k: {
				armL: [[0, {}], [0.4, R(165)], [0.58, R(-35), 1, EASE_HIT], [0.8, R(-35)], [1, {}]],
				armR: [[0, {}], [0.4, R(-165)], [0.58, R(35), 1, EASE_HIT], [0.8, R(35)], [1, {}]],
				foreL: [[0, {}], [0.4, R(-30)], [0.58, R(-20)], [1, {}]], foreR: [[0, {}], [0.4, R(30)], [0.58, R(20)], [1, {}]],
				upper: [[0, {}], [0.4, { y: -4 }], [0.58, { y: 7 }], [0.8, { y: 7 }], [1, {}]],
				shock: [[0, { s: 0.2 }, 0], [0.58, { s: 0.2 }, 0], [0.62, { s: 0.4 }, 1], [1, { s: 1.4 }, 0]],
			} },
			a1: { dur: 2.2, k: {
				armL: [[0, {}], [0.2, R(20)], [0.85, R(20)], [1, {}]], armR: [[0, {}], [0.2, R(-20)], [0.85, R(-20)], [1, {}]],
				foreL: [[0, {}], [0.2, R(-70)], [0.85, R(-70)], [1, {}]], foreR: [[0, {}], [0.2, R(70)], [0.85, R(70)], [1, {}]],
				upper: [[0, {}], [0.15, { y: -3 }], [0.25, {}], [0.85, {}], [1, {}]],
				chest: [[0, { s: 0.3 }, 0], [0.15, { s: 1 }, 1], [0.8, { s: 1.2 }, 1], [1, { s: 0.6 }, 0]],
				pourL: [[0, { s: 0.3 }, 0], [0.22, { s: 0.3 }, 0], [0.32, { s: 1 }, 1], [0.8, { s: 1 }, 1], [0.9, { s: 0.6 }, 0]],
				pourR: [[0, { s: 0.3 }, 0], [0.22, { s: 0.3 }, 0], [0.32, { s: 1 }, 1], [0.8, { s: 1 }, 1], [0.9, { s: 0.6 }, 0]],
				burn: [[0, { sy: 0.2 }, 0], [0.32, { sy: 0.2 }, 0], [0.42, {}, 1], [0.9, {}, 1], [1, { sy: 0.6 }, 0]],
				vein: [[0, {}, 0.4], [0.2, {}, 1], [0.85, {}, 1], [1, {}, 0.4]],
			} },
			a2: { dur: 2.2, k: {
				armL: [[0, {}], [0.18, R(-28)], [0.82, R(-28)], [1, {}]], armR: [[0, {}], [0.18, R(28)], [0.82, R(28)], [1, {}]],
				foreL: [[0, {}], [0.18, R(-95)], [0.82, R(-95)], [1, {}]], foreR: [[0, {}], [0.18, R(95)], [0.82, R(95)], [1, {}]],
				root: [[0, {}], [0.18, { y: 4, sy: 0.97 }], [0.82, { y: 4, sy: 0.97 }], [1, {}]],
				head: [[0, {}], [0.18, { y: 4 }], [0.82, { y: 4 }], [1, {}]],
				dome: [[0, { s: 0.6 }, 0], [0.2, { s: 0.6 }, 0], [0.34, { s: 1.04 }, 1], [0.42, { s: 1 }, 0.9], [0.62, { s: 1.02 }, 1], [0.82, { s: 1 }, 0.9], [1, { s: 1.1 }, 0]],
			} },
		},
	};

	T['titan.thor'] = {
		name: 'Тор', cls: 'Ravager', classRu: 'Берсерк', stars: 4,
		abil: ['Бросок молота', 'Раскат грома'],
		crop: [54, 12, 92, 92],
		draw(g) {
			const skin = '#e8b48a';
			const steel = '#9aa8c2';
			const steelD = '#5d6a80';
			const red = '#c4502a';
			const cape = '#9b2a22';
			const o = { leg: '#4a5160', shin: '#4a5160', boot: '#8a5a32', bootTall: 18, arm: skin, bracer: steelD, pauldron: steel, hand: skin,
				legExtra: (s, hip, kn) => L('M' + n1(kn[0] - 9) + ' ' + n1(kn[1] + 2) + 'l18 4M' + n1(kn[0] - 8) + ' ' + n1(kn[1] + 10) + 'l17 4', '#2e333d', 1.6) };
			const capeBack = P('M' + (100 - g.S + 4) + ' ' + (g.shY - 8) + 'C' + (100 - g.S - 22) + ' 140 ' + (100 - g.S - 30) + ' 190 ' + (100 - g.S - 26) + ' 212L' + (100 - 20) + ' 204L100 214L' + (100 + 20) + ' 204L' + (100 + g.S + 26) + ' 212C' + (100 + g.S + 30) + ' 190 ' + (100 + g.S + 22) + ' 140 ' + (100 + g.S - 4) + ' ' + (g.shY - 8) + 'Z', cape) +
				SHADE('M' + (100 + 12) + ' 100L' + (100 + g.S + 22) + ' 140C' + (100 + g.S + 30) + ' 190 ' + (100 + g.S + 28) + ' 200 ' + (100 + g.S + 26) + ' 212L' + (100 + 20) + ' 204Z');
			const chest = P(torsoPath(g), steel) + torsoShade(g) +
				L('M' + (100 - g.S + 8) + ' ' + (g.shY + 10) + 'H' + (100 + g.S - 8) + 'M' + (100 - g.W - 1) + ' ' + (g.shY + 26) + 'H' + (100 + g.W + 1) + 'M' + (100 - g.W) + ' ' + (g.shY + 38) + 'H' + (100 + g.W), steelD, 1.6) +
				P('M100 ' + (g.shY + 2) + 'L108 ' + (g.shY + 16) + 'L100 ' + (g.shY + 30) + 'L92 ' + (g.shY + 16) + 'Z', '#f4c44f') + L('M100 ' + (g.shY + 8) + 'V' + (g.shY + 24) + 'M96 ' + (g.shY + 12) + 'L104 ' + (g.shY + 20), INK, 1.4) +
				HI('M' + (100 - g.S + 6) + ' ' + (g.shY + 4) + 'C' + (100 - g.S + 6) + ' ' + (g.shY + 20) + ' ' + (100 - g.W) + ' ' + (g.waistY - 24) + ' ' + (100 - g.W + 2) + ' ' + (g.waistY - 12));
			const r = g.headR;
			const head = P('M' + (100 - r - 3) + ' 46C' + (100 - r - 8) + ' 70 ' + (100 - r - 4) + ' 88 ' + (100 - r + 2) + ' 92L' + (100 + r - 2) + ' 92C' + (100 + r + 4) + ' 88 ' + (100 + r + 8) + ' 70 ' + (100 + r + 3) + ' 46Z', red) +
				P(skull(100, 54, r), skin) + SHADE('M108 40C118 44 118 64 110 72L106 60Z') +
				P('M' + (100 - r + 1) + ' 58C' + (100 - r + 2) + ' 84 ' + (100 - 8) + ' 98 100 102C' + (100 + 8) + ' 98 ' + (100 + r - 2) + ' 84 ' + (100 + r - 1) + ' 58C' + (100 + r - 6) + ' 64 ' + (100 + 6) + ' 64 100 66C' + (100 - 6) + ' 64 ' + (100 - r + 6) + ' 64 ' + (100 - r + 1) + ' 58Z', red) +
				L('M92 76L96 92M100 72V96M108 76L104 92', '#8a2e14', 1.6) + P('M92 66Q100 62 108 66Q100 70 92 66Z', '#8a2e14') +
				eyes(100, 54, 5, '#bfe6ff', 3.4) +
				// Winged helmet: the silhouette read from afar, with the hammer.
				P('M' + (100 - r - 1) + ' 48C' + (100 - r) + ' 26 ' + (100 + r) + ' 26 ' + (100 + r + 1) + ' 48Z', steel) + P('M' + (100 - r - 3) + ' 44H' + (100 + r + 3) + 'V50H' + (100 - r - 3) + 'Z', steelD) +
				L('M100 30V44', steelD, 1.6) + HI('M88 40C88 36 92 33 96 32') +
				P('M' + (100 - r - 1) + ' 44C' + (100 - r - 14) + ' 36 ' + (100 - r - 20) + ' 22 ' + (100 - r - 16) + ' 10C' + (100 - r - 10) + ' 20 ' + (100 - r - 6) + ' 24 ' + (100 - r) + ' 32C' + (100 - r - 4) + ' 22 ' + (100 - r - 6) + ' 16 ' + (100 - r - 4) + ' 8C' + (100 - r + 2) + ' 18 ' + (100 - r + 4) + ' 30 ' + (100 - r + 4) + ' 38Z', '#f0f4fa') +
				P('M' + (100 + r + 1) + ' 44C' + (100 + r + 14) + ' 36 ' + (100 + r + 20) + ' 22 ' + (100 + r + 16) + ' 10C' + (100 + r + 10) + ' 20 ' + (100 + r + 6) + ' 24 ' + (100 + r) + ' 32C' + (100 + r + 4) + ' 22 ' + (100 + r + 6) + ' 16 ' + (100 + r + 4) + ' 8C' + (100 + r - 2) + ' 18 ' + (100 + r - 4) + ' 30 ' + (100 + r - 4) + ' 38Z', '#f0f4fa');
			const hd = g.hdR;
			// Mjolnir hangs head down from the fist; raised overhead it points to the sky.
			const hammer = ROD('M' + xy([hd[0], hd[1] - 12]) + 'V' + n1(hd[1] + 26), '#6b4222', 5) + L('M' + n1(hd[0] - 3) + ' ' + n1(hd[1] + 8) + 'l6 3M' + n1(hd[0] - 3) + ' ' + n1(hd[1] + 15) + 'l6 3', INK, 1.2) +
				C(hd[0], hd[1] - 14, 3.2, '#f4c44f') +
				P('M' + n1(hd[0] - 20) + ' ' + n1(hd[1] + 24) + 'H' + n1(hd[0] + 20) + 'V' + n1(hd[1] + 48) + 'H' + n1(hd[0] - 20) + 'Z', '#7d8593') +
				P('M' + n1(hd[0] - 22) + ' ' + n1(hd[1] + 22) + 'H' + n1(hd[0] - 14) + 'V' + n1(hd[1] + 50) + 'H' + n1(hd[0] - 22) + 'ZM' + n1(hd[0] + 14) + ' ' + n1(hd[1] + 22) + 'H' + n1(hd[0] + 22) + 'V' + n1(hd[1] + 50) + 'H' + n1(hd[0] + 14) + 'Z', '#5d6676') +
				L('M' + n1(hd[0] - 6) + ' ' + n1(hd[1] + 30) + 'L' + n1(hd[0] + 2) + ' ' + n1(hd[1] + 36) + 'L' + n1(hd[0] - 4) + ' ' + n1(hd[1] + 38) + 'L' + n1(hd[0] + 5) + ' ' + n1(hd[1] + 43), '#bfe6ff', 2) +
				HI('M' + n1(hd[0] - 11) + ' ' + n1(hd[1] + 28) + 'V' + n1(hd[1] + 44));
			return {
				back: capeBack,
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + chest + belt(g, '#6b4222', '#f4c44f') + tabard(g, 38),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o, hammer),
			};
		},
		fx(g) {
			const hd = g.hdR;
			const head = [hd[0], hd[1] + 36];
			const hammer = P('M-20 -12H20V12H-20Z', '#7d8593') + ROD('M0 -12V-34', '#6b4222', 5) + C(0, -36, 3, '#f4c44f') + L('M-6 -6L2 0L-4 2L5 7', '#bfe6ff', 2);
			const p0 = posed(g, 'R', head, -100);
			this.flyFrom = p0;
			this.flyBack = posed(g, 'R', head, -30);
			return {
				spark: [head, bolt([[head[0] - 14, head[1] - 16], [head[0] + 2, head[1] - 4], [head[0] - 8, head[1] + 4], [head[0] + 10, head[1] + 18]], 2) + spark(head[0], head[1], 18, '#e6f4ff') + spark(head[0] + 22, head[1] - 14, 6, WHITE), 'wpnR'],
				fly: [p0, '<g transform="translate(' + xy(p0) + ')">' + L('M-46 4H-14M-40 -8H-16', '#bfe6ff', 3, 'stroke-opacity=".7"') + hammer + '</g>'],
				flash: [[100, 128], spark(100, 128, 30, '#e6f4ff') + bolt([[70, 100], [86, 118], [76, 126], [92, 140]], 1.5) + bolt([[130, 100], [116, 116], [126, 124], [110, 140]], 1.5)],
				// Rides on the hammer: drawn pointing down in the hand's frame, it points to the sky when the hammer is raised.
				sky: [head, bolt([[head[0], head[1] + 10], [head[0] + 8, head[1] + 40], [head[0] - 6, head[1] + 62], [head[0] + 6, head[1] + 96], [head[0] - 4, head[1] + 150]], 3), 'wpnR'],
				aura: [head, GLOW(head[0], head[1], 30, '#bfe6ff', 0.5) + spark(head[0] - 22, head[1] - 12, 6, WHITE) + spark(head[0] + 24, head[1] + 10, 5, WHITE), 'wpnR'],
				clap: [[100, GROUND], ring(100, GROUND, 92, 16, '#bfe6ff', 5) + ring(100, GROUND, 66, 11, WHITE, 3) +
					bolt([[24, GROUND - 4], [40, GROUND - 30], [30, GROUND - 46]], 1.5) + bolt([[176, GROUND - 4], [160, GROUND - 32], [172, GROUND - 50]], 1.5)],
				stun: [[100, 190], stunStars(190)],
			};
		},
		clips(g) {
			this.fx(g);
			const d = (p) => ({ x: p[0] - this.flyFrom[0], y: p[1] - this.flyFrom[1] });
			return {
				idle: { dur: 1.6, k: {
					upper: [[0, {}], [0.5, { y: 1.5 }], [1, {}]],
					armR: [[0, {}], [0.5, R(-3)], [1, {}]], armL: [[0, {}], [0.5, R(3)], [1, {}]],
					head: [[0, {}], [0.5, R(2)], [1, {}]],
				} },
				atk: { dur: 1.3, k: {
					armR: [[0, {}], [0.42, R(-170)], [0.58, R(25), 1, EASE_HIT], [0.8, R(25)], [1, {}]],
					foreR: [[0, {}], [0.42, R(-20)], [0.58, R(10)], [1, {}]],
					upper: [[0, {}], [0.42, R(-4)], [0.58, R(5, { y: 4 })], [0.8, R(5, { y: 4 })], [1, {}]],
					spark: [[0, { s: 0.3 }, 0], [0.57, { s: 0.3 }, 0], [0.6, { s: 1 }, 1], [0.85, { s: 1.2 }, 0]],
				} },
				// The hammer flies at the foe in front (it grows as it nears the viewer), strikes, and comes back to the hand.
				a1: { dur: 2.4, k: {
					armR: [[0, {}], [0.22, R(-160)], [0.32, R(20), 1, EASE_HIT], [0.6, R(20)], [0.74, R(-30)], [0.84, R(-30)], [1, {}]],
					foreR: [[0, {}], [0.22, R(-20)], [0.32, R(0)], [1, {}]],
					wpnR: [[0, {}, 1], [0.27, {}, 1], [0.28, {}, 0], [0.81, {}, 0], [0.82, {}, 1]],
					upper: [[0, {}], [0.22, R(-6)], [0.32, R(6)], [0.6, R(2)], [1, {}]],
					fly: [[0, {}, 0], [0.27, {}, 0], [0.28, {}, 1], [0.5, Object.assign(d([100, 128]), { r: 540, s: 2.4 }), 1, EASE_OUT], [0.54, Object.assign(d([100, 124]), { r: 600, s: 2.3 }), 1],
						[0.81, Object.assign(d(this.flyBack), { r: 1080, s: 1 }), 1], [0.82, Object.assign(d(this.flyBack), { r: 1080 }), 0]],
					flash: [[0, { s: 0.3 }, 0], [0.5, { s: 0.3 }, 0], [0.53, { s: 1.2 }, 1], [0.66, { s: 1.4 }, 0]],
				} },
				a2: { dur: 2.4, k: {
					armR: [[0, {}], [0.22, R(-178)], [0.55, R(-178)], [0.66, R(15), 1, EASE_HIT], [0.85, R(15)], [1, {}]],
					foreR: [[0, {}], [0.22, R(4)], [0.55, R(4)], [0.66, R(10)], [1, {}]],
					armL: [[0, {}], [0.22, R(40)], [0.55, R(40)], [0.66, R(-10)], [1, {}]],
					upper: [[0, {}], [0.22, { y: -4 }], [0.55, { y: -4 }], [0.66, { y: 6 }], [0.85, { y: 6 }], [1, {}]],
					sky: [[0, {}, 0], [0.3, {}, 0], [0.32, {}, 1], [0.38, {}, 0.2], [0.4, {}, 1], [0.5, {}, 0]],
					aura: [[0, { s: 0.4 }, 0], [0.32, { s: 0.4 }, 0], [0.36, { s: 1.2 }, 1], [0.56, { s: 1 }, 1], [0.6, { s: 0.6 }, 0]],
					clap: [[0, { s: 0.2 }, 0], [0.66, { s: 0.2 }, 0], [0.7, { s: 0.6 }, 1], [1, { s: 1.3 }, 0]],
					stun: [[0, {}, 0], [0.7, {}, 0], [0.74, { y: -4 }, 1], [0.95, {}, 1], [1, {}, 0]],
				} },
			};
		},
	};

	T['titan.minotaur'] = {
		name: 'Минотавр', cls: 'Ravager', classRu: 'Берсерк', stars: 1,
		abil: ['Рывок по лабиринту', 'Кровавое бешенство'],
		crop: [46, 2, 108, 108],
		draw(g) {
			const fur = '#7a5238';
			const furD = '#4e3222';
			const leather = '#5a3a24';
			const horn = '#efe2c4';
			const o = { leg: fur, shin: furD, boot: '#2e2018', bootTall: 4, arm: fur, bracer: leather, hand: fur,
				legExtra: (s, hip, kn, an) => L('M' + n1(an[0] - 8) + ' ' + n1(an[1] + 6) + 'L' + n1(an[0] - 4) + ' ' + n1(an[1] - 6) + 'M' + n1(an[0] + 8) + ' ' + n1(an[1] + 6) + 'L' + n1(an[0] + 4) + ' ' + n1(an[1] - 6), '#3a2418', 1.6) +
					P('M' + n1(an[0] - 1) + ' ' + (GROUND - 1) + 'V' + (GROUND - 12), 'none', 'stroke-width="2"') };
			const chest = P(torsoPath(g), fur) + torsoShade(g) + pecs(g, '#3a2418') +
				ROD('M' + (100 - g.S + 6) + ' ' + (g.shY - 6) + 'L' + (100 + g.W - 2) + ' ' + (g.waistY - 6), leather, 5) + C(100 - 6, g.shY + 18, 3.5, '#c48a22') +
				HI('M' + (100 - g.S + 6) + ' ' + (g.shY + 8) + 'C' + (100 - g.S + 6) + ' ' + (g.shY + 22) + ' ' + (100 - g.W) + ' ' + (g.waistY - 24) + ' ' + (100 - g.W + 2) + ' ' + (g.waistY - 14));
			// Shaggy mane over the shoulders.
			let mane = 'M' + (100 - g.S - 6) + ' ' + (g.shY + 4);
			for (let i = 0; i <= 8; i++) {
				const x = 100 - g.S - 6 + (i * (2 * g.S + 12)) / 8;
				mane += 'L' + n1(x + (2 * g.S + 12) / 16) + ' ' + (g.shY + (i % 2 ? 10 : 16));
			}
			mane += 'L' + (100 + g.S + 6) + ' ' + (g.shY + 4) + 'C' + (100 + g.S) + ' 66 ' + (100 - g.S) + ' 66 ' + (100 - g.S - 6) + ' ' + (g.shY + 4) + 'Z';
			const head = P(mane, furD) +
				// Horns: the Minotaur's silhouette.
				P('M80 40C64 42 48 36 42 18C40 12 46 10 48 16C54 28 64 30 80 30Z', horn) + P('M120 40C136 42 152 36 158 18C160 12 154 10 152 16C146 28 136 30 120 30Z', horn) +
				L('M60 34l3 -6M68 37l2 -7M140 34l-3 -6M132 37l-2 -7', '#b0a080', 1.4) + P('M42 18C40 12 46 10 48 16Z', '#8a7a5a', 'stroke-width="1.4"') + P('M158 18C160 12 154 10 152 16Z', '#8a7a5a', 'stroke-width="1.4"') +
				P('M76 38C70 34 66 42 72 46L80 46ZM124 38C130 34 134 42 128 46L120 46Z', fur) +
				P('M80 30C80 20 120 20 120 30L118 60C116 70 84 70 82 60Z', fur) + SHADE('M110 26C118 30 120 40 118 58L110 62Z') +
				P('M84 58C82 72 88 82 100 82C112 82 118 72 116 58C112 54 88 54 84 58Z', '#b88a64') + SHADE('M108 56C116 60 116 72 110 78L106 60Z') +
				C(92, 68, 2.6, INK) + C(108, 68, 2.6, INK) + P('M96 76C96 86 104 86 104 76', 'none', 'stroke="#f4c44f" stroke-width="3"') +
				P('M86 26C92 22 98 30 100 24C102 30 108 22 114 26C110 32 90 32 86 26Z', furD) +
				eyes(100, 44, 4, '#ff5a3a', 3.4) + HI('M86 32C88 30 92 29 95 29');
			const hd = g.hdR;
			// Labrys: long shaft through the fist, double crescent head above.
			const blade = (s) => P('M' + n1(hd[0]) + ' ' + n1(hd[1] - 56) + 'C' + n1(hd[0] + s * 10) + ' ' + n1(hd[1] - 60) + ' ' + n1(hd[0] + s * 22) + ' ' + n1(hd[1] - 72) + ' ' + n1(hd[0] + s * 26) + ' ' + n1(hd[1] - 80) +
				'C' + n1(hd[0] + s * 32) + ' ' + n1(hd[1] - 64) + ' ' + n1(hd[0] + s * 32) + ' ' + n1(hd[1] - 44) + ' ' + n1(hd[0] + s * 26) + ' ' + n1(hd[1] - 28) +
				'C' + n1(hd[0] + s * 22) + ' ' + n1(hd[1] - 36) + ' ' + n1(hd[0] + s * 10) + ' ' + n1(hd[1] - 46) + ' ' + n1(hd[0]) + ' ' + n1(hd[1] - 46) + 'Z', '#c9d1da');
			const labrys = ROD('M' + xy([hd[0], hd[1] + 50]) + 'V' + n1(hd[1] - 90), '#7d4f27', 5) + blade(-1) + blade(1) +
				HI('M' + n1(hd[0] - 22) + ' ' + n1(hd[1] - 72) + 'C' + n1(hd[0] - 27) + ' ' + n1(hd[1] - 60) + ' ' + n1(hd[0] - 27) + ' ' + n1(hd[1] - 48) + ' ' + n1(hd[0] - 23) + ' ' + n1(hd[1] - 36)) +
				P('M' + n1(hd[0] - 4) + ' ' + n1(hd[1] - 60) + 'H' + n1(hd[0] + 4) + 'V' + n1(hd[1] - 44) + 'H' + n1(hd[0] - 4) + 'Z', '#c48a22');
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: chest + belt(g, leather, '#c48a22') + tabard(g, 34),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o, labrys),
			};
		},
		fx(g) {
			const lines = [];
			for (let i = 0; i < 6; i++) {
				const y = 70 + i * 26;
				lines.push('M' + (8 + (i % 2) * 6) + ' ' + y + 'h' + (22 + (i % 3) * 6) + 'M' + (192 - (i % 2) * 6) + ' ' + (y + 12) + 'h-' + (22 + (i % 3) * 6));
			}
			const flames = [];
			for (let i = 0; i < 9; i++) {
				const a = (i * Math.PI) / 8;
				flames.push(flame(100 + Math.cos(a) * 74, GROUND - 4 + Math.sin(a) * 10, 8, 24 + (i % 3) * 8, '#e04b3a', '#ffb0a0'));
			}
			return {
				slash: [[150, 150], L('M60 70C120 70 168 120 160 200', WHITE, 8, 'stroke-opacity=".75"') + L('M70 80C120 82 158 126 152 190', '#ffd0c0', 3)],
				speed: [[100, 140], L(lines.join(''), WHITE, 3, 'stroke-opacity=".8"')],
				dustBack: [[100, GROUND], dust(46, GROUND, 1.1) + dust(156, GROUND, 1.1)],
				impact: [[100, 60], spark(100, 52, 34, '#fff3c0') + spark(62, 70, 14, WHITE) + spark(140, 66, 14, WHITE) + ring(100, 60, 50, 50, '#fff3c0', 4)],
				rage: [[100, GROUND], flames.join('')],
				steam: [[100, 76], C(84, 80, 5, WHITE, 'fill-opacity=".8" stroke="none"') + C(76, 84, 4, WHITE, 'fill-opacity=".6" stroke="none"') + C(116, 80, 5, WHITE, 'fill-opacity=".8" stroke="none"') + C(124, 84, 4, WHITE, 'fill-opacity=".6" stroke="none"')],
				aura: [[100, 130], E(100, 130, 66, 96, '#e04b3a', 'stroke="none" fill-opacity=".28"') + E(100, 130, 52, 84, 'none', 'stroke="#ff8a7d" stroke-width="3" stroke-opacity=".7"')],
			};
		},
		clips: {
			idle: { dur: 1.5, k: {
				upper: [[0, {}], [0.5, { y: 2 }], [1, {}]],
				head: [[0, {}], [0.5, R(-2, { y: 1 })], [1, {}]],
				armR: [[0, {}], [0.5, R(-2)], [1, {}]],
			} },
			atk: { dur: 1.3, k: {
				armR: [[0, {}], [0.4, R(-150)], [0.56, R(55), 1, EASE_HIT], [0.8, R(55)], [1, {}]],
				wpnR: [[0, {}], [0.4, R(170)], [0.56, R(180)], [0.8, R(180)], [1, {}]],
				upper: [[0, {}], [0.4, R(-5)], [0.56, R(6, { y: 3 })], [0.8, R(6, { y: 3 })], [1, {}]],
				slash: [[0, {}, 0], [0.5, {}, 0], [0.56, {}, 1], [0.75, {}, 0]],
			} },
			a1: { dur: 2.0, k: {
				head: [[0, {}], [0.2, { y: 10 }], [0.7, { y: 10 }], [1, {}]],
				root: [[0, {}], [0.2, { y: 6, sy: 0.95 }], [0.45, { y: 18, s: 1.16 }, 1, EASE_HIT], [0.7, { y: 18, s: 1.16 }], [1, {}]],
				armL: [[0, {}], [0.2, R(25)], [0.7, R(25)], [1, {}]], armR: [[0, {}], [0.2, R(-25)], [0.7, R(-25)], [1, {}]],
				speed: [[0, { sx: 0.4 }, 0], [0.25, { sx: 0.4 }, 0], [0.4, { sx: 1 }, 1], [0.55, { sx: 1.2 }, 0]],
				dustBack: [[0, { s: 0.4 }, 0], [0.3, { s: 0.6 }, 1], [0.6, { s: 1.3 }, 0]],
				impact: [[0, { s: 0.2 }, 0], [0.45, { s: 0.2 }, 0], [0.5, { s: 1.1 }, 1], [0.7, { s: 1.3 }, 0]],
			} },
			a2: { dur: 2.2, k: {
				head: [[0, {}], [0.2, { y: -6, s: 1.05 }], [0.7, { y: -6, s: 1.05 }], [1, {}]],
				armL: [[0, {}], [0.2, R(70)], [0.7, R(70)], [1, {}]], armR: [[0, {}], [0.2, R(-70)], [0.7, R(-70)], [1, {}]],
				foreL: [[0, {}], [0.2, R(60)], [0.7, R(60)], [1, {}]], foreR: [[0, {}], [0.2, R(-60)], [0.7, R(-60)], [1, {}]],
				upper: [[0, {}], [0.2, { y: -3 }], [0.3, { y: -1 }], [0.4, { y: -3 }], [0.5, { y: -1 }], [0.7, { y: -3 }], [1, {}]],
				rage: [[0, { sy: 0.2 }, 0], [0.22, { sy: 1 }, 1], [0.32, { sy: 0.8 }, 1], [0.42, { sy: 1.1 }, 1], [0.55, { sy: 0.85 }, 1], [0.68, { sy: 1.1 }, 1], [0.85, { sy: 1 }, 0.9], [1, { sy: 0.3 }, 0]],
				steam: [[0, {}, 0], [0.22, {}, 1], [0.5, { y: -12, s: 1.4 }, 0], [0.52, {}, 0], [0.6, {}, 1], [0.85, { y: -12, s: 1.4 }, 0]],
				aura: [[0, { s: 0.5 }, 0], [0.25, { s: 1 }, 1], [0.4, { s: 1.06 }, 0.7], [0.55, { s: 1 }, 1], [0.8, { s: 1.08 }, 0.7], [1, { s: 1.2 }, 0]],
			} },
		},
	};

	T['titan.cyclops'] = {
		name: 'Циклоп', cls: 'Warden', classRu: 'Страж', stars: 1,
		abil: ['Взор горна', 'Удар о наковальню'],
		crop: [56, 14, 88, 88],
		draw(g) {
			const skin = '#b3a684';
			const skinD = '#7f7458';
			const leather = '#7d4f27';
			const o = { leg: '#6b5a44', shin: skin, boot: '#4a3828', bootTall: 6, arm: skin, bracer: leather, hand: skin, knee: leather };
			const apron = P('M' + (100 - g.W + 2) + ' ' + (g.shY + 4) + 'H' + (100 + g.W - 2) + 'L' + (100 + g.W + 6) + ' ' + (g.waistY + 36) + 'Q100 ' + (g.waistY + 44) + ' ' + (100 - g.W - 6) + ' ' + (g.waistY + 36) + 'Z', leather) +
				SHADE('M100 ' + (g.shY + 4) + 'H' + (100 + g.W - 2) + 'L' + (100 + g.W + 6) + ' ' + (g.waistY + 36) + 'Q100 ' + (g.waistY + 44) + ' 100 ' + (g.waistY + 44) + 'Z') +
				L('M' + (100 - g.W + 2) + ' ' + (g.shY + 4) + 'L' + (100 - g.S + 12) + ' ' + (g.shY - 10) + 'M' + (100 + g.W - 2) + ' ' + (g.shY + 4) + 'L' + (100 + g.S - 12) + ' ' + (g.shY - 10), leather, 4) +
				C(100 - g.W + 4, g.shY + 7, 2, '#c48a22') + C(100 + g.W - 4, g.shY + 7, 2, '#c48a22') +
				P('M90 ' + (g.shY + 20) + 'H110V' + (g.shY + 34) + 'H90Z', '#5a3a1c') + L('M93 ' + (g.shY + 26) + 'H107', '#c48a22', 1.6) +
				P('M80 ' + (g.waistY + 14) + 'h40l-4 8h-32Z', '#4b4f57', 'stroke-width="1.8"');
			const head = P('M' + (100 - g.neck - 4) + ' 84V66H' + (100 + g.neck + 4) + 'V84Z', skinD) +
				P('M76 52C74 22 126 22 124 52C126 70 116 84 100 86C84 84 74 70 76 52Z', skin) + SHADE('M112 30C124 38 126 60 118 76L108 50Z') +
				P('M94 26C96 18 104 18 106 26C102 24 98 24 94 26Z', '#4a3828') +
				P('M80 42C88 34 112 34 120 42L118 46C110 40 90 40 82 46Z', skinD) +
				// The one great eye.
				E(100, 52, 13, 10, '#fffdf2') + C(100, 52, 7.5, '#ffb03a', 'class="p-iris"') + C(100, 52, 3.4, INK) + C(97, 49, 1.8, WHITE, 'stroke="none"') +
				L('M87 52C92 44 108 44 113 52', INK, 2.4) + P('M88 72C94 76 106 76 112 72C108 80 92 80 88 72Z', '#5a2e1c') + L('M92 73h3M105 73h3', WHITE, 1.4) +
				P('M74 52C70 50 68 58 74 60ZM126 52C130 50 132 58 126 60Z', skin) + HI('M82 34C86 28 92 26 97 26');
			const hd = g.hdR;
			const hammer = ROD('M' + xy([hd[0], hd[1] - 8]) + 'V' + n1(hd[1] + 40), '#7d4f27', 6) +
				P('M' + n1(hd[0] - 16) + ' ' + n1(hd[1] + 38) + 'H' + n1(hd[0] + 16) + 'L' + n1(hd[0] + 14) + ' ' + n1(hd[1] + 60) + 'H' + n1(hd[0] - 14) + 'Z', '#5e6670') +
				P('M' + n1(hd[0] - 18) + ' ' + n1(hd[1] + 36) + 'H' + n1(hd[0] + 18) + 'V' + n1(hd[1] + 42) + 'H' + n1(hd[0] - 18) + 'Z', '#3e444c') +
				HI('M' + n1(hd[0] - 10) + ' ' + n1(hd[1] + 46) + 'V' + n1(hd[1] + 56)) + GLOW(hd[0], hd[1] + 60, 6, '#ff8c2a', 0.6);
			const hl = g.hdL;
			// Round bronze shield with an anvil boss (wardens carry shields).
			const shield = C(hl[0] - 2, hl[1] - 12, 26, '#c48a3a') + C(hl[0] - 2, hl[1] - 12, 20, '#a86c2a', 'stroke-width="1.6"') +
				P('M' + n1(hl[0] - 14) + ' ' + n1(hl[1] - 18) + 'H' + n1(hl[0] + 10) + 'L' + n1(hl[0] + 4) + ' ' + n1(hl[1] - 12) + 'V' + n1(hl[1] - 6) + 'H' + n1(hl[0] + 6) + 'V' + n1(hl[1] - 2) + 'H' + n1(hl[0] - 10) + 'V' + n1(hl[1] - 6) + 'H' + n1(hl[0] - 8) + 'V' + n1(hl[1] - 12) + 'Z', '#3e444c') +
				HI('M' + n1(hl[0] - 22) + ' ' + n1(hl[1] - 22) + 'A22 22 0 0 1 ' + n1(hl[0] - 8) + ' ' + n1(hl[1] - 35));
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: P(torsoPath(g), skin) + torsoShade(g) + pecs(g, skinD) + apron + belt(g, '#5a3a1c', '#c48a22') + tabard(g, 26),
				head,
				armL: arm(g, -1, o, shield, true),
				armR: arm(g, 1, o, hammer),
			};
		},
		fx(g) {
			const hd = g.hdR;
			// The beam is drawn in the head's frame so it leaves the eye; its end lands on the ground once the head tilts 8 degrees.
			const beam = 'M100 52L' + xy(rot([186, GROUND - 4], [100, 80], -8));
			const rocks = [[40, -1], [64, 1], [140, -1], [162, 1]].map(([x, s]) => P('M' + x + ' ' + (GROUND - 6) + 'l6 -7l7 3l-1 8l-8 2Z', '#8a7a64', 'stroke-width="1.6" class="p-rk' + (s > 0 ? 'b' : 'a') + '" style="transform-origin:' + (x + 6) + 'px ' + (GROUND - 6) + 'px"')).join('');
			return {
				smash: [[hd[0], GROUND], ring(hd[0], GROUND - 2, 34, 7, '#ffd84f', 3) + spark(hd[0] - 18, GROUND - 26, 6, '#ffd84f') + spark(hd[0] + 22, GROUND - 30, 5, WHITE) + dust(hd[0], GROUND, 0.8)],
				eye: [[100, 52], GLOW(100, 52, 22, '#ffd84f', 0.55) + GLOW(100, 52, 12, WHITE, 0.8), 'head'],
				beam: [[100, 52], L(beam, '#ff8c2a', 14, 'stroke-opacity=".45"') + L(beam, '#ffd84f', 6) + L(beam, WHITE, 2), 'head'],
				scorch: [[186, GROUND], E(186, GROUND - 2, 20, 5, '#ff7a1a', 'fill-opacity=".8"') + flame(178, GROUND - 3, 5, 18, '#ff8c2a', '#ffd84f') + flame(193, GROUND - 3, 4, 13, '#ff8c2a', '#ffd84f')],
				quake: [[100, GROUND], L('M100 ' + GROUND + 'L80 ' + (GROUND - 3) + 'L66 ' + (GROUND + 1) + 'L40 ' + (GROUND - 2) + 'L20 ' + GROUND + 'M100 ' + GROUND + 'L122 ' + (GROUND + 2) + 'L136 ' + (GROUND - 2) + 'L160 ' + (GROUND + 1) + 'L182 ' + (GROUND - 1), INK, 3.5) +
					ring(100, GROUND, 96, 15, '#ffd84f', 4) + rocks],
				stun: [[100, 190], stunStars(190)],
			};
		},
		clips: {
			idle: { dur: 1.9, k: {
				upper: [[0, {}], [0.5, { y: 1.5 }], [1, {}]],
				head: [[0, {}], [0.3, R(-3)], [0.7, R(3)], [1, {}]],
				iris: [[0, {}], [0.3, { x: -2 }], [0.7, { x: 2 }], [1, {}]],
			} },
			atk: { dur: 1.4, k: {
				armR: [[0, {}], [0.42, R(-160)], [0.58, R(20), 1, EASE_HIT], [0.8, R(20)], [1, {}]],
				armL: [[0, {}], [0.42, R(15)], [0.58, R(-10)], [1, {}]],
				upper: [[0, {}], [0.42, R(-4)], [0.58, R(4, { y: 3 })], [0.8, R(4, { y: 3 })], [1, {}]],
				smash: [[0, { s: 0.3 }, 0], [0.58, { s: 0.3 }, 0], [0.62, { s: 1 }, 1], [0.95, { s: 1.3 }, 0]],
			} },
			a1: { dur: 2.0, k: {
				head: [[0, {}], [0.2, R(8, { y: 2 })], [0.85, R(8, { y: 2 })], [1, {}]],
				eye: [[0, { s: 0.3 }, 0], [0.18, { s: 1.2 }, 1], [0.85, { s: 1 }, 1], [1, { s: 0.3 }, 0]],
				beam: [[0, { s: 0 }, 0], [0.24, { s: 0 }, 1], [0.34, { s: 1 }, 1], [0.8, { s: 1 }, 1], [0.86, { s: 1 }, 0]],
				scorch: [[0, { sy: 0.2 }, 0], [0.34, { sy: 0.2 }, 0], [0.42, {}, 1], [0.9, {}, 1], [1, {}, 0]],
				armL: [[0, {}], [0.2, R(-20)], [0.85, R(-20)], [1, {}]],
			} },
			a2: { dur: 2.2, k: {
				armR: [[0, {}], [0.3, R(-175)], [0.48, R(5), 1, EASE_HIT], [0.75, R(5)], [1, {}]],
				foreR: [[0, {}], [0.3, R(-10)], [0.48, R(20)], [1, {}]],
				armL: [[0, {}], [0.3, R(40)], [0.48, R(-20)], [1, {}]],
				root: [[0, {}], [0.3, { y: -6 }], [0.48, { y: 4, sy: 0.96 }, 1, EASE_HIT], [0.75, { y: 4, sy: 0.96 }], [1, {}]],
				quake: [[0, { s: 0.3 }, 0], [0.48, { s: 0.3 }, 0], [0.52, { s: 0.8 }, 1], [0.85, { s: 1.15 }, 1], [1, { s: 1.2 }, 0]],
				rka: [[0, {}], [0.5, {}], [0.65, R(-40, { x: -6, y: -26 })], [0.85, R(-80, { x: -10, y: 0 })], [1, {}]],
				rkb: [[0, {}], [0.5, {}], [0.65, R(40, { x: 6, y: -30 })], [0.85, R(80, { x: 10, y: 0 })], [1, {}]],
				stun: [[0, {}, 0], [0.55, {}, 0], [0.6, { y: -4 }, 1], [0.92, {}, 1], [1, {}, 0]],
			} },
		},
	};

	T['titan.perun'] = {
		name: 'Перун', cls: 'Ravager', classRu: 'Берсерк', stars: 2,
		abil: ['Раскол дуба', 'Вестник грозы'],
		crop: [54, 8, 92, 92],
		draw(g) {
			const skin = '#e0b08a';
			const scale = '#a9b8c9';
			const scaleD = '#6f8296';
			const beard = '#eef3f8';
			const cloak = '#2f4a78';
			const o = { leg: '#a83a2a', shin: '#a83a2a', boot: '#6b4222', bootTall: 20, arm: skin, bracer: scaleD, pauldron: '#8a6a3a', hand: skin,
				legExtra: (s, hip, kn) => L('M' + n1(kn[0] - 8) + ' ' + n1(kn[1] + 6) + 'l16 6M' + n1(kn[0] - 8) + ' ' + n1(kn[1] + 14) + 'l16 6', '#4a2a14', 1.6) };
			const cloakBack = P('M' + (100 - g.S + 2) + ' ' + (g.shY - 6) + 'C' + (100 - g.S - 16) + ' 140 ' + (100 - g.S - 18) + ' 180 ' + (100 - g.S - 12) + ' 206L100 200L' + (100 + g.S + 12) + ' 206C' + (100 + g.S + 18) + ' 180 ' + (100 + g.S + 16) + ' 140 ' + (100 + g.S - 2) + ' ' + (g.shY - 6) + 'Z', cloak) +
				SHADE('M110 100L' + (100 + g.S + 14) + ' 140C' + (100 + g.S + 18) + ' 180 ' + (100 + g.S + 14) + ' 200 ' + (100 + g.S + 12) + ' 206L110 200Z');
			let scales = '';
			for (let row = 0; row < 4; row++) {
				const y = g.shY + 4 + row * 10;
				const w = g.S - 6 - row * 4;
				for (let x = 100 - w + 6; x <= 100 + w - 6; x += 9) scales += 'M' + n1(x - 4.5) + ' ' + y + 'Q' + n1(x) + ' ' + (y + 8) + ' ' + n1(x + 4.5) + ' ' + y;
			}
			const chest = P(torsoPath(g), scale) + torsoShade(g) + L(scales, scaleD, 1.3) +
				// Fur collar of the cloak.
				P('M' + (100 - g.S - 4) + ' ' + (g.shY - 2) + 'C' + (100 - g.S + 4) + ' ' + (g.shY - 16) + ' ' + (100 + g.S - 4) + ' ' + (g.shY - 16) + ' ' + (100 + g.S + 4) + ' ' + (g.shY - 2) + 'C' + (100 + g.S - 10) + ' ' + (g.shY + 4) + ' ' + (100 - g.S + 10) + ' ' + (g.shY + 4) + ' ' + (100 - g.S - 4) + ' ' + (g.shY - 2) + 'Z', '#8a7058');
			const r = g.headR;
			const leaves = [];
			for (let i = 0; i < 7; i++) {
				const a = Math.PI + 0.25 + (i * (Math.PI - 0.5)) / 6;
				const x = 100 + Math.cos(a) * (r + 1);
				const y = 46 + Math.sin(a) * (r * 0.7);
				leaves.push(P('M' + n1(x) + ' ' + n1(y + 3) + 'C' + n1(x - 6) + ' ' + n1(y - 2) + ' ' + n1(x - 2) + ' ' + n1(y - 10) + ' ' + n1(x + 1) + ' ' + n1(y - 11) + 'C' + n1(x + 5) + ' ' + n1(y - 8) + ' ' + n1(x + 6) + ' ' + n1(y - 2) + ' ' + n1(x) + ' ' + n1(y + 3) + 'Z', i % 2 ? '#5fa046' : '#3f8f45', 'stroke-width="1.6"'));
			}
			const head = P('M' + (100 - r - 4) + ' 44C' + (100 - r - 10) + ' 70 ' + (100 - r - 8) + ' 90 ' + (100 - r - 2) + ' 96L' + (100 + r + 2) + ' 96C' + (100 + r + 8) + ' 90 ' + (100 + r + 10) + ' 70 ' + (100 + r + 4) + ' 44Z', beard) +
				P(skull(100, 54, r), skin) + SHADE('M108 40C118 44 118 64 110 72L106 60Z') +
				// The silver beard down to the chest: Perun's mark, with the oak crown.
				P('M' + (100 - r + 1) + ' 58C' + (100 - r - 2) + ' 86 ' + (100 - 10) + ' 112 100 118C' + (100 + 10) + ' 112 ' + (100 + r + 2) + ' 86 ' + (100 + r - 1) + ' 58C' + (100 + r - 6) + ' 64 ' + (100 + 6) + ' 64 100 66C' + (100 - 6) + ' 64 ' + (100 - r + 6) + ' 64 ' + (100 - r + 1) + ' 58Z', beard) +
				SHADE('M106 66C114 68 ' + (100 + r) + ' 74 ' + (100 + r - 2) + ' 86C114 104 106 112 100 118Z') +
				L('M92 76C92 90 96 100 98 108M100 72V112M108 76C108 90 104 100 102 108', '#b8c4d0', 1.5) + P('M92 66Q100 62 108 66Q100 70 92 66Z', '#c8d2dc') +
				P('M' + (100 - r) + ' 46C' + (100 - r) + ' 30 ' + (100 + r) + ' 30 ' + (100 + r) + ' 46C' + (100 + r - 8) + ' 40 ' + (100 - r + 8) + ' 40 ' + (100 - r) + ' 46Z', beard) +
				eyes(100, 54, 5, '#d6f0ff', 3.2) + leaves.join('') + C(100 - r + 2, 40, 2.6, '#a8693a') + C(100 + r - 2, 40, 2.6, '#a8693a') +
				HI('M88 38C90 35 94 33 98 33');
			const hd = g.hdR;
			// Long-hafted bearded axe held upright, the head above the shoulder.
			const axe = ROD('M' + xy([hd[0], hd[1] + 40]) + 'V' + n1(hd[1] - 92), '#7d4f27', 5) +
				P('M' + n1(hd[0] + 2) + ' ' + n1(hd[1] - 88) + 'C' + n1(hd[0] + 14) + ' ' + n1(hd[1] - 92) + ' ' + n1(hd[0] + 26) + ' ' + n1(hd[1] - 100) + ' ' + n1(hd[0] + 30) + ' ' + n1(hd[1] - 106) +
					'C' + n1(hd[0] + 38) + ' ' + n1(hd[1] - 88) + ' ' + n1(hd[0] + 36) + ' ' + n1(hd[1] - 62) + ' ' + n1(hd[0] + 26) + ' ' + n1(hd[1] - 50) + 'C' + n1(hd[0] + 20) + ' ' + n1(hd[1] - 62) + ' ' + n1(hd[0] + 10) + ' ' + n1(hd[1] - 70) + ' ' + n1(hd[0] + 2) + ' ' + n1(hd[1] - 70) + 'Z', '#c9d1da') +
				L('M' + n1(hd[0] + 18) + ' ' + n1(hd[1] - 90) + 'l-5 8l6 0l-5 8', '#5aa8ff', 2) +
				HI('M' + n1(hd[0] + 31) + ' ' + n1(hd[1] - 98) + 'C' + n1(hd[0] + 34) + ' ' + n1(hd[1] - 86) + ' ' + n1(hd[0] + 32) + ' ' + n1(hd[1] - 70) + ' ' + n1(hd[0] + 27) + ' ' + n1(hd[1] - 60)) +
				P('M' + n1(hd[0] - 4) + ' ' + n1(hd[1] - 92) + 'h8v8h-8Z', '#c48a22', 'stroke-width="1.6"');
			return {
				back: cloakBack,
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + chest + belt(g, '#6b4222', '#c48a22') + tabard(g, 36),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o, axe),
			};
		},
		fx(g) {
			const cloud = P('M50 30C40 30 38 16 50 14C52 2 70 0 76 10C82 0 104 0 106 12C114 4 132 8 130 20C144 18 150 32 138 36H54C52 36 50 34 50 30Z', '#4a5468') +
				P('M60 30C56 26 62 20 68 22C72 16 84 18 84 24C92 20 100 26 96 30Z', '#5e6a80', 'stroke="none"');
			const chevrons = [[30, 170], [170, 170], [44, 120], [156, 120]].map(([x, y]) => L('M' + (x - 7) + ' ' + y + 'L' + x + ' ' + (y - 7) + 'L' + (x + 7) + ' ' + y, '#ffd84f', 3)).join('');
			// Each chop lands where the axe head ends its swing; the bolts strike that spot.
			const blade = [g.hdR[0] + 22, g.hdR[1] - 80];
			const hit = posed(g, 'R', blade, 30, 0, 180);
			const strike = (x, y) => bolt([[x - 6, -14], [x + 8, y * 0.3], [x - 4, y * 0.55], [x + 6, y * 0.8], [x, y]], 2) + ring(x, y + 4, 26, 6, '#d6f0ff', 3) + spark(x, y, 12, WHITE);
			return {
				chop: [blade, spark(blade[0], blade[1], 16, '#e6f4ff') + C(blade[0], blade[1], 14, 'none', 'stroke="#d6f0ff" stroke-width="3"'), 'wpnR'],
				b1: [hit, strike(hit[0] - 8, hit[1])],
				b2: [hit, strike(hit[0] + 6, hit[1] + 4)],
				b3: [hit, strike(hit[0] - 2, hit[1] + 2)],
				storm: [[100, 24], cloud + bolt([[86, 36], [80, 48], [88, 52], [82, 64]], 1.2) + bolt([[120, 36], [126, 46], [118, 50], [124, 62]], 1.2)],
				herald: [[100, GROUND], ring(100, GROUND - 2, 86, 14, '#ffd84f', 4) + GLOW(100, 150, 70, '#d6f0ff', 0.18) + chevrons],
			};
		},
		clips: {
			idle: { dur: 1.7, k: {
				upper: [[0, {}], [0.5, { y: 1.5 }], [1, {}]],
				head: [[0, {}], [0.5, R(-2)], [1, {}]],
				armR: [[0, {}], [0.5, R(-2)], [1, {}]],
			} },
			atk: { dur: 1.3, k: {
				armR: [[0, {}], [0.42, R(-165)], [0.56, R(30), 1, EASE_HIT], [0.8, R(30)], [1, {}]],
				wpnR: [[0, {}], [0.42, R(170)], [0.56, R(180)], [0.8, R(180)], [1, {}]],
				upper: [[0, {}], [0.42, R(-4)], [0.56, R(5, { y: 3 })], [0.8, R(5, { y: 3 })], [1, {}]],
				chop: [[0, { s: 0.3 }, 0], [0.56, { s: 0.3 }, 0], [0.6, { s: 1 }, 1], [0.9, { s: 1.3 }, 0]],
			} },
			a1: { dur: 2.2, k: {
				armR: [[0, {}], [0.14, R(-160)], [0.24, R(30), 1, EASE_HIT], [0.36, R(-150)], [0.46, R(30), 1, EASE_HIT], [0.58, R(-160)], [0.7, R(30), 1, EASE_HIT], [0.85, R(30)], [1, {}]],
				wpnR: [[0, {}], [0.14, R(175)], [0.85, R(180)], [1, {}]],
				upper: [[0, {}], [0.14, R(-4)], [0.24, R(5, { y: 3 })], [0.36, R(-4)], [0.46, R(5, { y: 3 })], [0.58, R(-4)], [0.7, R(5, { y: 3 })], [0.85, R(5, { y: 3 })], [1, {}]],
				b1: [[0, {}, 0], [0.24, {}, 0], [0.25, {}, 1], [0.32, {}, 0]],
				b2: [[0, {}, 0], [0.46, {}, 0], [0.47, {}, 1], [0.54, {}, 0]],
				b3: [[0, {}, 0], [0.7, {}, 0], [0.71, {}, 1], [0.8, {}, 0]],
			} },
			a2: { dur: 2.4, k: {
				armR: [[0, {}], [0.2, R(-120)], [0.8, R(-120)], [1, {}]], foreR: [[0, {}], [0.2, R(-60)], [0.8, R(-60)], [1, {}]],
				wpnR: [[0, {}], [0.2, R(130)], [0.8, R(130)], [1, {}]],
				armL: [[0, {}], [0.2, R(60)], [0.8, R(60)], [1, {}]], foreL: [[0, {}], [0.2, R(50)], [0.8, R(50)], [1, {}]],
				upper: [[0, {}], [0.2, { y: -3 }], [0.8, { y: -3 }], [1, {}]],
				storm: [[0, { s: 0.5, y: -10 }, 0], [0.25, { s: 1 }, 1], [0.35, { s: 1.03 }, 0.8], [0.45, { s: 1 }, 1], [0.85, { s: 1 }, 1], [1, { s: 1.1, y: -10 }, 0]],
				herald: [[0, { s: 0.2 }, 0], [0.35, { s: 0.2 }, 0], [0.5, { s: 1 }, 1], [0.85, { s: 1.15 }, 0.8], [1, { s: 1.3 }, 0]],
			} },
		},
	};

	T['titan.atlas'] = {
		name: 'Атлант', cls: 'Warden', classRu: 'Страж', stars: 2,
		abil: ['Падающий небосвод', 'Тяжесть небес'],
		crop: [46, 0, 108, 108],
		arms: (g) => ({ elL: [100 - g.S - 8, 70], wrL: [100 - g.S + 6, 44], hdL: [100 - g.S + 8, 38], elR: [100 + g.S + 8, 70], wrR: [100 + g.S - 6, 44], hdR: [100 + g.S - 8, 38] }),
		draw(g) {
			const skin = '#b88f62';
			const skinD = '#86643e';
			const o = Object.assign({ leg: skin, shin: skin, boot: '#7d4f27', bootTall: 14, arm: skin, bracer: '#c48a22', hand: skin, knee: '#c48a22' }, this.arms(g),
				{ legExtra: (s, hip, kn, an) => L('M' + n1(an[0] - 9) + ' ' + n1(an[1] - 2) + 'l18 -6M' + n1(an[0] - 9) + ' ' + n1(an[1] - 10) + 'l18 -6', '#4a2a14', 1.6) });
			const chest = P(torsoPath(g), skin) + torsoShade(g) + pecs(g, skinD) +
				// Cloth sash over one shoulder.
				P('M' + (100 + g.S - 14) + ' ' + (g.shY - 10) + 'L' + (100 + g.S - 2) + ' ' + (g.shY - 4) + 'L' + (100 - g.W + 6) + ' ' + (g.waistY - 2) + 'L' + (100 - g.W - 4) + ' ' + (g.waistY - 8) + 'Z', '#e6dcc4') +
				L('M' + (100 + g.S - 10) + ' ' + (g.shY - 4) + 'L' + (100 - g.W) + ' ' + (g.waistY - 6), '#b8a888', 1.4) +
				HI('M' + (100 - g.S + 6) + ' ' + (g.shY + 6) + 'C' + (100 - g.S + 6) + ' ' + (g.shY + 22) + ' ' + (100 - g.W) + ' ' + (g.waistY - 24) + ' ' + (100 - g.W + 2) + ' ' + (g.waistY - 14));
			const r = g.headR;
			const curls = [];
			for (let i = 0; i < 9; i++) curls.push(C(100 - r + 1 + (i * (2 * r - 2)) / 8, 36 + (i % 2) * 2, 4.5, '#3c2a1c', 'stroke-width="1.6"'));
			const head = P(skull(100, 56, r), skin) + SHADE('M108 42C118 46 118 66 110 74L106 62Z') + curls.join('') +
				P('M' + (100 - r + 1) + ' 60C' + (100 - r + 2) + ' 82 ' + (100 - 8) + ' 92 100 94C' + (100 + 8) + ' 92 ' + (100 + r - 2) + ' 82 ' + (100 + r - 1) + ' 60C' + (100 + r - 6) + ' 66 ' + (100 + 6) + ' 68 100 68C' + (100 - 6) + ' 68 ' + (100 - r + 6) + ' 66 ' + (100 - r + 1) + ' 60Z', '#3c2a1c') +
				L('M92 76c2 3 2 6 0 9M100 74c2 3 2 7 0 11M108 76c-2 3 -2 6 0 9', '#6e5038', 1.4) + P('M93 70Q100 66 107 70Q100 72 93 70Z', '#5a3e2a') +
				// Strained brows under the weight.
				L('M86 50l10 3M114 50l-10 3', INK, 2.6) + L('M88 55h6M106 55h6', '#9fd0ff', 2.6) + L('M96 44l4 2l4 -2', skinD, 1.4);
			// The celestial sphere on his shoulders: his silhouette from any distance.
			const globe = C(100, 26, 44, '#1f3566') + SHADE('M118 -14A44 44 0 0 1 118 66A60 60 0 0 0 118 -14Z') +
				E(100, 26, 44, 13, 'none', 'stroke="#f4c44f" stroke-width="3.2"') + E(100, 26, 16, 44, 'none', 'stroke="#f4c44f" stroke-width="2"') +
				L('M64 4L136 48', '#e8b85c', 5, 'stroke-opacity=".9"') + L('M70 9l3 -4M80 15l3 -4M90 21l3 -4M100 27l3 -4M110 33l3 -4M120 39l3 -4', '#7a5226', 1.4) +
				spark(78, 0, 4, WHITE) + spark(124, 14, 3.4, WHITE) + spark(86, 48, 3, '#9fd0ff') + spark(118, 56, 3, WHITE) + C(70, 30, 1.6, WHITE, 'stroke="none"') + C(132, 34, 1.6, WHITE, 'stroke="none"') +
				HI('M66 10A40 40 0 0 1 84 -12', 3);
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: G('p-globe', [100, 26], globe) + P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + chest + belt(g, '#7d4f27', '#c48a22') + tabard(g, 34),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o),
			};
		},
		fx() {
			// Shards of the sky fall from the upper right onto the ground around him.
			const meteor = (x, y, r) => L('M' + (x + 44) + ' ' + (y - 80) + 'L' + x + ' ' + y, '#9fd0ff', r + 4, 'stroke-opacity=".4"') + L('M' + (x + 26) + ' ' + (y - 48) + 'L' + x + ' ' + y, WHITE, 2, 'stroke-opacity=".8"') +
				C(x, y, r, '#2a4a8a') + spark(x, y, r * 0.8, '#e6f4ff');
			return {
				stomp: [[100, GROUND], ring(100, GROUND - 2, 66, 11, '#e6dcc4', 4) + dust(48, GROUND, 0.9) + dust(152, GROUND, 0.9)],
				fall: [[100, 200], meteor(34, GROUND - 10, 9) + meteor(166, GROUND - 14, 8) + meteor(116, GROUND - 4, 11)],
				blast: [[100, GROUND], ring(100, GROUND - 2, 94, 16, '#9fd0ff', 5) + ring(100, GROUND - 2, 60, 10, WHITE, 3) + spark(100, GROUND - 30, 22, '#e6f4ff') + dust(30, GROUND, 1) + dust(170, GROUND, 1)],
				press: [[100, GROUND], E(100, GROUND - 2, 92, 15, '#9fd0ff', 'fill-opacity=".25" stroke="#9fd0ff" stroke-width="3"') +
					L('M24 150v26l-6 -6M24 176l6 -6M176 150v26l-6 -6M176 176l6 -6M44 110v26l-6 -6M44 136l6 -6M156 110v26l-6 -6M156 136l6 -6', '#9fd0ff', 3)],
				halo: [[100, 26], GLOW(100, 26, 58, '#9fd0ff', 0.3) + C(100, 26, 52, 'none', 'stroke="#cfe8ff" stroke-width="3" stroke-dasharray="6 10"') + spark(46, 0, 7, WHITE) + spark(156, 44, 6, WHITE) + spark(150, -12, 5, WHITE)],
			};
		},
		clips: {
			idle: { dur: 2.2, k: {
				upper: [[0, {}], [0.5, { y: 2.5 }], [1, {}]],
				globe: [[0, {}], [0.5, R(4, { y: 2 })], [1, {}]],
				head: [[0, {}], [0.5, { y: 1.5 }], [1, {}]],
			} },
			atk: { dur: 1.4, k: {
				legR: [[0, {}], [0.4, R(-28, { y: -8 })], [0.56, {}, 1, EASE_HIT], [1, {}]],
				root: [[0, {}], [0.4, R(-4)], [0.56, { y: 3 }], [0.8, { y: 3 }], [1, {}]],
				stomp: [[0, { s: 0.3 }, 0], [0.56, { s: 0.3 }, 0], [0.6, { s: 1 }, 1], [0.95, { s: 1.4 }, 0]],
			} },
			a1: { dur: 2.6, k: {
				upper: [[0, {}], [0.15, { y: 8 }], [0.25, { y: -6 }, 1, EASE_OUT], [0.4, {}], [0.82, { y: 6 }], [1, {}]],
				armL: [[0, {}], [0.15, R(-6)], [0.25, R(12)], [0.4, R(12)], [0.78, R(12)], [0.86, R(-4)], [1, {}]],
				armR: [[0, {}], [0.15, R(6)], [0.25, R(-12)], [0.4, R(-12)], [0.78, R(-12)], [0.86, R(4)], [1, {}]],
				globe: [[0, {}], [0.15, { y: 8 }], [0.32, { y: -150 }, 1, EASE_OUT], [0.66, { y: -150 }], [0.82, { y: 6 }, 1, EASE_HIT], [1, {}]],
				fall: [[0, { x: 110, y: -200 }, 0], [0.38, { x: 110, y: -200 }, 1], [0.56, {}, 1, EASE_HIT], [0.58, {}, 0]],
				blast: [[0, { s: 0.2 }, 0], [0.56, { s: 0.2 }, 0], [0.6, { s: 0.8 }, 1], [0.95, { s: 1.3 }, 0]],
			} },
			a2: { dur: 2.4, k: {
				halo: [[0, { s: 0.5 }, 0], [0.2, { s: 1 }, 1], [0.8, { s: 1.1 }, 1], [1, { s: 1.2 }, 0]],
				globe: [[0, {}], [0.2, R(30, { y: 4 })], [0.8, R(150, { y: 4 })], [1, R(180)]],
				upper: [[0, {}], [0.25, { y: 5 }], [0.8, { y: 5 }], [1, {}]],
				press: [[0, { y: -70, s: 1.4 }, 0], [0.25, { y: -70, s: 1.4 }, 0.6], [0.45, {}, 1, EASE_HIT], [0.85, { s: 0.95 }, 1], [1, {}, 0]],
			} },
		},
	};

	T['titan.ymir'] = {
		name: 'Имир', cls: 'Juggernaut', classRu: 'Чемпион', stars: 2,
		abil: ['Ледяное дыхание', 'Шкура ледника'],
		crop: [52, 4, 96, 96],
		draw(g) {
			const skin = '#bcd4e6';
			const skinD = '#7f9cb4';
			const ice = '#a8eaff';
			const pelt = '#8a7a6a';
			const crystal = (x, y, h, w, tilt) => P('M' + n1(x - w) + ' ' + n1(y) + 'L' + n1(x + tilt) + ' ' + n1(y - h) + 'L' + n1(x + w) + ' ' + n1(y) + 'Z', ice) + L('M' + n1(x + tilt * 0.6) + ' ' + n1(y - h * 0.8) + 'L' + n1(x - w * 0.3) + ' ' + n1(y - 2), WHITE, 1.4, 'stroke-opacity=".8"');
			const o = { leg: pelt, shin: skin, boot: '#6e6052', bootTall: 10, arm: skin, hand: '#d6ecf8',
				armExtra: (s, sh) => crystal(sh[0] - s * 4, sh[1] - 6, 22, 6, -s * 6) + crystal(sh[0] + s * 8, sh[1] - 2, 16, 5, s * 6) + crystal(sh[0] - s * 14, sh[1], 12, 4, -s * 8) };
			const chest = P(torsoPath(g), skin) + torsoShade(g) + pecs(g, skinD) +
				L('M' + (100 - g.S + 14) + ' ' + (g.shY + 30) + 'l6 8l-4 6M' + (100 + g.S - 14) + ' ' + (g.shY + 24) + 'l-5 9l4 5', '#e6f6ff', 2);
			const r = g.headR;
			const head = P('M' + (100 - r - 6) + ' 44C' + (100 - r - 14) + ' 76 ' + (100 - r - 10) + ' 100 ' + (100 - r - 4) + ' 108L' + (100 + r + 4) + ' 108C' + (100 + r + 10) + ' 100 ' + (100 + r + 14) + ' 76 ' + (100 + r + 6) + ' 44Z', '#f4fbff') +
				P(skull(100, 54, r), skin) + SHADE('M108 40C118 44 118 64 110 72L106 60Z') +
				// White beard to the belt and an ice crown.
				P('M' + (100 - r) + ' 58C' + (100 - r - 6) + ' 96 ' + (100 - 14) + ' 128 100 136C' + (100 + 14) + ' 128 ' + (100 + r + 6) + ' 96 ' + (100 + r) + ' 58C' + (100 + r - 6) + ' 64 ' + (100 + 6) + ' 64 100 66C' + (100 - 6) + ' 64 ' + (100 - r + 6) + ' 64 ' + (100 - r) + ' 58Z', '#f4fbff') +
				SHADE('M106 66C116 68 ' + (100 + r + 2) + ' 76 ' + (100 + r + 2) + ' 92C116 116 106 130 100 136Z') +
				L('M90 76C88 94 92 112 96 126M100 72V132M110 76C112 94 108 112 104 126', '#c4d8e6', 1.6) + P('M90 66Q100 61 110 66Q100 70 90 66Z', '#e6f2fa') +
				P('M' + (100 - r) + ' 46C' + (100 - r) + ' 30 ' + (100 + r) + ' 30 ' + (100 + r) + ' 46C' + (100 + r - 8) + ' 40 ' + (100 - r + 8) + ' 40 ' + (100 - r) + ' 46Z', '#f4fbff') +
				eyes(100, 54, 5, ice, 3.4) +
				crystal(100, 36, 30, 6, 0) + crystal(86, 38, 20, 5, -4) + crystal(114, 38, 20, 5, 4) + crystal(75, 42, 12, 4, -6) + crystal(125, 42, 12, 4, 6);
			const loin = P('M' + (100 - g.W - 6) + ' ' + (g.waistY + 2) + 'L' + (100 - g.W - 10) + ' ' + (g.waistY + 26) + 'L' + (100 - g.W + 2) + ' ' + (g.waistY + 20) + 'L' + (100 - 8) + ' ' + (g.waistY + 28) + 'L100 ' + (g.waistY + 20) + 'L' + (100 + 8) + ' ' + (g.waistY + 28) + 'L' + (100 + g.W - 2) + ' ' + (g.waistY + 20) + 'L' + (100 + g.W + 10) + ' ' + (g.waistY + 26) + 'L' + (100 + g.W + 6) + ' ' + (g.waistY + 2) + 'Z', pelt);
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: chest + loin + belt(g, '#5a4a3c', ice) + tabard(g, 30),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o),
			};
		},
		fx(g) {
			const flakes = [[118, 120], [150, 160], [132, 190], [170, 196], [108, 170]].map(([x, y]) => L('M' + (x - 5) + ' ' + y + 'h10M' + x + ' ' + (y - 5) + 'v10M' + (x - 3.5) + ' ' + (y - 3.5) + 'l7 7M' + (x + 3.5) + ' ' + (y - 3.5) + 'l-7 7', WHITE, 1.8)).join('');
			const shell = [];
			const shards = [[52, 120, 40, -6], [148, 120, 40, 6], [60, 200, 34, -4], [140, 200, 34, 4], [100, 30, 30, 0], [38, 168, 26, -8], [162, 168, 26, 8]];
			for (const [x, y, h, t] of shards) shell.push(P('M' + (x - 10) + ' ' + y + 'L' + (x + t) + ' ' + (y - h) + 'L' + (x + 10) + ' ' + y + 'L' + x + ' ' + (y + 8) + 'Z', '#a8eaff', 'fill-opacity=".7" stroke="#e6f6ff" stroke-width="2.4"'));
			return {
				shards: [[g.hdR[0], GROUND], [[-14, 16], [0, 22], [14, 14]].map(([dx, h]) => P('M' + (g.hdR[0] + dx - 6) + ' ' + (GROUND - 1) + 'L' + (g.hdR[0] + dx) + ' ' + (GROUND - h) + 'L' + (g.hdR[0] + dx + 6) + ' ' + (GROUND - 1) + 'Z', '#a8eaff')).join('') + ring(g.hdR[0], GROUND - 2, 30, 6, WHITE, 3)],
				breath: [[100, 68], P('M100 68L204 132L176 ' + (GROUND - 4) + 'L130 ' + (GROUND - 4) + 'Z', '#a8eaff', 'stroke="none" fill-opacity=".55"') +
					P('M100 68L184 140L162 ' + (GROUND - 8) + 'L136 ' + (GROUND - 10) + 'Z', '#e6f6ff', 'stroke="none" fill-opacity=".6"') +
					L('M104 76C130 104 150 136 162 196M106 84C120 116 132 156 140 206M110 74C140 96 170 120 190 150', WHITE, 2, 'stroke-opacity=".75"') + flakes, 'head'],
				frost: [[164, GROUND], E(164, GROUND - 2, 38, 6, '#e6f6ff', 'stroke="#a8eaff" stroke-width="2"') + spark(150, GROUND - 10, 5, WHITE) + spark(184, GROUND - 6, 4, WHITE)],
				shell: [[100, 130], shell.join('')],
				glint: [[100, 120], spark(56, 90, 8, WHITE) + spark(150, 150, 7, WHITE) + spark(96, 200, 6, WHITE)],
			};
		},
		clips: {
			idle: { dur: 2.0, k: {
				upper: [[0, {}], [0.5, { y: 2 }], [1, {}]],
				head: [[0, {}], [0.5, R(1.5, { y: 0.8 })], [1, {}]],
				armL: [[0, {}], [0.5, R(2)], [1, {}]], armR: [[0, {}], [0.5, R(-2)], [1, {}]],
			} },
			atk: { dur: 1.4, k: {
				armR: [[0, {}], [0.4, R(-150)], [0.56, R(15), 1, EASE_HIT], [0.8, R(15)], [1, {}]],
				foreR: [[0, {}], [0.4, R(-40)], [0.56, R(0)], [1, {}]],
				upper: [[0, {}], [0.4, R(-5)], [0.56, R(5, { y: 4 })], [0.8, R(5, { y: 4 })], [1, {}]],
				shards: [[0, { sy: 0.1 }, 0], [0.56, { sy: 0.1 }, 0], [0.62, {}, 1], [0.95, {}, 0]],
			} },
			a1: { dur: 2.2, k: {
				head: [[0, {}], [0.2, R(6, { y: 3 })], [0.85, R(6, { y: 3 })], [1, {}]],
				upper: [[0, {}], [0.2, R(2, { y: 2 })], [0.85, R(2, { y: 2 })], [1, {}]],
				breath: [[0, { s: 0.1 }, 0], [0.22, { s: 0.2 }, 0.8], [0.4, { s: 1 }, 1], [0.8, { s: 1.03 }, 1], [0.95, { s: 1.05 }, 0]],
				frost: [[0, { s: 0.3 }, 0], [0.4, { s: 0.3 }, 0], [0.55, {}, 1], [0.92, {}, 1], [1, {}, 0]],
			} },
			a2: { dur: 2.2, k: {
				armL: [[0, {}], [0.2, R(45)], [0.8, R(45)], [1, {}]], armR: [[0, {}], [0.2, R(-45)], [0.8, R(-45)], [1, {}]],
				foreL: [[0, {}], [0.2, R(-80)], [0.8, R(-80)], [1, {}]], foreR: [[0, {}], [0.2, R(80)], [0.8, R(80)], [1, {}]],
				upper: [[0, {}], [0.2, { y: -2 }], [0.8, { y: -2 }], [1, {}]],
				shell: [[0, { s: 0.5 }, 0], [0.24, { s: 0.6 }, 0], [0.38, { s: 1.05 }, 1], [0.45, { s: 1 }, 1], [0.85, { s: 1 }, 1], [1, { s: 1.1 }, 0]],
				glint: [[0, {}, 0], [0.4, {}, 0], [0.48, { s: 1.3 }, 1], [0.6, { s: 0.8 }, 0.3], [0.7, { s: 1.2 }, 1], [0.85, {}, 0]],
			} },
		},
	};

	T['titan.surtr'] = {
		name: 'Сурт', cls: 'Ravager', classRu: 'Берсерк', stars: 3,
		abil: ['Клинок Муспелля', 'Мировой пожар'],
		crop: [54, 0, 92, 92],
		draw(g) {
			const skin = '#3a2a26';
			const skinD = '#241816';
			const lava = '#ff7a1a';
			const iron = '#4a4448';
			const cracks = (d) => L(d, lava, 4, 'stroke-opacity=".4"') + L(d, '#ffb03a', 1.6);
			const o = { leg: skin, shin: skin, boot: iron, bootTall: 14, arm: skin, bracer: iron, pauldron: iron, hand: skin, knee: iron,
				legExtra: (s, hip, kn) => cracks('M' + n1(hip[0] + s * 2) + ' ' + n1(hip[1] + 10) + 'l' + s * 3 + ' 10l' + -s * 4 + ' 8l' + s * 2 + ' 10'),
				armExtra: (s, sh, el) => cracks('M' + n1(sh[0] + s * 3) + ' ' + n1(sh[1] + 8) + 'l' + s * 2 + ' 8l' + -s * 3 + ' 6') + P('M' + n1(sh[0] - 6) + ' ' + n1(sh[1] - 10) + 'L' + n1(sh[0] + s * 4) + ' ' + n1(sh[1] - 24) + 'L' + n1(sh[0] + 6) + ' ' + n1(sh[1] - 10) + 'Z', '#2a2426') };
			const chest = P(torsoPath(g), skin) + torsoShade(g) + pecs(g, '#ff7a1a') +
				cracks('M' + (100 - g.S + 10) + ' ' + (g.shY + 2) + 'l6 10l-3 8l8 10M' + (100 + g.S - 10) + ' ' + (g.shY + 6) + 'l-7 9l4 9l-6 8M96 ' + (g.waistY - 20) + 'l4 6l4 -6') +
				GLOW(100, g.shY + 24, 10, lava, 0.5) + C(100, g.shY + 24, 5, '#ffd84f', 'stroke-width="1.6"');
			const r = g.headR;
			const fire = flame(100, 38, 26, 44, '#ff8c2a', '#ffd84f') + flame(80, 42, 12, 28, '#ff6a1a', '#ffb03a') + flame(120, 42, 12, 28, '#ff6a1a', '#ffb03a');
			const head = G('p-mane', [100, 40], fire) + P(skull(100, 54, r), skin) + SHADE('M108 40C118 44 118 64 110 72L106 60Z') +
				// Black horns and a mane of flame: the fire giant's crown.
				P('M84 40C76 30 72 18 76 8C80 18 86 26 92 34Z', '#1c1412') + P('M116 40C124 30 128 18 124 8C120 18 114 26 108 34Z', '#1c1412') +
				P('M' + (100 - r + 3) + ' 60C' + (100 - r + 4) + ' 76 ' + (100 - 8) + ' 82 100 84C' + (100 + 8) + ' 82 ' + (100 + r - 4) + ' 76 ' + (100 + r - 3) + ' 60C' + (100 + r - 8) + ' 66 ' + (100 + 8) + ' 68 100 68C' + (100 - 8) + ' 68 ' + (100 - r + 8) + ' 66 ' + (100 - r + 3) + ' 60Z', skinD) +
				cracks('M92 74l4 4l4 -4l4 4l4 -4') + eyes(100, 52, 5, '#ffd84f', 3.6) + cracks('M88 40l4 6M112 40l-4 6');
			const hd = g.hdR;
			// Muspell's flaming greatsword, point down at rest.
			const blade = 'M' + n1(hd[0] - 6) + ' ' + n1(hd[1] + 10) + 'H' + n1(hd[0] + 6) + 'L' + n1(hd[0] + 5) + ' ' + n1(hd[1] + 70) + 'L' + n1(hd[0]) + ' ' + n1(hd[1] + 82) + 'L' + n1(hd[0] - 5) + ' ' + n1(hd[1] + 70) + 'Z';
			const sword = flame(hd[0] - 8, hd[1] + 64, 6, 22, '#ff8c2a', '#ffd84f') + flame(hd[0] + 8, hd[1] + 44, 6, 20, '#ff8c2a', '#ffd84f') +
				P(blade, '#ff8c2a') + P('M' + n1(hd[0] - 2) + ' ' + n1(hd[1] + 12) + 'H' + n1(hd[0] + 2) + 'V' + n1(hd[1] + 74) + 'H' + n1(hd[0] - 2) + 'Z', '#fff3c0', 'stroke="none"') +
				ROD('M' + xy([hd[0], hd[1] - 12]) + 'V' + n1(hd[1] + 6), '#2a2426', 5) + P('M' + n1(hd[0] - 14) + ' ' + n1(hd[1] + 6) + 'H' + n1(hd[0] + 14) + 'L' + n1(hd[0] + 10) + ' ' + n1(hd[1] + 12) + 'H' + n1(hd[0] - 10) + 'Z', iron) +
				C(hd[0], hd[1] - 14, 3.4, lava);
			return {
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + chest + belt(g, iron, lava) + tabard(g, 36),
				head,
				armL: arm(g, -1, o),
				armR: arm(g, 1, o, sword),
			};
		},
		fx() {
			const ringFlames = [];
			for (let i = 0; i < 11; i++) {
				const a = (i * Math.PI * 2) / 11;
				const x = 100 + Math.cos(a) * 88;
				const y = GROUND - 4 + Math.sin(a) * 14;
				if (Math.sin(a) < -0.2) continue;
				ringFlames.push(flame(x, y, 9, 30 + (i % 3) * 8, '#ff8c2a', '#ffd84f'));
			}
			const embers = [[40, 150], [70, 110], [130, 120], [160, 160], [100, 90], [55, 190], [150, 200]].map(([x, y]) => C(x, y, 2.4, '#ffd84f', 'stroke="none"')).join('');
			return {
				arc: [[100, 140], L('M176 70C186 130 150 190 70 206', '#ff8c2a', 10, 'stroke-opacity=".6"') + L('M176 70C186 130 150 190 70 206', '#ffd84f', 3)],
				wave: [[100, 180], P('M28 196C60 150 140 150 172 196C140 172 60 172 28 196Z', '#ff8c2a', 'stroke="#ffd84f" stroke-width="3"') + L('M44 186C76 164 124 164 156 186', '#fff3c0', 2.4) +
					flame(66, 176, 7, 24, '#ff8c2a', '#ffd84f') + flame(100, 168, 8, 28, '#ff8c2a', '#ffd84f') + flame(134, 176, 7, 24, '#ff8c2a', '#ffd84f')],
				heat: [[100, GROUND], E(100, GROUND - 2, 100, 20, '#ff6a1a', 'stroke="none" fill-opacity=".45"') + E(100, GROUND - 2, 70, 13, '#ffd84f', 'stroke="none" fill-opacity=".35"')],
				ring: [[100, GROUND], ringFlames.join('')],
				embers: [[100, 150], embers],
			};
		},
		clips: {
			idle: { dur: 1.4, k: {
				upper: [[0, {}], [0.5, { y: 1.5 }], [1, {}]],
				mane: [[0, {}], [0.25, { sy: 1.08, sx: 0.96 }], [0.5, { sy: 0.95, sx: 1.03 }], [0.75, { sy: 1.06 }], [1, {}]],
				armR: [[0, {}], [0.5, R(-2)], [1, {}]],
			} },
			atk: { dur: 1.2, k: {
				armR: [[0, {}], [0.4, R(-150)], [0.55, R(40), 1, EASE_HIT], [0.8, R(40)], [1, {}]],
				wpnR: [[0, {}], [0.4, R(20)], [0.55, R(-10)], [1, {}]],
				upper: [[0, {}], [0.4, R(-5)], [0.55, R(6, { y: 3 })], [0.8, R(6, { y: 3 })], [1, {}]],
				arc: [[0, {}, 0], [0.5, {}, 0], [0.55, {}, 1], [0.75, {}, 0]],
			} },
			a1: { dur: 2.0, k: {
				armR: [[0, {}], [0.35, R(-178)], [0.5, R(15), 1, EASE_HIT], [0.8, R(15)], [1, {}]],
				armL: [[0, {}], [0.35, R(-150)], [0.5, R(-15), 1, EASE_HIT], [0.8, R(-15)], [1, {}]],
				upper: [[0, {}], [0.35, { y: -5 }], [0.5, { y: 6 }], [0.8, { y: 6 }], [1, {}]],
				wave: [[0, { s: 0.3, y: -20 }, 0], [0.5, { s: 0.3, y: -20 }, 0], [0.55, { s: 0.7 }, 1], [0.85, { s: 1.6, y: 30 }, 0]],
			} },
			a2: { dur: 2.4, k: {
				armR: [[0, {}], [0.25, R(-178)], [0.42, R(-178)], [0.52, R(-10), 1, EASE_HIT], [0.85, R(-10)], [1, {}]],
				wpnR: [[0, {}], [0.25, R(0)], [0.52, R(0)], [1, {}]],
				armL: [[0, {}], [0.25, R(70)], [0.85, R(70)], [1, {}]],
				upper: [[0, {}], [0.25, { y: -4 }], [0.52, { y: 6 }], [0.85, { y: 6 }], [1, {}]],
				ring: [[0, { sy: 0.1 }, 0], [0.52, { sy: 0.1 }, 0], [0.6, { sy: 1.1 }, 1], [0.7, { sy: 0.9 }, 1], [0.8, { sy: 1.05 }, 1], [0.92, {}, 1], [1, { sy: 0.4 }, 0]],
				heat: [[0, { s: 0.5 }, 0], [0.55, { s: 0.8 }, 0], [0.62, { s: 1 }, 1], [0.92, { s: 1.1 }, 1], [1, { s: 1.2 }, 0]],
				embers: [[0, {}, 0], [0.55, { y: 20 }, 0], [0.62, { y: 10 }, 1], [1, { y: -60 }, 0]],
			} },
		},
	};

	T['titan.hecate'] = {
		name: 'Геката', cls: 'Mystic', classRu: 'Чародей', stars: 3,
		abil: ['Порча перекрёстка', 'Факелы преисподней'],
		crop: [56, 14, 88, 88],
		hover: 10,
		draw(g) {
			const skin = '#e6d6f0';
			const skinD = '#b8a2cc';
			const robe = '#4a3a6a';
			const robeL = '#6a568e';
			const gold = '#e8b85c';
			const o = { arm: robeL, hand: skin, bracer: gold };
			// A floating robe in place of legs: mystics hover (master prompt 16.2).
			const gown = P('M' + (100 - g.W) + ' ' + (g.waistY - 4) + 'C' + (100 - g.W - 18) + ' 170 ' + (100 - g.W - 30) + ' 196 ' + (100 - g.W - 34) + ' 214Q' + (100 - 30) + ' 206 ' + (100 - 22) + ' 216Q' + (100 - 10) + ' 206 100 216Q' + (100 + 10) + ' 206 ' + (100 + 22) + ' 216Q' + (100 + 30) + ' 206 ' + (100 + g.W + 34) + ' 214C' + (100 + g.W + 30) + ' 196 ' + (100 + g.W + 18) + ' 170 ' + (100 + g.W) + ' ' + (g.waistY - 4) + 'Z', robe) +
				SHADE('M108 ' + g.waistY + 'C' + (100 + g.W + 14) + ' 170 ' + (100 + g.W + 26) + ' 196 ' + (100 + g.W + 34) + ' 214Q' + (100 + 30) + ' 206 ' + (100 + 22) + ' 216Q114 206 108 214Z') +
				L('M' + (100 - g.W - 30) + ' 208Q' + (100 - 30) + ' 200 ' + (100 - 22) + ' 210Q' + (100 - 10) + ' 200 100 210Q' + (100 + 10) + ' 200 ' + (100 + 22) + ' 210Q' + (100 + 30) + ' 200 ' + (100 + g.W + 30) + ' 208', gold, 2.4) +
				TEAMFILL('M92 ' + (g.waistY + 2) + 'H108L112 206H88Z') + L('M95 ' + (g.waistY + 10) + 'L93 198', WHITE, 1.6, 'stroke-opacity=".35"') +
				L('M' + (100 - 30) + ' 150C' + (100 - 34) + ' 176 ' + (100 - 40) + ' 192 ' + (100 - 44) + ' 204M' + (100 + 30) + ' 150C' + (100 + 34) + ' 176 ' + (100 + 40) + ' 192 ' + (100 + 44) + ' 204', '#2a2238', 1.6);
			const chest = P(torsoPath(g), robeL) + torsoShade(g) +
				P('M' + (100 - 10) + ' ' + (g.shY - 10) + 'L100 ' + (g.shY + 20) + 'L' + (100 + 10) + ' ' + (g.shY - 10) + 'Z', skin) +
				L('M' + (100 - 12) + ' ' + (g.shY - 10) + 'L100 ' + (g.shY + 22) + 'L' + (100 + 12) + ' ' + (g.shY - 10), gold, 2.2) +
				C(100, g.shY + 30, 5, gold) + P('M97 ' + (g.shY + 30) + 'a3 3 0 1 0 6 0a4 4 0 0 1 -6 0Z', '#4a3a6a', 'stroke="none"');
			const r = g.headR;
			// Three faces: the goddess of crossroads looks down every road.
			// Profiles facing left and right behind the front face.
			const side = (s) => {
				const x = (v) => n1(100 + s * v);
				return P('M' + x(10) + ' 34C' + x(22) + ' 30 ' + x(32) + ' 36 ' + x(33) + ' 46L' + x(39) + ' 54L' + x(33) + ' 57C' + x(35) + ' 60 ' + x(34) + ' 62 ' + x(32) + ' 63C' + x(33) + ' 68 ' + x(30) + ' 72 ' + x(22) + ' 72L' + x(10) + ' 70Z', skinD) +
					L('M' + x(24) + ' 47l' + s * 7 + ' 1.5', '#c89bff', 2.6) + L('M' + x(23) + ' 43l' + s * 8 + ' 1', INK, 1.6) + HI('M' + x(14) + ' 38C' + x(20) + ' 35 ' + x(26) + ' 35 ' + x(29) + ' 38', 1.6);
			};
			const head = P('M' + (100 - r - 8) + ' 40C' + (100 - r - 14) + ' 70 ' + (100 - r - 12) + ' 100 ' + (100 - r - 18) + ' 112L' + (100 + r + 18) + ' 112C' + (100 + r + 12) + ' 100 ' + (100 + r + 14) + ' 70 ' + (100 + r + 8) + ' 40Z', '#2a2238') +
				side(-1) + side(1) + P(skull(100, 54, r, 1.05), skin) + SHADE('M106 42C116 46 116 64 108 72L104 60Z') +
				P('M' + (100 - r - 1) + ' 50C' + (100 - r - 2) + ' 26 ' + (100 + r + 2) + ' 26 ' + (100 + r + 1) + ' 50C' + (100 + r - 6) + ' 40 ' + (100 - r + 6) + ' 40 ' + (100 - r - 1) + ' 50Z', '#2a2238') +
				// Crescent tiara.
				P('M88 30C90 18 110 18 112 30C108 24 92 24 88 30Z', '#e8f0ff') + C(100, 31, 2.4, '#c89bff') +
				L('M91 54l6 1M109 54l-6 1', '#c89bff', 3) + L('M90 50l7 1.5M110 50l-7 1.5', INK, 1.8) + P('M96 66Q100 68 104 66Q100 70 96 66Z', '#8a3a6a');
			const torch = (hd) => ROD('M' + xy([hd[0], hd[1] + 24]) + 'V' + n1(hd[1] - 30), '#5a3a24', 4.5) + P('M' + n1(hd[0] - 8) + ' ' + n1(hd[1] - 28) + 'H' + n1(hd[0] + 8) + 'L' + n1(hd[0] + 5) + ' ' + n1(hd[1] - 37) + 'H' + n1(hd[0] - 5) + 'Z', gold) +
				G('p-flame', [hd[0], hd[1] - 37], GLOW(hd[0], hd[1] - 52, 16, '#a46cf0', 0.35) + flame(hd[0], hd[1] - 37, 11, 36, '#a46cf0', '#e8d6ff'));
			return {
				legs: gown,
				torso: P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + chest + belt(g, gold),
				head,
				armL: arm(g, -1, o, torch(g.hdL), true),
				armR: arm(g, 1, o, torch(g.hdR), true),
			};
		},
		fx(g) {
			const rad = 80;
			const runes = [];
			for (let i = 0; i < 12; i++) {
				const a = (i * Math.PI) / 6;
				runes.push('M' + xy([100 + Math.cos(a) * (rad - 8), GROUND + Math.sin(a) * 9]) + 'l3 -4');
			}
			const bones = (x) => '<g class="sk">' + C(x, GROUND - 26, 9, '#f0e6d0') + C(x - 3.5, GROUND - 26, 2.2, '#4ff08a', 'stroke="none"') + C(x + 3.5, GROUND - 26, 2.2, '#4ff08a', 'stroke="none"') +
				P('M' + (x - 5) + ' ' + (GROUND - 18) + 'h10v4h-10Z', '#f0e6d0', 'stroke-width="1.6"') + L('M' + x + ' ' + (GROUND - 14) + 'V' + (GROUND + 6) + 'M' + (x - 8) + ' ' + (GROUND - 8) + 'h16M' + (x - 7) + ' ' + (GROUND - 2) + 'h14M' + (x - 12) + ' ' + (GROUND - 16) + 'l-6 -10M' + (x + 12) + ' ' + (GROUND - 16) + 'l6 -10', '#f0e6d0', 3) + '</g>';
			const flameG = (hd) => GLOW(hd[0], hd[1] - 54, 20, '#3ad07a', 0.4) + flame(hd[0], hd[1] - 37, 13, 42, '#3ad07a', '#c8ffd8');
			const o = [g.hdR[0] + 12, g.hdR[1] - 56];
			return {
				// The bolt rides on the right torch: at the cast pose that torch stands upright, so it flies straight to the right.
				orb: [o, C(o[0], o[1], 9, '#c89bff') + GLOW(o[0], o[1], 17, '#a46cf0', 0.45) + spark(o[0], o[1], 7, WHITE) + L('M' + n1(o[0] - 30) + ' ' + n1(o[1] - 4) + 'H' + n1(o[0] - 12) + 'M' + n1(o[0] - 26) + ' ' + n1(o[1] + 5) + 'H' + n1(o[0] - 12), '#c89bff', 2.4), 'wpnR'],
				circle: [[100, GROUND], E(100, GROUND, rad, 13, '#a46cf0', 'fill-opacity=".2" stroke="#c89bff" stroke-width="3"') + E(100, GROUND, rad - 14, 9, 'none', 'stroke="#c89bff" stroke-width="1.6"') +
					L('M100 ' + GROUND + 'V' + (GROUND - 9) + 'M100 ' + GROUND + 'L' + (100 - 50) + ' ' + (GROUND + 6) + 'M100 ' + GROUND + 'L' + (100 + 50) + ' ' + (GROUND + 6) + runes.join(''), '#e8d6ff', 2.2)],
				pillars: [[100, GROUND], L('M30 ' + GROUND + 'V120M170 ' + GROUND + 'V120M100 ' + (GROUND + 8) + 'V150', '#c89bff', 8, 'stroke-opacity=".35"') + spark(30, 118, 7, WHITE) + spark(170, 118, 7, WHITE)],
				greenL: [[g.hdL[0], g.hdL[1] - 37], flameG(g.hdL), 'wpnL', 'green'],
				greenR: [[g.hdR[0], g.hdR[1] - 37], flameG(g.hdR), 'wpnR', 'green'],
				cracks: [[100, GROUND], L('M20 ' + GROUND + 'L40 ' + (GROUND - 2) + 'L52 ' + (GROUND + 2) + 'M148 ' + (GROUND + 1) + 'L162 ' + (GROUND - 2) + 'L182 ' + GROUND, '#3ad07a', 3)],
				raise: [[100, GROUND], '<g clip-path="url(#dtGround)">' + bones(36) + bones(164) + bones(70) + '</g>'],
			};
		},
		clips: {
			idle: { dur: 2.4, k: {
				root: [[0, {}], [0.5, { y: -6 }], [1, {}]],
				shadow: [[0, {}], [0.5, { s: 0.85 }], [1, {}]],
				armL: [[0, {}], [0.5, R(3)], [1, {}]], armR: [[0, {}], [0.5, R(-3)], [1, {}]],
				flame: [[0, {}], [0.25, { sy: 1.12, sx: 0.92 }], [0.5, { sy: 0.94 }], [0.75, { sy: 1.08 }], [1, {}]],
			} },
			atk: { dur: 1.3, k: {
				armR: [[0, {}], [0.4, R(-80)], [0.5, R(-95), 1, EASE_HIT], [0.8, R(-90)], [1, {}]],
				wpnR: [[0, {}], [0.4, R(80)], [0.8, R(90)], [1, {}]],
				root: [[0, {}], [0.4, { y: -6 }], [1, {}]],
				orb: [[0, { s: 0.3 }, 0], [0.45, { s: 0.5 }, 0], [0.5, { s: 1 }, 1], [0.8, { x: 80, s: 1.2 }, 1], [0.86, { x: 86, s: 1.8 }, 0]],
			} },
			a1: { dur: 2.4, k: {
				armL: [[0, {}], [0.2, R(105)], [0.8, R(105)], [1, {}]], armR: [[0, {}], [0.2, R(-105)], [0.8, R(-105)], [1, {}]],
				wpnL: [[0, {}], [0.2, R(-105)], [0.8, R(-105)], [1, {}]], wpnR: [[0, {}], [0.2, R(105)], [0.8, R(105)], [1, {}]],
				root: [[0, {}], [0.2, { y: -10 }], [0.8, { y: -10 }], [1, {}]],
				circle: [[0, { s: 0.2 }, 0], [0.25, { s: 0.2 }, 0], [0.4, { s: 1 }, 1], [0.85, { s: 1.05 }, 1], [1, { s: 1.2 }, 0]],
				pillars: [[0, { sy: 0.1 }, 0], [0.42, { sy: 0.1 }, 0], [0.5, {}, 1], [0.62, {}, 0.3], [0.7, {}, 1], [0.86, {}, 0]],
			} },
			a2: { dur: 2.6, k: {
				armL: [[0, {}], [0.18, R(128)], [0.85, R(128)], [1, {}]], armR: [[0, {}], [0.18, R(-128)], [0.85, R(-128)], [1, {}]],
				wpnL: [[0, {}], [0.18, R(-128)], [0.85, R(-128)], [1, {}]], wpnR: [[0, {}], [0.18, R(128)], [0.85, R(128)], [1, {}]],
				root: [[0, {}], [0.18, { y: -3 }], [0.85, { y: -3 }], [1, {}]],
				green: [[0, {}, 0], [0.18, {}, 0], [0.24, {}, 1], [0.85, {}, 1], [0.95, {}, 0]],
				flame: [[0, {}, 1], [0.18, {}, 1], [0.24, {}, 0], [0.85, {}, 0], [0.95, {}, 1]],
				cracks: [[0, {}, 0], [0.3, {}, 0], [0.36, {}, 1], [0.9, {}, 1], [1, {}, 0]],
				raise: [[0, { y: 40 }, 1], [0.36, { y: 40 }, 1], [0.6, { y: 0 }, 1, EASE_OUT], [0.88, { y: 0 }, 1], [1, { y: 40 }, 1]],
			} },
		},
	};

	T['titan.artemis'] = {
		name: 'Артемида', cls: 'Hunter', classRu: 'Следопыт', stars: 3,
		abil: ['Серебряный залп', 'Лунная стрела'],
		crop: [58, 12, 84, 84],
		draw(g) {
			const skin = '#e6b98f';
			const tunic = '#7a9a5a';
			const tunicD = '#4a5e34';
			const hair = '#7a3a22';
			const silver = '#e0e8f4';
			const o = { leg: skin, shin: skin, boot: '#a8693a', bootTall: 22, arm: skin, bracer: '#a8693a', hand: skin,
				legExtra: (s, hip, kn, an) => L('M' + n1(an[0] - 7) + ' ' + n1(an[1] - 14) + 'l14 4M' + n1(an[0] - 7) + ' ' + n1(an[1] - 6) + 'l14 4', '#6b4222', 1.4) };
			const quiver = P('M' + (100 + 18) + ' 76L' + (100 + 34) + ' 60L' + (100 + 44) + ' 70L' + (100 + 28) + ' 86Z', '#8a5530') +
				L('M' + (100 + 33) + ' 62L' + (100 + 40) + ' 46M' + (100 + 37) + ' 64L' + (100 + 46) + ' 50M' + (100 + 41) + ' 67L' + (100 + 50) + ' 56', '#d6dde4', 2) +
				P('M' + (100 + 38) + ' 44l4 -6l2 8ZM' + (100 + 44) + ' 48l5 -5l1 8ZM' + (100 + 48) + ' 54l5 -4l0 8Z', '#f6ecd6', 'stroke-width="1.4"');
			const chiton = P('M' + (100 - g.S + 2) + ' ' + (g.shY - 4) + 'L' + (100 + g.S - 2) + ' ' + (g.shY - 4) + 'L' + (100 + g.W + 14) + ' ' + (g.waistY + 30) + 'Q100 ' + (g.waistY + 36) + ' ' + (100 - g.W - 14) + ' ' + (g.waistY + 30) + 'Z', tunic) +
				SHADE('M104 ' + (g.shY - 4) + 'H' + (100 + g.S - 2) + 'L' + (100 + g.W + 14) + ' ' + (g.waistY + 30) + 'Q104 ' + (g.waistY + 35) + ' 104 ' + (g.waistY + 35) + 'Z') +
				L('M' + (100 - g.W - 8) + ' ' + (g.waistY + 8) + 'L' + (100 - g.W - 12) + ' ' + (g.waistY + 28) + 'M' + (100 - 6) + ' ' + (g.waistY + 10) + 'V' + (g.waistY + 32) + 'M' + (100 + g.W + 8) + ' ' + (g.waistY + 8) + 'L' + (100 + g.W + 12) + ' ' + (g.waistY + 28), tunicD, 1.4) +
				L('M' + (100 - g.W - 13) + ' ' + (g.waistY + 28) + 'Q100 ' + (g.waistY + 34) + ' ' + (100 + g.W + 13) + ' ' + (g.waistY + 28), '#e8b85c', 2.2) +
				// Team-coloured sash across the chest.
				TEAMFILL('M' + (100 - g.S + 4) + ' ' + (g.shY - 8) + 'L' + (100 - g.S + 14) + ' ' + (g.shY - 8) + 'L' + (100 + g.W + 4) + ' ' + (g.waistY - 4) + 'L' + (100 + g.W - 6) + ' ' + (g.waistY + 2) + 'Z');
			const r = g.headR;
			const head = P('M' + (100 - r - 2) + ' 48C' + (100 - r - 6) + ' 66 ' + (100 - r - 2) + ' 80 ' + (100 - r + 4) + ' 84L' + (100 + r - 4) + ' 84C' + (100 + r + 2) + ' 80 ' + (100 + r + 6) + ' 66 ' + (100 + r + 2) + ' 48Z', hair) +
				C(100, 30, 9, hair) + P(skull(100, 54, r, 1.08), skin) + SHADE('M106 42C116 46 116 64 108 74L104 60Z') +
				P('M' + (100 - r - 1) + ' 52C' + (100 - r - 2) + ' 30 ' + (100 + r + 2) + ' 30 ' + (100 + r + 1) + ' 52C' + (100 + r - 4) + ' 42 ' + (100 + 4) + ' 40 100 44C' + (100 - 4) + ' 40 ' + (100 - r + 4) + ' 42 ' + (100 - r - 1) + ' 52Z', hair) +
				// Crescent moon diadem.
				P('M90 38C92 24 108 24 110 38C106 30 94 30 90 38Z', silver) + spark(100, 30, 3, '#e8ffb0') +
				L('M91 55l6 0.5M109 55l-6 0.5', '#3a5a2a', 2.6) + L('M90 51l7 1M110 51l-7 1', INK, 1.6) + P('M97 66Q100 68 103 66Q100 69 97 66Z', '#b85a4a');
			const hl = g.hdL;
			// A tall silver bow held upright: the huntress's silhouette.
			const bowPath = 'M' + n1(hl[0] - 2) + ' ' + n1(hl[1] - 68) + 'C' + n1(hl[0] - 30) + ' ' + n1(hl[1] - 30) + ' ' + n1(hl[0] - 30) + ' ' + n1(hl[1] + 30) + ' ' + n1(hl[0] - 2) + ' ' + n1(hl[1] + 68);
			const bow = L(bowPath, INK, 7) + L(bowPath, silver, 4) + L('M' + n1(hl[0] - 2) + ' ' + n1(hl[1] - 68) + 'L' + n1(hl[0] + 6) + ' ' + n1(hl[1]) + 'L' + n1(hl[0] - 2) + ' ' + n1(hl[1] + 68), '#f6ecd6', 1.2, 'class="p-string"') +
				spark(hl[0] - 2, hl[1] - 70, 4, '#e8ffb0') + spark(hl[0] - 2, hl[1] + 70, 4, '#e8ffb0');
			return {
				back: quiver,
				legs: G('p-legL', g.hipL, leg(g, -1, o)) + G('p-legR', g.hipR, leg(g, 1, o)),
				torso: P('M' + (100 - g.neck) + ' 84V66H' + (100 + g.neck) + 'V84Z', skin) + P(torsoPath(g), tunic) + chiton + belt(g, '#a8693a', '#e8b85c'),
				head,
				armL: arm(g, -1, o, bow),
				armR: arm(g, 1, o),
			};
		},
		fx(g) {
			const arrow = (len) => L('M0 0H' + len, '#f6ecd6', 2.4) + P('M' + len + ' 0l-8 -4v8Z', '#e0e8f4', 'stroke-width="1.2"') + P('M0 0l-6 -4h6l4 4l-4 4h-6Z', '#e8ffb0', 'stroke-width="1"');
			const hl = g.hdL;
			const falling = [[40, 190], [80, 205], [140, 196], [170, 210], [110, 214]].map(([x, y]) => '<g transform="translate(' + x + ' ' + y + ') rotate(110)">' + arrow(30) + '</g>').join('');
			return {
				// Shots ride on the bow (the bow hand's frame is upright while aiming), so they leave from the string.
				shot: [[hl[0], hl[1]], '<g transform="translate(' + (hl[0] + 10) + ' ' + hl[1] + ') rotate(180)">' + arrow(40) + '</g>', 'wpnL'],
				up: [[hl[0], hl[1]], '<g transform="translate(' + (hl[0] - 2) + ' ' + (hl[1] - 30) + ') rotate(-90)">' + arrow(30) + '</g><g transform="translate(' + (hl[0] + 8) + ' ' + (hl[1] - 36) + ') rotate(-80)">' + arrow(30) + '</g><g transform="translate(' + (hl[0] - 10) + ' ' + (hl[1] - 36) + ') rotate(-100)">' + arrow(30) + '</g>', 'wpnL'],
				rain: [[100, 200], falling + spark(60, 200, 5, WHITE) + spark(150, 206, 5, WHITE)],
				moon: [[30, 40], C(30, 40, 30, '#f2f6e0', 'fill-opacity=".9" stroke="#e8ffb0" stroke-width="3"') + C(22, 34, 6, '#d6dcc0', 'stroke="none"') + C(38, 50, 4, '#d6dcc0', 'stroke="none"') + GLOW(30, 40, 44, '#e8ffb0', 0.3)],
				beam: [[hl[0], hl[1]], L('M' + (hl[0] + 8) + ' ' + hl[1] + 'H-120', '#e8ffb0', 14, 'stroke-opacity=".45"') + L('M' + (hl[0] + 8) + ' ' + hl[1] + 'H-120', WHITE, 4) + P('M' + (hl[0] - 30) + ' ' + (hl[1] - 16) + 'C' + (hl[0] - 44) + ' ' + (hl[1] - 6) + ' ' + (hl[0] - 44) + ' ' + (hl[1] + 6) + ' ' + (hl[0] - 30) + ' ' + (hl[1] + 16) + 'C' + (hl[0] - 38) + ' ' + (hl[1] + 4) + ' ' + (hl[0] - 38) + ' ' + (hl[1] - 4) + ' ' + (hl[0] - 30) + ' ' + (hl[1] - 16) + 'Z', '#f2f6e0'), 'wpnL'],
			};
		},
		clips: {
			idle: { dur: 1.8, k: {
				upper: [[0, {}], [0.5, { y: 1.2 }], [1, {}]],
				head: [[0, {}], [0.5, R(-2)], [1, {}]],
				armL: [[0, {}], [0.5, R(1.5)], [1, {}]], armR: [[0, {}], [0.5, R(-2)], [1, {}]],
			} },
			atk: { dur: 1.3, k: {
				armL: [[0, {}], [0.3, R(84)], [0.8, R(84)], [1, {}]], foreL: [[0, {}], [0.3, R(6)], [0.8, R(6)], [1, {}]],
				wpnL: [[0, {}], [0.3, R(-90)], [0.8, R(-90)], [1, {}]],
				armR: [[0, {}], [0.3, R(100)], [0.5, R(80)], [0.8, R(80)], [1, {}]], foreR: [[0, {}], [0.3, R(-120)], [0.5, R(-140)], [0.8, R(-140)], [1, {}]],
				head: [[0, {}], [0.3, R(-6)], [0.8, R(-6)], [1, {}]],
				root: [[0, {}], [0.3, { x: 28 }], [0.5, { x: 28 }], [0.56, { x: 32 }], [0.8, { x: 28 }], [1, {}]],
				shot: [[0, {}, 0], [0.5, {}, 0], [0.52, {}, 1], [0.66, { x: -90 }, 1], [0.7, { x: -110 }, 0]],
			} },
			a1: { dur: 2.2, k: {
				armL: [[0, {}], [0.2, R(160)], [0.5, R(160)], [0.7, {}]], wpnL: [[0, {}], [0.2, R(-150)], [0.5, R(-150)], [0.7, {}]],
				armR: [[0, {}], [0.2, R(150)], [0.4, R(140)], [0.5, R(140)], [0.7, {}]],
				head: [[0, {}], [0.2, { y: -3 }], [0.5, { y: -3 }], [0.7, {}]],
				up: [[0, { y: -10 }, 0], [0.38, { y: -10 }, 0], [0.4, {}, 1], [0.55, { y: -140 }, 1], [0.56, { y: -140 }, 0]],
				rain: [[0, { x: -50, y: -170 }, 0], [0.58, { x: -50, y: -170 }, 1], [0.72, {}, 1, EASE_HIT], [0.92, {}, 1], [1, {}, 0]],
			} },
			a2: { dur: 2.6, k: {
				moon: [[0, { s: 0.4 }, 0], [0.2, { s: 1 }, 1], [0.85, { s: 1 }, 1], [1, { s: 1.1 }, 0]],
				armL: [[0, {}], [0.25, R(84)], [0.85, R(84)], [1, {}]], foreL: [[0, {}], [0.25, R(6)], [0.85, R(6)], [1, {}]],
				wpnL: [[0, {}], [0.25, R(-90)], [0.85, R(-90)], [1, {}]],
				armR: [[0, {}], [0.25, R(100)], [0.55, R(70)], [0.85, R(70)], [1, {}]], foreR: [[0, {}], [0.25, R(-120)], [0.55, R(-150)], [0.85, R(-150)], [1, {}]],
				root: [[0, {}], [0.25, { x: 28 }], [0.55, { x: 28 }], [0.6, { x: 34 }], [0.85, { x: 30 }], [1, {}]],
				beam: [[0, { sx: 0 }, 0], [0.56, { sx: 0 }, 1], [0.62, { sx: 1 }, 1], [0.8, { sx: 1 }, 1], [0.86, { sx: 1, sy: 0.2 }, 0]],
			} },
		},
	};

	// --- Assembly -------------------------------------------------------------------------------------
	// Effects drawn behind the figure (on the ground under it); every other effect is drawn over it.
	const UNDER = ['circle', 'press', 'herald', 'aura', 'halo', 'glowG', 'heat'];
	const cache = new Map();
	function parts(id) {
		let c = cache.get(id);
		if (!c) {
			const d = T[id];
			const g = rig(d.cls);
			// fx entries: [pivot, markup, part it rides on (optional), shared class (optional)].
			const fx = d.fx(g);
			const fxG = (k) => G('p-' + (fx[k][3] || k) + ' fx', fx[k][0], fx[k][1]);
			const attach = {};
			const loose = [];
			for (const k in fx) {
				if (fx[k][2]) attach[fx[k][2]] = (attach[fx[k][2]] || '') + fxG(k);
				else loose.push(k);
			}
			g.fxAt = (part) => attach[part] || '';
			const body = d.draw(g);
			const hov = d.hover || 0;
			const under = loose.filter((k) => UNDER.includes(k)).map(fxG).join('');
			const over = loose.filter((k) => !UNDER.includes(k)).map(fxG).join('');
			const upper = G('p-upper', [100, g.waistY + 6], (body.torso || '') + G('p-head', [100, 80], body.head + g.fxAt('head')) + body.armL + body.armR + g.fxAt('upper'));
			const root = G('p-root', [100, GROUND], (hov ? '<g transform="translate(0 ' + -hov + ')">' : '') + (body.back || '') + body.legs + upper + (hov ? '</g>' : ''));
			const markup = G('p-shadow', [100, GROUND], E(100, GROUND, d.cls === 'Mystic' ? 40 : 60, 8, '#000', 'stroke="none" fill-opacity=".28"')) + under + root + over;
			const clips = typeof d.clips === 'function' ? d.clips(g) : d.clips;
			c = { markup, def: d, g, clips, fx: new Set(Object.keys(fx).map((k) => fx[k][3] || k)) };
			cache.set(id, c);
		}
		return c;
	}

	// --- Animation: clips to CSS keyframes -----------------------------------------------------------------
	const REST = { x: 0, y: 0, r: 0, s: 1 };
	const tf = (f) => {
		const v = Object.assign({}, REST, f);
		const sx = v.sx === undefined ? v.s : v.sx;
		const sy = v.sy === undefined ? v.s : v.sy;
		return 'translate(' + n1(v.x) + 'px,' + n1(v.y) + 'px) rotate(' + n1(v.r) + 'deg) scale(' + n1(sx * 100) / 100 + ',' + n1(sy * 100) / 100 + ')';
	};
	const slug = (id) => id.replace('titan.', '');
	const isFx = (c, part) => c.fx.has(part);
	/** Keyframe list of one part across a sequence of [clip, startSec]; rest pose where the part is idle. */
	function track(c, part, seq, total) {
		const fx = isFx(c, part);
		const restOp = fx ? 0 : 1;
		const out = [];
		for (const [name, start] of seq) {
			const clip = c.clips[name];
			const k = clip.k[part];
			const frames = k || [[0, {}, restOp], [1, {}, restOp]];
			for (const f of frames) {
				const pct = ((start + f[0] * clip.dur) / total) * 100;
				out.push([pct, f[1] || {}, f[2] === undefined ? restOp : f[2], f[3]]);
			}
		}
		return out;
	}
	function keyframes(name, frames, withOpacity) {
		let s = '@keyframes ' + name + '{';
		let last = -1;
		for (const [pct, f, op, ease] of frames) {
			let p = n1(pct * 10) / 10;
			if (p <= last) p = last + 0.01;
			last = p;
			s += +p.toFixed(2) + '%{transform:' + tf(f) + (withOpacity ? ';opacity:' + op : '') + (ease ? ';animation-timing-function:' + ease : '') + '}';
		}
		return s + '}';
	}
	function partsOf(c) {
		const set = new Set();
		for (const name in c.clips) for (const p in c.clips[name].k) set.add(p);
		return [...set];
	}
	const hasOpacity = (c, part) => isFx(c, part) || Object.values(c.clips).some((cl) => (cl.k[part] || []).some((f) => f[2] !== undefined));
	/** CSS for a looping showcase: idle, attack, idle, ability 1, idle, ability 2. */
	function loopCss(id, scope) {
		const c = parts(id);
		const order = ['idle', 'atk', 'idle', 'a1', 'idle', 'a2'];
		const seq = [];
		let t = 0;
		for (const name of order) {
			seq.push([name, t]);
			t += c.clips[name].dur;
		}
		let css = '';
		for (const part of partsOf(c)) {
			const name = 'dt-' + slug(id) + '-loop-' + part;
			css += keyframes(name, track(c, part, seq, t), hasOpacity(c, part));
			css += scope + ' .p-' + part + '{animation:' + name + ' ' + n1(t) + 's ease-in-out infinite}';
		}
		// Clip names under the portrait while each clip plays; the class line shows during idle.
		const caps = { atk: 'cap-atk', a1: 'cap-a1', a2: 'cap-a2', idle: 'cap-sub' };
		for (const clip in caps) {
			const frames = [];
			for (const [name, start] of seq) frames.push([(start / t) * 100, name === clip ? 1 : 0]);
			const kn = 'dt-' + slug(id) + '-' + caps[clip];
			css += '@keyframes ' + kn + '{' + frames.map(([p, op]) => +n1(p).toFixed(2) + '%{opacity:' + op + '}').join('') + '}' +
				scope + ' .' + caps[clip] + '{animation:' + kn + ' ' + n1(t) + 's step-end infinite}';
		}
		return { css, total: t };
	}
	/** CSS for inline figures: idle loops; adding class play-atk / play-a1 / play-a2 to the svg plays that clip once. */
	function gameCss(id) {
		const c = parts(id);
		const s = slug(id);
		let css = '';
		for (const name in c.clips) {
			const clip = c.clips[name];
			for (const part in clip.k) {
				const kn = 'dt-' + s + '-' + name + '-' + part;
				css += keyframes(kn, clip.k[part].map((f) => [f[0] * 100, f[1] || {}, f[2] === undefined ? (isFx(c, part) ? 0 : 1) : f[2], f[3]]), hasOpacity(c, part));
				css += name === 'idle' ? '.dtF.t-' + s + ' .p-' + part + '{animation:' + kn + ' ' + clip.dur + 's ease-in-out infinite}'
					: '.dtF.t-' + s + '.play-' + name + ' .p-' + part + '{animation:' + kn + ' ' + clip.dur + 's ease-in-out both}';
			}
		}
		return css;
	}
	const BASE_CSS = '.dtF g{transform-box:view-box}.dtF .fx{opacity:0}.dtF.bust .fx,.dtF.still .fx{display:none}';
	let styleEl = null;
	const injected = new Set();
	/** Adds the keyframes of a titan to the page once (inline use). */
	function ensureCss(id) {
		if (typeof document === 'undefined' || injected.has(id)) return;
		if (!styleEl) {
			styleEl = document.createElement('style');
			styleEl.textContent = BASE_CSS;
			document.head.appendChild(styleEl);
		}
		styleEl.textContent += gameCss(id);
		injected.add(id);
	}
	const defs = '<defs><clipPath id="dtGround"><rect x="0" y="-40" width="200" height="' + (GROUND + 40) + '"/></clipPath></defs>';
	const svgOpen = (vb, cls, extra) => '<svg class="' + cls + '" viewBox="' + vb + '"' + (extra || '') + ' fill="none" stroke="' + INK + '" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">';

	/** Inline figure. opts.view 'bust' crops to the head; opts.still disables animation; opts.enemy recolours the team cloth. */
	function svg(id, opts) {
		const o = opts || {};
		if (!T[id]) return '';
		const c = parts(id);
		const bust = o.view === 'bust';
		if (!bust && !o.still) ensureCss(id);
		const cr = c.def.crop;
		const vb = bust ? cr.join(' ') : '0 -20 200 260';
		const cls = 'dtF t-' + slug(id) + (bust ? ' bust gi' : '') + (o.still ? ' still' : '') + (o.cls ? ' ' + o.cls : '');
		let m = c.markup;
		if (o.enemy) m = m.split(TEAM).join('#d9483b');
		return svgOpen(vb, cls, ' aria-hidden="true"') + (bust ? '' : defs) + m + '</svg>';
	}
	const playing = new WeakMap();
	/** Plays a clip once on an inline figure. */
	function play(el, clip) {
		if (!el || !el.classList) return;
		const m = /t-([a-z]+)/.exec(el.getAttribute('class') || '');
		const d = m && T['titan.' + m[1]] && parts('titan.' + m[1]);
		if (!d || !d.clips[clip]) return;
		for (const k of ['atk', 'a1', 'a2']) el.classList.remove('play-' + k);
		void el.getBoundingClientRect();
		el.classList.add('play-' + clip);
		clearTimeout(playing.get(el));
		playing.set(el, setTimeout(() => el.classList.remove('play-' + clip), d.clips[clip].dur * 1000 + 50));
	}

	const RARITY = ['#9aa3ad', '#3d8bff', '#a458f5', '#f2b23a'];
	/** Self-contained SVG file: portrait card, or with opts.loop the looping showcase of every clip. */
	function standalone(id, opts) {
		const o = opts || {};
		const c = parts(id);
		const d = c.def;
		const s = slug(id);
		const rc = RARITY[d.stars - 1];
		const scope = '#dt-' + s;
		let css = '.fx{opacity:0}g{transform-box:view-box}text{font-family:Cinzel,Georgia,"Times New Roman",serif;text-anchor:middle;stroke:none}';
		let caps = '';
		if (o.loop) {
			const lc = loopCss(id, scope);
			css += lc.css;
			caps = ['atk', 'a1', 'a2'].map((k, i) => '<text class="cap-' + k + '" x="100" y="269" font-size="12" fill="#ffd98a" opacity="0">' + (i ? d.abil[i - 1] : 'Атака') + '</text>').join('');
		}
		const stars = '★'.repeat(d.stars) + '<tspan fill="#5a5048">' + '★'.repeat(4 - d.stars) + '</tspan>';
		return '<svg xmlns="http://www.w3.org/2000/svg" id="dt-' + s + '" viewBox="0 0 200 280" width="400" height="560" fill="none" stroke="' + INK + '" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">' +
			'<title>' + d.name + ' — Dawn of Titans</title><style>' + css + '</style>' +
			'<defs><clipPath id="dtGround"><rect x="0" y="-40" width="200" height="' + (GROUND + 40) + '"/></clipPath><clipPath id="dtCard"><rect x="4" y="4" width="192" height="272" rx="14"/></clipPath>' +
			'<radialGradient id="dtBg" cx="50%" cy="38%" r="70%"><stop offset="0" stop-color="#3b4a66"/><stop offset="1" stop-color="#151b28"/></radialGradient></defs>' +
			'<rect x="4" y="4" width="192" height="272" rx="14" fill="url(#dtBg)" stroke="' + rc + '" stroke-width="4"/>' +
			'<g clip-path="url(#dtCard)"><rect x="0" y="236" width="200" height="50" fill="#0f131c" stroke="none" opacity=".7"/>' +
			'<g transform="translate(14 24) scale(.86)">' + c.markup + '</g></g>' +
			'<text x="100" y="254" font-size="16" font-weight="700" fill="#f6ecd6">' + d.name + ' <tspan font-size="11" fill="' + rc + '">' + stars + '</tspan></text>' +
			'<text class="cap-sub" x="100" y="269" font-size="10.5" fill="#b8c0cc">' + d.classRu + '</text>' + caps + '</svg>';
	}

	return { svg, play, standalone, ensureCss, has: (id) => !!T[id], ids: () => Object.keys(T), clips: (id) => (T[id] ? parts(id).clips : null), info: (id) => T[id] };
})();
if (typeof module !== 'undefined') module.exports = DotTitans;
