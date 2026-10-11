"""真实 Windows 多进程快照契约。"""
import json
import multiprocessing as mp
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
if sys.platform == 'win32':
    from maintenance.snapshot_io import read_snapshot, publish_snapshot, snapshot_lock


def exchange(path, writer, queue):
    try:
        for i in range(1000):
            if writer: publish_snapshot(path, json.dumps({'sequence': i, 'content': '测试' * 50}))
            else:
                value = json.loads(read_snapshot(path))
                if value['content'] != '测试' * 50: raise ValueError('读到了不完整快照')
        queue.put(None)
    except Exception as error: queue.put(repr(error))


def hold_lock(path, ready):
    with snapshot_lock(path):
        ready.set()
        import time
        time.sleep(1)


@unittest.skipUnless(sys.platform == 'win32', '需要真实 Windows 文件共享语义')
class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / '中文快照.json'

    def test_multi_process_exchange(self):
        publish_snapshot(self.path, json.dumps({'sequence': -1, 'content': '测试' * 50}))
        queue = mp.Queue()
        processes = [mp.Process(target=exchange, args=(self.path, writer, queue)) for writer in (True, False, False)]
        try:
            for process in processes: process.start()
            for process in processes:
                process.join(20)
                self.assertEqual(process.exitcode, 0)
            for _ in processes: self.assertIsNone(queue.get(timeout=2))
            self.assertFalse(self.path.with_name(self.path.name + '.pending').exists())
        finally:
            for process in processes:
                if process.is_alive(): process.terminate(); process.join()
            queue.close()

    def test_missing_file_fails(self):
        with self.assertRaises(OSError): read_snapshot(self.path)

    def test_lock_timeout(self):
        ready = mp.Event()
        process = mp.Process(target=hold_lock, args=(self.path, ready))
        process.start()
        try:
            self.assertTrue(ready.wait(5))
            with self.assertRaises(TimeoutError):
                with snapshot_lock(self.path, timeout_ms=20): pass
        finally:
            process.join(5)
            if process.is_alive(): process.terminate(); process.join()


if __name__ == '__main__': unittest.main()
