"""Deployment transaction tests; no SSH, processes, or installed apps are touched."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import subprocess

spec = importlib.util.spec_from_file_location(
    'deploy', Path(__file__).resolve().parents[2] / 'scripts/pi/deploy-local.py')
deploy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(deploy)


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.binary = self.root / 'Visona'
        self.binary.write_text('old')
        wrapper = self.root / 'scripts/pi/visona-kiosk.sh'
        wrapper.parent.mkdir(parents=True)
        wrapper.touch()
        self.source = self.root / 'incoming'
        self.source.write_text('new')

    def run_deploy(self, result, launch):
        with patch.object(deploy.subprocess, 'run', return_value=result), \
             patch.object(deploy, 'session_environment', return_value=({'DISPLAY': ':0'}, [])), \
             patch.object(deploy, 'launch', side_effect=launch):
            deploy.deploy(self.source, self.root)

    def test_success_preserves_backup(self):
        self.run_deploy(subprocess.CompletedProcess([], 0, '', ''), [123])
        self.assertEqual(self.binary.read_text(), 'new')
        self.assertEqual((self.root / 'Visona.previous').read_text(), 'old')
        self.assertFalse((self.root / 'Visona.pending').exists())

    def test_failed_start_restores_previous_binary(self):
        with self.assertRaisesRegex(RuntimeError, 'startup failed'):
            self.run_deploy(subprocess.CompletedProcess([], 0, '', ''),
                            [RuntimeError('startup failed'), 124])
        self.assertEqual(self.binary.read_text(), 'old')

    def test_missing_library_leaves_installation_untouched(self):
        with self.assertRaisesRegex(RuntimeError, 'incompatible'):
            self.run_deploy(subprocess.CompletedProcess([], 0, 'libfoo => not found', ''), [])
        self.assertEqual(self.binary.read_text(), 'old')
        self.assertFalse((self.root / 'Visona.previous').exists())

    def test_missing_installation_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'existing kiosk installation'):
            deploy.deploy(self.source, self.root / 'missing')


if __name__ == '__main__':
    unittest.main()
