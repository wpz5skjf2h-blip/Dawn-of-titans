'use strict';
// Game icons: hand-drawn SVG glyphs (48 x 48, ink outline) shared by the kingdom screen and the battle HUD.
// The tile behind a glyph (colour by category) comes from CSS (.ico ...), so the same glyph works on any background.
// Names: troop.<id>, building.<id>, spell.<id>, relic.<kind id>, class.<Class>, altar.<id>, land.<gold|food|forest|lake|mountain|ruins|monument|arena>,
// mission.<hunt|elite|boss>, ui.<name>.

const DotIcons = (() => {
	const INK = '#26180c';
	const K = {
		steel: '#d6dde4', steelD: '#8e9aa6', iron: '#7d8593', wood: '#b57a43', woodD: '#7d4f27', woodL: '#d6a061',
		gold: '#f4c44f', goldD: '#c48a22', red: '#e04b3a', redD: '#a22d22', blue: '#4c8fe6', cream: '#f6ecd6',
		stone: '#ddd4c5', stoneD: '#aa9e8c', fire: '#ff8c2a', fireY: '#ffd84f', bone: '#f0e6d0', burlap: '#dcbc7e',
		brown: '#a8693a', brownD: '#6b4222', white: '#ffffff',
	};
	const attr = (extra) => (extra ? ' ' + extra : '');
	const p = (d, fill, extra) => '<path d="' + d + '" fill="' + fill + '"' + attr(extra) + '/>';
	const c = (x, y, r, fill, extra) => '<circle cx="' + x + '" cy="' + y + '" r="' + r + '" fill="' + fill + '"' + attr(extra) + '/>';
	const r = (x, y, w, h, rx, fill, extra) => '<rect x="' + x + '" y="' + y + '" width="' + w + '" height="' + h + '" rx="' + rx + '" fill="' + fill + '"' + attr(extra) + '/>';
	const line = (d, col, w, extra) => '<path d="' + d + '" stroke="' + col + '" stroke-width="' + w + '"' + attr(extra) + '/>';
	const hi = (d) => line(d, K.white, 1.6, 'stroke-opacity="0.65"');
	/** An outlined rod (handles, shafts, poles). */
	const stick = (d, col, w) => line(d, INK, w + 2.6) + line(d, col, w);
	const rot = (deg, body, cx, cy) => '<g transform="rotate(' + deg + ' ' + (cx === undefined ? 24 : cx) + ' ' + (cy === undefined ? 24 : cy) + ')">' + body + '</g>';
	const at = (x, y, s, body) => '<g transform="translate(' + x + ' ' + y + ') scale(' + s + ')">' + body + '</g>';
	/** Four-point sparkle. */
	const spark = (x, y, s, fill) => p('M' + x + ' ' + (y - s) + 'Q' + x + ' ' + y + ' ' + (x + s) + ' ' + y + 'Q' + x + ' ' + y + ' ' + x + ' ' + (y + s) +
		'Q' + x + ' ' + y + ' ' + (x - s) + ' ' + y + 'Q' + x + ' ' + y + ' ' + x + ' ' + (y - s) + 'Z', fill, 'stroke-width="1"');
	const drop = (x, y, s, fill) => p('M' + x + ' ' + y + 'c0 ' + 3 * s + ' ' + -2.2 * s + ' ' + 4.6 * s + ' ' + -2.2 * s + ' ' + 6.4 * s +
		'c0 ' + 2.6 * s + ' ' + 4.4 * s + ' ' + 2.6 * s + ' ' + 4.4 * s + ' 0c0 ' + -1.8 * s + ' ' + -2.2 * s + ' ' + -3.4 * s + ' ' + -2.2 * s + ' ' + -6.4 * s + 'z', fill);

	// --- Shared parts -----------------------------------------------------------------------------
	const SWORD = p('M24 3L28.5 9V30H19.5V9Z', K.steel) + line('M24 9V28', K.steelD, 1.4) + hi('M21.5 10V27') +
		r(13, 29.5, 22, 4.5, 2.2, K.gold) + r(21.5, 34, 5, 7, 1.5, K.woodD) + c(24, 43.5, 3, K.gold);
	const BOW = (fletch) => stick('M17 4C33 12 33 36 17 44', K.wood, 3.4) + line('M17 4L10 24L17 44', K.cream, 1.3) +
		stick('M8 24H37', K.woodL, 2) + p('M44 24L36 19.5L38 24L36 28.5Z', K.steel) + p('M10 24L5 19H11L15 24L11 29H5Z', fletch);
	const HELMET = p('M14 20C16 9 32 9 34 20L31 21C29 15 19 15 17 21Z', K.red) + p('M13 31C13 19 35 19 35 31Z', K.steel) +
		r(11.5, 29, 25, 4.5, 2, K.gold) + p('M13 33.5H19.5V42C16 42 13 39 13 36Z', K.steel) + p('M35 33.5H28.5V42C32 42 35 39 35 36Z', K.steel) +
		r(22.3, 33, 3.4, 9, 1.5, K.steel) + hi('M17 25C18 22.5 20 21.5 22 21');
	const HAMMER = stick('M24 15V44', K.wood, 3.4) + r(13, 6, 22, 9, 2, '#8e98a6') + hi('M15.5 8.5H32.5');
	const SACK = p('M15 17C10 23 8 31 9 37C10 43 38 43 39 37C40 31 38 23 33 17Z', K.burlap) + r(15, 14, 18, 4.5, 2.2, K.woodD) +
		p('M17 14.5C15 9 21 7 24 11C27 7 33 9 31 14.5Z', K.burlap) +
		line('M24 38V27M24 30.5L20.5 27.5M24 30.5L27.5 27.5M24 34.5L20.5 31.5M24 34.5L27.5 31.5', '#9a7a3e', 1.6) + hi('M14 24C12 28 12 32 13 35');
	const PADLOCK = line('M29 27V22C29 15 41 15 41 22V27', INK, 5.4) + line('M29 27V22C29 15 41 15 41 22V27', K.steelD, 3) +
		r(26, 26, 18, 16, 3, '#aab4c0') + c(35, 32, 2, INK) + p('M34.2 33H35.8L36.5 38H33.5Z', INK, 'stroke="none"') + hi('M29 29H41');
	const COIN = (x, y) => p('M' + (x - 11) + ' ' + y + 'v4a11 4 0 0 0 22 0v-4Z', K.goldD) + '<ellipse cx="' + x + '" cy="' + y + '" rx="11" ry="4" fill="' + K.gold + '"/>';
	const GEM = (fill, facet) => p('M24 5L33.5 13L24 25L14.5 13Z', fill) +
		line('M14.5 13H33.5M20 13L24 25L28 13M19.5 9L20 13M28.5 9L28 13', facet, 1.2) + hi('M17.5 12L22 7.5');
	const RAYS = line('M24 0.5V3M9 7L12 9.5M39 7L36 9.5M6 17H10M38 17H42', K.fireY, 2);
	const CLOUD = (fill) => p('M11 23C5 23 5 14 11.5 13.5C12.5 7 21 4.5 25 10C28.5 5.5 37 7 36.5 13.5C43 13.5 43 23 36.5 23Z', fill);
	const ISLAND = p('M5 24C5 20 43 20 43 24C43 28 36 30 33 35C30 41 26 45 24 45C21 45 18 41 15 35C12 30 5 28 5 24Z', '#9c7048') +
		line('M15 31L19 35M30 32L27 37', '#6b4a2e', 1.3) + p('M5 24C5 19.5 43 19.5 43 24C43 27 5 27 5 24Z', '#7fc95a');
	/** Five-point star (victory points). */
	const STAR = (x, y, rr, fill) => {
		let d = '';
		for (let i = 0; i < 10; i++) {
			const a = -Math.PI / 2 + (i * Math.PI) / 5;
			const q = i % 2 ? rr * 0.45 : rr;
			d += (i ? 'L' : 'M') + (x + Math.cos(a) * q).toFixed(1) + ' ' + (y + Math.sin(a) * q).toFixed(1);
		}
		return p(d + 'Z', fill, 'stroke-width="1.4"');
	};
	const PINE = (x, y, s) => r(x - 1.2 * s, y - 3 * s, 2.4 * s, 3.4 * s, 0.6, K.woodD, 'stroke-width="1.2"') +
		p('M' + (x - 6 * s) + ' ' + (y - 2.5 * s) + 'L' + x + ' ' + (y - 17 * s) + 'L' + (x + 6 * s) + ' ' + (y - 2.5 * s) + 'Z', '#3f8f45') +
		hi('M' + (x - 1.5 * s) + ' ' + (y - 12 * s) + 'L' + (x - 3.5 * s) + ' ' + (y - 6 * s));
	const snowflake = () => {
		let d = '';
		for (let k = 0; k < 6; k++) {
			const a = (k * Math.PI) / 3 - Math.PI / 2;
			const pt = (rr, aa) => (24 + rr * Math.cos(aa)).toFixed(1) + ' ' + (24 + rr * Math.sin(aa)).toFixed(1);
			d += 'M24 24L' + pt(19, a) + 'M' + pt(11, a) + 'L' + pt(16.5, a - 0.62) + 'M' + pt(11, a) + 'L' + pt(16.5, a + 0.62);
		}
		return stick(d, '#bff1ff', 2.6) + c(24, 24, 3.6, K.white);
	};

	const G = {
		// --- Troops -------------------------------------------------------------------------------
		'troop.militia': stick('M30 44L38 8', K.wood, 3.4) + p('M35 10C38 3 45 4 46 10C46 15 42 19 37 18Z', K.steel) + hi('M40 7C43 8 44 10 44 12') +
			c(19, 28, 14, K.steelD) + c(19, 28, 11, K.wood) + line('M14 18.5V37.5M19 17V39M24 18.5V37.5', K.woodD, 1.3) + c(19, 28, 4, K.steel),
		'troop.archers': BOW(K.red),
		'troop.spearmen': stick('M35 46V12', K.wood, 3) + p('M35 2L39.5 11L35 15L30.5 11Z', K.steel) + line('M35 4.5V12', K.steelD, 1.2) +
			p('M7 13H29V27C29 36 22 42 18 44C14 42 7 36 7 27Z', K.red) + line('M10.5 20L18 27L25.5 20', K.cream, 3) + c(18, 33, 2.6, K.gold) + hi('M10 16H26'),
		'troop.swordsmen': rot(45, SWORD),
		'troop.outriders': p('M30 8C37 12 41 24 41 44H45C45 24 42 10 33 5Z', K.brownD) +
			p('M27 4L31 9C37 14 40 26 40 44H21C23 38 23 34 21 30C19 31 14 32 10 32C7 32 5 29 6.5 26L15 14C18 10 21 8 24.5 8Z', K.brown) +
			p('M26 6.5L28.5 9.5H25.5Z', K.brownD, 'stroke="none"') + c(19.5, 16.5, 1.8, INK) + c(9.5, 27.5, 1.1, INK) +
			line('M8.5 24.5L21 27.5L24 12', K.red, 1.8) + hi('M25 10.5C30 13 34 18 36 26'),
		'troop.knights': p('M23 8C21 2 27 0 31 3C35 0 41 3 39 8C43 9 43 15 37 14C33 11 28 9 23 8Z', K.red) +
			p('M11 21C11 11 17 6 24 6C31 6 37 11 37 21V38C37 41 35 43 32 43H16C13 43 11 41 11 38Z', K.steel) +
			r(14, 20, 20, 4, 2, INK) + line('M24 7V19M24 25V42', K.steelD, 1.4) +
			c(18, 30, 1.2, INK) + c(18, 34.5, 1.2, INK) + c(30, 30, 1.2, INK) + c(30, 34.5, 1.2, INK) + hi('M15 14C17 10 20 8.5 23 8'),
		// A clay firepot with a burning fuse.
		'troop.grenadiers': c(22, 31, 13, K.brown) + r(16, 15, 12, 6, 2, K.brownD) + line('M12 27C16 24 28 24 32 27', K.brownD, 1.6) +
			stick('M22 15C24 10 28 9 31 8', K.cream, 1.6) + p('M33 1C39 5 40 12 35 14C30 15 28 10 31 7C32 9 34 9 33 1Z', K.fire) +
			p('M33.5 6.5C36 9 36 12 34 12.5C32 12.5 32 10 33.5 6.5Z', K.fireY, 'stroke="none"') + hi('M13 29C13 25 16 22 19 21'),
		// Two iron chain links and a barbed harpoon head.
		'troop.chainbreakers': stick('M6 42L30 18', K.wood, 3) + p('M27 13L40 8L35 21L32 18L28 22L26 16Z', K.steel) +
			'<g fill="none">' + line('M27 37a5 7 -45 1 1 9 -9a5 7 -45 1 1 -9 9Z', INK, 5.4) + line('M27 37a5 7 -45 1 1 9 -9a5 7 -45 1 1 -9 9Z', K.iron, 3) +
			line('M35 45a5 7 45 1 1 9 -9a5 7 45 1 1 -9 9Z', INK, 5.4) + line('M35 45a5 7 45 1 1 9 -9a5 7 45 1 1 -9 9Z', K.iron, 3) + '</g>',
		// A priest's staff crowned with a golden sun.
		'troop.priests': stick('M24 46V20', K.wood, 3.4) + RAYS + c(24, 13, 8, K.gold) + c(24, 13, 4, K.fireY) + hi('M19 10C20 8 22 7 24 7') +
			p('M15 30C19 27 29 27 33 30L31 33C27 31 21 31 17 33Z', K.cream),
		// A long pike with an ice-blue head and a frost sparkle.
		'troop.pikemen': stick('M7 45L34 10', K.wood, 3) + p('M41 1L38.5 12.5L35 10L31.5 7.5Z', '#9fe3ff') + line('M38 4.5L34.5 9.5', '#4f9fc8', 1.2) +
			p('M26 23C29 21 33 23 33 26C31 27 28 26 26 23Z', K.cream) + spark(14, 13, 5, '#bff1ff') + spark(38, 30, 3.4, K.white),
		// A giant's stone-headed club bound with a jade band.
		'troop.goliaths': stick('M8 45L26 22', K.wood, 4.6) + c(31, 15, 11, '#9a948a') + line('M23 11C28 7 36 9 39 14', '#2f9a72', 3.6) +
			c(26, 19, 1.6, '#6e695f', 'stroke="none"') + c(35, 18, 2, '#6e695f', 'stroke="none"') + hi('M24 12C26 8.5 30 7 33 7.5'),
		// A black panther's head with green eyes and a drop of venom on the fang.
		'troop.panthers': p('M10 7L17 15C21 13.5 27 13.5 31 15L38 7L39 21C42 25 41 33 36 37C32 40.5 28 42 24 42C20 42 16 40.5 12 37C7 33 6 25 9 21Z', '#2a2730') +
			p('M17 24L22 26.5L17 28Z', '#9cff6a', 'stroke-width="1"') + p('M31 24L26 26.5L31 28Z', '#9cff6a', 'stroke-width="1"') +
			p('M21 31.5H27L24 34.5Z', '#4a4452', 'stroke-width="1"') + p('M21 36L22 40L23 36Z', K.white, 'stroke-width="0.8"') + p('M25 36L26 40L27 36Z', K.white, 'stroke-width="0.8"') +
			drop(26, 40, 0.55, '#8fdc4f') + hi('M13 21C14 18.5 16 17 18 16.5'),
		// A bolt-thrower seen from the side: two wheels, the stock, the bow arms with their string and a steel-headed bolt.
		'troop.ballista': stick('M7 39L30 17', K.wood, 4.4) + c(11, 40, 5.2, K.woodD) + c(11, 40, 1.8, K.iron) + c(25, 41, 5.2, K.woodD) + c(25, 41, 1.8, K.iron) +
			'<g fill="none">' + stick('M19 6Q24 22 41 28', K.woodL, 3) + line('M19 6L21 26L41 28', K.cream, 1.3) + '</g>' +
			stick('M21 26L39 8', K.woodD, 2.2) + p('M45 2L42.5 11L36.5 5Z', K.steel) + p('M20 23L24 27L20 30L16 26Z', K.red, 'stroke-width="1"') +
			hi('M11 33L26 18'),

		// --- Buildings ----------------------------------------------------------------------------
		'building.castle': r(4, 18, 11, 25, 1, K.stone) + p('M3 19L9.5 6L16 19Z', K.blue) + r(33, 18, 11, 25, 1, K.stone) + p('M32 19L38.5 6L45 19Z', K.blue) +
			r(8, 26, 3, 4.5, 1.5, INK) + r(37, 26, 3, 4.5, 1.5, INK) +
			p('M16 43V12H19.5V15H22.3V12H25.7V15H28.5V12H32V43Z', K.stone) + p('M20 43V36C20 31.5 28 31.5 28 36V43Z', K.woodD) +
			r(22.5, 19, 3, 5, 1.5, INK) + line('M24 12V3', INK, 1.6) + p('M24 3L31 5L24 7.5Z', K.red) + hi('M6 21V40'),
		'building.gold_mine': p('M9 24C9 17 15 14 19 17C21 11 29 11 31 16C35 13 41 17 39 24Z', K.gold) +
			line('M15.5 20.5L19 17M25 15L27 19.5M33 19L35.5 21.5', K.goldD, 1.3) +
			p('M6 23H42L37.5 38H10.5Z', '#8d6a4c') + line('M8 28.5H40', '#5e4430', 1.4) + hi('M10 26H22') +
			c(15, 40, 4.2, '#5a5d68') + c(33, 40, 4.2, '#5a5d68') + c(15, 40, 1.4, K.steel, 'stroke="none"') + c(33, 40, 1.4, K.steel, 'stroke="none"') +
			spark(38, 9, 4.5, K.white),
		'building.farm': p('M7 22L24 9L41 22V43H7Z', K.red) + p('M4 24L24 7L44 24L40.5 25.5L24 12L7.5 25.5Z', '#6e3428') +
			r(17, 28, 14, 15, 0.5, K.redD) + line('M17 28L31 43M31 28L17 43', K.cream, 1.8) +
			'<rect x="17" y="28" width="14" height="15" fill="none" stroke="' + K.cream + '" stroke-width="1.8"/>' + r(21, 15.5, 6, 6, 1, K.cream),
		'building.gold_vault': p('M9 19L12 6H36L39 19Z', K.woodD) + line('M13 9.5H35', K.goldD, 1.4) +
			p('M7 21C10 13 18 12 24 14C30 12 38 13 41 21Z', K.gold) + c(17, 16.5, 2.6, K.gold) + c(30, 16, 2.6, K.gold) + c(23.5, 13, 2.6, K.gold) +
			r(7, 21, 34, 21, 2, K.wood) + r(7, 21, 34, 4.5, 1, K.gold) + r(12.5, 25.5, 3.5, 16.5, 0, K.goldD) + r(32, 25.5, 3.5, 16.5, 0, K.goldD) +
			r(21, 25, 6, 7, 1.5, K.gold) + c(24, 28, 1.1, INK, 'stroke="none"'),
		'building.granary': stick('M20 15L15 4M24 14V2.5M28 15L33 4', K.goldD, 1.3) +
			'<ellipse cx="15" cy="5" rx="2.2" ry="3.6" transform="rotate(-25 15 5)" fill="' + K.gold + '"/>' +
			'<ellipse cx="24" cy="3.5" rx="2.2" ry="3.6" fill="' + K.gold + '"/>' +
			'<ellipse cx="33" cy="5" rx="2.2" ry="3.6" transform="rotate(25 33 5)" fill="' + K.gold + '"/>' + SACK,
		'building.gold_reserve': COIN(18, 36) + COIN(18, 30) + COIN(18, 24) + PADLOCK,
		'building.food_reserve': at(-2, 4, 0.82, SACK) + PADLOCK,
		'building.barracks': at(3.6, 1, 0.85, rot(40, SWORD) + rot(-40, SWORD)) + HELMET,
		'building.army_camp': p('M4 41L24 9L44 41Z', K.cream) + p('M24 9L13 41H18.5Z', K.red, 'stroke="none"') + p('M24 9L29.5 41H35Z', K.red, 'stroke="none"') +
			p('M4 41L24 9L44 41Z', 'none') + p('M24 23L18.5 41H29.5Z', '#5a3a22') + line('M24 9V2.5', INK, 1.8) + p('M24 3L32 5.5L24 8Z', K.gold) +
			line('M2 41.5H46', INK, 2),
		'building.garrison': p('M13 43V9H17V12H21.5V9H26.5V12H31V9H35V43Z', K.stone) + p('M31 12H35V43H31Z', K.stoneD, 'stroke="none"') +
			p('M13 43V9H17V12H21.5V9H26.5V12H31V9H35V43Z', 'none') + line('M13 37H35', K.stoneD, 1.2) +
			p('M18 16H30V24C30 30 24 33 24 33C24 33 18 30 18 24Z', K.blue) + line('M24 18.5V30M20 22.5H28', K.cream, 1.8) +
			p('M20 43V39C20 36 28 36 28 39V43Z', K.woodD),
		'building.armoury': p('M4 19C10 19 14 18 16 16H41V22C37 23 33 24 31 26V31H37V37H13V31H19V26C15 24 10 22 4 19Z', K.iron) + hi('M17.5 18.5H39') +
			rot(-35, stick('M31 12V27', K.wood, 3) + r(24, 4, 14, 8, 2, K.steel), 31, 12) + spark(10, 10, 3.6, K.fireY) + spark(17, 5, 2.4, K.fireY),
		'building.medical_school': line('M18 15V11C18 7.5 30 7.5 30 11V15', INK, 4.6) + line('M18 15V11C18 7.5 30 7.5 30 11V15', '#8a5a3a', 2.4) +
			r(7, 15, 34, 27, 5, K.cream) + p('M21 19.5H27V25.5H33V31.5H27V37.5H21V31.5H15V25.5H21Z', K.red) + hi('M10 19V37'),
		'building.alliance_camp': stick('M12 44V7', K.woodD, 2.4) + c(12, 5.5, 2.4, K.gold) + p('M12 9H27V31L19.5 26.5L12 31Z', K.blue) + spark(19.5, 17, 4, K.cream) +
			stick('M36 44V7', K.woodD, 2.4) + c(36, 5.5, 2.4, K.gold) + p('M36 13H21V35L28.5 30.5L36 35Z', K.gold) + spark(28.5, 21, 4, K.redD),
		'building.arcane_tower': p('M17 43L19.5 18H28.5L31 43Z', K.stone) + line('M18.6 27H29.4M17.8 35H30.2', K.stoneD, 1.2) +
			p('M15 19.5L24 4L33 19.5Z', '#3d55c4') + r(14, 18, 20, 3.5, 1.5, K.gold) + p('M22 30V24C22 21.5 26 21.5 26 24V30Z', K.fireY) +
			p('M21.5 43V39C21.5 36.5 26.5 36.5 26.5 39V43Z', K.woodD) + spark(24, 4, 4, K.fireY) + spark(38, 11, 2.8, K.white) + spark(9.5, 14, 2.2, K.white),
		'building.spell_forge': p('M14 44C12 40 15 38 16 35C18 38 20 39 20 42C21 40 23 38 24 35C26 38 28 40 28 42C29 39 31 38 32 35C33 38 36 40 34 44Z', K.fire) +
			p('M8 21H40C40 33 34 40 24 40C14 40 8 33 8 21Z', '#5d5a6a') + hi('M12 25C12.5 30 15 34 18 36') + r(6, 18, 36, 5, 2.5, '#8a869a') +
			p('M9.5 18.5C10 14.5 38 14.5 38.5 18.5Z', '#6dffb0') + c(18, 11, 2.4, '#6dffb0') + c(27, 7.5, 3, '#6dffb0') + c(33.5, 12, 1.8, '#6dffb0') + spark(39, 5, 2.8, K.white),
		'building.portal': p('M7 43V20C7 10 15 4 24 4C33 4 41 10 41 20V43H33V21C33 15.5 29 11.5 24 11.5C19 11.5 15 15.5 15 21V43Z', K.stone) +
			p('M15 43V21C15 15.5 19 11.5 24 11.5C29 11.5 33 15.5 33 21V43Z', '#8d5cf6') +
			line('M24 29C21.5 29 21.5 25.5 24 25.5C27.5 25.5 27.5 31.5 24 32.5C18.5 33.5 17.5 25 21.5 22C26 19 32 23 31 30C30 36 24 39 19 37', '#efe4ff', 1.8) +
			p('M21.5 3.5H26.5L25.5 9H22.5Z', K.stoneD) + line('M7 30H15M33 30H41M9 15L15.5 18M39 15L32.5 18', K.stoneD, 1.2) + r(4, 41, 40, 4, 1.5, K.stoneD),
		'building.builders_yard': rot(38, p('M19.5 15H28.5V43L19.5 40Z', K.steel) +
				line('M28.5 17l2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8 2.4 1.8-2.4 1.8', K.steelD, 1.2) +
				r(17.5, 4, 13, 11, 3, K.woodL) + r(21, 7, 6, 4.5, 2, INK)) +
			rot(-38, HAMMER),
		'building.hall_of_titans': p('M4 20L24 7L44 20Z', K.stone) + p('M19 17.5V12L21.5 14.5L24 11L26.5 14.5L29 12V17.5Z', K.gold) +
			r(6, 20, 36, 3.5, 0.5, K.stoneD) + r(9, 23.5, 4, 15.5, 0, K.stone) + r(16.5, 23.5, 4, 15.5, 0, K.stone) +
			r(27.5, 23.5, 4, 15.5, 0, K.stone) + r(35, 23.5, 4, 15.5, 0, K.stone) + p('M21.5 39V30C21.5 27 26.5 27 26.5 30V39Z', INK) +
			r(4, 39, 40, 5, 1, K.stoneD),
		'building.relic_temple': RAYS + GEM('#5fbaff', '#2c6fb0') +
			p('M13 44H35V40H32V32H35V28H13V32H16V40H13Z', K.stone) + line('M16 36H32', K.stoneD, 1.2),
		'building.eternal_shrine': p('M24 3C30 11 36 15 33.5 23C32.5 26.5 28.5 28 24 28C18 28 14 25 14.5 20C15 16 18.5 14.5 19.5 11.5C21 15.5 22.5 16.5 24 17C22.5 12.5 21.5 8.5 24 3Z', K.fire) +
			p('M24 13C27 17 30 19.5 28.5 23.5C27.5 26 25.5 26.5 24 26.5C21 26.5 19.5 25 19.8 22.5C20 20 22 19 22.5 17C23.5 19.5 24.5 20 25 20.5C24.5 18 23.5 16 24 13Z', K.fireY, 'stroke="none"') +
			p('M9 26H39C39 32.5 33 37 24 37C15 37 9 32.5 9 26Z', '#c58f43') + r(7.5, 24, 33, 4, 2, '#e0aa55') +
			r(21, 36.5, 6, 4, 0.5, '#8d6a3a') + r(14, 40, 20, 4.5, 2, K.stoneD),
		// Envoy councils: a sun palace, a frost longhouse, a jade pagoda and a marsh stilt hut.
		'building.council_sunreach': RAYS + p('M13 22C13 13 18 9 24 9C30 9 35 13 35 22Z', K.gold) + line('M24 9V4', INK, 1.6) + c(24, 3.5, 1.8, K.gold) +
			r(9, 22, 30, 21, 1, '#ecd09a') + r(5, 17, 5, 26, 1, '#d8b578') + r(38, 17, 5, 26, 1, '#d8b578') + c(7.5, 16, 3, K.gold) + c(40.5, 16, 3, K.gold) +
			p('M20 43V34C20 29 28 29 28 34V43Z', '#7a3a1c') + line('M12 27H36', K.goldD, 1.3) + hi('M16 18C17 14 20 12 23 11.5'),
		'building.council_rimeheim': p('M4 25L24 8L44 25Z', '#4f7fa8') + p('M4 25L24 8L44 25L40 25L24 12L8 25Z', '#f4f8fb', 'stroke="none"') +
			p('M4 25L24 8L44 25Z', 'none') + line('M9 21L5 15M39 21L43 15', INK, 2) + r(8, 25, 32, 18, 0.5, '#7a5634') +
			line('M8 31H40M8 37H40', '#5b3e24', 1.2) + p('M20 43V33H28V43Z', '#4a3220') + p('M33 33L35.5 27L38 33L35.5 39Z', '#bfe8ff', 'stroke-width="1.2"') +
			spark(12, 6, 3.6, '#bff1ff'),
		'building.council_jade': p('M6 21C12 21 16 18 18 14H30C32 18 36 21 42 21Z', '#2f9a72') + r(14, 21, 20, 8, 0.5, '#b83a2e') +
			p('M3 33C10 33 14 31 16 28H32C34 31 38 33 45 33Z', '#2f9a72') + r(11, 33, 26, 11, 0.5, '#b83a2e') + p('M20 44V37H28V44Z', '#5e2018') +
			p('M21 14L24 4L27 14Z', K.gold) + line('M8 33.5L4 37M40 33.5L44 37', INK, 1.4) + hi('M9 20.5H17M7 32.5H15'),
		'building.council_mistmoor': c(37, 9, 5.5, '#dfffc0') + c(35, 7.5, 5, '#3e6a5a', 'stroke="none"') +
			line('M12 44V30M36 44V30M18 44V33M30 44V33', '#4e3a26', 2.4) + p('M3 44C10 41 38 41 45 44Z', '#3f6a4a') +
			p('M10 31H38V22H10Z', '#7a5b3a') + p('M6 23L24 7L42 23Z', '#caa458') + line('M12 18L24 9.5L36 18', '#9b7c3a', 1.2) +
			p('M21 31V25H27V31Z', '#1d1a22') + c(8, 34, 1.6, '#9cff6a', 'stroke="none"') + c(41, 30, 1.4, '#9cff6a', 'stroke="none"') + c(15, 38, 1.2, '#9cff6a', 'stroke="none"'),
		'building.adventurers_guild': p('M5 13L16.5 9L31.5 13L43 9V36L31.5 40L16.5 36L5 40Z', '#f0dfb2') + line('M16.5 9V36M31.5 13V40', '#b89a62', 1.3) +
			line('M9 33C14 27 19 31 23 25C27 19 32 23 36 17', '#c0392b', 1.8, 'stroke-dasharray="2.5 2.5"') + line('M34 11.5L39 16.5M39 11.5L34 16.5', K.red, 2.2) +
			c(12.5, 19, 4.5, K.cream) + p('M12.5 15.5L13.9 19L12.5 22.5L11.1 19Z', K.red, 'stroke-width="1"'),

		// --- Spells -------------------------------------------------------------------------------
		'spell.fireball': p('M7 32C7 22 15 16 23 18C29 12 37 8 45 6C39 12 39 16 41 20C35 20 33 24 31.5 28C30 36 24 42 16.5 42C11 42 7 38 7 32Z', K.fire) +
			p('M11 32C11 26 16 22 21 23C23.5 26 25.5 28.5 24.5 32.5C23.5 36 20.5 38 17 38C13.5 38 11 35.5 11 32Z', K.fireY, 'stroke="none"') +
			c(16.5, 32, 3.2, '#fff7d6', 'stroke="none"'),
		'spell.shield': p('M24 4L39 9.5V21C39 31.5 32.5 38.5 24 43C15.5 38.5 9 31.5 9 21V9.5Z', '#8fd0ff') +
			line('M24 8.5L35 12.5V21C35 29 30 34.5 24 38C18 34.5 13 29 13 21V12.5Z', K.white, 1.6, 'stroke-opacity="0.8"') +
			spark(24, 22, 7.5, K.white) + spark(41, 6, 2.6, K.white) + spark(7, 37, 2.2, K.white),
		'spell.freeze': snowflake(),
		'spell.lightning': CLOUD('#d3d9e6') + p('M25.5 18L15.5 32H22.5L18.5 45L33 27.5H26L30.5 18Z', K.fireY),
		'spell.rage': [0, 9.5, 19].map((dx) => at(dx, 0, 1, p('M8 7C16 16 18.5 29 14.5 42C23 31 22.5 17 8 7Z', '#ff5a3c') + hi('M10.5 10C14 14 16 19 16.5 24'))).join(''),
		'spell.terror': p('M24 5C14 5 8 12 8 21C8 27 11 30 14 32V38C14 40 16 42 18 42H30C32 42 34 40 34 38V32C37 30 40 27 40 21C40 12 34 5 24 5Z', K.bone) +
			'<ellipse cx="17.5" cy="22" rx="4.6" ry="5.2" fill="#2a1d33"/><ellipse cx="30.5" cy="22" rx="4.6" ry="5.2" fill="#2a1d33"/>' +
			c(17.5, 22.5, 1.8, '#d9a8ff', 'stroke="none"') + c(30.5, 22.5, 1.8, '#d9a8ff', 'stroke="none"') + p('M24 27L21.5 32H26.5Z', INK) +
			line('M16 36.5H32M19.5 36.5V42M24 36.5V42M28.5 36.5V42', INK, 1.4) + hi('M13 15C14.5 11.5 17.5 9 21 8.5'),
		'spell.acid_cloud': drop(14, 29, 1, '#8fdc4f') + drop(24, 30, 1.3, '#8fdc4f') + drop(33.5, 29, 1, '#8fdc4f') +
			p('M10 26C3.5 26 3.5 16 10.5 15.5C11.5 8 21 5.5 25.5 11.5C29.5 6.5 39 8 38.5 15.5C45 15.5 45 26 38.5 26Z', '#8fdc4f') +
			c(19, 18, 2.2, '#d8ffb0', 'stroke="none"') + c(29, 15, 1.6, '#d8ffb0', 'stroke="none"') + c(33, 20, 1.2, '#d8ffb0', 'stroke="none"'),
		'spell.raise_dead': p('M3 45C7 37 15 35 24 35C33 35 41 37 45 45Z', '#6b5a44') +
			p('M19.5 36V25.5L15.5 21.5C14 20 16 17.8 17.6 19.3L19.5 21.2V15.5C19.5 13.5 22.5 13.5 22.5 15.5V22L23.3 12C23.5 10 26.5 10.2 26.3 12.2L25.8 22.2L28.3 14.8C29 12.9 31.8 13.8 31.2 15.7L28.8 24.5L31.6 20.6C32.8 19 35.1 20.6 34 22.3L29.5 30C28.5 32 28 34 28 36Z', K.bone) +
			line('M22.5 22V26.5M25.8 22.2V26.5M28.8 24.5V27', INK, 1.1) +
			c(11, 20, 2, '#9dff8a', 'stroke="none"') + c(38, 8, 1.6, '#9dff8a', 'stroke="none"') + spark(10, 8, 2.8, '#9dff8a') + spark(40, 27, 2.4, '#9dff8a'),
		'spell.meteor_strike': p('M18 25C24 15 34 9 45 4C40 11 40 15 42 18C36 18 33 22 31 27Z', K.fire) +
			p('M23 24C28 17 35 12 41 9C38 14 38 16 39 18C35 18 32 21 30 25Z', K.fireY, 'stroke="none"') +
			p('M8 30L12 22L21 19L29 23L31 32L26 40L16 42L9 38Z', '#8a7a6a') + c(16, 30, 2.6, '#6b5e52', 'stroke-width="1"') +
			c(23, 35, 1.8, '#6b5e52', 'stroke-width="1"') + c(24, 26, 1.6, '#6b5e52', 'stroke-width="1"'),
		'spell.war_drums': stick('M11 4L23 17', K.woodL, 2.6) + c(10.5, 3.8, 2.6, K.cream) + stick('M37 4L25 17', K.woodL, 2.6) + c(37.5, 3.8, 2.6, K.cream) +
			p('M9 22V35C9 41 39 41 39 35V22Z', K.red) + line('M9 28L14 37.5L19 29L24 39L29 29L34 37.5L39 28', K.cream, 1.6) +
			p('M9 22C9 17 39 17 39 22C39 27 9 27 9 22Z', K.cream),
		'spell.healing_spring': p('M24 4C30 14 38.5 20 38.5 29.5C38.5 37.5 32 43.5 24 43.5C16 43.5 9.5 37.5 9.5 29.5C9.5 20 18 14 24 4Z', '#5cc6ff') +
			p('M21.5 22H26.5V27.5H32V32.5H26.5V38H21.5V32.5H16V27.5H21.5Z', K.white) + hi('M15 27C15 23.5 17 20.5 19.5 18'),

		// --- Relics (the stat sets the shape, the rarity tile the colour) ---------------------------
		'relic.atk': p('M24 9L29 15L37 13L34 21L41 26L34 31L37 39L29 37L24 44L19 37L11 39L14 31L7 26L14 21L11 13L19 15Z', K.gold) +
			p('M24 15L31 26L24 37L17 26Z', '#ff4a4a') + line('M17 26H31M24 15V37', '#a8221f', 1) + hi('M20 23L23.5 18'),
		'relic.hp': c(24, 26, 15, K.gold) + c(24, 26, 12, K.goldD, 'stroke-width="1"') +
			p('M24 37C16 31 12 26 12 21.5C12 17.5 15 15 18.5 15C21 15 23 16.5 24 18.5C25 16.5 27 15 29.5 15C33 15 36 17.5 36 21.5C36 26 32 31 24 37Z', '#49d17a') +
			hi('M15.5 21C15.5 19 17 18 18.5 18'),
		'relic.armor': p('M24 9L40 14V26C40 35 32 41 24 44C16 41 8 35 8 26V14Z', K.gold) +
			p('M24 14L35 17.5V26C35 32 30 36.5 24 39C18 36.5 13 32 13 26V17.5Z', '#4f8dff') + line('M24 14V39M13 25H35', '#2a4fa8', 1) + hi('M16 19L21 17'),
		'relic.eye': c(24, 25, 16, K.gold) + p('M9 25C14 16.5 34 16.5 39 25C34 33.5 14 33.5 9 25Z', K.cream) + c(24, 25, 6.5, '#5fbf4a') +
			c(24, 25, 2.8, INK, 'stroke="none"') + c(21.8, 22.8, 1.3, K.white, 'stroke="none"') + hi('M13 17C15 14 18 12.5 21 12'),
		'relic.fang': r(13, 5, 22, 7, 2.5, K.gold) + p('M15 12C21 13.5 28 13.5 33 12C33 25 28 36 21 45C20 33 18 22 15 12Z', K.bone) +
			line('M18.5 15C20 24 21 32 21.5 39', '#c9b98f', 1.2) + drop(31, 30, 1.2, '#7ad94a') + c(18, 8.5, 1.6, '#ff4a4a', 'stroke-width="1"') + c(30, 8.5, 1.6, '#ff4a4a', 'stroke-width="1"'),
		'relic.banner': stick('M12 3V45', K.woodD, 3) + c(12, 3.5, 2.4, K.gold) + p('M14 7H39L33 16L39 25H14Z', K.red) +
			c(24, 16, 4.2, K.gold) + line('M24 12.5V19.5M20.5 16H27.5', K.goldD, 1.2) + hi('M16.5 10H31'),
		'relic.amulet': line('M12 5C13 15 19 20 24 20.5C29 20 35 15 36 5', INK, 4.4) + line('M12 5C13 15 19 20 24 20.5C29 20 35 15 36 5', K.goldD, 2.2) +
			c(24, 31, 12, K.gold) + c(24, 31, 7, '#ff4a4a') + p('M24 26L26.5 31L24 36L21.5 31Z', '#ffb0a8', 'stroke-width="1"') + hi('M16.5 27C17.5 24.5 19.5 23 22 22.5'),
		'relic.scroll': r(12, 9, 24, 30, 1.5, K.cream) + r(8.5, 6, 31, 6.5, 3.2, K.woodL) + r(8.5, 35.5, 31, 6.5, 3.2, K.woodL) +
			STAR(24, 23.5, 7.5, K.gold) + line('M16 32H32', '#c9b98f', 1.2),
		'relic.pickaxe': stick('M13 43L31 13', K.wood, 3.2) + p('M15 12C22 4.5 35 4.5 44 13C35 10 27 10.5 21 15Z', K.steel) + hi('M19 10C24 7 31 6.5 36 8') +
			p('M6 35L11 31L16 34L14 40L8 41Z', K.gold) + spark(8, 29, 2.4, K.white),
		'relic.plough': stick('M7 9L25 27', K.woodD, 3) + stick('M18 20L13 27', K.woodD, 2.4) + p('M23 25L40 27L36 41L19 37Z', K.steel) + hi('M25 28L36 29.5') +
			stick('M37 4V16', '#9a7a3e', 1.2) + '<ellipse cx="37" cy="5" rx="2.4" ry="4" fill="' + K.gold + '"/>',
		'relic.bugle': p('M7 21C19 21 29 15 35 7L41 9C38.5 20 38.5 30 41 41L35 43C29 35 19 29 7 29Z', K.gold) + r(4, 20, 5.5, 10, 2, K.goldD) +
			line('M35 7.5C33 19 33 31 35 42.5', K.goldD, 1.4) + line('M13 29C16 35 22 37 28 34', K.red, 2.2) + hi('M12 23.5C19 23.5 25 20 30 15'),
		'relic.seal': p('M16 30L12 45L18 41L21 46L24 33Z', K.red) + p('M32 30L36 45L30 41L27 46L24 33Z', K.red) +
			p('M24 6L28 9L33 8L34 13L38.5 16L36.5 21L38.5 26L34 29L33 34L28 33L24 36L20 33L15 34L14 29L9.5 26L11.5 21L9.5 16L14 13L15 8L20 9Z', '#c4322a') +
			c(24, 21, 7.5, '#a22d22', 'stroke-width="1.2"') + STAR(24, 21, 5.2, K.gold),
		'relic.hammer': rot(-35, HAMMER) + spark(38, 9, 3.4, '#c48bff') + spark(9, 12, 2.4, '#c48bff') + spark(40, 27, 2, K.white),
		'relic.tome': p('M8 10C14 8 20 9 24 12C28 9 34 8 40 10V39C34 37 28 38 24 41C20 38 14 37 8 39Z', '#7d4f27') +
			p('M11 12.5C15.5 11 20 12 23 14.5V37C20 35 15.5 34.5 11 35.5Z', K.cream, 'stroke-width="1.2"') +
			p('M37 12.5C32.5 11 28 12 25 14.5V37C28 35 32.5 34.5 37 35.5Z', K.cream, 'stroke-width="1.2"') + STAR(31, 24, 4.5, K.gold) + line('M14 20H20M14 25H20M14 30H20', '#c9b98f', 1.2),
		'relic.chalice': p('M12 7H36C36 20 31 27 24 27C17 27 12 20 12 7Z', K.gold) + p('M14.5 9.5H33.5C33 13 32 15 31 16.5H17C16 15 15 13 14.5 9.5Z', '#5cc6ff', 'stroke-width="1.2"') +
			stick('M24 27V37', K.goldD, 2.8) + r(14, 37, 20, 6, 2.5, K.gold) + hi('M16 18C17 21 19 23 21 24'),

		// --- Titan classes ------------------------------------------------------------------------
		'class.Berserker': stick('M24 46V8', K.woodD, 3.2) + p('M22 10C14 6 7 10 6 18C7 25 13 28 22 25Z', K.steel) + p('M26 10C34 6 41 10 42 18C41 25 35 28 26 25Z', K.steel) +
			hi('M9 16C10 12 13 10 17 10') + r(21.5, 24, 5, 4, 1, K.gold) + p('M24 2L26.5 8H21.5Z', K.steel),
		'class.Champion': rot(25, SWORD),
		'class.Guardian': p('M10 6H38V30C38 38 31 43 24 45C17 43 10 38 10 30Z', K.blue) + line('M24 9V41M13 22H35', '#2c5aa8', 2.2) +
			line('M13.5 9.5H34.5V29.5C34.5 36 29 40 24 41.5C19 40 13.5 36 13.5 29.5Z', K.cream, 1.4, 'stroke-opacity="0.8"') + c(24, 22, 4.5, K.gold),
		'class.Sorcerer': stick('M24 46V19', K.wood, 3) + p('M16.5 17C17 23 31 23 31.5 17L28 19.5L24 15.5L20 19.5Z', K.gold) + c(24, 11, 8, '#c48bff') +
			c(21.5, 8.5, 2.6, '#f3e6ff', 'stroke="none"') + spark(36, 6, 2.8, K.white) + spark(11, 20, 2.2, K.white),
		'class.Ranger': BOW('#5fbf4a'),

		// --- Relic Temple altars, lands, menu -------------------------------------------------------
		'altar.daily': r(9, 21, 30, 22, 2, '#ff6b5e') + r(7, 15, 34, 7, 2, '#ff8a7d') + r(21.5, 15, 5, 28, 0, K.gold) +
			p('M24 15C18 6 11 9 15 14Z', K.gold) + p('M24 15C30 6 37 9 33 14Z', K.gold),
		'altar.silver': at(-6, -3, 1.25, RAYS + GEM('#e6eef6', '#7d8a99')),
		'altar.gold': at(-6, -3, 1.25, RAYS + GEM('#ffd257', '#b0801c')),
		'altar.power': at(-6, -3, 1.25, RAYS + GEM('#ff6b9a', '#a8224f')),
		'land.gold': ISLAND + COIN(19, 15) + COIN(29, 13) + spark(37, 6, 3.2, K.white),
		'land.food': ISLAND + stick('M17 22V10M24 22V7M31 22V10', '#9a7a3e', 1.3) +
			'<ellipse cx="17" cy="9" rx="2.4" ry="4" fill="' + K.gold + '"/><ellipse cx="24" cy="6" rx="2.4" ry="4" fill="' + K.gold + '"/><ellipse cx="31" cy="9" rx="2.4" ry="4" fill="' + K.gold + '"/>',
		'land.forest': ISLAND + PINE(14, 24, 0.9) + PINE(31, 24, 1) + PINE(23, 25, 1.15) + STAR(39, 8, 5.5, K.gold),
		'land.lake': ISLAND + '<ellipse cx="22" cy="23.4" rx="12.5" ry="2.5" fill="#4fa7e8" stroke-width="1.4"/>' +
			hi('M15 23.2H21') + stick('M36 23V14M38.5 23V16', '#6f8f3a', 1.1) + STAR(13, 11, 6, K.gold),
		'land.mountain': ISLAND + p('M8 24L21 5L34 24Z', '#9a958a') + p('M16.5 11.5L21 5L25.5 11.5L23 10.3L21 12.5L19 10.3Z', K.white, 'stroke-width="1.3"') +
			p('M26 24L33 13L40 24Z', '#7f7a6e') + STAR(39, 7, 5, K.gold),
		'land.ruins': ISLAND + r(10, 9, 5, 15, 1, K.stone) + r(9, 7, 7, 3, 1, K.stoneD) + r(31, 14, 5, 10, 1, K.stone) +
			p('M31 14L33 12L36 14Z', K.stone, 'stroke-width="1.2"') + p('M24 4L28.5 12L24 22L19.5 12Z', '#8fd8ff') + hi('M22 11L24 6.5') + spark(39, 6, 2.8, K.white),
		// Titan Monument: a titan's statue on a plinth, with a shard beside it.
		'land.monument': ISLAND + r(15, 18, 14, 5, 1, K.stoneD) + p('M17 18L18.5 10L16 6.5L19 3.5H25L28 6.5L25.5 10L27 18Z', K.stone) + c(22, 3.6, 2.6, K.stone) +
			hi('M19.5 7L20.5 12') + p('M35 9L38 14L35 21L32 14Z', '#8fd8ff') + hi('M34 13L35 10.5'),
		// XP Arena: a stone ring with crossed blades over it.
		'land.arena': ISLAND + '<ellipse cx="24" cy="20" rx="14" ry="4.8" fill="' + K.stone + '"/>' + '<ellipse cx="24" cy="19.4" rx="9" ry="2.6" fill="#d9b779"/>' +
			at(13.5, -1.5, 0.45, rot(38, SWORD) + rot(-38, SWORD)) + spark(39, 7, 3, K.white),
		'ui.attack': rot(40, SWORD) + rot(-40, SWORD),
		'ui.army': HELMET,
		'ui.build': rot(-30, HAMMER),
		// Two crossed alliance banners.
		'ui.alliance': rot(-14, stick('M24 45V6', K.woodD, 2.4) + c(24, 4.5, 2.4, K.gold) + p('M24 8H10V27L17 23L24 27Z', K.red) + spark(17, 15.5, 3.6, K.cream), 24, 45) +
			rot(14, stick('M24 45V6', K.woodD, 2.4) + c(24, 4.5, 2.4, K.gold) + p('M24 8H38V27L31 23L24 27Z', K.blue) + spark(31, 15.5, 3.6, K.cream), 24, 45),
		// A league crystal (the original's reward crystals hold a relic or a titan).
		'ui.crystal': p('M24 3L34 13L30 43H18L14 13Z', '#b9a6ff') + p('M24 3L28 13L26 43H22L20 13Z', '#d9ceff', 'stroke-width="1.2"') +
			line('M14 13H34', '#6b55c4', 1.4) + hi('M17 15L19.5 38') + spark(37, 7, 3.2, K.white) + spark(9, 26, 2.4, K.white),
		'ui.crack': line('M15 7L22 19L17 25L28 36L26 45', INK, 3.4) + line('M15 7L22 19L17 25L28 36L26 45', K.white, 1.2),
		// Campaigns: a map with a dashed road to a red cross.
		'ui.campaign': p('M6 11L17 7L31 11L42 7V37L31 41L17 37L6 41Z', K.cream) + line('M17 7V37M31 11V41', '#c9b48a', 1.3) +
			line('M11 33C15 26 20 30 23 23C26 17 31 21 34 15', K.woodD, 1.8, 'stroke-dasharray="2.6 2.4"') +
			line('M33 11L38 16M38 11L33 16', K.red, 2.4) + c(11, 33, 2.2, K.gold),
		// Daily quests: a scroll with a green tick.
		'ui.quest': r(9, 4, 30, 7, 3.5, K.burlap) + p('M12 8H36V40H12Z', K.cream) + r(9, 37, 30, 7, 3.5, K.burlap) +
			line('M17 15H31M17 20H29M17 25H25', '#c9b48a', 1.6) + line('M20 31L24 35L33 24', '#3f9a4a', 3.4) + spark(40, 9, 3, K.fireY),
		// A star chest of a campaign act.
		'ui.chest': p('M8 22H40V40C40 41.5 39 42 37.5 42H10.5C9 42 8 41.5 8 40Z', K.wood) + p('M8 22C8 12 40 12 40 22Z', K.woodL) +
			r(8, 20.5, 32, 4, 1, K.gold) + r(13, 14.5, 4, 27.5, 1, K.gold) + r(31, 14.5, 4, 27.5, 1, K.gold) + r(20.5, 23, 7, 8, 1.5, K.gold) +
			c(24, 27, 1.4, INK) + spark(41, 9, 3.2, K.fireY) + hi('M11 18C13 15.5 17 14.5 20 14.5'),
		// Mission types: a titan hunt (a horned head in the sights), an elite fight (a shield with a star), a boss (a crowned skull).
		'mission.hunt': p('M14 20C9 17 7 10 10 5C11 11 15 13 18 15Z', K.bone) + p('M34 20C39 17 41 10 38 5C37 11 33 13 30 15Z', K.bone) +
			p('M15 22C15 13 33 13 33 22V30C33 37 28 41 24 41C20 41 15 37 15 30Z', K.brown) + c(20, 26, 1.8, K.fireY) + c(28, 26, 1.8, K.fireY) +
			line('M24 2V10M24 38V46M2 26H10M38 26H46', K.red, 2.2) + c(24, 26, 15, 'none', 'stroke="' + K.red + '" stroke-width="2"'),
		'mission.elite': p('M24 4L40 9V23C40 33 33 40 24 44C15 40 8 33 8 23V9Z', K.blue) + p('M24 8L36 12V23C36 31 31 36 24 40Z', '#78aef0', 'stroke="none"') +
			STAR(24, 23, 9, K.gold) + hi('M12 12V22'),
		'mission.boss': p('M11 20L14 6L20 13L24 4L28 13L34 6L37 20Z', K.gold) + c(14, 6, 1.6, K.red) + c(24, 4, 1.6, K.blue) + c(34, 6, 1.6, K.red) +
			p('M12 22C12 15 36 15 36 22V31C36 34 33 35 31 35V41H17V35C15 35 12 34 12 31Z', K.bone) +
			p('M16 25C16 22 22 22 22 25C22 28 16 28 16 25Z', INK) + p('M26 25C26 22 32 22 32 25C32 28 26 28 26 25Z', INK) +
			p('M22.5 31L24 28.5L25.5 31Z', INK) + line('M21 35V41M24 35V41M27 35V41', INK, 1.2),
	};

	function svg(name, cls) {
		const body = G[name];
		if (!body) return '';
		return '<svg class="gi' + (cls ? ' ' + cls : '') + '" viewBox="0 0 48 48" aria-hidden="true" fill="none" stroke="' + INK +
			'" stroke-width="2" stroke-linejoin="round" stroke-linecap="round">' + body + '</svg>';
	}
	const images = new Map();
	/** Cached <img> of an icon for canvas drawing; null until it has loaded. */
	function image(name) {
		let im = images.get(name);
		if (!im) {
			im = new Image();
			im.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svg(name).replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" '));
			images.set(name, im);
		}
		return im.complete && im.naturalWidth ? im : null;
	}
	return { svg, image, has: (name) => !!G[name], names: () => Object.keys(G) };
})();
