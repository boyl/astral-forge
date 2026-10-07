"""验证预览归档的哈希、文件集合、默认开关与禁止分发的负载，仅使用标准库。"""
import argparse, hashlib, json, re, zipfile
from pathlib import Path, PurePosixPath

def digest(data):
    return hashlib.sha256(data).hexdigest().upper()

def audit(manifest_path):
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    expected={f"aamod-{manifest['version']}-source.zip",f"aamod-{manifest['version']}-runtime.zip"}
    if len(manifest['archives'])!=2 or {e['file'] for e in manifest['archives']}!=expected:
        raise ValueError('必须同时提供且仅提供源码与运行归档')
    results=[]
    for item in manifest['archives']:
        name=item['file']
        if Path(name).name!=name: raise ValueError('归档清单包含目录路径')
        path=manifest_path.parent/name
        if digest(path.read_bytes())!=item['sha256']:raise ValueError(f'归档哈希不符：{name}')
        with zipfile.ZipFile(path) as z:
            if z.testzip() is not None:raise ValueError('归档 CRC 失败')
            names=z.namelist()
            if len(names)!=len(set(names)):raise ValueError('归档路径重复')
            for n in names:
                parts=PurePosixPath(n).parts
                if n.startswith('/') or '\\' in n or ':' in n or '..' in parts:raise ValueError('归档路径越界')
                if re.search(r'(^|/)(work|SavedGames|research|astral-ascent-re|\.git|__pycache__|\.build)(/|$)|\.(sav|dmp|bin|dat)$',n,re.I):raise ValueError(f'禁止负载：{n}')
                if n.lower().endswith(('version.dll','winmmhooked.dll','d3d11hooked.dll','astral_bd_overlay.dll')):raise ValueError('禁止分发系统或 BD DLL')
            kind='runtime' if name.endswith('-runtime.zip') else 'source'
            entries=manifest[kind+'Entries']
            if set(names)!={item['name'] for item in entries}:raise ValueError('文件集合与清单不符')
            for entry in entries:
                if digest(z.read(entry['name']))!=entry['sha256']:raise ValueError(f"文件哈希不符：{entry['name']}")
            for name in ('include/aamod/native_images.h','mods/native_image_sample/mod.cpp','mods/native_image_sample/assets/replacement.png','docs/SDK稳定规则.md','THIRD_PARTY_NOTICES.md','schemas/mod.schema.json'):
                if name not in names:raise ValueError(f'缺少交付文件：{name}')
            for plugin in ('heal_once','native_image_sample'):
                if json.loads(z.read(f'mods/{plugin}/mod.json'))['enabled'] is not False:raise ValueError('可变更示例被默认启用')
            content_phase='include/aamod/gameplay_events.h' in names
            if content_phase:
                for required in ('include/aamod/menu.hpp','include/aamod/data.h','include/aamod/catalog.h','include/aamod/equipment_commands.h','mods/build_selector/mod.cpp','mods/event_responder/mod.cpp'):
                    if required not in names:raise ValueError(f'内容闭环交付文件缺失：{required}')
                for plugin in ('content_smoke','build_selector','native_event_watch','event_responder'):
                    if json.loads(z.read(f'mods/{plugin}/mod.json'))['enabled'] is not False:raise ValueError('内容或事件示例被默认启用')
                license_path='licenses/nlohmann-json/LICENSE.MIT' if kind=='runtime' else 'src/vendor/nlohmann/LICENSE.MIT'
                if license_path not in names:raise ValueError('缺少 JSON 依赖完整许可')
                if digest(z.read(license_path))!='46A65CFFD1EA955132D95A8DD921640714A8D6B537D2E4E482D31145AE95B603':raise ValueError('JSON 依赖许可与审核基线不符')
            if kind=='runtime':
                build=json.loads(z.read('out/build.json'))
                for name in ('aamod_core.dll','winmm.dll','d3d11.dll','sdk-host.exe'):
                    if digest(z.read('out/'+name))!=build['files'][name]:raise ValueError('构建与归档不一致')
                if build['files']['aamod_core.dll']!=manifest['coreSha256']:raise ValueError('核心候选不一致')
                for plugin in ('hello','template','diagnostics','state_watch','event_watch','heal_once','image_sample','native_image_sample'):
                    mod=json.loads(z.read(f'mods/{plugin}/mod.json'))
                    if f"mods/{plugin}/{mod['entry']}" not in names:raise ValueError('示例未编译')
                if content_phase:
                    for plugin in ('data_sample','equipment_watch','content_smoke','build_selector','native_event_watch','event_responder'):
                        mod=json.loads(z.read(f'mods/{plugin}/mod.json'))
                        if f"mods/{plugin}/{mod['entry']}" not in names:raise ValueError('内容闭环示例未编译')
            results.append({'archive':path.name,'files':len(names),'passed':True})
    text='\n'.join(f"{e['name']}:{e['sha256']}" for e in manifest['sourceEntries'])
    if digest(text.encode())!=manifest['sourceSnapshotSha256']:raise ValueError('源码快照标识不一致')
    return results

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('manifest',type=Path);p.add_argument('--report',type=Path)
    args=p.parse_args();results=audit(args.manifest)
    if args.report:
        with args.report.open('x',encoding='utf-8') as stream:json.dump(results,stream,ensure_ascii=False,indent=2)
    print('PASS: 归档与逐文件哈希、源码快照、SDK 示例、默认开关及负载审计')
if __name__=='__main__':main()
