"""对已生成预览包执行独立破坏测试，不修改原交付。"""
import copy, hashlib, importlib.util, json, shutil, sys, tempfile, zipfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('preview_audit',Path(__file__).resolve().parents[1]/'tools/audit-preview.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
original=Path(sys.argv[1]).resolve();baseline=json.loads(original.read_text(encoding='utf-8-sig'))
assert all(result['passed'] for result in module.audit(original))
def rejected(manifest):
    try:module.audit(manifest)
    except ValueError:return
    raise AssertionError('损坏交付被接受')
with tempfile.TemporaryDirectory(prefix='aamod-audit-') as temporary:
    root=Path(temporary);manifest=root/original.name
    for item in baseline['archives']:shutil.copyfile(original.parent/item['file'],root/item['file'])
    def save(data):manifest.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
    missing=copy.deepcopy(baseline);missing['archives'].pop();save(missing);rejected(manifest)
    corrupted=copy.deepcopy(baseline);corrupted['archives'][0]['sha256']='0'*64;save(corrupted);rejected(manifest)
    for entry in ('work/private.sav','../escape.txt','harmless-extra.txt'):
        archive=baseline['archives'][1]['file'];shutil.copyfile(original.parent/archive,root/archive)
        with zipfile.ZipFile(root/archive,'a') as z:z.writestr(entry,b'fixture')
        changed=copy.deepcopy(baseline);changed['archives'][1]['sha256']=hashlib.sha256((root/archive).read_bytes()).hexdigest().upper()
        save(changed);rejected(manifest)
    save(baseline);shutil.copyfile(original.parent/baseline['archives'][1]['file'],root/baseline['archives'][1]['file'])
    changed=copy.deepcopy(baseline);changed['sourceEntries'][0]['sha256']='0'*64;save(changed);rejected(manifest)
print('PASS: 缺失归档、坏哈希、存档负载、越界路径、额外文件和逐文件哈希漂移均拒绝')
