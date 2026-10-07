"""资源计划只读契约与漂移检测。"""
import copy
import contextlib
import importlib.util
import io
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('resource_plan', ROOT / 'tools/resource-plan.py')
plan_tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plan_tool)


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))


def png(width=3, height=2, depth=8, color=6, raw=None):
    if raw is None:
        raw = (b'\0' + b'\x10\x20\x30\x80' * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, color, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


class ResourcePlanContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='aamod-plan-', dir=ROOT / 'out')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.mod = self.directory / 'mod'
        self.mod.mkdir()
        self.image = self.mod / '自有图片.png'
        self.image.write_bytes(png())
        self.bank = self.directory / 'Assets.dat'
        offset = 0x8c5f0
        with self.bank.open('wb') as stream:
            stream.write(struct.pack('<IIII', offset, 8, offset + 8, 8))
            stream.seek(offset)
            # Buffer padding deliberately differs from logical dimensions.
            stream.write(struct.pack('<8H', 3, 2, 4, 4, 3, 2, 3, 2))
        self.request = {'schema': 1, 'replacements': [{'imageId': 0, 'png': self.image.name}]}

    def prepare(self, request=None):
        return plan_tool.prepare(self.bank, self.mod, self.request if request is None else request)

    def test_prepare_verify_and_input_unchanged(self):
        original_bank = self.bank.read_bytes()
        original_png = self.image.read_bytes()
        plan = self.prepare()
        self.assertEqual(plan['bindings'][0]['original']['bufferWidth'], 4)
        self.assertEqual(plan['bindings'][0]['replacement']['width'], 3)
        self.assertFalse(plan['runtimeValidated'])
        self.assertFalse(plan['nativeOverrideAvailable'])
        self.assertEqual(plan_tool.verify(self.bank, self.mod, plan), plan)
        self.assertEqual(self.bank.read_bytes(), original_bank)
        self.assertEqual(self.image.read_bytes(), original_png)

    def test_png_and_bank_drift(self):
        plan = self.prepare()
        self.image.write_bytes(png(raw=(b'\0' + b'\xff\0\0\xff' * 3) * 2))
        with self.assertRaises(ValueError):
            plan_tool.verify(self.bank, self.mod, plan)
        plan = self.prepare()
        with self.bank.open('ab') as stream:
            stream.write(b'changed')
        with self.assertRaises(ValueError):
            plan_tool.verify(self.bank, self.mod, plan)

    def test_plan_tamper(self):
        plan = self.prepare()
        for key, value in [('runtimeValidated', True), ('nativeOverrideAvailable', True), ('schema', 2)]:
            altered = copy.deepcopy(plan)
            altered[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                plan_tool.verify(self.bank, self.mod, altered)

    def test_request_and_conflicts(self):
        requests = [None, {}, {'schema': 2, 'replacements': []}, {'schema': 1, 'replacements': []},
                    {'schema': 1, 'replacements': self.request['replacements'] * 2},
                    {'schema': 1, 'replacements': [{'imageId': True, 'png': self.image.name}]},
                    {'schema': 1, 'replacements': [{'imageId': -1, 'png': self.image.name}]},
                    {'schema': 1, 'replacements': [{'imageId': 71870, 'png': self.image.name}]}]
        for request in requests:
            with self.subTest(request=request), self.assertRaises(ValueError):
                plan_tool.prepare(self.bank, self.mod, request)
        batch = copy.deepcopy(self.request)
        batch['replacements'].append({'imageId': 1, 'png': self.image.name})
        self.assertEqual(len(self.prepare(batch)['bindings']), 2)

    def test_paths(self):
        for path in ['', '../outside.png', '/outside.png', 'C:/outside.png', 'image.png:secret', 'a\\b.png']:
            with self.subTest(path=path), self.assertRaises(ValueError):
                plan_tool.owned_file(self.mod, path)
    def test_external_link(self):
        outside = self.directory / 'outside.png'
        outside.write_bytes(png())
        link = self.mod / 'link.png'
        try:
            link.symlink_to(outside)
        except OSError:
            self.skipTest('当前账户不能创建符号链接；解析后路径归属检查未覆盖')
        with self.assertRaises(ValueError):
            plan_tool.owned_file(self.mod, link.name)

    def test_png_validation(self):
        corrupt = bytearray(png())
        corrupt[29] ^= 1
        for data in [bytes(corrupt), png()[:-12], png() + b'trailing', png(depth=16), png(color=2),
                     png(0, 2), png(8193, 1), png(raw=b''), png(raw=b'\x05' + b'\0' * 25),
                     png(raw=b'\0' * 100000), b'not png']:
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                plan_tool.png_info(data)
        self.image.write_bytes(png(2, 2))
        with self.assertRaises(ValueError):
            self.prepare()

    def test_png_chunk_order_and_optional_palette(self):
        base = png()
        header = base[:33]
        compressed = zlib.compress((b'\0' + b'\x10\x20\x30\x80' * 3) * 2)
        end = chunk(b'IEND', b'')
        split = chunk(b'IDAT', compressed[:2]) + chunk(b'IDAT', compressed[2:])
        valid = header + chunk(b'PLTE', b'\0\0\0') + split + end
        self.assertEqual(plan_tool.png_info(valid)['height'], 2)
        for middle in [chunk(b'IDAT', b'') + chunk(b'tEXt', b'key\0value') + split,
                       split + chunk(b'PLTE', b'\0\0\0'),
                       chunk(b'PLTE', b'\0') + split,
                       chunk(b'PLTE', b'\0\0\0') * 2 + split,
                       chunk(b'ABCD', b'') + split,
                       chunk(b'abcD', b'') + split]:
            with self.subTest(middle=middle[:12]), self.assertRaises(ValueError):
                plan_tool.png_info(header + middle + end)

    def test_cli_unknown_exe_creates_no_output(self):
        (self.directory / 'Astral Ascent.exe').write_bytes(b'unknown')
        request_path = self.directory / 'request.json'
        request_path.write_text(json.dumps(self.request), encoding='utf-8')
        output = self.directory / 'plan.json'
        result = subprocess.run([sys.executable, str(ROOT / 'tools/resource-plan.py'), 'prepare',
                                 str(self.directory), str(self.mod), str(request_path), '--output', str(output)],
                                capture_output=True)
        self.assertEqual(result.returncode, 2)
        self.assertFalse(output.exists())

    def test_cli_prepare_verify_and_existing_output(self):
        exe = self.directory / 'Astral Ascent.exe'
        exe.write_bytes(b'synthetic-profile')
        request_path = self.directory / 'request.json'
        request_path.write_text(json.dumps(self.request), encoding='utf-8')
        output = self.directory / 'plan.json'
        real_hash = plan_tool.file_hash

        def synthetic_profile_hash(path):
            return plan_tool.bank.PROFILE_SHA if path == exe else real_hash(path)

        def run(action, input_path, destination=None):
            arguments = ['resource-plan.py', action, str(self.directory), str(self.mod), str(input_path)]
            if destination is not None:
                arguments += ['--output', str(destination)]
            with patch.object(sys, 'argv', arguments), patch.object(plan_tool, 'file_hash', synthetic_profile_hash), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                plan_tool.main()

        run('prepare', request_path, output)
        original = output.read_bytes()
        run('verify', output)
        for destination in [output, request_path, self.bank, self.image, exe]:
            before = destination.read_bytes()
            with self.subTest(destination=destination.name), self.assertRaises(SystemExit) as caught:
                run('prepare', request_path, destination)
            self.assertEqual(caught.exception.code, 2)
            self.assertEqual(destination.read_bytes(), before)
        self.assertEqual(output.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
