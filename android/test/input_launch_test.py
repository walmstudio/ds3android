import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1] / 'src/com/ds3port/launcher'


class InputLaunchTest(unittest.TestCase):
    def test_launcher_does_not_preconnect_a_phantom_pad(self):
        source = (ROOT / 'MainActivity.java').read_text()
        self.assertNotIn('env.put("ME1_XINPUT", "1")', source)

    def test_short_layout_one_tap_does_not_warp_the_cursor(self):
        source = (ROOT / 'ControlsOverlay.java').read_text()
        self.assertIn('GameActivity.nativeMouseButtonFrom(GameActivity.KBM_POINTER, 0, 1);', source)
        self.assertIn('GameActivity.nativeMouseButtonFrom(GameActivity.KBM_POINTER, 0, 0);', source)


if __name__ == '__main__':
    unittest.main()
