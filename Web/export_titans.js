'use strict';
// Writes the titan art from titans.js as standalone SVG files: node Web/export_titans.js <out dir>
//   <id>.svg       portrait card (static)
//   <id>.anim.svg  looping showcase: idle, attack, ability 1, ability 2 (CSS animation, plays in any browser)
const fs = require('fs');
const path = require('path');
const DotTitans = require('./titans.js');

const out = process.argv[2] || path.join(__dirname, 'dist', 'titans');
fs.mkdirSync(out, { recursive: true });
for (const id of DotTitans.ids()) {
	const name = id.replace('titan.', 'titan_');
	fs.writeFileSync(path.join(out, name + '.svg'), DotTitans.standalone(id));
	fs.writeFileSync(path.join(out, name + '.anim.svg'), DotTitans.standalone(id, { loop: true }));
}
for (const f of fs.readdirSync(out).sort()) console.log(f, fs.statSync(path.join(out, f)).size);
