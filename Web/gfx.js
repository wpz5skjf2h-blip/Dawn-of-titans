'use strict';
// Web/gfx.js - look of the battlefield on top of the battle view in app.js: sky, textured ground, water with ripples, grass
// and flowers that sway, forests and hills around the field, and particles (dust, fire, sparks, frost, stars, debris),
// lightning and scorch marks for the battle's events. The pictures are the SVG files in Web/art (bundled by bundle.py as
// <script id="art-data">); each is rasterised once into a canvas texture. Nothing here changes the battle itself.
//   DotGfx.init(THREE, renderer)                      once, before anything else
//   DotGfx.attach(scene, camera, { height, quality })  the battle scene; height(x, z) is the sim's ground height
//   DotGfx.dressTerrain(mesh, st, CF)                  splat-textured ground for the battle's heightfield
//   DotGfx.surroundings(group, W, D)                   sky, clouds, land, forest and hills beyond the field
//   DotGfx.foliage(group, st, CF)                      grass tufts and flowers
//   DotGfx.forest(group, items), rockGeometry()        trees (items [x, y, z, scale, r]) and rough rocks
//   DotGfx.water(deep), worldUv(geo, x0, z0, size)     water material; world-scaled UVs for a ground-aligned geometry
//   DotGfx.burst(kind, x, z, y, s0, s1, h)             effects for the battle's spawnFx kinds (sim coordinates)
//   DotGfx.emit(kind, v3, size)                        one particle from a titan emitter (world coordinates)
//   DotGfx.trail(kind, x, y, z)                        trail particle for projectiles (world coordinates)
//   DotGfx.glow(hex), envMap(), tex(name)              shared textures
//   DotGfx.update(dt, t)                               every battle frame, before rendering

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
	/** Draws art[name] into a canvas of `scale` times its size; calls done(canvas) once drawn. */
	function raster(name, scale, done) {
		const text = art[name];
		if (!text) return;
		const [w, h] = svgSize(text);
		const c = document.createElement('canvas');
		c.width = Math.round(w * scale);
		c.height = Math.round(h * scale);
		const img = new Image();
		img.onload = () => {
			const g = c.getContext('2d');
			g.drawImage(img, 0, 0, c.width, c.height);
			done(c);
		};
		img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(text);
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
	/** Small sky-and-ground environment for metal and water reflections. */
	function envMap() {
		if (env) return env;
		const c = document.createElement('canvas');
		c.width = 256;
		c.height = 128;
		const g = c.getContext('2d');
		const grd = g.createLinearGradient(0, 0, 0, 128);
		grd.addColorStop(0, '#5f8fd0');
		grd.addColorStop(0.42, '#cfe1ee');
		grd.addColorStop(0.5, '#b9b49a');
		grd.addColorStop(0.62, '#6d7a44');
		grd.addColorStop(1, '#3c4a24');
		g.fillStyle = grd;
		g.fillRect(0, 0, 256, 128);
		g.fillStyle = 'rgba(255,248,225,0.95)';
		g.beginPath();
		g.arc(64, 26, 9, 0, Math.PI * 2);
		g.fill();
		const t = new T.CanvasTexture(c);
		t.mapping = T.EquirectangularReflectionMapping;
		t.encoding = T.sRGBEncoding;
		const pm = new T.PMREMGenerator(renderer);
		env = pm.fromEquirectangular(t).texture;
		pm.dispose();
		t.dispose();
		return env;
	}

	// --- Ground -----------------------------------------------------------------------------------------------
	const groundTex = () => ({ g: tex('tex_grass', { repeat: true, scale: 2 }), d: tex('tex_dirt', { repeat: true, scale: 2 }),
		r: tex('tex_rock', { repeat: true, scale: 2 }), m: tex('tex_mud', { repeat: true, scale: 2 }) });
	/** Lambert material that blends grass, dirt, rock and mud textures by a per-vertex `splat` (dirt, rock, mud). */
	function splatMaterial(tile) {
		const t = groundTex();
		const mat = new T.MeshLambertMaterial({ color: 0xffffff, vertexColors: true });
		mat.onBeforeCompile = (sh) => {
			Object.assign(sh.uniforms, { tG: { value: t.g }, tD: { value: t.d }, tR: { value: t.r }, tM: { value: t.m }, tTile: { value: 1 / tile } });
			sh.vertexShader = sh.vertexShader.replace('#include <common>', '#include <common>\nattribute vec3 splat;\nvarying vec3 vSplat;\nvarying vec2 vGuv;\nuniform float tTile;')
				.replace('#include <begin_vertex>', '#include <begin_vertex>\nvSplat = splat;\nvGuv = (modelMatrix * vec4(position, 1.0)).xz * tTile;');
			sh.fragmentShader = sh.fragmentShader.replace('#include <common>', '#include <common>\nuniform sampler2D tG, tD, tR, tM;\nvarying vec3 vSplat;\nvarying vec2 vGuv;')
				.replace('#include <map_fragment>', [
					'vec3 gA = texture2D(tG, vGuv).rgb;',
					'vec3 gB = texture2D(tG, vGuv * 0.29 + vec2(0.37, 0.11)).rgb;',
					'vec3 grassC = mix(gA, gB, 0.45);',
					'vec3 dirtC = mix(texture2D(tD, vGuv).rgb, texture2D(tD, vGuv * 0.41 + vec2(0.2, 0.6)).rgb, 0.35);',
					'vec3 rockC = texture2D(tR, vGuv * 0.8).rgb;',
					'vec3 mudC = texture2D(tM, vGuv).rgb;',
					'float wG = max(0.0, 1.0 - vSplat.x - vSplat.y - vSplat.z);',
					'diffuseColor.rgb *= (grassC * wG + dirtC * vSplat.x + rockC * vSplat.y + mudC * vSplat.z) * 1.55;',
				].join('\n'));
		};
		return mat;
	}
	/** Replaces the battle heightfield's flat vertex colours with textures: splat weights and tints from the sim's flags. */
	function dressTerrain(mesh, st, CF) {
		const g = st.grid;
		const vx = g.nx + 1;
		const vz = g.nz + 1;
		const flagAt = (cx, cz) => g.f[Math.min(g.nz - 1, Math.max(0, cz)) * g.nx + Math.min(g.nx - 1, Math.max(0, cx))];
		const hAt = (cx, cz) => g.h[Math.min(g.nz - 1, Math.max(0, cz)) * g.nx + Math.min(g.nx - 1, Math.max(0, cx))];
		const splat = new Float32Array(vx * vz * 3);
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
				let sd = road / 4;
				let sr = Math.min(1, rock / 4 + ruin / 8);
				let sm = deep ? 1 : Math.min(1, ford / 3);
				if (ruin) sd = Math.max(sd, ruin / 8);
				const sum = sd + sr + sm;
				if (sum > 1) { sd /= sum; sr /= sum; sm /= sum; }
				splat[k] = sd;
				splat[k + 1] = sr;
				splat[k + 2] = sm;
				// Tint: high ground a little sun-dried, forest floor dark, a little noise everywhere.
				const dry = Math.min(1, Math.max(0, h / 7)) * 0.55;
				const n = 0.94 + rnd() * 0.12;
				let r = (1 + dry * 0.32) * n;
				let gg = (1 + dry * 0.12) * n;
				let b = (1 - dry * 0.3) * n;
				if (forest) { const f = 0.35 + 0.1 * (4 - forest); r *= 0.62 + f * 0.2; gg *= 0.72 + f * 0.15; b *= 0.6 + f * 0.2; }
				if (deep) { r *= 0.55; gg *= 0.62; b *= 0.7; }
				col[k] = r * 0.82;
				col[k + 1] = gg * 0.82;
				col[k + 2] = b * 0.82;
			}
		}
		mesh.geometry.setAttribute('splat', new T.BufferAttribute(splat, 3));
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

	// --- Water ------------------------------------------------------------------------------------------------
	const waters = [];
	function water(deep) {
		const n = tex('tex_water', { repeat: true, linear: true, post: normalsFrom });
		const m = new T.MeshStandardMaterial({ color: deep ? 0x1f5f86 : 0x3d8296, roughness: deep ? 0.12 : 0.18, metalness: 0.0, transparent: true, opacity: deep ? 0.88 : 0.42,
			normalMap: n, normalScale: new T.Vector2(0.55, 0.55), envMap: envMap(), envMapIntensity: deep ? 1.1 : 0.45 });
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
		for (const [geo, hex, x, y, z, sx, sy, sz, ry] of parts) {
			m4.compose(new T.Vector3(x, y, z), new T.Quaternion().setFromEuler(new T.Euler(0, ry || 0, 0)), new T.Vector3(sx, sy, sz));
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
		// Pine: four tiers; fir: tall and narrow; oak: round clumps on a thicker trunk; birch: light trunk, small crown.
		treeGeo = {
			pine: colored([[trunk, 0x5b3d22, 0, 1.1, 0, 1, 2.2, 1], [cone, 0x2b5224, 0, 2.6, 0, 2.0, 2.4, 2.0], [cone, 0x31602a, 0, 3.8, 0, 1.65, 2.2, 1.65, 0.4],
				[cone, 0x3a6c2e, 0, 4.9, 0, 1.25, 2.0, 1.25, 0.8], [cone, 0x447a34, 0, 5.9, 0, 0.8, 1.6, 0.8, 1.2]], true),
			fir: colored([[trunk, 0x4f3520, 0, 1.0, 0, 0.9, 2.0, 0.9], [cone, 0x24482a, 0, 3.0, 0, 1.4, 3.4, 1.4], [cone, 0x2b5530, 0, 4.8, 0, 1.05, 3.0, 1.05, 0.5],
				[cone, 0x336236, 0, 6.4, 0, 0.7, 2.4, 0.7, 1.0]], true),
			oak: colored([[trunk, 0x5e4228, 0, 1.3, 0, 1.4, 2.6, 1.4], [blob, 0x4a7c32, 0, 3.6, 0, 2.2, 1.8, 2.2], [blob, 0x568a3a, 1.1, 4.2, 0.5, 1.5, 1.3, 1.5],
				[blob, 0x3f7030, -1.0, 4.0, -0.6, 1.6, 1.3, 1.6], [blob, 0x62963f, 0.1, 4.9, -0.2, 1.3, 1.1, 1.3]], true),
			birch: colored([[trunk, 0xd8d2c4, 0, 1.6, 0, 0.7, 3.2, 0.7], [blob, 0x7aa848, 0, 4.0, 0, 1.3, 1.7, 1.3], [blob, 0x8bb854, 0.5, 4.7, 0.2, 0.9, 1.1, 0.9]], true),
		};
		return treeGeo;
	}
	/** Instanced forest from [x, y, z(sim), scale, r] items: mostly pines and firs, some oaks and birches, varied tints. */
	function forest(group, items, castShadow) {
		const g = trees();
		const kinds = ['pine', 'fir', 'oak', 'birch'];
		const lists = { pine: [], fir: [], oak: [], birch: [] };
		for (const it of items) {
			const r = it[4];
			lists[r < 0.42 ? 'pine' : r < 0.7 ? 'fir' : r < 0.9 ? 'oak' : 'birch'].push(it);
		}
		const mat = new T.MeshLambertMaterial({ vertexColors: true, flatShading: true });
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
				c.setRGB(v * (0.96 + ((it[4] * 3.1) % 0.08)), v, v * 0.96);
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
		return m;
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
		const n = quality === 'high' ? 5200 : 1600;
		const nf = Math.round(n * 0.12);
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
		place(tuftGeometry(1.5, 1.15), swayMaterial(tex('foliage_grass')), pick(n, true), (c, r) => c.setRGB(0.85 + r * 0.25, 0.9 + r * 0.15, 0.8 + r * 0.15));
		place(tuftGeometry(1.4, 1.2), swayMaterial(tex('foliage_flowers')), pick(nf, false), (c) => c.setRGB(1, 1, 1));
	}

	// --- Sky and the land around the field --------------------------------------------------------------------
	const clouds = [];
	function surroundings(group, W, D) {
		clouds.length = 0;
		const rnd = mulberry(5);
		const cx = W / 2;
		const cz = -D / 2;
		// Sky dome: deep blue overhead, hazy at the horizon (the fog colour), drawn behind everything.
		const sky = new T.SphereGeometry(1300, 32, 16);
		const sp = sky.attributes.position;
		const sc = [];
		const top = new T.Color(0x4f86c8);
		const mid = new T.Color(0x8fb6dc);
		const hor = new T.Color(0xc8d9e4);
		const c = new T.Color();
		for (let i = 0; i < sp.count; i++) {
			const y = sp.getY(i) / 1300;
			if (y > 0.25) c.copy(mid).lerp(top, Math.min(1, (y - 0.25) / 0.6));
			else c.copy(hor).lerp(mid, Math.max(0, y / 0.25));
			sc.push(c.r, c.g, c.b);
		}
		sky.setAttribute('color', new T.Float32BufferAttribute(sc, 3));
		const dome = new T.Mesh(sky, new T.MeshBasicMaterial({ vertexColors: true, side: T.BackSide, fog: false, depthWrite: false }));
		dome.position.set(cx, -120, cz);
		dome.renderOrder = -10;
		group.add(dome);
		if (scene) { scene.background = hor.clone(); if (scene.fog) scene.fog.color.copy(hor); }
		// Land around the field: textured, gently rolling, a little darker than the field.
		const land = new T.PlaneGeometry(1600, 1600, 160, 160).rotateX(-Math.PI / 2);
		const lp = land.attributes.position;
		const splat = new Float32Array(lp.count * 3);
		const lc = new Float32Array(lp.count * 3);
		for (let i = 0; i < lp.count; i++) {
			const x = lp.getX(i) + cx;
			const z = lp.getZ(i) + cz;
			const dx = Math.max(0, Math.max(-x, x - W));
			const dz = Math.max(0, Math.max(-(z + D), z));
			const d = Math.hypot(dx, dz);
			const hill = d < 4 ? 0 : Math.min(1, (d - 4) / 60) * (6 * Math.sin(x * 0.018 + 1.3) * Math.cos(z * 0.021) + 4 * Math.sin(x * 0.041 + z * 0.033) + 5);
			// Well inside the field the battle's own ground covers it: keep it below the river beds.
			const inside = Math.min(x, W - x, -z, D + z);
			lp.setY(i, inside > 8 ? -4 : -0.3 + Math.max(0, hill));
			const v = 0.62 + rnd() * 0.08;
			lc[i * 3] = v * 0.9;
			lc[i * 3 + 1] = v;
			lc[i * 3 + 2] = v * 0.85;
		}
		land.setAttribute('splat', new T.BufferAttribute(splat, 3));
		land.setAttribute('color', new T.BufferAttribute(lc, 3));
		land.computeVertexNormals();
		const lm = new T.Mesh(land, splatMaterial(9));
		lm.position.set(cx, 0, cz);
		lm.receiveShadow = true;
		group.add(lm);
		// Forest belt just beyond the field's edge, thinning out with distance.
		const belt = [];
		for (let i = 0; i < (quality === 'high' ? 900 : 420); i++) {
			const side = rnd();
			const depth = 6 + Math.pow(rnd(), 1.6) * 90;
			let x;
			let z;
			if (side < 0.3) { x = rnd() * (W + 2 * depth) - depth; z = -depth; } else if (side < 0.6) { x = rnd() * (W + 2 * depth) - depth; z = D + depth; } else if (side < 0.8) { x = -depth; z = rnd() * D; } else { x = W + depth; z = rnd() * D; }
			if (rnd() < 0.35) continue;
			const wx = x;
			const wz = -z;
			const dd = Math.hypot(Math.max(0, Math.max(-wx, wx - W)), Math.max(0, Math.max(-(wz + D), wz)));
			const hill = dd < 4 ? 0 : Math.min(1, (dd - 4) / 60) * (6 * Math.sin(wx * 0.018 + 1.3) * Math.cos(wz * 0.021) + 4 * Math.sin(wx * 0.041 + wz * 0.033) + 5);
			belt.push([x, -0.3 + Math.max(0, hill), z, 1.0 + rnd() * 0.9, rnd()]);
		}
		forest(group, belt, false);
		// Mountains on the horizon, softened by the fog.
		const mount = new T.ConeGeometry(1, 1, 7, 3);
		const mp = mount.attributes.position;
		for (let i = 0; i < mp.count; i++) if (mp.getY(i) < 0.49) { const k = 0.85 + rnd() * 0.3; mp.setX(i, mp.getX(i) * k); mp.setZ(i, mp.getZ(i) * k); }
		mount.computeVertexNormals();
		const mm = new T.InstancedMesh(mount, new T.MeshLambertMaterial({ color: 0x6d7a86, flatShading: true }), 26);
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
		for (let i = 0; i < 16; i++) {
			const s = new T.Sprite(new T.SpriteMaterial({ map: ct, transparent: true, depthWrite: false, opacity: 0.75 + rnd() * 0.2, fog: false }));
			const w = 160 + rnd() * 200;
			s.scale.set(w, w * 0.5, 1);
			s.position.set(cx + (rnd() - 0.5) * 1100, 230 + rnd() * 140, cz + (rnd() - 0.5) * 1100);
			s.renderOrder = -5;
			group.add(s);
			clouds.push({ s, v: 2 + rnd() * 3, x0: cx });
		}
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
		tex, glow, envMap, dressTerrain, surroundings, foliage, forest, rockGeometry, water, worldUv, burst, emit, trail, decal,
		update(dt, t) {
			time.t = t;
			time.u.value = t;
			if (!scene || !camera) return;
			stepParticles(dt);
			stepBolts(dt);
			stepDecals(dt);
			for (const c of clouds) { c.s.position.x += c.v * dt; if (c.s.position.x > c.x0 + 650) c.s.position.x -= 1300; }
		},
		stats: () => ({ particles: P.n, decals: decals.length, bolts: bolts.length }),
	};
})();
if (typeof module !== 'undefined') module.exports = DotGfx;
