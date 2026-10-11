"""更新维护工具：证据边界、真实文件漂移与跨轮次隔离。"""
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from maintenance import workflow as w


class MaintenanceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.scan = {'format': 1, 'kind': 'offline-native-contracts', 'imageMapped': True,
                     'capabilities': {name: True for name in ('state', 'equipment', 'checkpoint', 'events', 'images',
                                      'damage', 'crystalPickup', 'abandon', 'defeat', 'victory')},
                     'rvas': {'frame_update': 100}, 'fields': {'mana': 200},
                     'gameSha256': 'a' * 64, 'coreSha256': 'b' * 64}

    def evidence(self):
        path = self.root / 'evidence.json'
        path.write_text('{"nativeEvent": 8}', encoding='utf-8')
        return path

    def complete(self):
        plan = w.new_plan(self.scan)
        for check in plan['checks']:
            w.record(plan, check['id'], 'passed', check['kind'], self.evidence(),
                     'a' * 64, 'b' * 64, '观察到预期行为')
        return plan

    def test_identity_change_is_not_capability_failure(self):
        other = copy.deepcopy(self.scan)
        other['gameSha256'] = 'c' * 64
        delta = w.compare(self.scan, other)
        self.assertTrue(delta['gameChanged'])
        self.assertEqual(delta['regressions'], [])

    def test_compare_fields_and_regression(self):
        other = copy.deepcopy(self.scan)
        other['capabilities']['victory'] = False
        other['fields']['mana'] = 208
        other['rvas']['frame_update'] = 101
        delta = w.compare(self.scan, other)
        self.assertEqual(delta['regressions'], ['victory'])
        self.assertEqual(delta['fieldChanges']['mana'], {'before': 200, 'after': 208})
        self.assertIn('frame_update', delta['rvaChanges'])

    def test_missing_contract_is_invalid(self):
        del self.scan['capabilities']['images']
        with self.assertRaises(ValueError): w.new_plan(self.scan)

    def test_pending_cannot_finish(self):
        self.assertEqual(w.finish(w.new_plan(self.scan))['status'], 'incomplete')

    def test_complete_candidate(self):
        self.assertEqual(w.finish(self.complete())['status'], 'passed')

    def test_cross_game_and_core_rejected(self):
        plan = w.new_plan(self.scan)
        for game, core in [('c' * 64, 'b' * 64), ('a' * 64, 'c' * 64)]:
            with self.assertRaises(ValueError):
                w.record(plan, 'actual.startup', 'passed', 'actual', self.evidence(), game, core, 'x')

    def test_simulation_cannot_claim_actual(self):
        with self.assertRaises(ValueError):
            w.record(w.new_plan(self.scan), 'actual.startup', 'passed', 'simulated', self.evidence(),
                     'a' * 64, 'b' * 64, 'x')

    def test_evidence_changed_or_deleted(self):
        plan = self.complete()
        self.evidence().write_text('changed', encoding='utf-8')
        self.assertEqual(w.finish(plan)['status'], 'incomplete')
        self.evidence().unlink()
        self.assertEqual(w.finish(plan)['status'], 'incomplete')

    def test_failed_or_missing_check_cannot_finish(self):
        plan = self.complete()
        plan['checks'][0]['status'] = 'failed'
        self.assertEqual(w.finish(plan)['status'], 'incomplete')
        plan = self.complete()
        plan['checks'].pop()
        with self.assertRaises(ValueError): w.finish(plan)

    def test_partial_native_contract_cannot_finish(self):
        self.scan['capabilities']['images'] = False
        self.assertEqual(w.finish(self.complete())['status'], 'incomplete')

    def test_save_modified_added_deleted(self):
        saves = self.root / '存档'
        saves.mkdir()
        (saves / 'aa_game').write_bytes(b'original')
        baseline = w.capture_saves(saves)
        self.assertTrue(w.verify_saves(baseline)['unchanged'])
        (saves / 'extra').write_bytes(b'new')
        self.assertEqual(w.verify_saves(baseline)['added'], ['extra'])
        (saves / 'extra').unlink()
        (saves / 'aa_game').write_bytes(b'changed')
        self.assertEqual(w.verify_saves(baseline)['changed'], ['aa_game'])
        (saves / 'aa_game').unlink()
        self.assertEqual(w.verify_saves(baseline)['removed'], ['aa_game'])

    def test_empty_save_baseline_refused(self):
        with self.assertRaises(ValueError): w.capture_saves(self.root)

    def test_save_manifest_cannot_inject_outside_path(self):
        save = self.root / 'save'
        save.write_bytes(b'original')
        baseline = w.capture_saves(self.root)
        baseline['files']['../outside'] = 'a' * 64
        with self.assertRaises(ValueError): w.verify_saves(baseline)

    def test_report_create_never_overwrites(self):
        path = self.root / 'report.json'
        w.write_new(path, {'original': True})
        with self.assertRaises(FileExistsError): w.write_new(path, {})
        self.assertEqual(json.loads(path.read_text())['original'], True)


if __name__ == '__main__': unittest.main()
