"""The Vulkan layers the launcher keeps out of the game: overlays known to crash it while it makes its device."""
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

if sys.platform == 'win32':
    import bbport_launcher_win as launcher


@unittest.skipUnless(sys.platform == 'win32', 'the Windows launcher')
class CrashingLayerTests(unittest.TestCase):
    def environment(self, inherited=None, extra_env=''):
        settings = {**launcher.APP_DEFAULTS, 'game_dir': 'G', 'extra_env': extra_env}
        with mock.patch.dict(launcher.os.environ), mock.patch.object(launcher, 'load_ini', return_value=({}, [])):
            launcher.os.environ.pop('VK_LOADER_LAYERS_DISABLE', None)
            if inherited is not None:
                launcher.os.environ['VK_LOADER_LAYERS_DISABLE'] = inherited
            return launcher.game_environment(settings)

    def test_hudsight_is_skipped(self):
        self.assertEqual(self.environment()['VK_LOADER_LAYERS_DISABLE'], '*HudSight*')

    def test_an_inherited_list_is_kept(self):
        self.assertEqual(self.environment('~implicit~')['VK_LOADER_LAYERS_DISABLE'], '~implicit~,*HudSight*')
        self.assertEqual(self.environment('*HudSight*')['VK_LOADER_LAYERS_DISABLE'], '*HudSight*')

    def test_extra_env_has_the_last_word(self):
        env = self.environment(extra_env='VK_LOADER_LAYERS_DISABLE=')
        self.assertEqual(env['VK_LOADER_LAYERS_DISABLE'], '')


if __name__ == '__main__':
    unittest.main()
