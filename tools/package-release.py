"""Create an explicit, private-data-free public source folder and Windows alpha ZIP."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = '0.2.0-alpha.2'
DIST = ROOT / 'dist'
REPO = DIST / 'RE4Craft-github'
PACKAGE = DIST / f'RE4Craft-{VERSION}-Windows.zip'

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def checked(path):
    if not path.is_file() or path.is_symlink():
        raise SystemExit(f'Missing/nonregular required release input: {path.relative_to(ROOT)}')
    return path

def tree(folder, suffixes):
    return [p for p in (ROOT / folder).rglob('*') if p.is_file()
            and not p.is_symlink() and p.suffix.lower() in suffixes
            and not any(x in {'.gradle', 'build', 'run', '__pycache__', '.git'} for x in p.relative_to(ROOT / folder).parts)]

selected = [ROOT / name for name in (
    'README.md','INSTALL.md','PUBLISH.md','RELEASE_NOTES.md','CHANGELOG.md',
    'LICENSE','THIRD-PARTY-NOTICES.md','RE4Craft.ini',
    'docs/PROTOCOL.md','docs/RELEASE_VALIDATION.md',
    'tools/common.ps1','tools/install.ps1','tools/uninstall.ps1','tools/inspect-install.ps1',
    'tools/fetch-asi-loader.ps1','tools/fetch-fabric.ps1','tools/build-release.ps1',
    'tools/prepare-release-dependencies.ps1','tools/package-release.py',
    'tools/probe-native-boxes.py','tools/probe_loot.py','tools/qa-loot.ps1',
    'protocol/re4craft_protocol.h',
    'minecraft/build.gradle','minecraft/settings.gradle','minecraft/gradle.properties',
    'minecraft/gradlew','minecraft/gradlew.bat','minecraft/LICENSE-SkyCraft',
    'minecraft/gradle/wrapper/gradle-wrapper.jar','minecraft/gradle/wrapper/gradle-wrapper.properties',
)]
selected += tree('native', {'.cpp','.hpp','.h','.c','.cmd','.def','.txt'})
selected += tree('minecraft/src', {'.java','.json','.toml','.mcmeta','.txt'})
selected += tree('release', {'.ps1','.cmd'})
selected += tree('tests', {'.cpp','.hpp','.h'})
selected += [ROOT / 'tests/fake_minecraft.py']
selected += [ROOT / 'licenses' / name for name in (
    'SkyCraft-MIT.txt','re4_tweaks-License.txt','Ultimate-ASI-Loader-MIT.txt',
    'BakingLab-ACES-MIT.txt','Gradle-Apache-2.0.txt','MinHook-LICENSE.txt','ASM-BSD-3-Clause.txt')]
selected = sorted(set(map(checked, selected)))
files = {p.relative_to(ROOT).as_posix():p.read_bytes() for p in selected}
files['.gitignore'] = b'/build/\n/dist/\n/.tools/\n/.instance/\n/.dev-instance/\n/.re4craft/\n/.release-source-manifest.json\n/payload/\n/minecraft/.gradle/\n/minecraft/build/\n/minecraft/run/\n/AGENTS.md\n/docs/PROJECT_CHECKPOINT.md\n*.log\n*.obj\n*.pdb\n*.lib\n*.exp\n*.ilk\n__pycache__/\n'
private_roots={'.tools','.instance','.dev-instance','.halo-instance','.ultrakill-instance','.beamng-instance','saves','backups','reference'}
assert not any(private_roots.intersection(Path(name).parts) for name in files)
# Refuse to remove or overwrite extra files in a previously prepared source
# folder: users may have started their own repository or edited its contents.
DIST.mkdir(exist_ok=True)
REPO.mkdir(exist_ok=True)
stamp=REPO / '.release-source-manifest.json'
old=json.loads(stamp.read_text('utf-8')) if stamp.exists() else {}
for name, data in files.items():
    target=REPO / name
    if target.exists() and (name not in old or digest(target)!=old[name]):
        raise SystemExit(f'Prepared source modified/unowned; preserved: {name}')
for name, data in files.items():
    target=REPO / name
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(data)
manifest={name:hashlib.sha256(data).hexdigest() for name,data in files.items()}
stamp.write_text(json.dumps(manifest,indent=2),encoding='utf-8')

payload = {
    'RE4Craft.asi':checked(ROOT / 'build/native/RE4Craft.asi'),
    'RE4Craft.ini':checked(ROOT / 'RE4Craft.ini'),
    'winmm.dll':checked(ROOT / '.tools/asi-loader/winmm.dll'),
    're4craft-fabric.jar':checked(ROOT / f'minecraft/build/libs/re4craft-fabric-{VERSION}.jar'),
    'fabric-api.jar':checked(ROOT / '.tools/fabric-api/fabric-api-0.161.0+26.3.jar'),
}
assert digest(payload['winmm.dll']) == 'ec2f4824eca58dd40f425756a4a7cec77b8e381f21d11d7c846ec4b339b617ab'
assert digest(payload['fabric-api.jar']) == '86f16178a3cecc887a85a4cfe9a79d92fa7341d8f39b5951a4d6ad800ab657a6'
launch=json.loads(checked(ROOT / 'build/release-preparation/fabric-launch.json').read_text('utf-8-sig'))
assert launch['MinecraftVersion']=='26.3' and launch['FabricLoader']=='0.19.5'
for lib in launch['Libraries']:
    path=Path(lib['Path'])
    assert path.parts[0]=='fabric-libraries' and len(path.parts)==2 and path.suffix=='.jar'
    local=checked(ROOT / '.tools/release-fabric' / path.name)
    assert digest(local)==lib['Sha256'], path.name
    payload[lib['Path']]=local
data={f'payload/{name}':path.read_bytes() for name,path in payload.items()}
data['payload/fabric-launch.json']=json.dumps(launch,indent=2).encode('utf-8')
payload_manifest=dict(SchemaVersion=1,Version=VERSION,Files=[dict(Path=name.removeprefix('payload/'),Sha256=hashlib.sha256(value).hexdigest()) for name,value in sorted(data.items())])
data['payload/manifest.json']=json.dumps(payload_manifest,indent=2).encode('utf-8')
for name in ('README.md','INSTALL.md','LICENSE','THIRD-PARTY-NOTICES.md','CHANGELOG.md','RELEASE_NOTES.md','docs/RELEASE_VALIDATION.md'):
    data[name]=files[name]
for name,value in files.items():
    if name.startswith('release/') and name.endswith('.ps1') and '/tests/' not in name:
        data[name]=value
    elif name.startswith('release/') and name.endswith('.cmd'):
        data[Path(name).name]=value
    elif name.startswith('licenses/') or name in {'tools/common.ps1','tools/install.ps1','tools/uninstall.ps1'}:
        data[name]=value
data['PACKAGE-SHA256.json']=json.dumps({name:hashlib.sha256(value).hexdigest() for name,value in sorted(data.items())},indent=2).encode('utf-8')
with zipfile.ZipFile(PACKAGE,'w',zipfile.ZIP_DEFLATED) as archive:
    for name,value in sorted(data.items()): archive.writestr(name,value)
with zipfile.ZipFile(PACKAGE) as archive:
    assert archive.testzip() is None
    assert len(archive.namelist())==len(set(archive.namelist()))
    for name,value in data.items(): assert archive.read(name)==value
sha=digest(PACKAGE)
(DIST / (PACKAGE.name+'.sha256')).write_text(sha+'  '+PACKAGE.name+'\n',encoding='ascii')
sourcezip=DIST / f'RE4Craft-{VERSION}-Source.zip'
with zipfile.ZipFile(sourcezip,'w',zipfile.ZIP_DEFLATED) as archive:
    for name,value in sorted(files.items()): archive.writestr('RE4Craft/'+name,value)
report=dict(version=VERSION,sourceFolder=str(REPO),sourceFiles=len(files),windowsPackage=str(PACKAGE),windowsFiles=len(data),sha256=sha,
            binaries={name:digest(path) for name,path in payload.items()},originalGameFilesIncluded=False,userSavesIncluded=False,published=False)
(ROOT / 'build/minecraft-background-release-package-proof.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k!='binaries'},indent=2))
