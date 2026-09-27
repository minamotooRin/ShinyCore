"""Pinned local SDK versions, modified copies and pruned distributions."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import sdk


class SdkTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory(prefix='shiny-sdk-');self.addCleanup(temp.cleanup)
        self.root=Path(temp.name);(self.root/'lib/shiny').mkdir(parents=True)
        (self.root/'lib/shiny/ui.lua').write_text('return {}\n',encoding='utf-8')
        (self.root/'lib/shiny/LICENSE.txt').write_text('license\n',encoding='utf-8')
        self.version=dict(format=1,version='test.1',engine_version='test-engine',contract_version=1)
        self.engine=dict(version='test-engine',contract_version=1)

    def test_matching_version_and_line_endings(self):
        record=sdk.write(self.root,self.version)
        (self.root/'lib/shiny/ui.lua').write_bytes(b'return {}\r\n')
        self.assertEqual(sdk.audit(self.root,self.engine)['files'],record['files'])

    def test_missing_and_incompatible_versions(self):
        with self.assertRaisesRegex(OSError,'require shiny-sdk.json'):sdk.audit(self.root,self.engine)
        sdk.write(self.root,self.version)
        with self.assertRaisesRegex(OSError,'requires engine'):sdk.audit(self.root,dict(version='other',contract_version=1))
        with self.assertRaisesRegex(OSError,'requires engine'):sdk.audit(self.root,dict(version='test-engine',contract_version=2))

    def test_local_fork_requires_explicit_new_version(self):
        sdk.write(self.root,self.version)
        (self.root/'lib/shiny/ui.lua').write_text('return {custom=true}',encoding='utf-8')
        with self.assertRaisesRegex(OSError,'content changed'):sdk.audit(self.root,self.engine)
        with self.assertRaisesRegex(OSError,'new --version'):sdk.write(self.root,self.version)
        sdk.write(self.root,{**self.version,'version':'test.1.local.1'})
        self.assertEqual(sdk.audit(self.root,self.engine)['version'],'test.1.local.1')

    def test_pruned_selection_and_license(self):
        (self.root/'lib/shiny/unused.lua').write_text('return {}',encoding='utf-8')
        sdk.write(self.root,self.version)
        (self.root/'lib/shiny/unused.lua').unlink()
        self.assertNotIn('lib/shiny/unused.lua',sdk.audit(self.root,self.engine)['files'])
        (self.root/'lib/shiny/LICENSE.txt').unlink()
        with self.assertRaisesRegex(OSError,'license is missing'):sdk.audit(self.root,self.engine)


if __name__=='__main__':unittest.main(verbosity=2)
