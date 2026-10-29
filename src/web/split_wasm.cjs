const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [input, destination] = process.argv.slice(2);
if (!input || !destination) throw new Error('Usage: node split_wasm.cjs input.wasm output-directory');
fs.mkdirSync(destination, {recursive: true});
const size = fs.statSync(input).size;
const manifest = {format: 1, size, sha256: '', parts: []};
const digest = crypto.createHash('sha256');
const fd = fs.openSync(input, 'r');
try {
  let offset = 0;
  while (offset < size) {
    const bytes = Buffer.alloc(Math.min(20 * 1024 * 1024, size - offset));
    let read = 0;
    while (read < bytes.length) {
      const count = fs.readSync(fd, bytes, read, bytes.length - read, offset + read);
      if (!count) throw new Error('Unexpected end of WASM');
      read += count;
    }
    const file = `CardWirthPy_C_All.wasm.${String(manifest.parts.length).padStart(3, '0')}.part`;
    fs.writeFileSync(path.join(destination, file), bytes, {flag: 'wx'});
    manifest.parts.push({file, size: bytes.length, sha256: crypto.createHash('sha256').update(bytes).digest('hex')});
    digest.update(bytes);
    offset += bytes.length;
  }
} finally { fs.closeSync(fd); }
manifest.sha256 = digest.digest('hex');
fs.writeFileSync(path.join(destination, 'wasm-parts.json'), JSON.stringify(manifest, null, 2), {flag: 'wx'});
console.log(`WASM: ${size} bytes, ${manifest.parts.length} parts (max 20 MiB)`);
