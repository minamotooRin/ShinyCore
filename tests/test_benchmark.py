"""A normal window close must stop a benchmark, not launch another native window."""
import contextlib
import io
import json
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


class BenchmarkTests(unittest.TestCase):
    def test_incomplete_run_stops_sequence(self):
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'evidence'
            def closed_window(command, *, stdout, stderr):
                stdout.write(json.dumps({'ok':True,'frames':611}))
                return SimpleNamespace(returncode=0)
            with patch.object(sys,'argv',['benchmark.py','unused.exe','--output',str(output)]), \
                    patch.object(subprocess,'run',side_effect=closed_window) as launch, \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as stopped:
                    runpy.run_path(str(root/'tools/benchmark.py'),run_name='__main__')
            self.assertEqual(stopped.exception.code,1)
            self.assertEqual(launch.call_count,1)
            report=json.loads((output/'report.json').read_text(encoding='utf-8'))
            self.assertFalse(report['ok'])
            self.assertIn('611 of 12720',report['errors'][0])


if __name__=='__main__':
    unittest.main()
