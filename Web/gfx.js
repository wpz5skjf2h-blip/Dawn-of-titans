'use strict';
// Web/gfx.js - look of the battlefield on top of the battle view in app.js: sky, textured ground, water with ripples, grass
// and flowers that sway, forests and hills around the field, and particles (dust, fire, sparks, frost, stars, debris),
// lightning and scorch marks for the battle's events. The pictures are the SVG and JPG files in Web/art (bundled by bundle.py
// as <script id="art-data">); each is rasterised once into a canvas texture. Nothing here changes the battle itself.
//   DotGfx.init(THREE, renderer)                      once, before anything else
//   DotGfx.attach(scene, camera, { height, quality })  the battle scene; height(x, z) is the sim's ground height
//   DotGfx.dressTerrain(mesh, st, CF)                  splat-textured ground for the battle's heightfield; also picks the map's
//                                                      look (meadow, dark forest, dry steppe, snowy pass at night) and sets the
//                                                      scene's sun and sky light to it
//   DotGfx.surroundings(group, W, D)                   sky, clouds, land, forest and hills beyond the field
//   DotGfx.foliage(group, st, CF)                      grass tufts and flowers
//   DotGfx.forest(group, items), rockGeometry()        trees (items [x, y, z, scale, r]) and rough rocks
//   DotGfx.water(deep), worldUv(geo, x0, z0, size)     water material; world-scaled UVs for a ground-aligned geometry
//   DotGfx.shore(geo, round, fade)                     water fading out over its last `fade` metres (needed by water())
//   DotGfx.burst(kind, x, z, y, s0, s1, h)             effects for the battle's spawnFx kinds (sim coordinates)
//   DotGfx.emit(kind, v3, size)                        one particle from a titan emitter (world coordinates)
//   DotGfx.trail(kind, x, y, z)                        trail particle for projectiles (world coordinates)
//   DotGfx.glow(hex), envMap(), tex(name)              shared textures
//   DotGfx.cloudHeap(out, rnd, x, y, z, w, h, n)       puffs of one cumulus heap, for puffClouds
//   DotGfx.puffClouds(puffs, opts)                     soft 3D clouds from puffs: { mesh, update(dt, camera) } (the kingdom's sky)
//   DotGfx.cloudShadows(mat, scene)                    drifting cloud shadows on a lit material ('battle' or 'kingdom'); tune
//                                                      them with cloudShadowSet(scene), move them with stepCloudShadows(scene, dt)
//   DotGfx.update(dt, t)                               every battle frame, before rendering (also moves the battle's cloud shadows)

