'use strict';

// electron-builder only signs with identities macOS already trusts, so release.js
// hands a self-signed certificate over as ad-hoc ("-") and this hook signs with it.
// The app's designated requirement is what Squirrel.Mac checks each update against.
// It accepts this certificate or a Developer ID build with the same bundle ID, so a
// later switch to Apple signing still updates existing installs.
const { execFileSync } = require('node:child_process');
const { signApp } = require('@electron/osx-sign');

const APP_ID = require('../package.json').build.appId;
const DEVELOPER_ID = 'anchor apple generic and certificate 1[field.1.2.840.113635.100.6.2.6] exists' +
  ' and certificate leaf[field.1.2.840.113635.100.6.1.13] exists';

function identityHash(keychain) {
  const output = execFileSync('/usr/bin/security', ['find-identity', '-p', 'codesigning', keychain], { encoding: 'utf8' });
  const hashes = [...new Set(output.match(/\b[0-9A-F]{40}\b/g) || [])];
  if (hashes.length !== 1) throw new Error(`Expected one signing identity in the release keychain, found ${hashes.length}.`);
  return hashes[0];
}

function designatedRequirement(certificateHash) {
  return `=designated => identifier "${APP_ID}" and ` +
    `(certificate leaf = H"${certificateHash.toLowerCase()}" or (${DEVELOPER_ID}))`;
}

async function sign(options) {
  if (!options.keychain) throw new Error('Set CSC_LINK and CSC_KEY_PASSWORD to sign the Mac app.');
  const identity = identityHash(options.keychain);
  const optionsForFile = options.optionsForFile;
  await signApp({
    ...options,
    identity,
    identityValidation: false,
    optionsForFile: file => file === options.app
      ? { ...optionsForFile(file), requirements: designatedRequirement(identity) }
      : optionsForFile(file)
  });
}

module.exports = sign;
module.exports.designatedRequirement = designatedRequirement;
