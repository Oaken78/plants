// Turns web/index.html into the page used for the hosted (Claude artifact) version of the demo:
// drops the document wrapper the host adds itself, and loads three.js from jsDelivr instead of node_modules.
//   node tools/make-artifact-page.mjs <output.html>
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const out = process.argv[2];
if (!out) throw new Error('usage: node tools/make-artifact-page.mjs <output.html>');

const THREE_VERSION = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8')).dependencies.three;
const cdn = `https://cdn.jsdelivr.net/npm/three@${THREE_VERSION}`;

const html = readFileSync(join(root, 'web', 'index.html'), 'utf8');
const head = html.match(/<head>([\s\S]*?)<\/head>/)[1];
const body = html.match(/<body>([\s\S]*?)<\/body>/)[1];
const page = (head.replace(/\s*<meta [^>]*>/g, '') + '\n' + body)
  .replace('../node_modules/three/build/three.module.js', `${cdn}/build/three.module.js`)
  .replace('../node_modules/three/examples/jsm/', `${cdn}/examples/jsm/`)
  .trim() + '\n';
if (page.includes('node_modules')) throw new Error('page still references node_modules');
mkdirSync(dirname(out), { recursive: true });
writeFileSync(out, page);
console.log(`wrote ${out} (three ${THREE_VERSION} from jsDelivr)`);
