"""游戏更新维护入口：只读扫描、差异、逐轮证据与存档保护；不启动游戏。"""
import argparse
import json
import subprocess
import sys
from pathlib import Path
from maintenance import workflow as w


def scan(game, core, probe):
    executable = game / 'Astral Ascent.exe'
    inputs = [executable, core, probe]
    before = [w.digest(path) for path in inputs]
    process = subprocess.run([str(probe.resolve()), str(executable.resolve())],
                             capture_output=True, timeout=30, encoding='utf-8', errors='strict')
    if process.returncode not in (0, 2):
        raise ValueError(f'原生扫描器异常退出：{process.returncode}')
    try: result = json.loads(process.stdout)
    except json.JSONDecodeError as error:
        raise ValueError(f'原生扫描器没有返回报告：{process.stderr.strip()}') from error
    after = [w.digest(path) for path in inputs]
    if before != after: raise ValueError('扫描期间游戏、核心或扫描器变化；停止记录')
    result.update(gameSha256=before[0], coreSha256=before[1], probeSha256=before[2],
                  createdAt=w.now(), status='contracts-located' if result.get('imageMapped') and
                  all(result.get('capabilities', {}).values()) else 'contracts-incomplete',
                  limits='只读静态契约，不证明已安装、已加载或玩法通过；哈希只绑定本轮证据。')
    w.validate_scan(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    root = Path(__file__).resolve().parents[1]
    p = commands.add_parser('scan', help='扫描当前真实 EXE，核心未被加载')
    p.add_argument('--game-directory', type=Path, required=True)
    p.add_argument('--core', type=Path, default=root / 'out/aamod_core.dll')
    p.add_argument('--probe', type=Path, default=root / 'out/maintenance/layout-probe.exe')
    p = commands.add_parser('compare', help='比较两轮静态契约')
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p = commands.add_parser('init', help='建立全为 pending 的新验收计划')
    p.add_argument('--scan', type=Path, required=True)
    p = commands.add_parser('record', help='关联维护者观察和证据，输出新计划')
    p.add_argument('--plan', type=Path, required=True)
    p.add_argument('--id', required=True)
    p.add_argument('--status', choices=['passed', 'failed', 'unverified'], required=True)
    p.add_argument('--kind', choices=['offline', 'actual'], required=True)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--game-sha256', required=True)
    p.add_argument('--core-sha256', required=True)
    p.add_argument('--observation', required=True)
    p = commands.add_parser('finish', help='复核全部必需项与证据文件哈希')
    p.add_argument('--plan', type=Path, required=True)
    p = commands.add_parser('save-baseline', help='本轮开始前重新采集，禁止发布此个人清单')
    p.add_argument('--directory', type=Path, required=True)
    p = commands.add_parser('save-verify', help='核对本轮开始与结束；不恢复存档')
    p.add_argument('--baseline', type=Path, required=True)
    for p in commands.choices.values(): p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        if args.output.exists(): raise FileExistsError('输出已存在，请使用新文件名')
        protected = None
        if args.command == 'scan': protected = args.game_directory
        elif args.command == 'save-baseline': protected = args.directory
        elif args.command == 'save-verify': protected = Path(w.read_json(args.baseline)['root'])
        if protected and args.output.resolve().is_relative_to(protected.resolve()):
            raise ValueError('输出必须位于游戏/受保护存档目录之外')
        if args.command == 'scan': result = scan(args.game_directory, args.core, args.probe)
        elif args.command == 'compare': result = w.compare(w.read_json(args.before), w.read_json(args.after))
        elif args.command == 'init': result = w.new_plan(w.read_json(args.scan))
        elif args.command == 'record':
            result = w.read_json(args.plan)
            w.record(result, args.id, args.status, args.kind, args.evidence,
                     args.game_sha256.lower(), args.core_sha256.lower(), args.observation)
        elif args.command == 'finish': result = w.finish(w.read_json(args.plan))
        elif args.command == 'save-baseline': result = w.capture_saves(args.directory)
        else: result = w.verify_saves(w.read_json(args.baseline))
        w.write_new(args.output, result)
        print(json.dumps({'output': str(args.output), 'status': result.get('status'),
                          'unchanged': result.get('unchanged')}, ensure_ascii=False))
        return 2 if result.get('status') in ('incomplete', 'contracts-incomplete') or result.get('unchanged') is False else 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        parser.exit(2, f'维护检查失败：{error}\n')


if __name__ == '__main__': sys.exit(main())
