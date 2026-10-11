"""只读契约比较、候选证据关联与每次验收的存档保护基线。"""
import hashlib
import json
import re
import stat
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

CAPABILITIES = ('state', 'equipment', 'checkpoint', 'events', 'images', 'damage',
                'crystalPickup', 'abandon', 'defeat', 'victory')
CHECKS = (
    ('offline.contracts', 'offline', '当前 EXE 原生契约完整，扫描器身份已记录'),
    ('offline.tests', 'offline', '受影响契约与宿主测试通过'),
    ('actual.startup', 'actual', '隔离存档重定向已证明，核心与玩家状态就绪'),
    ('actual.equipment', 'actual', '光环应用/恢复与普通技能替换/实际施法'),
    ('actual.recovery', 'actual', '原生伤害事件后受控回血与补法'),
    ('actual.pickup', 'actual', '原生水晶拾取、数值增加及响应'),
    ('actual.endings', 'actual', '分别观察真实死亡、胜利和放弃；未提前重复派发'),
    ('actual.presets', 'actual', '预设保存、恢复及新进程恢复'),
    ('actual.checkpoint', 'actual', '保存派发后重启证明持久化并验证恢复'),
    ('actual.resources', 'actual', '图片实际应用与恢复，保留可见截图'),
    ('actual.saveProtection', 'actual', '本轮开始与结束原存档文件集合和哈希不变'),
)


def now():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read_json(path):
    with Path(path).open('r', encoding='utf-8-sig') as stream:
        return json.load(stream)


def write_new(path, value):
    # 显式创建新报告；不覆盖上一轮证据或手工补充的记录。
    with Path(path).open('x', encoding='utf-8') as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write('\n')


