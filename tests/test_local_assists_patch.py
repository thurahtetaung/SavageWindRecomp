"""Patch guard checks; no game files are used."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import local_assists

class PatchGuardTests(unittest.TestCase):
    def module(self, build=local_assists.BUILD, word=local_assists.WORD):
        return SimpleNamespace(nso=SimpleNamespace(build_id=build), base=0x7000000000,
                               word=lambda address: word, patches={})

    def test_verified_build_installs_one_patch(self):
        module = self.module()
        local_assists.install(module)
        self.assertEqual(list(module.patches), [module.base + local_assists.OFFSET])
        self.assertIn('rt_local_stamina', next(iter(module.patches.values()))(module, module.base + local_assists.OFFSET))

    def test_other_build_rejected(self):
        module = self.module(build='unsupported')
        with self.assertRaises(ValueError):
            local_assists.install(module)
        self.assertEqual(module.patches, {})

    def test_changed_instruction_rejected(self):
        module = self.module(word=0)
        with self.assertRaises(ValueError):
            local_assists.install(module)
        self.assertEqual(module.patches, {})

    def test_conflicting_patch_preserved(self):
        module = self.module()
        address = module.base + local_assists.OFFSET
        existing = object()
        module.patches[address] = existing
        with self.assertRaises(ValueError):
            local_assists.install(module)
        self.assertIs(module.patches[address], existing)

if __name__ == '__main__':
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