const DotGfx = (() => {
	let T = null;
	let renderer = null;
	let scene = null;
	let camera = null;
	let heightAt = () => 0;
	let quality = 'high';
	const art = {};
	const canvases = {};
	const textures = {};
	const time = { t: 0, u: { value: 0 } };

	// --- Looks of the maps --------------------------------------------------------------------------------------
	// After the original's battles (screenshots in the project): olive meadows worn brown in patches with cobbled roads, dark
	// conifer woods, dry tan steppe with dark round trees and dead ones, and a frosty pass under a night sky. Per look:
	// grass tint and saturation, dirt tint, snow over the ground, worn patches, cobbles on roads, tree mix and tint, grass
	// tufts and flowers, sun and sky light, sky tint, fog range, snow line on the mountains, the buildings around the field.
	const LOOKS = {
		meadow: { grass: [0.45, 0.32, 0.38], sat: 0.62, dirt: [0.64, 0.56, 0.47], snow: 0, worn: 0.5, cobble: 1,
			trees: [['pine', 0.38], ['fir', 0.66], ['oak', 0.92], ['birch', 1]], treeTint: [0.8, 0.6, 0.56], tuft: [0.6, 0.52, 0.4], tufts: 1, flowers: 0.06,
			sun: [0xffe8c6, 1.5], hemi: [0xd6e2ea, 0x5b4a2e, 0.58], sky: 0xffffff, fog: [190, 560], snowLine: 0.6, props: 'village', water: [0x21434c, 0x48686c] },
		forest: { grass: [0.42, 0.34, 0.38], sat: 0.6, dirt: [0.58, 0.5, 0.44], snow: 0, worn: 0.35, cobble: 0.6,
			trees: [['pine', 0.45], ['fir', 0.88], ['oak', 0.97], ['birch', 1]], treeTint: [0.7, 0.62, 0.58], tuft: [0.54, 0.5, 0.38], tufts: 1, flowers: 0.03,
			sun: [0xffe4c0, 1.35], hemi: [0xc8d6de, 0x4a3e28, 0.55], sky: 0xe6ecf0, fog: [170, 520], snowLine: 0.62, props: 'cabins', water: [0x1d3a40, 0x405c5a] },
		steppe: { grass: [0.98, 0.7, 0.62], sat: 0.26, dirt: [0.86, 0.76, 0.66], snow: 0, worn: 0.6, cobble: 0,
			trees: [['oak', 0.72], ['dead', 1]], treeTint: [0.6, 0.6, 0.44], tuft: [0.92, 0.76, 0.5], tufts: 0.7, flowers: 0.01,
			sun: [0xffdcaa, 1.7], hemi: [0xeadcc0, 0x6a5232, 0.6], sky: 0xfff0dc, fog: [210, 620], snowLine: 0.85, props: 'ruins', water: [0x365660, 0x66807c] },
		winter: { grass: [0.74, 0.74, 0.74], sat: 0.3, dirt: [0.62, 0.62, 0.64], snow: 0.82, worn: 0.4, cobble: 0,
			trees: [['snowfir', 0.8], ['dead', 1]], treeTint: [0.62, 0.68, 0.74], tuft: [0.62, 0.66, 0.72], tufts: 0.35, flowers: 0,
			sun: [0xd4dae6, 1.15], hemi: [0x8592ac, 0x2c303a, 0.62], sky: 0x46536e, fog: [150, 470], snowLine: 0.25, props: 'fort', water: [0x1c3042, 0x4e6276] },
	};
	const MAP_LOOK = { 'Highvale Ford': 'meadow', 'Mirror Lake': 'meadow', Darkwood: 'forest', 'Fallen City': 'steppe', 'The Pass': 'winter' };
	let lookName = 'meadow';
	let look = LOOKS.meadow;
	/** Picks the look for a map and sets the scene's sun, sky light and fog to it. */
	function setLook(mapName) {
		lookName = MAP_LOOK[mapName] || 'meadow';
		look = LOOKS[lookName];
		if (!scene) return;
		scene.traverse((o) => {
			if (o.isDirectionalLight) { o.color.setHex(look.sun[0]); o.intensity = look.sun[1]; }
			else if (o.isHemisphereLight) { o.color.setHex(look.hemi[0]); o.groundColor.setHex(look.hemi[1]); o.intensity = look.hemi[2]; }
		});
		if (scene.fog) { scene.fog.near = look.fog[0]; scene.fog.far = look.fog[1]; }
	}

	// --- Art: SVG text -> canvas -> texture (textures exist at once and fill in when the image has loaded) ----------
	function readArt() {
		const el = document.getElementById('art-data');
		if (!el) return;
		try { Object.assign(art, JSON.parse(el.textContent)); } catch (e) { /* no art: plain colours */ }
	}
	function svgSize(text) {
		const m = /viewBox="0 0 (\d+) (\d+)"/.exec(text);
		return m ? [+m[1], +m[2]] : [128, 128];
	}
	/**
	 * Draws art[name] into a canvas of `scale` times its size; calls done(canvas) once drawn. Photo textures (JPEG data URIs)
	 * keep their own resolution and are never scaled up.
	 */
	function raster(name, scale, done) {
		const text = art[name];
		if (!text) return;
		const photo = text.startsWith('data:image/');
		const img = new Image();
		img.onload = () => {
			const [w, h] = photo ? [img.naturalWidth, img.naturalHeight] : svgSize(text);
			const k = photo ? Math.min(1, scale) : scale;
			const c = document.createElement('canvas');
			c.width = Math.round(w * k);
			c.height = Math.round(h * k);
			c.getContext('2d').drawImage(img, 0, 0, c.width, c.height);
			done(c);
		};
		img.src = photo ? text : 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(text);
	}
	/**
	 * RGBA bytes of a canvas with the colour of opaque texels spread into the transparent ones. A canvas keeps
	 * no colour under alpha 0, so filtered and mipmapped cut-outs would otherwise grow dark fringes.
	 */
	function bled(c, flip) {
		const w = c.width;
		const h = c.height;
		const src = c.getContext('2d').getImageData(0, 0, w, h).data;
		const d = new Uint8Array(src.length);
		d.set(src);
		let known = new Uint8Array(w * h);
		let sum = [0, 0, 0, 0];
		for (let i = 0; i < w * h; i++) {
			if (d[i * 4 + 3] > 8) {
				known[i] = 1;
				sum[0] += d[i * 4]; sum[1] += d[i * 4 + 1]; sum[2] += d[i * 4 + 2]; sum[3]++;
			}
		}
		for (let pass = 0; pass < 8; pass++) {
			const next = known.slice();
			for (let y = 0; y < h; y++) {
				for (let x = 0; x < w; x++) {
					const i = y * w + x;
					if (known[i]) continue;
					let r = 0, g = 0, b = 0, n = 0;
					for (let dy = -1; dy <= 1; dy++) {
						const yy = y + dy;
						if (yy < 0 || yy >= h) continue;
						for (let dx = -1; dx <= 1; dx++) {
							const xx = x + dx;
							if (xx < 0 || xx >= w || !known[yy * w + xx]) continue;
							const j = (yy * w + xx) * 4;
							r += d[j]; g += d[j + 1]; b += d[j + 2]; n++;
						}
					}
					if (!n) continue;
					d[i * 4] = r / n; d[i * 4 + 1] = g / n; d[i * 4 + 2] = b / n;
					next[i] = 1;
				}
			}
			known = next;
		}
		const n = Math.max(1, sum[3]);
		for (let i = 0; i < w * h; i++) {
			if (!known[i]) { d[i * 4] = sum[0] / n; d[i * 4 + 1] = sum[1] / n; d[i * 4 + 2] = sum[2] / n; }
		}
		if (!flip) return d;
		// Canvas rows run top-down; a data texture's first row is the bottom one.
		const out = new Uint8Array(d.length);
		const row = w * 4;
		for (let y = 0; y < h; y++) out.set(d.subarray(y * row, (y + 1) * row), (h - 1 - y) * row);
		return out;
	}
	function dataTexture(w, h, data) {
		const t = new T.DataTexture(data, w, h, T.RGBAFormat);
		t.magFilter = T.LinearFilter;
		t.minFilter = T.LinearMipmapLinearFilter;
		t.generateMipmaps = true;
		t.encoding = T.sRGBEncoding;
		t.unpackAlignment = 4;
		return t;
	}
	/** A texture for art[name]; repeat for tiling textures, otherwise an alpha cut-out. */
	function tex(name, opts) {
		opts = opts || {};
		const key = name + (opts.scale || 1);
		if (textures[key]) return textures[key];
		let t;
		if (opts.repeat) {
			const ph = document.createElement('canvas');
			ph.width = ph.height = 4;
			const g = ph.getContext('2d');
			g.fillStyle = opts.fill || '#808080';
			g.fillRect(0, 0, 4, 4);
			t = new T.CanvasTexture(ph);
			t.encoding = opts.linear ? T.LinearEncoding : T.sRGBEncoding;
			t.wrapS = t.wrapT = T.RepeatWrapping;
			t.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
		} else {
			t = dataTexture(4, 4, new Uint8Array(64));
		}
		raster(name, opts.scale || 1, (c) => {
			canvases[key] = c;
			// The GPU copy was made for the placeholder's size: drop it so the next upload allocates the real size.
			t.dispose();
			if (opts.repeat) t.image = opts.post ? opts.post(c) : c;
			else t.image = { data: bled(c, true), width: c.width, height: c.height };
			t.needsUpdate = true;
		});
		textures[key] = t;
		return t;
	}
	function normalsFrom(c) {
		const w = c.width;
		const h = c.height;
		const src = c.getContext('2d').getImageData(0, 0, w, h).data;
		const out = document.createElement('canvas');
		out.width = w;
		out.height = h;
		const og = out.getContext('2d');
		const img = og.createImageData(w, h);
		const H = (x, y) => src[(((y + h) % h) * w + ((x + w) % w)) * 4] / 255;
		for (let y = 0; y < h; y++) {
			for (let x = 0; x < w; x++) {
				const dx = (H(x + 1, y) - H(x - 1, y)) * 2.2;
				const dy = (H(x, y + 1) - H(x, y - 1)) * 2.2;
				const l = Math.hypot(dx, dy, 1);
				const k = (y * w + x) * 4;
				img.data[k] = ((-dx / l) * 0.5 + 0.5) * 255;
				img.data[k + 1] = ((dy / l) * 0.5 + 0.5) * 255;
				img.data[k + 2] = ((1 / l) * 0.5 + 0.5) * 255;
				img.data[k + 3] = 255;
			}
		}
		og.putImageData(img, 0, 0);
		return out;
	}

	const glowCache = {};
	function glow() {
		// One white glow texture; sprites tint it with their colour.
		if (!glowCache.t) glowCache.t = tex('fx_glow');
		return glowCache.t;
	}

	let env = null;
	/**
	 * Sky-and-ground environment for metal and water reflections: a painted gradient at once, replaced in the same render
	 * target by the photographed sky (art sky_env) when that has loaded. Both are 512 px wide so the target sizes match.
	 */
	function envMap() {
		if (env) return env;
		const c = document.createElement('canvas');
		c.width = 512;
		c.height = 256;
		const g = c.getContext('2d');
		const grd = g.createLinearGradient(0, 0, 0, 256);
		grd.addColorStop(0, '#5f8fd0');
		grd.addColorStop(0.42, '#cfe1ee');
		grd.addColorStop(0.5, '#b9b49a');
		grd.addColorStop(0.62, '#6d7a44');
		grd.addColorStop(1, '#3c4a24');
		g.fillStyle = grd;
		g.fillRect(0, 0, 512, 256);
		g.fillStyle = 'rgba(255,248,225,0.95)';
		g.beginPath();
		g.arc(128, 52, 18, 0, Math.PI * 2);
		g.fill();
		const fromImage = (img, target) => {
			const t = new T.CanvasTexture(img);
			t.mapping = T.EquirectangularReflectionMapping;
			t.encoding = T.sRGBEncoding;
			const pm = new T.PMREMGenerator(renderer);
			const rt = pm.fromEquirectangular(t, target);
			pm.dispose();
			t.dispose();
			return rt;
		};
		const rt = fromImage(c, null);
		env = rt.texture;
		raster('sky_env', 1, (img) => { if (img.width === 512) fromImage(img, rt); });
		return env;
	}

	// --- Ground -----------------------------------------------------------------------------------------------
	const groundTex = () => ({ g: tex('tex_grass', { repeat: true, scale: 2 }), d: tex('tex_dirt', { repeat: true, scale: 2 }),
		r: tex('tex_rock', { repeat: true, scale: 2 }), m: tex('tex_mud', { repeat: true, scale: 2 }), s: tex('tex_snow', { repeat: true, scale: 2 }),
		c: tex('tex_cobble', { repeat: true, scale: 2 }) });
	/** Lambert material that blends grass, dirt, rock, mud and road textures by a per-vertex `splat` (dirt, rock, mud, road),
	 *  graded to the map's look (grass tint, snow, cobbled roads). haze (0..1, optional) caps how much of the fog colour it takes. */
	function splatMaterial(tile, haze) {
		const t = groundTex();
		const L = look;
		const v3 = (a) => new T.Vector3(a[0], a[1], a[2]);
		const mat = new T.MeshLambertMaterial({ color: 0xffffff, vertexColors: true });
		mat.onBeforeCompile = (sh) => {
			Object.assign(sh.uniforms, { tG: { value: t.g }, tD: { value: t.d }, tR: { value: t.r }, tM: { value: t.m }, tS: { value: t.s }, tC: { value: t.c },
				tTile: { value: 1 / tile }, uGrass: { value: v3(L.grass) }, uSat: { value: L.sat }, uDirt: { value: v3(L.dirt) }, uSnow: { value: L.snow }, uCobble: { value: L.cobble } });
			sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nattribute vec4 splat;\nvarying vec4 vSplat;\nvarying vec2 vGuv;\nuniform float tTile;')
				.replace('#include <begin_vertex>', '#include <begin_vertex>\nvSplat = splat;\nvGuv = (modelMatrix * vec4(position, 1.0)).xz * tTile;');
			sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nuniform sampler2D tG, tD, tR, tM, tS, tC;\nuniform vec3 uGrass, uDirt;\nuniform float uSat, uSnow, uCobble;\nvarying vec4 vSplat;\nvarying vec2 vGuv;')
				.replace('#include <map_fragment>', [
					'vec3 gA = texture2D(tG, vGuv).rgb;',
					'vec3 gB = texture2D(tG, vGuv * 0.29 + vec2(0.37, 0.11)).rgb;',
					'vec3 grassC = mix(gA, gB, 0.45);',
					'grassC = mix(vec3(dot(grassC, vec3(0.2126, 0.7152, 0.0722))), grassC, uSat) * uGrass;',
					'vec3 dirtC = mix(texture2D(tD, vGuv).rgb, texture2D(tD, vGuv * 0.41 + vec2(0.2, 0.6)).rgb, 0.35);',
					'dirtC = mix(vec3(dot(dirtC, vec3(0.2126, 0.7152, 0.0722))), dirtC, 0.45 + 0.55 * uSat) * uDirt;',
					'vec3 rockC = texture2D(tR, vGuv * 0.8).rgb;',
					'vec3 mudC = texture2D(tM, vGuv).rgb;',
					'vec3 roadC = dirtC;',
					'if (uCobble > 0.0 && vSplat.w > 0.01) roadC = mix(dirtC, texture2D(tC, vGuv * 1.7).rgb * 0.62, uCobble);',
					'if (uSnow > 0.0) {',
					'	vec3 sn = texture2D(tS, vGuv * 0.6).rgb;',
					'	grassC = mix(grassC, sn, uSnow);',
					'	dirtC = mix(dirtC, sn * 0.92, uSnow * 0.55);',
					'	roadC = mix(roadC, sn * 0.85, uSnow * 0.7);',
					'	rockC = mix(rockC, sn, uSnow * 0.35);',
					'}',
					'float wG = max(0.0, 1.0 - vSplat.x - vSplat.y - vSplat.z - vSplat.w);',
					'diffuseColor.rgb *= (grassC * wG + dirtC * vSplat.x + rockC * vSplat.y + mudC * vSplat.z + roadC * vSplat.w) * 1.2;',
				].join('\n'));
			if (haze !== undefined) sh.fragmentShader = sh.fragmentShader.replace('#include <fog_fragment>', partialFog(haze));
		};
		// Same onBeforeCompile text for every haze and look: tell three's program cache they differ.
		mat.customProgramCacheKey = () => 'splat' + haze + lookName;
		return cloudShadows(mat, 'battle');
	}
	/** Soft blotches (0..1) where the ground is worn to bare earth, from world x, z. */
	const wornAt = (x, z) => Math.max(0, Math.min(1, (0.5 + 0.28 * Math.sin(x * 0.043 + 1.7) * Math.cos(z * 0.051 + 0.4) + 0.22 * Math.sin(x * 0.11 + z * 0.083 + 2.1) * Math.cos(z * 0.13 - x * 0.02) - 0.56) / 0.26));
	/** Fog that takes at most `k` of the fog colour, for far scenery that should stay readable against the sky. */
	const partialFog = (k) => '#ifdef USE_FOG\n\tgl_FragColor.rgb = mix(gl_FragColor.rgb, fogColor, smoothstep(fogNear, fogFar, vFogDepth) * ' + k.toFixed(2) + ');\n#endif';
	/** Replaces the battle heightfield's flat vertex colours with textures: splat weights and tints from the sim's flags. */
	function dressTerrain(mesh, st, CF) {
		const g = st.grid;
		const vx = g.nx + 1;
		const vz = g.nz + 1;
		const flagAt = (cx, cz) => g.f[Math.min(g.nz - 1, Math.max(0, cz)) * g.nx + Math.min(g.nx - 1, Math.max(0, cx))];
		const hAt = (cx, cz) => g.h[Math.min(g.nz - 1, Math.max(0, cz)) * g.nx + Math.min(g.nx - 1, Math.max(0, cx))];
		setLook(st.map.name);
		const splat = new Float32Array(vx * vz * 4);
		const col = mesh.geometry.attributes.color.array;
		const rnd = mulberry(23);
		for (let j = 0; j < vz; j++) {
			for (let i = 0; i < vx; i++) {
				let road = 0;
				let rock = 0;
				let ruin = 0;
				let ford = 0;
				let deep = 0;
				let forest = 0;
				let h = 0;
				for (const [di, dj] of [[-1, -1], [0, -1], [-1, 0], [0, 0]]) {
					const f = flagAt(i + di, j + dj);
					h += hAt(i + di, j + dj) / 4;
					if (f & CF.Road) road++;
					if (f & CF.Rock) rock++;
					if (f & CF.Ruins) ruin++;
					if (f & CF.Ford) ford++;
					if (f & CF.Deep) deep++;
					if (f & CF.Forest) forest++;
				}
				const k = (j * vx + i) * 3;
				let sw = road / 4;
				let sr = Math.min(1, rock / 4 + ruin / 8);
				let sm = deep ? 1 : Math.min(1, ford / 3);
				// Bare earth: ruins, and worn patches over open ground (and less of it under the trees).
				let sd = Math.max(ruin / 8, wornAt(i * g.cell, j * g.cell) * look.worn * (forest ? 0.5 : 1) * (1 - sw));
				const sum = sd + sr + sm + sw;
				if (sum > 1) { sd /= sum; sr /= sum; sm /= sum; sw /= sum; }
				const q = (j * vx + i) * 4;
				splat[q] = sd;
				splat[q + 1] = sr;
				splat[q + 2] = sm;
				splat[q + 3] = sw;
				// Tint: high ground a little sun-dried, forest floor dark, a little noise everywhere.
				const dry = Math.min(1, Math.max(0, h / 7)) * 0.55;
				const n = 0.94 + rnd() * 0.12;
				let r = (1 + dry * 0.32) * n;
				let gg = (1 + dry * 0.12) * n;
				let b = (1 - dry * 0.3) * n;
				if (forest) { const f = 0.35 + 0.1 * (4 - forest); r *= 0.62 + f * 0.2; gg *= 0.72 + f * 0.15; b *= 0.6 + f * 0.2; }
				if (deep) { r *= 0.55; gg *= 0.62; b *= 0.7; }
				else if (ford) { const w = 1 - 0.07 * ford; r *= w - 0.08; gg *= w; b *= w - 0.04; } // wet, darker ford beds
				col[k] = r * 0.82;
				col[k + 1] = gg * 0.82;
				col[k + 2] = b * 0.82;
			}
		}
		mesh.geometry.setAttribute('splat', new T.BufferAttribute(splat, 4));
		mesh.geometry.attributes.color.needsUpdate = true;
		mesh.material = splatMaterial(9);
	}

	function mulberry(seed) {
		let a = seed >>> 0;
		return () => {
			a = (a + 0x6d2b79f5) >>> 0;
			let t = Math.imul(a ^ (a >>> 15), 1 | a);
			t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
			return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
		};
	}

	/** Sets uv = world xz / size on a geometry already lying in the xz plane at (x0, z0). */
	function worldUv(geo, x0, z0, size) {
		const p = geo.attributes.position;
		const uv = new Float32Array(p.count * 2);
		for (let i = 0; i < p.count; i++) { uv[i * 2] = (p.getX(i) + x0) / size; uv[i * 2 + 1] = (p.getZ(i) + z0) / size; }
		geo.setAttribute('uv', new T.BufferAttribute(uv, 2));
	}
	/** Vertex alpha for a flat water geometry: 1 inside, 0 at its rectangle (or circle, when round) edge `fade` metres out. */
	function shore(geo, round, fade) {
		geo.computeBoundingBox();
		const b = geo.boundingBox;
		const mx = (b.min.x + b.max.x) / 2;
		const mz = (b.min.z + b.max.z) / 2;
		const p = geo.attributes.position;
		const c = new Float32Array(p.count * 4).fill(1);
		for (let i = 0; i < p.count; i++) {
			const x = p.getX(i);
			const z = p.getZ(i);
			const d = round ? (b.max.x - mx) - Math.hypot(x - mx, z - mz) : Math.min(x - b.min.x, b.max.x - x, z - b.min.z, b.max.z - z);
			const k = Math.min(1, Math.max(0, d / fade));
			c[i * 4 + 3] = k * k * (3 - 2 * k);
		}
		geo.setAttribute('color', new T.BufferAttribute(c, 4));
	}

	// --- Water ------------------------------------------------------------------------------------------------
	const waters = [];
	function water(deep) {
		const n = tex('tex_water', { repeat: true, linear: true, post: normalsFrom });
		// Dark grey-blue water that takes the map's look; vertex alpha from shore() softens the banks.
		const m = new T.MeshStandardMaterial({ color: look.water[deep ? 0 : 1], roughness: deep ? 0.1 : 0.2, metalness: 0.0, transparent: true, opacity: deep ? 0.9 : 0.5,
			vertexColors: true, normalMap: n, normalScale: new T.Vector2(0.55, 0.55), envMap: envMap(), envMapIntensity: deep ? 0.9 : 0.35 });
		// A second ripple layer drifting the other way, mixed into the normal in the shader.
		m.onBeforeCompile = (sh) => {
			sh.uniforms.uTime = time.u;
			sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nuniform float uTime;')
				.replace('#include <normal_fragment_maps>', T.ShaderChunk.normal_fragment_maps.replace('vec3 mapN = texture2D( normalMap, vUv ).xyz * 2.0 - 1.0;', [
					'vec3 n1 = texture2D(normalMap, vUv + vec2(uTime * 0.021, uTime * 0.013)).xyz * 2.0 - 1.0;',
					'vec3 n2 = texture2D(normalMap, vUv * 1.7 + vec2(-uTime * 0.017, uTime * 0.026)).xyz * 2.0 - 1.0;',
					'vec3 mapN = normalize(vec3(n1.xy + n2.xy, n1.z * n2.z));',
				].join('\n')));
		};
		waters.push(m);
		return m;
	}

	// --- Trees and rocks --------------------------------------------------------------------------------------
	function colored(parts, flat) {
		const pos = [];
		const col = [];
		const c = new T.Color();
		const m4 = new T.Matrix4();
		for (const [geo, hex, x, y, z, sx, sy, sz, ry, rx, rz] of parts) {
			m4.compose(new T.Vector3(x, y, z), new T.Quaternion().setFromEuler(new T.Euler(rx || 0, ry || 0, rz || 0)), new T.Vector3(sx, sy, sz));
			const g = (geo.index ? geo.toNonIndexed() : geo.clone()).applyMatrix4(m4);
			c.set(hex);
			const a = g.attributes.position.array;
			for (let i = 0; i < a.length; i++) pos.push(a[i]);
			for (let i = 0; i < a.length / 3; i++) {
				// Darker towards the bottom of each part: cheap ambient occlusion.
				const yy = a[i * 3 + 1];
				const k = 0.78 + 0.22 * Math.min(1, Math.max(0, (yy - y + sy * 0.5) / (sy || 1)));
				col.push(c.r * k, c.g * k, c.b * k);
			}
			g.dispose();
		}
		const geo = new T.BufferGeometry();
		geo.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
		geo.setAttribute('color', new T.Float32BufferAttribute(col, 3));
		if (!flat) geo.computeVertexNormals();
		return geo;
	}
	let treeGeo = null;
	function trees() {
		if (treeGeo) return treeGeo;
		const trunk = new T.CylinderGeometry(0.16, 0.3, 1, 6);
		const cone = new T.ConeGeometry(1, 1, 8);
		const blob = new T.IcosahedronGeometry(1, 1);
		// Pine: four tiers; fir: tall and narrow; oak: round clumps on a thicker trunk; birch: light trunk, small crown;
		// snowfir: a fir with snow on every tier; dead: bare grey trunk and branches. Dark, muted greens as in the original.
		const bark = 0x4a3c30;
		treeGeo = {
			pine: colored([[trunk, 0x4e3620, 0, 1.1, 0, 1, 2.2, 1], [cone, 0x223d1d, 0, 2.6, 0, 2.0, 2.4, 2.0], [cone, 0x284822, 0, 3.8, 0, 1.65, 2.2, 1.65, 0.4],
				[cone, 0x2f5227, 0, 4.9, 0, 1.25, 2.0, 1.25, 0.8], [cone, 0x375c2c, 0, 5.9, 0, 0.8, 1.6, 0.8, 1.2]], true),
			fir: colored([[trunk, 0x443020, 0, 1.0, 0, 0.9, 2.0, 0.9], [cone, 0x1c3620, 0, 3.0, 0, 1.4, 3.4, 1.4], [cone, 0x223f25, 0, 4.8, 0, 1.05, 3.0, 1.05, 0.5],
				[cone, 0x29492a, 0, 6.4, 0, 0.7, 2.4, 0.7, 1.0]], true),
			oak: colored([[trunk, 0x523a24, 0, 1.3, 0, 1.4, 2.6, 1.4], [blob, 0x34552a, 0, 3.6, 0, 2.2, 1.8, 2.2], [blob, 0x3d602e, 1.1, 4.2, 0.5, 1.5, 1.3, 1.5],
				[blob, 0x2e4c26, -1.0, 4.0, -0.6, 1.6, 1.3, 1.6], [blob, 0x466a33, 0.1, 4.9, -0.2, 1.3, 1.1, 1.3]], true),
			birch: colored([[trunk, 0xcfc9ba, 0, 1.6, 0, 0.7, 3.2, 0.7], [blob, 0x587a36, 0, 4.0, 0, 1.3, 1.7, 1.3], [blob, 0x648640, 0.5, 4.7, 0.2, 0.9, 1.1, 0.9]], true),
			snowfir: colored([[trunk, 0x3c2c20, 0, 1.0, 0, 0.9, 2.0, 0.9], [cone, 0x1a2e22, 0, 3.0, 0, 1.4, 3.4, 1.4], [cone, 0xdfe6ee, 0, 3.75, 0, 1.0, 1.9, 1.0, 0.3],
				[cone, 0x1f3626, 0, 4.8, 0, 1.05, 3.0, 1.05, 0.5], [cone, 0xe6ecf2, 0, 5.45, 0, 0.75, 1.7, 0.75, 0.8], [cone, 0x24402a, 0, 6.4, 0, 0.7, 2.4, 0.7, 1.0],
				[cone, 0xeef2f6, 0, 7.0, 0, 0.45, 1.3, 0.45, 1.3]], true),
			dead: colored([[trunk, bark, 0, 1.7, 0, 1.0, 3.4, 1.0], [trunk, bark, 0.55, 3.5, 0, 0.42, 1.9, 0.42, 0, 0, -0.75], [trunk, bark, -0.5, 3.9, 0.15, 0.38, 1.7, 0.38, 0.5, 0, 0.8],
				[trunk, bark, 0.1, 4.1, -0.5, 0.34, 1.5, 0.34, 0, -0.75, 0], [trunk, bark, 0.05, 4.6, 0.1, 0.3, 1.4, 0.3], [trunk, bark, 1.05, 4.3, 0.1, 0.22, 1.0, 0.22, 0.3, 0, -0.35]], true),
		};
		return treeGeo;
	}
	/** Instanced forest from [x, y, z(sim), scale, r] items: the map look's mix of trees (r picks the kind), varied tints. */
	function forest(group, items, castShadow) {
		const g = trees();
		const kinds = Object.keys(g);
		const lists = {};
		for (const k of kinds) lists[k] = [];
		for (const it of items) {
			const pick = look.trees.find(([, upTo]) => it[4] < upTo) || look.trees[look.trees.length - 1];
			lists[pick[0]].push(it);
		}
		const tt = look.treeTint;
		const mat = cloudShadows(new T.MeshLambertMaterial({ vertexColors: true, flatShading: true }), 'battle');
		const o = new T.Object3D();
		const c = new T.Color();
		for (const k of kinds) {
			const list = lists[k];
			if (!list.length) continue;
			const m = new T.InstancedMesh(g[k], mat, list.length);
			list.forEach((it, i) => {
				o.position.set(it[0], it[1] - 0.1, -it[2]);
				o.rotation.set(0, it[4] * 40, 0);
				const s = it[3] * (k === 'oak' ? 1.05 : 0.95);
				o.scale.set(s, s * (0.9 + ((it[4] * 7.3) % 0.25)), s);
				o.updateMatrix();
				m.setMatrixAt(i, o.matrix);
				const v = 0.88 + ((it[4] * 13.7) % 0.24);
				c.setRGB(v * (0.96 + ((it[4] * 3.1) % 0.08)) * tt[0], v * tt[1], v * 0.96 * tt[2]);
				m.setColorAt(i, c);
			});
			m.castShadow = castShadow !== false;
			m.receiveShadow = true;
			group.add(m);
		}
	}
	/** Lumpy boulder: a subdivided icosahedron pushed in and out, faceted. */
	function rockGeometry() {
		const g = new T.IcosahedronGeometry(1, 1);
		const p = g.attributes.position;
		const rnd = mulberry(77);
		const seen = new Map();
		for (let i = 0; i < p.count; i++) {
			const key = p.getX(i).toFixed(3) + ',' + p.getY(i).toFixed(3) + ',' + p.getZ(i).toFixed(3);
			if (!seen.has(key)) seen.set(key, 0.78 + rnd() * 0.36);
			const k = seen.get(key);
			p.setXYZ(i, p.getX(i) * k, p.getY(i) * k * 0.85, p.getZ(i) * k);
		}
		g.computeVertexNormals();
		return g;
	}

	// --- Grass and flowers ------------------------------------------------------------------------------------
	const swaying = [];
	function swayMaterial(map) {
		const m = new T.MeshLambertMaterial({ map, alphaTest: 0.45 });
		m.onBeforeCompile = (sh) => {
			sh.uniforms.uTime = time.u;
			sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nuniform float uTime;')
				.replace('#include <begin_vertex>', [
					'#include <begin_vertex>',
					'#ifdef USE_INSTANCING',
					'vec2 wp = vec2(instanceMatrix[3].x, instanceMatrix[3].z);',
					'#else',
					'vec2 wp = vec2(0.0);',
					'#endif',
					'float sway = sin(uTime * 1.6 + wp.x * 0.21 + wp.y * 0.17) + 0.5 * sin(uTime * 2.7 + wp.x * 0.5);',
					'transformed.x += sway * 0.09 * uv.y;',
					'transformed.z += sway * 0.05 * uv.y;',
				].join('\n'));
		};
		swaying.push(m);
		return cloudShadows(m, 'battle');
	}
	/**
	 * Two crossed upright quads, base at y = 0, lit as if facing up (so they shade like the ground). Each quad has
	 * both windings rather than a double-sided material, which would flip the normal to face down from behind.
	 */
	function tuftGeometry(w, h) {
		const g = new T.BufferGeometry();
		const pos = [];
		const uv = [];
		const idx = [];
		for (let q = 0; q < 2; q++) {
			const a = q * (Math.PI / 2) + 0.4;
			const cx = Math.cos(a) * w * 0.5;
			const cz = Math.sin(a) * w * 0.5;
			const b = pos.length / 3;
			pos.push(-cx, 0, -cz, cx, 0, cz, cx, h, cz, -cx, h, -cz);
			uv.push(0, 0, 1, 0, 1, 1, 0, 1);
			idx.push(b, b + 1, b + 2, b, b + 2, b + 3, b, b + 2, b + 1, b, b + 3, b + 2);
		}
		g.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
		g.setAttribute('uv', new T.Float32BufferAttribute(uv, 2));
		g.setAttribute('normal', new T.Float32BufferAttribute(new Array(pos.length / 3).fill(0).flatMap(() => [0, 1, 0]), 3));
		g.setIndex(idx);
		return g;
	}
	/** Grass tufts and flowers scattered over open ground (not on roads, water, rocks or ruins). */
	function foliage(group, st, CF) {
		const g = st.grid;
		const W = st.map.w;
		const D = st.map.d;
		const n = Math.round((quality === 'high' ? 5200 : 1600) * look.tufts);
		const nf = Math.round(n * look.flowers);
		const rnd = mulberry(91);
		const bad = CF.Road | CF.Deep | CF.Ford | CF.Rock | CF.Ruins;
		const pick = (count, forestOk) => {
			const out = [];
			for (let i = 0; i < count * 3 && out.length < count; i++) {
				const x = rnd() * W;
				const z = rnd() * D;
				const f = g.f[Math.min(g.nz - 1, Math.floor(z / g.cell)) * g.nx + Math.min(g.nx - 1, Math.floor(x / g.cell))];
				if (f & bad) continue;
				if (!forestOk && f & CF.Forest) continue;
				out.push([x, heightAt(x, z), z, 0.7 + rnd() * 0.7, rnd()]);
			}
			return out;
		};
		const place = (geo, mat, list, tint) => {
			const m = new T.InstancedMesh(geo, mat, Math.max(1, list.length));
			const o = new T.Object3D();
			const c = new T.Color();
			list.forEach((it, i) => {
				o.position.set(it[0], it[1] - 0.05, -it[2]);
				o.rotation.set(0, it[4] * 6.3, 0);
				o.scale.setScalar(it[3]);
				o.updateMatrix();
				m.setMatrixAt(i, o.matrix);
				tint(c, it[4]);
				m.setColorAt(i, c);
			});
			m.count = list.length;
			m.receiveShadow = true;
			group.add(m);
			return m;
		};
		const ft = look.tuft;
		place(tuftGeometry(1.5, 1.15), swayMaterial(tex('foliage_grass')), pick(n, true), (c, r) => c.setRGB((0.85 + r * 0.25) * ft[0], (0.9 + r * 0.15) * ft[1], (0.8 + r * 0.15) * ft[2]));
		if (nf) place(tuftGeometry(1.4, 1.2), swayMaterial(tex('foliage_flowers')), pick(nf, false), (c) => c.setRGB(0.9, 0.88, 0.82));
	}

	// --- Buildings around the field ---------------------------------------------------------------------------
	let propGeo = null;
	/** Vertex-coloured buildings: timber-framed houses, stone towers (whole and broken), walls, a palisade, a haystack. */
	function propGeometries() {
		if (propGeo) return propGeo;
		const box = new T.BoxGeometry(1, 1, 1);
		// Roof prism: base 1 wide at y = 0, ridge at y = 1, running along z.
		const roof = new T.ExtrudeGeometry(new T.Shape([new T.Vector2(-0.5, 0), new T.Vector2(0.5, 0), new T.Vector2(0, 1)]), { depth: 1, bevelEnabled: false }).translate(0, 0, -0.5);
		const log = new T.CylinderGeometry(0.5, 0.5, 1, 6);
		const tip = new T.ConeGeometry(0.5, 1, 6);
		const plaster = 0xb3a589;
		const beam = 0x3e2e22;
		const stone = 0x7c766b;
		const stoneD = 0x645f57;
		const hole = 0x26221e;
		const house = (roofHex) => colored([
			[box, plaster, 0, 1.6, 0, 6, 3.2, 4.4],
			// Dark timber frame: corner posts, sill and eaves beams, a middle post, a window and a door.
			[box, beam, 2.95, 1.6, 2.15, 0.3, 3.2, 0.3], [box, beam, -2.95, 1.6, 2.15, 0.3, 3.2, 0.3], [box, beam, 2.95, 1.6, -2.15, 0.3, 3.2, 0.3], [box, beam, -2.95, 1.6, -2.15, 0.3, 3.2, 0.3],
			[box, beam, 0, 3.1, 0, 6.1, 0.3, 4.5], [box, beam, 0, 0.25, 0, 6.1, 0.5, 4.5], [box, beam, 0.2, 1.6, 2.23, 0.25, 3.0, 0.08], [box, beam, 0.2, 1.6, -2.23, 0.25, 3.0, 0.08],
			[box, hole, 1.6, 1.8, 2.24, 0.9, 0.9, 0.06], [box, hole, -1.5, 1.05, 2.24, 0.95, 1.9, 0.06], [box, hole, -1.6, 1.8, -2.24, 0.9, 0.9, 0.06],
			[roof, roofHex, 0, 3.2, 0, 5.4, 2.4, 6.9, Math.PI / 2], [box, stoneD, 1.9, 4.7, 0.9, 0.6, 1.8, 0.6],
		], true);
		const merlons = (y, r) => [[r, r], [-r, r], [r, -r], [-r, -r], [0, r], [0, -r], [r, 0], [-r, 0]].map(([x, z]) => [box, stone, x, y, z, 0.9, 1.0, 0.9]);
		propGeo = {
			house: house(0x4f3d2a),
			snowhouse: house(0xdfe6ee),
			tower: colored([[box, stone, 0, 5.5, 0, 4, 11, 4], [box, stoneD, 0, 6, 0, 4.15, 0.4, 4.15], [box, stoneD, 0, 11.3, 0, 4.8, 0.8, 4.8],
				[box, hole, 0, 8.4, 2.02, 0.4, 1.3, 0.05], [box, hole, 0, 3.6, -2.02, 0.4, 1.3, 0.05], [box, hole, 0, 1.2, 2.02, 1.3, 2.4, 0.05]].concat(merlons(12.2, 2.0)), true),
			broken: colored([[box, stone, 0, 3.5, 0, 4, 7, 4], [box, stone, 1.5, 7.6, 1.5, 1, 1.2, 1], [box, stone, -1.5, 8.0, 1.5, 1, 2.0, 1], [box, stone, 1.5, 7.3, -1.5, 1, 0.6, 1],
				[box, hole, 0, 4.4, 2.02, 0.4, 1.3, 0.05], [box, stoneD, 2.9, 0.4, 1.3, 1.5, 0.8, 1.1, 0.5], [box, stoneD, 3.4, 0.3, -0.8, 1.1, 0.6, 0.9, 1.1], [box, stoneD, -2.7, 0.35, -1.6, 1.2, 0.7, 1.0, 0.3]], true),
			wall: colored([[box, stone, 0, 0.8, 0, 10, 1.6, 0.9], [box, stoneD, 0, 1.68, 0, 10.1, 0.16, 1.0]], true),
			ruin: colored([[box, stone, -3.5, 0.9, 0, 3, 1.8, 0.9], [box, stone, 0.2, 0.55, 0, 2.6, 1.1, 0.9], [box, stone, 3.6, 1.2, 0, 2.8, 2.4, 0.9],
				[box, stoneD, 1.6, 0.25, 0.9, 1.0, 0.5, 0.8, 0.6], [box, stoneD, -1.4, 0.2, -1.0, 0.9, 0.4, 0.7, 1.2]], true),
			palisade: colored([].concat(...Array.from({ length: 12 }, (_, i) => {
				const x = -5.5 + i;
				const h = 3.2 + ((i * 7) % 3) * 0.25;
				return [[log, 0x5a4630, x, h / 2, 0, 0.55, h, 0.55], [tip, 0x4e3c28, x, h + 0.35, 0, 0.55, 0.7, 0.55]];
			})), true),
			hay: colored([[tip, 0xa48a4c, 0, 1.3, 0, 2.8, 2.6, 2.8], [log, 0x8f7840, 0, 0.3, 0, 2.8, 0.6, 2.8]], true),
		};
		return propGeo;
	}
	// Where each look puts its buildings, in sim coordinates relative to the field (w, d its size; d + n is beyond the far edge).
	const PROPS = {
		village: (w, d) => [['house', -14, d + 12, 0.3], ['house', -23, d + 3, 1.9], ['house', -7, d + 23, 0.1], ['house', w + 12, d + 16, -0.4], ['house', w + 21, d + 5, 1.2],
			['house', -13, d * 0.62, 1.5], ['tower', w * 0.62, d + 15, 0.2], ['wall', w * 0.62 - 8, d + 15, 0], ['wall', w * 0.62 + 8, d + 15, 0],
			['hay', -9, d + 4, 0], ['hay', -3, d + 8, 0], ['hay', w + 8, d + 7, 0]],
		cabins: (w, d) => [['house', -12, d + 10, 0.4], ['house', w + 14, d + 12, -0.5], ['house', -16, d * 0.55, 1.6], ['tower', w * 0.3, d + 18, 0.1]],
		ruins: (w, d) => [['ruin', w * 0.25, d + 10, 0.1], ['ruin', w * 0.25 + 10, d + 12, -0.2], ['wall', w * 0.7, d + 14, 0.05], ['ruin', w + 10, d * 0.7, 1.57],
			['ruin', -10, d * 0.65, 1.5], ['broken', w * 0.7 + 12, d + 15, 0.4], ['broken', -14, d + 9, 1.1]],
		fort: (w, d) => [['tower', w * 0.2, d + 12, 0], ['tower', w * 0.8, d + 12, 0], ['palisade', w * 0.2 + 8.5, d + 12, 0], ['palisade', w * 0.2 + 20.5, d + 12, 0],
			['palisade', w * 0.8 - 8.5, d + 12, 0], ['palisade', w * 0.8 - 20.5, d + 12, 0], ['palisade', w * 0.2 - 8.5, d + 12, 0], ['palisade', w * 0.8 + 8.5, d + 12, 0],
			['snowhouse', -12, d + 8, 0.3], ['snowhouse', w + 12, d + 10, -0.3]],
	};
	const PROP_R = { house: 6, snowhouse: 6, tower: 5, broken: 5, wall: 6, ruin: 5, palisade: 7, hay: 2.5 };
	/** Places the look's buildings; returns their footprints [x, z, r] (sim coordinates) for the forest to keep clear of. */
	function props(group, W, D, hillAt) {
		const list = PROPS[look.props] ? PROPS[look.props](W, D) : [];
		if (!list.length) return [];
		const g = propGeometries();
		const mat = cloudShadows(new T.MeshLambertMaterial({ vertexColors: true, flatShading: true }), 'battle');
		const out = [];
		for (const [kind, x, z, ry] of list) {
			const m = new T.Mesh(g[kind], mat);
			m.position.set(x, hillAt(x, -z) - 0.1, -z);
			m.rotation.y = ry;
			m.castShadow = true;
			m.receiveShadow = true;
			group.add(m);
			out.push([x, z, PROP_R[kind]]);
		}
		return out;
	}

	// --- Sky and the land around the field --------------------------------------------------------------------
	const clouds = [];
	function surroundings(group, W, D) {
		clouds.length = 0;
		const rnd = mulberry(5);
		const cx = W / 2;
		const cz = -D / 2;
		// Sky dome drawn behind everything; its horizon colour is also the fog colour. The look tints both (dark blue at night).
		const skyTint = new T.Color(look.sky);
		const hor = new T.Color(0xc8d9e4).multiply(skyTint);
		// Cloud shadows over the field: faint under the night sky of the pass, strongest on the sunny steppe.
		const cs = cloudShadowSet('battle');
		cs.uCsK.value = lookName === 'winter' ? 0.15 : lookName === 'steppe' ? 0.46 : 0.42;
		cs.uCsSun.value.set(-90, 160, 60).normalize();
		const setHorizon = (col) => { if (scene) { scene.background = col.clone(); if (scene.fog) scene.fog.color.copy(col); } };
		let dome;
		if (art.sky_dome) {
			// Photographed sky (art sky_dome): the upper 0.5625 of an equirectangular panorama, horizon at 8/9 of its height.
			// Turned half a circle so its sun stands where the battle's sun light comes from.
			const mat = new T.MeshBasicMaterial({ color: hor, side: T.BackSide, fog: false, depthWrite: false });
			dome = new T.Mesh(new T.SphereGeometry(1300, 48, 18, 0, Math.PI * 2, 0, Math.PI * 0.5625), mat);
			dome.rotation.y = Math.PI;
			raster('sky_dome', 1, (img) => {
				const t = new T.CanvasTexture(img);
				t.encoding = T.sRGBEncoding;
				mat.map = t;
				mat.color.copy(skyTint);
				mat.needsUpdate = true;
				const row = img.getContext('2d').getImageData(0, Math.round(img.height * (8 / 9)) - 3, img.width, 1).data;
				let r = 0, gg = 0, b = 0;
				for (let i = 0; i < row.length; i += 4) { r += row[i]; gg += row[i + 1]; b += row[i + 2]; }
				const n = (row.length / 4) * 255;
				setHorizon(new T.Color().setRGB(r / n, gg / n, b / n).convertSRGBToLinear().multiply(skyTint));
			});
		} else {
			// Without the photo: deep blue overhead, hazy at the horizon.
			const sky = new T.SphereGeometry(1300, 32, 16);
			const sp = sky.attributes.position;
			const sc = [];
			const top = new T.Color(0x4f86c8);
			const mid = new T.Color(0x8fb6dc);
			const c = new T.Color();
			for (let i = 0; i < sp.count; i++) {
				const y = sp.getY(i) / 1300;
				if (y > 0.25) c.copy(mid).lerp(top, Math.min(1, (y - 0.25) / 0.6));
				else c.copy(hor).lerp(mid, Math.max(0, y / 0.25));
				sc.push(c.r, c.g, c.b);
			}
			sky.setAttribute('color', new T.Float32BufferAttribute(sc, 3));
			dome = new T.Mesh(sky, new T.MeshBasicMaterial({ vertexColors: true, side: T.BackSide, fog: false, depthWrite: false }));
		}
		dome.position.set(cx, -120, cz);
		dome.renderOrder = -10;
		group.add(dome);
		setHorizon(hor);
		if (lookName === 'winter') {
			// A pale moon low over the far side of the pass.
			const moon = new T.Sprite(new T.SpriteMaterial({ map: glow(), color: 0xdfe8ff, transparent: true, depthWrite: false, fog: false, blending: T.AdditiveBlending }));
			moon.scale.set(150, 150, 1);
			moon.position.set(cx - 260, 300, cz - 900);
			moon.renderOrder = -9;
			group.add(moon);
		}
		// Ground height beyond the field (world x, z): flat at the edge, rolling hills further out.
		const hillAt = (x, z) => {
			const d = Math.hypot(Math.max(0, Math.max(-x, x - W)), Math.max(0, Math.max(-(z + D), z)));
			return -0.3 + (d < 4 ? 0 : Math.max(0, Math.min(1, (d - 4) / 60) * (6 * Math.sin(x * 0.018 + 1.3) * Math.cos(z * 0.021) + 4 * Math.sin(x * 0.041 + z * 0.033) + 5)));
		};
		// Land around the field: the field's own ground and worn patches, darkening with distance so the field's edge does not show.
		const land = new T.PlaneGeometry(1600, 1600, 160, 160).rotateX(-Math.PI / 2);
		const lp = land.attributes.position;
		const splat = new Float32Array(lp.count * 4);
		const lc = new Float32Array(lp.count * 3);
		for (let i = 0; i < lp.count; i++) {
			const x = lp.getX(i) + cx;
			const z = lp.getZ(i) + cz;
			const d = Math.hypot(Math.max(0, Math.max(-x, x - W)), Math.max(0, Math.max(-(z + D), z)));
			// Well inside the field the battle's own ground covers it: keep it below the river beds.
			const inside = Math.min(x, W - x, -z, D + z);
			lp.setY(i, inside > 8 ? -4 : hillAt(x, z));
			splat[i * 4] = wornAt(x, -z) * look.worn * 0.8;
			const v = (0.8 - 0.16 * Math.min(1, d / 90)) * (0.95 + rnd() * 0.08);
			lc[i * 3] = v * 0.97;
			lc[i * 3 + 1] = v;
			lc[i * 3 + 2] = v * 0.93;
		}
		land.setAttribute('splat', new T.BufferAttribute(splat, 4));
		land.setAttribute('color', new T.BufferAttribute(lc, 3));
		land.computeVertexNormals();
		const lm = new T.Mesh(land, splatMaterial(9, 0.6));
		lm.position.set(cx, 0, cz);
		lm.receiveShadow = true;
		group.add(lm);
		// Houses, towers and walls of the look just beyond the field; the forest keeps clear of them.
		const clear = props(group, W, D, hillAt);
		// Forest belt just beyond the field's edge, thinning out with distance.
		const belt = [];
		for (let i = 0; i < (quality === 'high' ? 900 : 420); i++) {
			const side = rnd();
			const depth = 6 + Math.pow(rnd(), 1.6) * 90;
			let x;
			let z;
			if (side < 0.3) { x = rnd() * (W + 2 * depth) - depth; z = -depth; } else if (side < 0.6) { x = rnd() * (W + 2 * depth) - depth; z = D + depth; } else if (side < 0.8) { x = -depth; z = rnd() * D; } else { x = W + depth; z = rnd() * D; }
			if (rnd() < 0.35) continue;
			if (clear.some(([px, pz, pr]) => Math.hypot(x - px, z - pz) < pr)) continue;
			belt.push([x, hillAt(x, -z), z, 1.0 + rnd() * 0.9, rnd()]);
		}
		forest(group, belt, false);
		// Mountains on the horizon: forested foot, rock (art tex_rock) and snow caps, only half lost in the haze.
		const mount = new T.ConeGeometry(1, 1, 11, 7);
		const mp = mount.attributes.position;
		const snow = new Float32Array(mp.count);
		const mcol = new Float32Array(mp.count * 3);
		for (let i = 0; i < mp.count; i++) {
			const x = mp.getX(i);
			const y = mp.getY(i);
			const z = mp.getZ(i);
			// Ridges from the vertex position itself, so the seam's twin vertices move together.
			if (y < 0.49) { const k = 0.8 + 0.22 * Math.sin(x * 7.1 + z * 3.7 + y * 5) + 0.12 * Math.sin(z * 13.3 - x * 2.9); mp.setX(i, x * k); mp.setZ(i, z * k); mp.setY(i, y + 0.035 * Math.sin(x * 11 + z * 9)); }
			const t = y + 0.5;
			snow[i] = Math.min(1, Math.max(0, (t - look.snowLine) / 0.16 + 0.35 * Math.sin(x * 9 + z * 5)));
			const f = Math.min(1, Math.max(0, (0.3 - t) / 0.2));
			mcol[i * 3] = 1 - f * 0.55;
			mcol[i * 3 + 1] = 1 - f * 0.25;
			mcol[i * 3 + 2] = 1 - f * 0.6;
		}
		mount.setAttribute('snow', new T.BufferAttribute(snow, 1));
		mount.setAttribute('color', new T.BufferAttribute(mcol, 3));
		mount.computeVertexNormals();
		const rockT = groundTex().r;
		const mmat = new T.MeshLambertMaterial({ color: 0xffffff, vertexColors: true, flatShading: true });
		mmat.onBeforeCompile = (sh) => {
			sh.uniforms.tR = { value: rockT };
			sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nattribute float snow;\nvarying float vSnow;\nvarying vec2 vMuv;')
				.replace('#include <begin_vertex>', '#include <begin_vertex>\nvSnow = snow;\nvMuv = uv * vec2(7.0, 2.5);');
			sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nuniform sampler2D tR;\nvarying float vSnow;\nvarying vec2 vMuv;')
				.replace('#include <map_fragment>', [
					'vec3 rk = texture2D(tR, vMuv).rgb;',
					'diffuseColor.rgb = mix(diffuseColor.rgb * rk * 1.25, vec3(0.86, 0.89, 0.94) * (0.75 + rk.g * 0.8), vSnow);',
				].join('\n'))
				.replace('#include <fog_fragment>', partialFog(0.55));
		};
		const mm = new T.InstancedMesh(mount, mmat, 26);
		const o = new T.Object3D();
		for (let i = 0; i < 26; i++) {
			const a = (i / 26) * Math.PI * 2 + rnd() * 0.2;
			const r = 520 + rnd() * 160;
			const h = 90 + rnd() * 150;
			o.position.set(cx + Math.cos(a) * r, h * 0.42 - 10, cz + Math.sin(a) * r);
			o.scale.set(h * (0.9 + rnd() * 0.6), h, h * (0.9 + rnd() * 0.6));
			o.rotation.y = rnd() * 6;
			o.updateMatrix();
			mm.setMatrixAt(i, o.matrix);
		}
		group.add(mm);
		// Clouds drifting high over the field.
		const ct = tex('sky_cloud', { scale: 2 });
		const cc = new T.Color(0xffffff).lerp(skyTint, 0.8);
		for (let i = 0; i < 16; i++) {
			const s = new T.Sprite(new T.SpriteMaterial({ map: ct, color: cc, transparent: true, depthWrite: false, opacity: 0.75 + rnd() * 0.2, fog: false }));
			const w = 160 + rnd() * 200;
			s.scale.set(w, w * 0.5, 1);
			s.position.set(cx + (rnd() - 0.5) * 1100, 230 + rnd() * 140, cz + (rnd() - 0.5) * 1100);
			s.renderOrder = -5;
			group.add(s);
			clouds.push({ s, v: 2 + rnd() * 3, x0: cx });
		}
	}

	// --- Puff clouds ------------------------------------------------------------------------------------------
	// Cumulus heaps drawn as soft 3D puffs: instanced lumpy spheres lit by the sun (white tops, blue-grey bellies, a
	// silver rim against the light) whose edges fade out, sorted back to front so nearer puffs cover farther ones.
	// Used over the kingdom's island and in the battle sky.
	const PUFF_VS = [
		'#include <common>',
		'#include <fog_pars_vertex>',
		'uniform float uTime;',
		// Per puff: x the heap's base height (local), y how deep in the heap it sits (0 bottom, 1 top), for the shading.
		'attribute vec2 aPuff;',
		'varying vec3 vN;',
		'varying vec3 vW;',
		'varying float vBase, vAo;',
		'void main() {',
		'	vBase = (modelMatrix * vec4(0.0, aPuff.x, 0.0, 1.0)).y;',
		'	vAo = aPuff.y;',
		'	vec3 n0 = normalize(position);',
		// Seed from the puff's own centre (local to the mesh), so the lumps stay put while the mesh drifts.
		'	float sd = fract(sin(dot(instanceMatrix[3].xz, vec2(12.9898, 78.233))) * 43758.5453) * 6.283;',
		// Lumps that slowly roll, so the heaps seem to boil a little.
		'	float d = 0.13 * sin(n0.x * 3.1 + sd + uTime * 0.13) * sin(n0.y * 2.7 + sd * 0.7) * sin(n0.z * 3.3 + uTime * 0.1)',
		'		+ 0.07 * sin(n0.x * 7.0 + n0.z * 5.0 + sd * 1.3 + uTime * 0.07) + 0.05 * sin(n0.y * 9.0 + n0.x * 4.0 + sd);',
		'	vec3 s = vec3(length(instanceMatrix[0].xyz), length(instanceMatrix[1].xyz), length(instanceMatrix[2].xyz));',
		'	vN = normalize(mat3(modelMatrix) * (mat3(instanceMatrix) * (n0 / (s * s))));',
		'	vec4 wp = modelMatrix * instanceMatrix * vec4(position + n0 * d, 1.0);',
		'	vW = wp.xyz;',
		'	vec4 mvPosition = viewMatrix * wp;',
		'	gl_Position = projectionMatrix * mvPosition;',
		'	#include <fog_vertex>',
		'}',
	].join('\n');
	const PUFF_FS = [
		'#include <common>',
		'#include <fog_pars_fragment>',
		'uniform vec3 uSun, uLit, uShade, uRim;',
		'uniform float uOpacity, uNear, uBaseFade, uFogK;',
		'varying vec3 vN;',
		'varying vec3 vW;',
		'varying float vBase, vAo;',
		'void main() {',
		'	vec3 n = normalize(vN);',
		'	vec3 toCam = cameraPosition - vW;',
		'	vec3 v = normalize(toCam);',
		'	float facing = abs(dot(n, v));',
		'	float sun = smoothstep(-0.6, 1.0, dot(n, uSun));',
		'	float sky = 0.5 + 0.5 * n.y;',
		'	vec3 col = mix(uShade, uLit, clamp(0.75 * sun + 0.45 * sky - 0.05, 0.0, 1.0));',
		// Puffs low in a heap sit in its shadow: greyer, so the domes on top stand out.
		'	col = mix(col, uShade * 0.9, (1.0 - vAo) * 0.45);',
		'	col += uRim * pow(1.0 - facing, 2.5) * (0.35 + 0.65 * max(0.0, dot(-v, uSun)));',
		'	float a = uOpacity * smoothstep(0.0, 0.55, facing) * smoothstep(uNear, uNear * 2.5, length(toCam));',
		// The heap's flat bottom: puffs dissolve below its base, so heaps rise out of the haze instead of floating as balls.
		'	a *= smoothstep(vBase - uBaseFade * 0.3, vBase + uBaseFade, vW.y);',
		'	gl_FragColor = vec4(col, a);',
		'	#include <tonemapping_fragment>',
		'	#include <encodings_fragment>',
		// The scene fog, scaled by uFogK: clouds keep more of their white through the haze than the ground does.
		'	#ifdef USE_FOG',
		'		#ifdef FOG_EXP2',
		'			float fogFactor = 1.0 - exp(-fogDensity * fogDensity * vFogDepth * vFogDepth);',
		'		#else',
		'			float fogFactor = smoothstep(fogNear, fogFar, vFogDepth);',
		'		#endif',
		'		gl_FragColor.rgb = mix(gl_FragColor.rgb, fogColor, fogFactor * uFogK);',
		'	#endif',
		'}',
	].join('\n');
	let puffGeo = null;
	/**
	 * One cumulus heap as puffs [x, y, z, sx, sy, sz, base, ao] pushed onto `out`: a flat base row and rounder domes piled
	 * up toward the middle. y is the heap's flat base, w its width, h how tall it towers, n how many puffs.
	 */
	function cloudHeap(out, rnd, x, y, z, w, h, n) {
		const ang = rnd() * Math.PI;
		const ca = Math.cos(ang), sa = Math.sin(ang);
		for (let i = 0; i < n; i++) {
			const u = (rnd() - 0.5) * 2, v = (rnd() - 0.5) * 2;
			const edge = Math.min(1, Math.hypot(u, v * 1.6));
			const lx = u * w * 0.5, lz = v * w * 0.28;
			const base = i < n * 0.4;
			const r = w * (0.15 + 0.12 * rnd()) * (1.15 - 0.55 * edge) * (base ? 1.2 : 1);
			const up = base ? 0 : (1 - edge) * h * (0.3 + 0.7 * rnd());
			const flat = base ? 0.5 : 0.82;
			const ao = base ? 0.15 + 0.2 * rnd() : 0.4 + 0.6 * (h > 0 ? up / h : 1);
			out.push([x + lx * ca - lz * sa, y + up + r * flat * 0.45, z + lx * sa + lz * ca, r * (1 + 0.25 * rnd()), r * flat, r * (0.85 + 0.3 * rnd()), y, ao]);
		}
		return out;
	}
	/**
	 * Instanced puff clouds from puffs [x, y, z, sx, sy, sz, base?, ao?] (see cloudHeap). opts: sun (direction), lit, shade,
	 * rim (colours), opacity, fog (false to ignore the scene fog), near (metres from the camera where puffs fade out),
	 * baseFade (how far above a heap's base its puffs fade in), fogK (0..1, how much of the fog they take). Returns { mesh, update(dt, camera) }; call update every frame.
	 */
	function puffClouds(puffs, opts) {
		opts = opts || {};
		if (!puffGeo) puffGeo = new T.IcosahedronGeometry(1, 2);
		const col = (h) => new T.Color(h);
		const uniforms = T.UniformsUtils.merge([T.UniformsLib.fog, {
			uTime: { value: 0 }, uSun: { value: new T.Vector3(-0.4, 0.85, -0.2) }, uLit: { value: col(0xffffff) }, uShade: { value: col(0xa6b4c8) },
			uRim: { value: col(0x8a8274) }, uOpacity: { value: 1 }, uNear: { value: 12 }, uBaseFade: { value: 6 }, uFogK: { value: 1 },
		}]);
		if (opts.sun) uniforms.uSun.value.copy(opts.sun).normalize();
		if (opts.lit !== undefined) uniforms.uLit.value.set(opts.lit);
		if (opts.shade !== undefined) uniforms.uShade.value.set(opts.shade);
		if (opts.rim !== undefined) uniforms.uRim.value.set(opts.rim);
		if (opts.opacity !== undefined) uniforms.uOpacity.value = opts.opacity;
		if (opts.near !== undefined) uniforms.uNear.value = opts.near;
		if (opts.baseFade !== undefined) uniforms.uBaseFade.value = opts.baseFade;
		if (opts.fogK !== undefined) uniforms.uFogK.value = opts.fogK;
		const mat = new T.ShaderMaterial({ uniforms, vertexShader: PUFF_VS, fragmentShader: PUFF_FS, transparent: true, depthWrite: false, fog: opts.fog !== false });
		// Its own geometry (sharing the sphere's buffers) for the per-puff attribute.
		const geo = new T.BufferGeometry();
		geo.setAttribute('position', puffGeo.attributes.position);
		geo.setAttribute('normal', puffGeo.attributes.normal);
		const extra = new T.InstancedBufferAttribute(new Float32Array(puffs.length * 2), 2);
		geo.setAttribute('aPuff', extra);
		const mesh = new T.InstancedMesh(geo, mat, puffs.length);
		mesh.frustumCulled = false;
		const o = new T.Object3D();
		const order = puffs.map((p, i) => i);
		const dist = new Float32Array(puffs.length);
		const camLocal = new T.Vector3();
		const lastCam = new T.Vector3(1e9, 0, 0);
		let since = 1e9;
		const sort = () => {
			for (let i = 0; i < puffs.length; i++) {
				const p = puffs[i];
				dist[i] = (p[0] - camLocal.x) ** 2 + (p[1] - camLocal.y) ** 2 + (p[2] - camLocal.z) ** 2;
			}
			order.sort((a, b) => dist[b] - dist[a]);
			order.forEach((k, i) => {
				const p = puffs[k];
				o.position.set(p[0], p[1], p[2]);
				o.scale.set(p[3], p[4], p[5]);
				o.updateMatrix();
				mesh.setMatrixAt(i, o.matrix);
				extra.setXY(i, p[6] !== undefined ? p[6] : -1e5, p[7] !== undefined ? p[7] : 1);
			});
			mesh.instanceMatrix.needsUpdate = true;
			extra.needsUpdate = true;
		};
		camLocal.set(0, 1e4, 0);
		sort();
		return {
			mesh,
			update(dt, cam) {
				uniforms.uTime.value += dt;
				since += dt;
				if (!cam) return;
				mesh.updateMatrixWorld();
				camLocal.copy(cam.position);
				mesh.worldToLocal(camLocal);
				// Re-sort when the camera (seen from the drifting clouds) has moved or a while has passed.
				if (camLocal.distanceToSquared(lastCam) > 4 || since > 0.5) { lastCam.copy(camLocal); since = 0; sort(); }
			},
		};
	}

	// --- Cloud shadows ----------------------------------------------------------------------------------------
	// Soft shadows of clouds drifting over the land. A tileable density map, sampled twice at two scales that drift at
	// different speeds, dims the direct sunlight of every material patched by cloudShadows(); the ambient light stays, so
	// shadowed ground keeps its colour. Each scene ('battle', 'kingdom') has its own set of uniforms: strength, scale, sun.
	let csMap = null;
	function cloudShadowMap() {
		if (csMap) return csMap;
		const S = 256;
		const c = document.createElement('canvas');
		c.width = c.height = S;
		const g = c.getContext('2d');
		g.fillStyle = '#000';
		g.fillRect(0, 0, S, S);
		const rnd = mulberry(41);
		for (let i = 0; i < 34; i++) {
			const x = rnd() * S, y = rnd() * S, r = 16 + rnd() * 44;
			// Drawn nine times, shifted by the map size, so it tiles without seams.
			for (const dx of [-S, 0, S]) for (const dy of [-S, 0, S]) {
				const grd = g.createRadialGradient(x + dx, y + dy, 0, x + dx, y + dy, r);
				grd.addColorStop(0, 'rgba(255,255,255,0.5)');
				grd.addColorStop(1, 'rgba(255,255,255,0)');
				g.fillStyle = grd;
				g.fillRect(x + dx - r, y + dy - r, r * 2, r * 2);
			}
		}
		csMap = new T.CanvasTexture(c);
		csMap.wrapS = csMap.wrapT = T.RepeatWrapping;
		return csMap;
	}
	const csSets = {};
	/** The shadow uniforms of one scene: uCsK strength (0 none), uCsScale (1 / metres the map spans), uCsSun (towards the sun), wind (m/s). */
	function cloudShadowSet(name) {
		return csSets[name] || (csSets[name] = { uCsMap: { value: cloudShadowMap() }, uCsOff: { value: new T.Vector4() }, uCsK: { value: 0.36 },
			uCsScale: { value: 1 / 230 }, uCsSun: { value: new T.Vector3(0, 1, 0) }, wind: new T.Vector2(2.4, 0.9) });
	}
	/** Moves a scene's cloud shadows on with the wind. */
	function stepCloudShadows(name, dt) {
		const s = cloudShadowSet(name);
		const o = s.uCsOff.value;
		const k = dt * s.uCsScale.value;
		// Each layer wraps on its own, so neither jumps.
		o.x = (o.x - s.wind.x * k) % 1;
		o.y = (o.y - s.wind.y * k) % 1;
		o.z = (o.z - s.wind.x * k * 1.5) % 1;
		o.w = (o.w - s.wind.y * k * 1.5) % 1;
	}
	/** Patches a lit (Lambert or Standard) material so the named scene's cloud shadows dim its sunlight; returns it. */
	function cloudShadows(mat, name) {
		if (mat.userData.cloudShadows) return mat;
		mat.userData.cloudShadows = true;
		const set = cloudShadowSet(name);
		const prev = mat.onBeforeCompile;
		const oldKey = mat.customProgramCacheKey;
		const baseKey = oldKey === T.Material.prototype.customProgramCacheKey ? () => prev.toString() : () => oldKey.call(mat);
		mat.onBeforeCompile = function (sh, r) {
			prev.call(this, sh, r);
			Object.assign(sh.uniforms, { uCsMap: set.uCsMap, uCsOff: set.uCsOff, uCsK: set.uCsK, uCsScale: set.uCsScale, uCsSun: set.uCsSun });
			sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nvarying vec3 vCsW;')
				.replace('#include <project_vertex>', [
					'#include <project_vertex>',
					'#ifdef USE_INSTANCING',
					'vCsW = (modelMatrix * instanceMatrix * vec4(transformed, 1.0)).xyz;',
					'#else',
					'vCsW = (modelMatrix * vec4(transformed, 1.0)).xyz;',
					'#endif',
				].join('\n'));
			sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nuniform sampler2D uCsMap;\nuniform vec4 uCsOff;\nuniform float uCsK, uCsScale;\nuniform vec3 uCsSun;\nvarying vec3 vCsW;')
				.replace('#include <lights_fragment_end>', [
					'#include <lights_fragment_end>',
					'{',
					// Cast along the sun, so a hillside, a roof and the ground under it are shaded by the same cloud.
					'	vec2 cp = (vCsW.xz - uCsSun.xz / max(uCsSun.y, 0.2) * vCsW.y) * uCsScale;',
					'	float cd = texture2D(uCsMap, cp + uCsOff.xy).r * 0.75 + texture2D(uCsMap, cp * 1.9 + uCsOff.zw).r * 0.45;',
					'	float cs = 1.0 - uCsK * smoothstep(0.28, 0.58, cd);',
					'	reflectedLight.directDiffuse *= cs;',
					'	reflectedLight.directSpecular *= cs;',
					'}',
				].join('\n'));
		};
		mat.customProgramCacheKey = () => baseKey() + '|cs';
		return mat;
	}

	// --- Particles --------------------------------------------------------------------------------------------
	// Atlas cells (4 x 2 of 128 px): 0 smoke, 1 spark, 2 flame, 3 glow, 4 shard, 5 debris, 6 ring, 7 star.
	const CELLS = ['fx_smoke', 'fx_spark', 'fx_flame', 'fx_glow', 'fx_shard', 'fx_debris', 'fx_ring', 'fx_star'];
	const F = { smoke: 0, spark: 1, flame: 2, glow: 3, shard: 4, debris: 5, ring: 6, star: 7 };
	let atlas = null;
	function atlasTexture() {
		if (atlas) return atlas;
		const W = 512;
		const data = new Uint8Array(W * 256 * 4);
		atlas = dataTexture(W, 256, data);
		CELLS.forEach((name, i) => raster(name, 1, (img) => {
			const cell = bled(img, false);
			const x0 = (i % 4) * 128;
			const y0 = Math.floor(i / 4) * 128;
			for (let y = 0; y < 128; y++) data.set(cell.subarray(y * 512, (y + 1) * 512), ((y0 + y) * W + x0) * 4);
			atlas.needsUpdate = true;
		}));
		return atlas;
	}
	const MAXP = 2400;
	const P = {
		n: 0,
		x: new Float32Array(MAXP * 3), v: new Float32Array(MAXP * 3), life: new Float32Array(MAXP), age: new Float32Array(MAXP),
		s0: new Float32Array(MAXP), s1: new Float32Array(MAXP), c0: new Float32Array(MAXP * 3), c1: new Float32Array(MAXP * 3), a: new Float32Array(MAXP),
		frame: new Uint8Array(MAXP), add: new Uint8Array(MAXP), grav: new Float32Array(MAXP), drag: new Float32Array(MAXP), rot: new Float32Array(MAXP), spin: new Float32Array(MAXP),
		fadeIn: new Float32Array(MAXP),
	};
	const layers = [];
	function makeLayer(additive) {
		const geo = new T.BufferGeometry();
		const pos = new Float32Array(MAXP * 3);
		const col = new Float32Array(MAXP * 4);
		const size = new Float32Array(MAXP);
		const frame = new Float32Array(MAXP);
		const rot = new Float32Array(MAXP);
		geo.setAttribute('position', new T.BufferAttribute(pos, 3).setUsage(T.DynamicDrawUsage));
		geo.setAttribute('pcolor', new T.BufferAttribute(col, 4).setUsage(T.DynamicDrawUsage));
		geo.setAttribute('psize', new T.BufferAttribute(size, 1).setUsage(T.DynamicDrawUsage));
		geo.setAttribute('pframe', new T.BufferAttribute(frame, 1).setUsage(T.DynamicDrawUsage));
		geo.setAttribute('prot', new T.BufferAttribute(rot, 1).setUsage(T.DynamicDrawUsage));
		const mat = new T.ShaderMaterial({
			uniforms: T.UniformsUtils.merge([T.UniformsLib.fog, { uAtlas: { value: null }, uScale: { value: 800 } }]),
			vertexShader: [
				'attribute vec4 pcolor; attribute float psize; attribute float pframe; attribute float prot;',
				'varying vec4 vColor; varying float vFrame; varying float vRot;',
				'uniform float uScale;',
				'#include <fog_pars_vertex>',
				'void main() {',
				'  vColor = pcolor; vFrame = pframe; vRot = prot;',
				'  vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);',
				'  gl_PointSize = psize * uScale / max(1.0, -mvPosition.z);',
				'  gl_Position = projectionMatrix * mvPosition;',
				'  #include <fog_vertex>',
				'}',
			].join('\n'),
			fragmentShader: [
				'uniform sampler2D uAtlas;',
				'varying vec4 vColor; varying float vFrame; varying float vRot;',
				'#include <fog_pars_fragment>',
				'void main() {',
				'  vec2 p = gl_PointCoord - 0.5;',
				'  float c = cos(vRot), s = sin(vRot);',
				'  p = vec2(c * p.x - s * p.y, s * p.x + c * p.y) + 0.5;',
				'  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) discard;',
				'  vec2 uv = (vec2(mod(vFrame, 4.0), floor(vFrame / 4.0)) + p) * vec2(0.25, 0.5);',
				'  vec4 t = texture2D(uAtlas, uv);',
				'  gl_FragColor = vec4(t.rgb * vColor.rgb, t.a * vColor.a);',
				additive ? '  #ifdef USE_FOG\n  gl_FragColor.a *= 1.0 - smoothstep(fogNear, fogFar, vFogDepth);\n  #endif' : '  #include <fog_fragment>',
				'}',
			].join('\n'),
			transparent: true, depthWrite: false, fog: true,
			blending: additive ? T.AdditiveBlending : T.NormalBlending,
		});
		mat.uniforms.uAtlas.value = atlasTexture();
		const pts = new T.Points(geo, mat);
		pts.frustumCulled = false;
		pts.renderOrder = additive ? 7 : 6;
		return { pts, pos, col, size, frame, rot, geo, mat };
	}
	function hexRgb(hex, out) { out.r = ((hex >> 16) & 255) / 255; out.g = ((hex >> 8) & 255) / 255; out.b = (hex & 255) / 255; return out; }
	const ca = { r: 1, g: 1, b: 1 };
	const cb = { r: 1, g: 1, b: 1 };
	/** Adds one particle. o: frame, add, life, s0, s1, c0, c1 (hex), a (peak alpha), v [x, y, z], grav, drag, spin, fadeIn. */
	function spawn(x, y, z, o) {
		if (P.n >= MAXP) return;
		const i = P.n++;
		P.x[i * 3] = x; P.x[i * 3 + 1] = y; P.x[i * 3 + 2] = z;
		const v = o.v || [0, 0, 0];
		P.v[i * 3] = v[0]; P.v[i * 3 + 1] = v[1]; P.v[i * 3 + 2] = v[2];
		P.life[i] = o.life;
		P.age[i] = 0;
		P.s0[i] = o.s0;
		P.s1[i] = o.s1 === undefined ? o.s0 : o.s1;
		hexRgb(o.c0, ca);
		hexRgb(o.c1 === undefined ? o.c0 : o.c1, cb);
		P.c0[i * 3] = ca.r; P.c0[i * 3 + 1] = ca.g; P.c0[i * 3 + 2] = ca.b;
		P.c1[i * 3] = cb.r; P.c1[i * 3 + 1] = cb.g; P.c1[i * 3 + 2] = cb.b;
		P.a[i] = o.a === undefined ? 1 : o.a;
		P.frame[i] = o.frame;
		P.add[i] = o.add ? 1 : 0;
		P.grav[i] = o.grav || 0;
		P.drag[i] = o.drag || 0;
		P.rot[i] = Math.random() * 6.283;
		P.spin[i] = o.spin === undefined ? (Math.random() - 0.5) * 2 : o.spin;
		P.fadeIn[i] = o.fadeIn || 0.08;
	}
	function kill(i) {
		const j = --P.n;
		if (i === j) return;
		for (const k of ['x', 'v', 'c0', 'c1']) { P[k][i * 3] = P[k][j * 3]; P[k][i * 3 + 1] = P[k][j * 3 + 1]; P[k][i * 3 + 2] = P[k][j * 3 + 2]; }
		for (const k of ['life', 'age', 's0', 's1', 'a', 'frame', 'add', 'grav', 'drag', 'rot', 'spin', 'fadeIn']) P[k][i] = P[k][j];
	}
	function stepParticles(dt) {
		for (let i = P.n - 1; i >= 0; i--) {
			P.age[i] += dt;
			if (P.age[i] >= P.life[i]) { kill(i); continue; }
			const d = Math.max(0, 1 - P.drag[i] * dt);
			P.v[i * 3] *= d; P.v[i * 3 + 1] = P.v[i * 3 + 1] * d - P.grav[i] * dt; P.v[i * 3 + 2] *= d;
			P.x[i * 3] += P.v[i * 3] * dt; P.x[i * 3 + 1] += P.v[i * 3 + 1] * dt; P.x[i * 3 + 2] += P.v[i * 3 + 2] * dt;
			P.rot[i] += P.spin[i] * dt;
			// Debris and shards come to rest on the ground.
			if (P.grav[i] > 5) {
				const g = heightAt(P.x[i * 3], -P.x[i * 3 + 2]) + 0.2;
				if (P.x[i * 3 + 1] < g) { P.x[i * 3 + 1] = g; P.v[i * 3] *= 0.4; P.v[i * 3 + 2] *= 0.4; P.v[i * 3 + 1] = Math.abs(P.v[i * 3 + 1]) * 0.25; P.spin[i] *= 0.5; }
			}
		}
		const fill = [0, 0];
		for (let i = 0; i < P.n; i++) {
			const L = layers[P.add[i]];
			const k = fill[P.add[i]]++;
			const t = P.age[i] / P.life[i];
			L.pos[k * 3] = P.x[i * 3]; L.pos[k * 3 + 1] = P.x[i * 3 + 1]; L.pos[k * 3 + 2] = P.x[i * 3 + 2];
			L.col[k * 4] = P.c0[i * 3] + (P.c1[i * 3] - P.c0[i * 3]) * t;
			L.col[k * 4 + 1] = P.c0[i * 3 + 1] + (P.c1[i * 3 + 1] - P.c0[i * 3 + 1]) * t;
			L.col[k * 4 + 2] = P.c0[i * 3 + 2] + (P.c1[i * 3 + 2] - P.c0[i * 3 + 2]) * t;
			const fi = P.fadeIn[i];
			L.col[k * 4 + 3] = P.a[i] * (t < fi ? t / fi : 1 - (t - fi) / (1 - fi));
			L.size[k] = P.s0[i] + (P.s1[i] - P.s0[i]) * Math.sqrt(t);
			L.frame[k] = P.frame[i];
			L.rot[k] = P.rot[i];
		}
		layers.forEach((L, li) => {
			L.geo.setDrawRange(0, fill[li]);
			for (const a of ['position', 'pcolor', 'psize', 'pframe', 'prot']) L.geo.attributes[a].needsUpdate = true;
			L.mat.uniforms.uScale.value = renderer.domElement.height / (2 * Math.tan((camera.fov * Math.PI) / 360));
		});
	}
	const rr = (a, b) => a + Math.random() * (b - a);
	/** n particles in a disc of radius r around (x, y, z) (world), each from tmpl() with an outward push. */
	function ring(n, x, y, z, r, out, tmpl) {
		for (let k = 0; k < n; k++) {
			const a = Math.random() * 6.283;
			const d = r * Math.sqrt(Math.random());
			const o = tmpl();
			const vx = Math.cos(a);
			const vz = Math.sin(a);
			o.v = [vx * out * rr(0.5, 1) + (o.v ? o.v[0] : 0), o.v ? o.v[1] : 0, vz * out * rr(0.5, 1) + (o.v ? o.v[2] : 0)];
			spawn(x + vx * d, y, z + vz * d, o);
		}
	}
	const dustT = (sz) => () => ({ frame: F.smoke, life: rr(1.0, 1.9), s0: sz * rr(0.8, 1.2), s1: sz * rr(2.2, 3.2), c0: 0x9a8566, c1: 0xc9b99c, a: 0.5, v: [0, rr(1, 3), 0], drag: 2.2, spin: rr(-0.6, 0.6), fadeIn: 0.12 });
	const smokeT = (sz) => () => ({ frame: F.smoke, life: rr(1.8, 3.2), s0: sz * rr(0.7, 1.1), s1: sz * rr(2.4, 3.4), c0: 0x2e2a26, c1: 0x77726a, a: 0.55, v: [rr(-0.6, 0.6), rr(3, 6), rr(-0.6, 0.6)], drag: 0.8, fadeIn: 0.15 });
	const fireT = (sz) => () => ({ frame: F.flame, add: true, life: rr(0.35, 0.75), s0: sz * rr(0.9, 1.3), s1: sz * rr(0.3, 0.6), c0: 0xffe0a0, c1: 0xff4a1a, a: 0.95, v: [rr(-1.5, 1.5), rr(4, 9), rr(-1.5, 1.5)], drag: 1.2, spin: rr(-1, 1), fadeIn: 0.1 });
	const emberT = (hex) => () => ({ frame: F.spark, add: true, life: rr(0.9, 1.8), s0: rr(0.25, 0.45), s1: 0.08, c0: hex || 0xffb04a, c1: 0xff5a1a, a: 1, v: [rr(-2, 2), rr(3, 7), rr(-2, 2)], grav: -0.6, drag: 0.6 });
	const sparkT = (hex) => () => ({ frame: F.spark, add: true, life: rr(0.25, 0.55), s0: rr(0.5, 0.9), s1: 0.1, c0: 0xffffff, c1: hex || 0x8fc8ff, a: 1, v: [rr(-9, 9), rr(4, 14), rr(-9, 9)], grav: 14, drag: 1.5 });
	const debrisT = (sz, hex) => () => ({ frame: F.debris, life: rr(0.9, 1.6), s0: sz * rr(0.5, 1.1), s1: sz * rr(0.4, 0.9), c0: hex || 0x8a7a66, c1: hex || 0x6a5e50, a: 1, v: [0, rr(9, 18), 0], grav: 30, drag: 0.4, spin: rr(-8, 8), fadeIn: 0.02 });
	const flashT = (sz, hex) => () => ({ frame: F.glow, add: true, life: rr(0.18, 0.3), s0: sz, s1: sz * 1.6, c0: hex || 0xfff2d0, a: 0.9, fadeIn: 0.05 });

	// --- Lightning bolts: jagged camera-facing ribbons, a few frames long ---------------------------------------
	const bolts = [];
	let boltMat = null;
	function strike(x, y, z, h, hex) {
		if (!boltMat) boltMat = new T.MeshBasicMaterial({ color: 0xdff2ff, transparent: true, blending: T.AdditiveBlending, depthWrite: false, side: T.DoubleSide, fog: false });
		const pts = [];
		const n = 14;
		let px = x + rr(-6, 6);
		let pz = z + rr(-6, 6);
		for (let i = 0; i <= n; i++) {
			const k = i / n;
			const tx = px + (x - px) * k;
			const tz = pz + (z - pz) * k;
			const j = i === n ? 0 : (1 - k * 0.5) * 2.6;
			pts.push(new T.Vector3(tx + rr(-j, j), y + h * (1 - k), tz + rr(-j, j)));
		}
		const mesh = new T.Mesh(new T.BufferGeometry(), boltMat.clone());
		mesh.material.color.set(hex || 0xdff2ff);
		mesh.renderOrder = 8;
		mesh.frustumCulled = false;
		scene.add(mesh);
		bolts.push({ mesh, pts, age: 0, life: 0.32, w: 0.9 });
		// A fork off the main bolt.
		if (Math.random() < 0.8) {
			const at = 3 + Math.floor(Math.random() * 6);
			const fp = [pts[at].clone()];
			for (let i = 1; i < 6; i++) fp.push(fp[i - 1].clone().add(new T.Vector3(rr(-3, 3), -h / 16, rr(-3, 3))));
			const m2 = new T.Mesh(new T.BufferGeometry(), mesh.material);
			m2.renderOrder = 8;
			m2.frustumCulled = false;
			scene.add(m2);
			bolts.push({ mesh: m2, pts: fp, age: 0, life: 0.22, w: 0.45, shared: true });
		}
	}
	const tv1 = { x: 0, y: 0, z: 0 };
	function stepBolts(dt) {
		const cp = camera.position;
		for (let b = bolts.length - 1; b >= 0; b--) {
			const B = bolts[b];
			B.age += dt;
			if (B.age >= B.life) {
				scene.remove(B.mesh);
				B.mesh.geometry.dispose();
				if (!B.shared) B.mesh.material.dispose();
				bolts.splice(b, 1);
				continue;
			}
			// Flicker: rebuild the ribbon facing the camera each frame.
			const pos = [];
			const w = B.w * (1 - B.age / B.life) * (0.7 + Math.random() * 0.6);
			for (let i = 0; i < B.pts.length; i++) {
				const p = B.pts[i];
				const q = B.pts[Math.min(B.pts.length - 1, i + 1)];
				const r = B.pts[Math.max(0, i - 1)];
				const dx = q.x - r.x;
				const dy = q.y - r.y;
				const dz = q.z - r.z;
				const vx = cp.x - p.x;
				const vy = cp.y - p.y;
				const vz = cp.z - p.z;
				let sx = dy * vz - dz * vy;
				let sy = dz * vx - dx * vz;
				let sz = dx * vy - dy * vx;
				const l = Math.hypot(sx, sy, sz) || 1;
				sx = (sx / l) * w; sy = (sy / l) * w; sz = (sz / l) * w;
				pos.push(p.x - sx, p.y - sy, p.z - sz, p.x + sx, p.y + sy, p.z + sz);
			}
			const idx = [];
			for (let i = 0; i < B.pts.length - 1; i++) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
			B.mesh.geometry.setAttribute('position', new T.Float32BufferAttribute(pos, 3));
			B.mesh.geometry.setIndex(idx);
			B.mesh.material.opacity = 1 - B.age / B.life;
		}
		void tv1;
	}

	// --- Ground marks (scorch, cracks) ------------------------------------------------------------------------
	const decals = [];
	const decalMats = {};
	function decal(kind, x, z, size, life) {
		if (decals.length > 40) { const d = decals.shift(); scene.remove(d.m); }
		if (!decalMats[kind]) decalMats[kind] = new T.MeshBasicMaterial({ map: tex(kind), transparent: true, depthWrite: false, polygonOffset: true, polygonOffsetFactor: -4, polygonOffsetUnits: -4 });
		const m = new T.Mesh(decalGeo(), decalMats[kind].clone());
		m.position.set(x, heightAt(x, z) + 0.12, -z);
		m.rotation.set(-Math.PI / 2, 0, Math.random() * 6.283);
		m.scale.setScalar(size);
		m.renderOrder = 1;
		scene.add(m);
		decals.push({ m, age: 0, life: life || 14 });
	}
	let dGeo = null;
	const decalGeo = () => dGeo || (dGeo = new T.PlaneGeometry(1, 1));
	function stepDecals(dt) {
		for (let i = decals.length - 1; i >= 0; i--) {
			const d = decals[i];
			d.age += dt;
			const k = d.age / d.life;
			if (k >= 1) { scene.remove(d.m); d.m.material.dispose(); decals.splice(i, 1); continue; }
			d.m.material.opacity = k < 0.7 ? 0.9 : 0.9 * (1 - (k - 0.7) / 0.3);
		}
	}

	// --- Effects for the battle's events ----------------------------------------------------------------------
	/** spawnFx kinds from app.js: sim coordinates (x, z), y above the ground, s0/s1 start/end size, h column height. */
	function burst(kind, x, z, y, s0, s1, h) {
		if (!scene) return;
		const gy = heightAt(x, z);
		const X = x;
		const Y = gy + (y || 0);
		const Z = -z;
		const big = Math.max(1, s1 || 4);
		const few = quality === 'high' ? 1 : 0.55;
		const N = (n) => Math.max(1, Math.round(n * few));
		switch (kind) {
		case 'dust':
			ring(N(Math.min(40, 4 + big * 1.6)), X, gy + 0.6, Z, big * 0.35, big * 0.6, dustT(Math.min(7, 1 + big * 0.28)));
			if (big >= 9) ring(N(big * 0.9), X, gy + 0.5, Z, big * 0.2, big * 0.5, debrisT(Math.min(2.4, big * 0.1)));
			if (big >= 12) decal('decal_crack', x, z, Math.min(30, big * 1.1), 18);
			break;
		case 'shock':
			spawn(X, Y + 1.5, Z, flashT(Math.min(26, big * 1.1))());
			break;
		case 'fire': {
			const r = big * 0.25;
			ring(N(10 + big * 2.2), X, Y, Z, r, big * 0.35, fireT(Math.min(6, 1.2 + big * 0.35)));
			ring(N(6 + big), X, Y + 1, Z, r, big * 0.2, smokeT(Math.min(6, 1 + big * 0.3)));
			ring(N(8 + big), X, Y, Z, r, 2, emberT());
			if (big >= 8) ring(N(big * 0.8), X, gy + 0.6, Z, r, big * 0.6, debrisT(Math.min(2.2, big * 0.12), 0x5a4a3c));
			spawn(X, Y + 1, Z, flashT(big * 1.8, 0xffb060)());
			decal('decal_scorch', x, z, Math.min(28, big * 1.4), 20);
			break;
		}
		case 'bolt':
			strike(X, gy, Z, 70 + Math.random() * 30);
			ring(N(18), X, gy + 1, Z, 1.5, 4, sparkT());
			spawn(X, gy + 2, Z, flashT(18, 0xcfe8ff)());
			decal('decal_scorch', x, z, 7, 10);
			break;
		case 'frost':
			ring(N(14), X, gy + 1.5, Z, big * 0.3, big * 0.8, () => ({ frame: F.shard, life: rr(0.8, 1.3), s0: rr(0.8, 1.6), s1: 0.5, c0: 0xe6f8ff, c1: 0xa8eaff, a: 0.95, v: [0, rr(6, 14), 0], grav: 22, spin: rr(-6, 6), fadeIn: 0.03 }));
			ring(N(12), X, gy + 1, Z, big * 0.4, big * 0.5, () => ({ frame: F.smoke, life: rr(1.2, 2.0), s0: 2, s1: 6, c0: 0xeaf8ff, c1: 0xbfe6ff, a: 0.45, v: [0, rr(0.5, 2), 0], drag: 1.5 }));
			spawn(X, gy + 2, Z, flashT(16, 0xbfeaff)());
			break;
		case 'flame':
			ring(N(26), X, gy + 0.5, Z, big * 0.3, 1.5, () => Object.assign(fireT(2.4)(), { v: [rr(-1, 1), rr(10, 20), rr(-1, 1)], life: rr(0.5, 0.9) }));
			ring(N(10), X, gy + 2, Z, big * 0.3, 1, smokeT(2.5));
			ring(N(10), X, gy + 1, Z, big * 0.3, 2, emberT());
			decal('decal_scorch', x, z, Math.min(18, big * 1.5), 16);
			break;
		case 'moon':
			ring(N(18), X, gy + 1, Z, big * 0.3, 1, () => ({ frame: F.spark, add: true, life: rr(0.6, 1.1), s0: rr(0.6, 1.1), s1: 0.1, c0: 0xffffff, c1: 0xe8ffb0, a: 1, v: [0, rr(8, 18), 0], drag: 1.2 }));
			spawn(X, gy + 3, Z, flashT(14, 0xf2ffd0)());
			break;
		case 'necroCol':
		case 'necro':
			ring(N(kind === 'necro' ? 16 : 22), X, gy + 0.6, Z, big * 0.35, kind === 'necro' ? big * 0.3 : 1,
				() => ({ frame: kind === 'necro' ? F.smoke : F.flame, add: true, life: rr(0.8, 1.5), s0: rr(1.2, 2.2), s1: kind === 'necro' ? 4 : 0.6, c0: 0xb8ffcf, c1: 0x2fa860, a: 0.7, v: [0, rr(2, 8), 0], drag: 1 }));
			break;
		case 'heal':
			ring(N(20), X, gy + 0.5, Z, big * 0.45, 0.3, () => ({ frame: F.spark, add: true, life: rr(1.0, 1.7), s0: rr(0.6, 1.0), s1: 0.15, c0: 0xeafff0, c1: 0x5fd38d, a: 1, v: [0, rr(2.5, 5), 0], drag: 0.5 }));
			break;
		case 'stagger':
			for (let k = 0; k < 5; k++) {
				const a = (k / 5) * 6.283;
				spawn(X + Math.cos(a) * 2.5, Y, Z + Math.sin(a) * 2.5, { frame: F.star, add: true, life: 1.1, s0: 1.4, s1: 0.8, c0: 0xfff3a0, c1: 0xffc040, a: 1, v: [-Math.sin(a) * 6, 1.5, Math.cos(a) * 6], drag: 0.8, spin: 3 });
			}
			break;
		default: break;
		}
	}
	/** One particle from a titan's emitter (world coordinates); size is about the titan's scale. */
	function emit(kind, w, size) {
		if (!scene || P.n > MAXP * 0.8) return;
		const s = size || 1;
		switch (kind) {
		case 'flame': spawn(w.x + rr(-0.3, 0.3) * s, w.y, w.z + rr(-0.3, 0.3) * s, { frame: F.flame, add: true, life: rr(0.3, 0.55), s0: s * rr(1.4, 2.0), s1: s * 0.5, c0: 0xffe6a0, c1: 0xff4a1a, a: 0.9, v: [rr(-0.5, 0.5), rr(3, 5) * s, rr(-0.5, 0.5)], drag: 1, spin: rr(-1, 1) }); break;
		case 'witchfire': spawn(w.x, w.y, w.z, { frame: F.flame, add: true, life: rr(0.3, 0.5), s0: s * rr(1.1, 1.5), s1: s * 0.4, c0: 0xf2e6ff, c1: 0x8a4ae8, a: 0.95, v: [rr(-0.3, 0.3), rr(2.5, 4) * s, rr(-0.3, 0.3)], drag: 1 }); break;
		case 'ember': spawn(w.x, w.y, w.z, Object.assign(emberT()(), { s0: rr(0.25, 0.4) * s * 1.5, v: [rr(-0.8, 0.8), rr(1, 3), rr(-0.8, 0.8)] })); break;
		case 'spark': spawn(w.x, w.y, w.z, { frame: F.spark, add: true, life: rr(0.15, 0.35), s0: s * rr(0.8, 1.3), s1: 0.05, c0: 0xffffff, c1: 0x6fb0ff, a: 1, v: [rr(-3, 3), rr(-1, 4), rr(-3, 3)], grav: 6 }); break;
		case 'frost': spawn(w.x, w.y, w.z, { frame: F.smoke, life: rr(1.0, 1.8), s0: s * 0.8, s1: s * 2.8, c0: 0xf2fbff, c1: 0xbfe6ff, a: 0.35, v: [rr(-0.8, 0.8), rr(-0.8, 0.4), rr(-0.8, 0.8)], drag: 0.8 }); break;
		case 'star': spawn(w.x + rr(-1.5, 1.5) * s, w.y + rr(-1.5, 1.5) * s, w.z + rr(-1.5, 1.5) * s, { frame: F.star, add: true, life: rr(0.7, 1.2), s0: s * rr(0.4, 0.7), s1: s * 0.1, c0: 0xfff3c0, a: 1, fadeIn: 0.4, spin: 1 }); break;
		case 'shade': spawn(w.x + rr(-0.6, 0.6) * s, w.y, w.z + rr(-0.6, 0.6) * s, { frame: F.smoke, life: rr(0.8, 1.4), s0: s * 0.7, s1: s * 2.2, c0: 0x2c4428, c1: 0x1c2a1a, a: 0.32, v: [rr(-0.4, 0.4), rr(0.4, 1.2), rr(-0.4, 0.4)], drag: 0.8 }); break;
		case 'nav': spawn(w.x + rr(-0.5, 0.5) * s, w.y + rr(-0.5, 0.5) * s, w.z + rr(-0.5, 0.5) * s, { frame: F.spark, add: true, life: rr(0.6, 1.1), s0: s * rr(0.5, 0.8), s1: 0.05, c0: 0xd8fff0, c1: 0x5fe0b0, a: 0.9, v: [rr(-0.3, 0.3), rr(0.5, 1.0), rr(-0.3, 0.3)], fadeIn: 0.3 }); break;
		case 'moon': spawn(w.x + rr(-0.4, 0.4) * s, w.y + rr(-1, 1) * s, w.z + rr(-0.4, 0.4) * s, { frame: F.spark, add: true, life: rr(0.5, 0.9), s0: s * 0.6, s1: 0.05, c0: 0xf2ffd8, a: 0.9, v: [0, 0.6, 0], fadeIn: 0.3 }); break;
		default: break;
		}
	}
	/** Trail particle behind a projectile (world coordinates). */
	function trail(kind, x, y, z) {
		if (!scene) return;
		if (kind === 'meteor') {
			spawn(x, y, z, Object.assign(fireT(3.5)(), { v: [rr(-1, 1), rr(0, 2), rr(-1, 1)], life: rr(0.25, 0.45) }));
			if (Math.random() < 0.5) spawn(x, y, z, smokeT(2.4)());
		} else if (kind === 'firepot') {
			if (Math.random() < 0.5) spawn(x, y, z, { frame: F.smoke, life: rr(0.5, 0.9), s0: 0.6, s1: 1.6, c0: 0x4a4440, c1: 0x8a847c, a: 0.4 });
		} else if (kind === 'burn') {
			spawn(x, y, z, Object.assign(fireT(1.6)(), { life: rr(0.3, 0.6), v: [0, rr(2, 5), 0] }));
		} else if (kind === 'heal') {
			spawn(x, y, z, { frame: F.spark, add: true, life: rr(0.8, 1.3), s0: 0.7, s1: 0.1, c0: 0xeafff0, c1: 0x5fd38d, a: 0.9, v: [0, rr(2, 4), 0] });
		} else if (kind === 'ichor') {
			spawn(x, y, z, { frame: F.glow, add: true, life: rr(0.5, 0.9), s0: 1.2, s1: 0.4, c0: 0xffc06a, c1: 0xff6a1a, a: 0.8, v: [0, rr(1, 2.5), 0] });
		}
	}

	return {
		init(three, r) {
			T = three;
			renderer = r;
			readArt();
			if (!layers.length) layers.push(makeLayer(false), makeLayer(true));
		},
		attach(sc, cam, opts) {
			scene = sc;
			camera = cam;
			heightAt = (opts && opts.height) || heightAt;
			quality = (opts && opts.quality) || quality;
			for (const L of layers) if (L.pts.parent !== sc) sc.add(L.pts);
		},
		/** Drops everything spawned for the last battle (particles, bolts, marks). */
		clear() {
			P.n = 0;
			for (const d of decals) scene.remove(d.m);
			decals.length = 0;
			for (const b of bolts) scene.remove(b.mesh);
			bolts.length = 0;
		},
		setQuality(q) { quality = q; },
		get hasArt() { return Object.keys(art).length > 0; },
		/** True once init() has handed over three.js, so the builders (puffClouds and the rest) can be used. */
		get ready() { return !!T; },
		tex, glow, envMap, dressTerrain, surroundings, foliage, forest, rockGeometry, water, worldUv, shore, burst, emit, trail, decal, puffClouds, cloudHeap, cloudShadows, cloudShadowSet, stepCloudShadows,
		update(dt, t) {
			time.t = t;
			time.u.value = t;
			if (!scene || !camera) return;
			stepParticles(dt);
			stepBolts(dt);
			stepDecals(dt);
			stepCloudShadows('battle', dt);
			for (const c of clouds) { c.s.position.x += c.v * dt; if (c.s.position.x > c.x0 + 650) c.s.position.x -= 1300; }
		},
		stats: () => ({ particles: P.n, decals: decals.length, bolts: bolts.length }),
	};
})();
if (typeof module !== 'undefined') module.exports = DotGfx;
