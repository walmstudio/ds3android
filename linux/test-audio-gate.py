import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).parent / 'verify-audio.py'


class GateTest(unittest.TestCase):
    def test_device_loss_is_not_a_pass(self):
        self.assertTrue(path.is_file(), 'native device-health gate missing')
        spec = importlib.util.spec_from_file_location('verify_audio', path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        base = '[ds3-audio-out] selected=pipewire rate=48000\n[ds3-pipewire] submitted=512 status='
        self.assertTrue(mod.native_audio_ok(base + 'ok age_ms=40\n', 'pipewire'))
        self.assertFalse(mod.native_audio_ok(base + 'failed age_ms=40\n', 'pipewire'))
        self.assertFalse(mod.native_audio_ok(base + 'ok age_ms=4000\n', 'pipewire'))
        self.assertFalse(mod.native_audio_ok(base + 'ok age_ms=40\n[ds3-pipewire] output stopped unexpectedly\n', 'pipewire'))


if __name__ == '__main__':
    unittest.main()
