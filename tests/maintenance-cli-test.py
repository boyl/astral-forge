"""真实 CLI 退出码、报告保留和原生宿主负例。"""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT / 'tools/maintain-update.py'


class CliTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def run_tool(self, *args):
        return subprocess.run([sys.executable, str(TOOL), *map(str, args)],
                              capture_output=True, encoding='utf-8', timeout=30)

    def test_save_cli_and_output_protection(self):
        save = self.root / '存档'
        save.mkdir()
        (save / 'aa_game').write_bytes(b'original')
        protected = self.run_tool('save-baseline', '--directory', save, '--output', save / 'baseline.json')
        self.assertEqual(protected.returncode, 2)
        self.assertFalse((save / 'baseline.json').exists())
        baseline = self.root / 'baseline.json'
        self.assertEqual(self.run_tool('save-baseline', '--directory', save, '--output', baseline).returncode, 0)
        (save / 'aa_game').write_bytes(b'changed')
        result = self.root / 'verification.json'
        self.assertEqual(self.run_tool('save-verify', '--baseline', baseline, '--output', result).returncode, 2)
        self.assertFalse(json.loads(result.read_text(encoding='utf-8'))['unchanged'])
        self.assertEqual((save / 'aa_game').read_bytes(), b'changed')

    def test_invalid_json_has_clear_nonzero_exit(self):
        invalid = self.root / 'invalid.json'
        invalid.write_text('{invalid', encoding='utf-8')
        result = self.run_tool('init', '--scan', invalid, '--output', self.root / 'plan.json')
        self.assertEqual(result.returncode, 2)
        self.assertNotIn('Traceback', result.stderr)

    def test_output_not_overwritten(self):
        output = self.root / 'output.json'
        output.write_text('keep', encoding='utf-8')
        result = self.run_tool('init', '--scan', self.root / 'missing.json', '--output', output)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(output.read_text(), 'keep')

    @unittest.skipUnless(sys.platform == 'win32', '需要 Windows 原生扫描器')
    def test_native_host_partial_and_invalid_executable(self):
        probe = ROOT / 'out/maintenance/layout-probe.exe'
        host = ROOT / 'out/sdk-host.exe'
        self.assertTrue(probe.is_file(), '先运行 tools/build-maintenance.ps1')
        self.assertTrue(host.is_file(), '先运行 build.ps1')
        game = self.root / 'game'
        game.mkdir()
        shutil.copyfile(host, game / 'Astral Ascent.exe')
        core = self.root / 'core.dll'
        core.write_bytes(b'candidate identity fixture')
        report = self.root / 'partial.json'
        result = self.run_tool('scan', '--game-directory', game, '--core', core, '--probe', probe, '--output', report)
        self.assertEqual(result.returncode, 2, result.stderr)
        data = json.loads(report.read_text(encoding='utf-8'))
        self.assertEqual(data['status'], 'contracts-incomplete')
        self.assertFalse(any(data['capabilities'].values()))
        (game / 'Astral Ascent.exe').write_bytes(b'not PE')
        invalid = self.root / 'invalid-exe.json'
        result = self.run_tool('scan', '--game-directory', game, '--core', core, '--probe', probe, '--output', invalid)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(invalid.exists())


if __name__ == '__main__': unittest.main()