def valid_hash(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def validate_scan(scan):
    if not isinstance(scan, dict): raise ValueError('扫描报告必须是 JSON 对象')
    if scan.get('format') != 1 or scan.get('kind') != 'offline-native-contracts':
        raise ValueError('不是格式 1 的原生契约报告')
    if not isinstance(scan.get('imageMapped'), bool):
        raise ValueError('缺少映像解析状态')
    for key in ('gameSha256', 'coreSha256'):
        if not valid_hash(scan.get(key)): raise ValueError(f'{key} 缺失或无效')
    caps = scan.get('capabilities', {})
    if not isinstance(caps, dict) or any(type(caps.get(key)) is not bool for key in CAPABILITIES):
        raise ValueError('能力清单不完整')
    for key in ('rvas', 'fields'):
        if not isinstance(scan.get(key), dict): raise ValueError(f'{key} 缺失')
        for value in scan[key].values():
            values = value if isinstance(value, list) else [value]
            if not values or any(type(v) is not int or not 0 <= v <= 0xffffffff for v in values):
                raise ValueError(f'{key} 含无效 RVA 或字段偏移')


def compare(before, after):
    validate_scan(before)
    validate_scan(after)
    def changes(key):
        return {name: {'before': before[key].get(name), 'after': after[key].get(name)}
                for name in sorted(before[key].keys() | after[key].keys())
                if before[key].get(name) != after[key].get(name)}
    return {'format': 1, 'kind': 'offline-contract-diff', 'createdAt': now(),
            'gameChanged': before['gameSha256'] != after['gameSha256'],
            'coreChanged': before['coreSha256'] != after['coreSha256'],
            'probeChanged': before.get('probeSha256') != after.get('probeSha256'),
            'regressions': [k for k in CAPABILITIES if before['capabilities'][k] and not after['capabilities'][k]],
            'recoveries': [k for k in CAPABILITIES if not before['capabilities'][k] and after['capabilities'][k]],
            'rvaChanges': changes('rvas'), 'fieldChanges': changes('fields'),
            'limits': '契约定位变化仅是诊断；没有执行游戏，不证明玩法通过。'}


def new_plan(scan):
    validate_scan(scan)
    return {'format': 1, 'kind': 'update-acceptance-plan', 'createdAt': now(),
            'candidate': scan,
            'checks': [{'id': key, 'kind': kind, 'criterion': criterion, 'status': 'pending', 'evidence': []}
                       for key, kind, criterion in CHECKS],
            'limits': '维护者声明关联证据；工具不代替语义判断，不认证多人、控制器或其他游戏版本。'}


def validate_plan(plan):
    if not isinstance(plan, dict): raise ValueError('验收计划必须是 JSON 对象')
    if plan.get('format') != 1 or plan.get('kind') != 'update-acceptance-plan':
        raise ValueError('验收计划格式无效')
    validate_scan(plan['candidate'])
    checks = plan.get('checks', [])
    expected = {key: (kind, criterion) for key, kind, criterion in CHECKS}
    if len(checks) != len(expected) or {item.get('id') for item in checks} != set(expected):
        raise ValueError('必需验收项缺失或重复')
    for item in checks:
        if (item.get('kind'), item.get('criterion')) != expected[item['id']]:
            raise ValueError('验收类型或标准被更改')
        if item.get('status') not in ('pending', 'passed', 'failed', 'unverified'):
            raise ValueError('无效验收状态')


def record(plan, check_id, status, kind, evidence, game_hash, core_hash, observation):
    validate_plan(plan)
    if status not in ('passed', 'failed', 'unverified'): raise ValueError('无效记录状态')
    if not isinstance(observation, str) or not observation.strip(): raise ValueError('必须描述实际观察')
    candidate = plan['candidate']
    if (game_hash, core_hash) != (candidate['gameSha256'], candidate['coreSha256']):
        raise ValueError('证据与计划的游戏/核心身份不同，必须建立新轮次')
    item = next((c for c in plan['checks'] if c['id'] == check_id), None)
    if item is None or item['kind'] != kind: raise ValueError('验收编号或证据层级不匹配')
    path = Path(evidence).resolve(strict=True)
    if not path.is_file() or not path.stat().st_size: raise ValueError('证据必须是非空文件')
    entry = {'path': str(path), 'sha256': digest(path), 'gameSha256': game_hash, 'coreSha256': core_hash,
             'kind': kind, 'recordedAt': now(), 'observation': observation, 'source': 'maintainer-attestation'}
    # 同一项可添加日志、JSON 与截图；重新记录结果不会丢失既有证据。
    item['evidence'].append(entry)
    item['status'] = status


def finish(plan):
    validate_plan(plan)
    issues = []
    candidate = plan['candidate']
    if not candidate['imageMapped'] or not all(candidate['capabilities'].values()):
        issues.append('原生契约未完整定位')
    for item in plan['checks']:
        records = item.get('evidence', [])
        if item['status'] != 'passed' or not records:
            issues.append(f"{item['id']}: 未通过或没有证据")
        for entry in records:
            if (entry.get('gameSha256'), entry.get('coreSha256'), entry.get('kind')) != (
                    candidate['gameSha256'], candidate['coreSha256'], item['kind']):
                issues.append(f"{item['id']}: 候选或证据层级不匹配")
            try:
                if digest(entry['path']) != entry['sha256']: issues.append(f"{item['id']}: 证据已漂移")
            except (OSError, KeyError): issues.append(f"{item['id']}: 证据不可读取")
    return {'format': 1, 'kind': 'update-acceptance-summary', 'createdAt': now(),
            'status': 'incomplete' if issues else 'passed', 'issues': issues,
            'gameSha256': candidate['gameSha256'], 'coreSha256': candidate['coreSha256'],
            'checks': plan['checks'], 'limits': plan.get('limits')}


def save_files(root):
    root = Path(root)
    if root.is_symlink() or getattr(root.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
        raise ValueError('存档根目录含链接')
    root = root.resolve(strict=True)
    if not root.is_dir(): raise ValueError('存档根目录必须是目录')
    result = {}
    # 不跟随符号链接或 junction；不能让存档清单扩张到外部目录。
    import os
    def unreadable(error):
        raise error
    for current, directories, files in os.walk(root, followlinks=False, onerror=unreadable):
        for name in directories + files:
            path = Path(current) / name
            if path.is_symlink() or getattr(path.lstat(), 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                raise ValueError('存档目录含链接，停止建立基线')
        for name in files:
            path = Path(current) / name
            result[path.relative_to(root).as_posix()] = digest(path)
    return result


def capture_saves(root):
    files = save_files(root)
    if not files: raise ValueError('存档基线为空')
    return {'format': 1, 'kind': 'local-save-baseline', 'createdAt': now(),
            'root': str(Path(root).resolve()), 'files': files,
            'private': True, 'limits': '本地保护记录含个人路径，不提交或发布。'}


def verify_saves(baseline):
    if baseline.get('format') != 1 or baseline.get('kind') != 'local-save-baseline' or not baseline.get('files'):
        raise ValueError('存档基线无效或为空')
    previous = baseline['files']
    for name, value in previous.items():
        path = PurePosixPath(name)
        if '\\' in name or ':' in name or path.is_absolute() or '..' in path.parts or not valid_hash(value):
            raise ValueError('存档清单含无效相对路径或哈希')
    current = save_files(baseline['root'])
    added = sorted(current.keys() - previous.keys())
    removed = sorted(previous.keys() - current.keys())
    changed = sorted(k for k in current.keys() & previous.keys() if current[k] != previous[k])
    return {'format': 1, 'kind': 'save-protection-result', 'checkedAt': now(),
            'unchanged': not (added or removed or changed), 'filesChecked': len(previous),
            'added': added, 'removed': removed, 'changed': changed}
