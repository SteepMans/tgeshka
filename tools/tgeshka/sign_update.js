// This file is part of TGeshka, an unofficial client
// based on Telegram Desktop (GPLv3).
//
// Writes update-manifest.json for a release package, the format of
// SteepMans/api-gateway docs/update-v1.md (see docs/updates.md here):
//
//   node sign_update.js <package.zip> <version> [min_version] [notes file]
//
// The signature is Ed25519 over the 32 raw bytes of the package sha256.
// The private key (PEM) comes from the TGESHKA_UPDATE_KEY environment
// variable and is never printed.
'use strict';

const crypto = require('crypto');
const fs = require('fs');
const path = require('path');

const VERSION_RE = /^\d{1,6}(\.\d{1,6}){0,3}(-[0-9A-Za-z.]{1,16})?$/;
const MAX_NOTES = 16 * 1024;

function fail(text) {
  console.error(`sign_update: ${text}`);
  process.exit(1);
}

const [packagePath, version, minVersion, notesPath] = process.argv.slice(2);
if (!packagePath || !version) {
  fail('usage: sign_update.js <package.zip> <version> [min_version] [notes file]');
}
if (!VERSION_RE.test(version)) fail(`bad version ${version}`);
if (minVersion && !VERSION_RE.test(minVersion)) fail(`bad min_version ${minVersion}`);

const pem = process.env.TGESHKA_UPDATE_KEY;
if (!pem) fail('TGESHKA_UPDATE_KEY is not set');
const key = crypto.createPrivateKey(pem);
if (key.asymmetricKeyType !== 'ed25519') fail('the key is not Ed25519');

const bytes = fs.readFileSync(packagePath);
const digest = crypto.createHash('sha256').update(bytes).digest();
const signature = crypto.sign(null, digest, key);

// Self-check with the public half before anything is published.
if (!crypto.verify(null, digest, crypto.createPublicKey(key), signature)) {
  fail('signature self-check failed');
}

let notes = '';
if (notesPath && fs.existsSync(notesPath)) {
  notes = fs.readFileSync(notesPath, 'utf8').slice(0, MAX_NOTES);
}

const manifest = {
  platform: 'desktop',
  version,
  sha256: digest.toString('hex'),
  size: bytes.length,
  signature: signature.toString('base64'),
  package: path.basename(packagePath),
};
if (minVersion) manifest.min_version = minVersion;
if (notes) manifest.notes = notes;

const out = path.join(path.dirname(packagePath), 'update-manifest.json');
fs.writeFileSync(out, JSON.stringify(manifest, null, 2) + '\n');
console.log(`update-manifest.json: ${version}, ${bytes.length} bytes, sha256 ${manifest.sha256}`);
