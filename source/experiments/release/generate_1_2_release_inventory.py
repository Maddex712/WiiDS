"""Prepare local updater metadata for an audited WiiDS 1.2 APK/source pair."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PRIVATE = Path('M:/WiiDS-release-wrap-2026-11-08')
PACKAGE = 'io.github.maddex712.wiids'
OWNER = '72eef06b956bc1c6848a09bf4956b158095736a6e81f43131cf93cb30788345f'
REPOSITORY = 'https://github.com/Maddex712/WiiDS'
PARSER = 'reference/driftdroid/android/app/src/main/kotlin/com/driftdroid/android/WiiDsUpdateRelease.kt'
METADATA_LIMIT = 512 * 1024
APK_LIMIT = 2 * 1024**3


def digest(data):
    return hashlib.sha256(data).hexdigest()


def file_record(path):
    with path.open('rb') as stream:
        return {'bytes': path.stat().st_size, 'sha256': hashlib.file_digest(stream, 'sha256').hexdigest()}


def json_bytes(data):
    return (json.dumps(data, indent=2, ensure_ascii=True) + '\n').encode('utf-8')


def write_exact(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise ValueError('Use a new output directory for a different release pair: ' + path.name)
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as stream:
        stream.write(data)


def members(archive):
    records = {}
    for info in archive.infolist():
        name = info.filename
        path = PurePosixPath(name)
        if (name in records or '\\' in name or ':' in name or path.is_absolute() or
                any(part in ('', '.', '..') for part in name.removesuffix('/').split('/'))):
            raise ValueError('Duplicate or unsafe archive member: ' + name)
        with archive.open(info) as stream:
            records[name] = {'bytes': info.file_size, 'sha256': hashlib.file_digest(stream, 'sha256').hexdigest()}
    return records


def apk_metadata(apk, build_tools):
    badging = subprocess.check_output([str(build_tools / 'aapt.exe'), 'dump', 'badging', str(apk)], text=True)
    cert = subprocess.check_output([str(build_tools / 'apksigner.bat'), 'verify', '--print-certs', str(apk)], text=True)
    match = re.search(r"^package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'", badging, re.M)
    signers = re.findall(r'^Signer #\d+ certificate SHA-256 digest:\s*([a-fA-F0-9]{64})\s*$', cert, re.M)
    if (not match or match[1] != PACKAGE or match[3] != '1.2' or 'application-debuggable' in badging or
            [x.lower() for x in signers] != [OWNER] or not 1 <= int(match[2]) <= 2**31 - 1):
        raise ValueError('Expected a signed, nondebuggable WiiDS 1.2 APK with a valid Android versionCode')
    return int(match[2])


def verify_parser(output, parser_bytes, inventory, version_code, java):
    """Run the actual APK-bound Kotlin source with a synthetic local GitHub listing."""
    fixture = output / 'verification'
    fixture.mkdir(exist_ok=True)
    cache = Path.home() / '.gradle/caches/modules-2/files-2.1'

    def jar(group, name, version):
        return next((cache / group / name / version).rglob('*.jar'))

    stdlib = jar('org.jetbrains.kotlin', 'kotlin-stdlib', '1.9.24')
    org_json = jar('org.json', 'json', '20240303')
    compiler = [jar('org.jetbrains.kotlin', name, '1.9.24') for name in
                ('kotlin-compiler-embeddable', 'kotlin-script-runtime', 'kotlin-daemon-embeddable')]
    compiler += [stdlib, jar('org.jetbrains.intellij.deps', 'trove4j', '1.0.20200330'), jar('org.jetbrains', 'annotations', '13.0')]
    apk_name = 'WiiDS-1.2-arm64.apk'
    tag = inventory['tag']
    local_assets = {apk_name: inventory['artifacts'][apk_name],
                    'RELEASE-INVENTORY.json': file_record(output / 'RELEASE-INVENTORY.json')}
    listing = {'draft': False, 'prerelease': False, 'tag_name': tag, 'html_url': REPOSITORY + '/releases/tag/' + tag,
               'assets': [{'id': index, 'name': name, 'state': 'uploaded', 'size': record['bytes'],
                           'digest': 'sha256:' + record['sha256'],
                           'browser_download_url': f'{REPOSITORY}/releases/download/{tag}/{name}'}
                          for index, (name, record) in enumerate(local_assets.items(), 1)]}
    write_exact(fixture / 'SYNTHETIC-GITHUB-LISTING.json', json_bytes(listing))
    write_exact(fixture / 'WiiDsUpdateRelease.kt', parser_bytes)
    write_exact(fixture / 'Verify.kt', b'''package com.driftdroid.android
import java.io.File
import org.json.JSONObject

fun main(args: Array<String>) {
    val raw = File(args[0]).readText()
    val data = File(args[1]).readBytes()
    val listing = WiiDsUpdateRelease.parseListing(raw)
    val result = WiiDsUpdateRelease.bindInventory(listing, data)
    check(result.tag == args[2] && result.version == "1.2" && result.versionCode == args[3].toLong())
    check(result.apk.name == "WiiDS-1.2-arm64.apk" && result.apk.sha256 == args[4])
    check(result.newerThan(result.versionCode - 1) && !result.newerThan(result.versionCode))
    check(WiiDsUpdateRelease.restore(result.toJson().toString()) == result)
    check(runCatching { WiiDsUpdateRelease.bindInventory(listing, data + byteArrayOf(0)) }.isFailure)
    val draft = JSONObject(raw).put("draft", true).toString()
    check(runCatching { WiiDsUpdateRelease.parseListing(draft) }.isFailure)
    println("6 actual Kotlin updater checks passed with local synthetic listing")
}
''')
    classes = fixture / 'classes'
    subprocess.run([str(java), '-cp', os.pathsep.join(map(str, compiler)), 'org.jetbrains.kotlin.cli.jvm.K2JVMCompiler',
                    '-no-stdlib', '-no-reflect', '-jvm-target', '17', '-classpath', os.pathsep.join(map(str, (stdlib, org_json))),
                    '-d', str(classes), str(fixture / 'WiiDsUpdateRelease.kt'), str(fixture / 'Verify.kt')], check=True)
    result = subprocess.check_output([str(java), '-cp', os.pathsep.join(map(str, (classes, stdlib, org_json))),
                                     'com.driftdroid.android.VerifyKt', str(fixture / 'SYNTHETIC-GITHUB-LISTING.json'),
                                     str(output / 'RELEASE-INVENTORY.json'), tag, str(version_code),
                                     inventory['artifacts'][apk_name]['sha256']], text=True).strip()
    write_exact(fixture / 'parser-verification.json', json_bytes({'status': 'PASS', 'checks': 6, 'synthetic_listing': True,
                'parser_source_sha256': digest(parser_bytes), 'result': result}))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apk', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--private-root', type=Path, default=PRIVATE)
    p.add_argument('--tag', default='v1.2.0')
    p.add_argument('--verify-parser', action='store_true')
    p.add_argument('--java', type=Path, default=Path('C:/Program Files/Microsoft/jdk-21.0.12.101-hotspot/bin/java.exe'))
    a = p.parse_args()
    output = a.output.resolve()
    if not output.is_relative_to(a.private_root.resolve()) or output == a.private_root.resolve():
        p.error('Choose an output folder below the private release work directory')
    if not re.fullmatch(r'v1\.2(?:\.0){0,2}', a.tag):
        p.error('The planned tag must match app version 1.2')
    apk = a.apk.resolve()
    source = a.source.resolve()
    apk_record, source_record = file_record(apk), file_record(source)
    if not 1 <= apk_record['bytes'] <= APK_LIMIT:
        raise ValueError('APK is outside the updater size limit')
    code = apk_metadata(apk, ROOT / '.tools/android-sdk/build-tools/34.0.0')
    with zipfile.ZipFile(source) as z:
        manifest = json.loads(z.read('SOURCE-MANIFEST.json'))
        actual = members(z)
        actual.pop('SOURCE-MANIFEST.json')
        expected = {name: {k: entry[k] for k in ('bytes', 'sha256')} for name, entry in manifest['files'].items()}
        if manifest['candidate_apk']['sha256'] != apk_record['sha256'] or actual != expected:
            raise ValueError('Source archive is not bound byte-for-byte to this APK')
        parser = z.read(PARSER)
    with zipfile.ZipFile(apk) as z:
        apk_members = members(z)
        presentation_raw = z.read('assets/presentation/manifest.json')
        presentation = json.loads(presentation_raw)
    for item in presentation['files']:
        if apk_members.get('assets/' + item['asset']) != {key: item[key] for key in ('bytes', 'sha256')}:
            raise ValueError('Presentation asset hash differs from its manifest: ' + item['asset'])
    if digest(presentation_raw) != manifest['presentation_assets']['manifest_sha256']:
        raise ValueError('Source/APK presentation manifests differ')
    native = {name: record for name, record in apk_members.items() if name.startswith('lib/') and name.endswith('.so')}
    if not native or any(not name.startswith('lib/arm64-v8a/') for name in native):
        raise ValueError('Expected only ARM64 native libraries')
    if {name: item['sha256'] for name, item in native.items()} != manifest['candidate_apk']['native_libraries']:
        raise ValueError('Source manifest and APK native inventories differ')
    apk_name, source_name = 'WiiDS-1.2-arm64.apk', 'WiiDS-1.2-source.zip'
    assets = {apk_name: apk_record, source_name: source_record}
    source_summary = {'archive_entries': len(actual) + 1, 'selected_inputs': len(actual),
                      'coverage_omissions': len(manifest['coverage_omissions']), 'all_manifest_bytes_verified': True,
                      'paused_files_verified_unchanged': manifest['paused_files_verified_unchanged'],
                      'runtime_asset_recipe_in_archive': 'runtime_asset_kit' in manifest,
                      'clean_machine_rebuild_claimed': False}
    inventory = {'package': PACKAGE, 'version': '1.2', 'versionCode': code, 'tag': a.tag,
                 'signer_certificate_sha256': OWNER, 'debuggable': False, 'single_apk': True,
                 'artifacts': assets, 'source': source_summary, 'native_libraries': native}
    audit = {'schema': 1, 'apk_sha256': apk_record['sha256'], 'status': 'pass', 'findings': [],
             'package': PACKAGE, 'version': '1.2', 'versionCode': code, 'signer_certificate_sha256': OWNER,
             'debuggable': False, 'inventory': [{'path': n, **record} for n, record in sorted(apk_members.items())],
             'approved_presentation': {'revision': presentation['revision'], 'files': len(presentation['files']),
                 'bytes': sum(item['bytes'] for item in presentation['files']), 'manifest_sha256': digest(presentation_raw),
                 'all_payload_hashes_verified': True},
             'native_libraries': native, 'source': source_summary,
             'limits': ['Checks cover package identity, signer, archive member bytes and source binding.',
                        'Rights and gameplay acceptance remain separate release review items.']}
    metadata = {'RELEASE-INVENTORY.json': json_bytes(inventory), 'APK-AUDIT.json': json_bytes(audit)}
    if len(metadata['RELEASE-INVENTORY.json']) > METADATA_LIMIT:
        raise ValueError('Release inventory exceeds the updater JSON limit')
    checksums = {**assets, **{name: {'sha256': digest(raw)} for name, raw in metadata.items()}}
    metadata['SHA256SUMS.txt'] = ''.join(f"{entry['sha256']}  {name}\n" for name, entry in sorted(checksums.items())).encode('utf-8')
    metadata['LOCAL-REVIEW-STATUS.json'] = json_bytes({'status': 'PRIVATE_UNPUBLISHED_REVIEW', 'published': False,
         'planned_tag': a.tag, 'apk_sha256': apk_record['sha256'], 'source_sha256': source_record['sha256'],
         'runtime_asset_recipe_in_archive': source_summary['runtime_asset_recipe_in_archive']})
    output.mkdir(parents=True, exist_ok=True)
    for name, original in ((apk_name, apk), (source_name, source)):
        target = output / name
        if not target.exists():
            os.link(original, target)
        if file_record(target) != assets[name]:
            raise ValueError('An existing output artifact differs from the reviewed input: ' + name)
    for name, data in metadata.items():
        write_exact(output / name, data)
    for name, record in checksums.items():
        if file_record(output / name)['sha256'] != record['sha256']:
            raise ValueError('Release checksum readback mismatch: ' + name)
    if a.verify_parser:
        print(verify_parser(output, parser, inventory, code, a.java))
    print(json.dumps({'status': 'PRIVATE_UNPUBLISHED_REVIEW', 'tag': a.tag, 'versionCode': code,
                      'apk_members': len(apk_members), 'source_members': len(actual) + 1,
                      'checksum_entries': len(checksums), 'output': str(output)}))


if __name__ == '__main__':
    main()
