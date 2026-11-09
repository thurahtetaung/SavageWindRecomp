"""Patch guard checks; no game files are used."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import local_assists

class PatchGuardTests(unittest.TestCase):
    def module(self, build=local_assists.BUILD, word=None):
        words = {0x7000000000 + offset: value for offset, value, _ in local_assists.SITES}
        return SimpleNamespace(nso=SimpleNamespace(build_id=build), base=0x7000000000,
                               word=lambda address: words[address] if word is None else word, patches={})

    def test_verified_build_installs_expected_patches(self):
        module = self.module()
        local_assists.install(module)
        self.assertEqual(list(module.patches), [module.base + offset for offset, _, _ in local_assists.SITES])
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

    def test_late_mismatch_does_not_install_earlier_patches(self):
        module = self.module()
        original = module.word
        last = module.base + local_assists.SITES[-1][0]
        module.word = lambda address: 0 if address == last else original(address)
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

    def test_upstream_sites_step_aside_and_life_is_observed(self):
        module = self.module()
        stamina, life = module.base + local_assists.OFFSET, module.base + 0xcc4e70
        upstream = lambda m, a: '{ upstream(); }'
        module.sites = {'stamina_store': stamina, 'link_life_store': life}
        module.patches = {stamina: upstream, life: upstream}
        local_assists.install(module)
        self.assertIs(module.patches[stamina], upstream)  # upstream's option does the job
        text = module.patches[life](module, life)
        self.assertIn('upstream();', text)  # upstream's patch still runs, unchanged
        self.assertIn('rt_local_health_seen', text)
        self.assertIn(module.base + 0xcc7174, module.patches)  # this fork's own sites still go in

if __name__ == '__main__':
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
