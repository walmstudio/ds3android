import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).with_name('summarize-audio.py')
spec = importlib.util.spec_from_file_location('audio_summary', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AudioSummaryTest(unittest.TestCase):
    def test_shortage_and_late_callback_are_distinct(self):
        rows = [
            '[ds3-audio] 0 sink_depth_unavailable 0 48000 480 0',
            '[ds3-audio] 1000000 callback_begin 0 480 48000 0',
            '[ds3-audio] 1100000 mix_lock_wait 0 480 0 0',
            '[ds3-audio] 1200000 mix_begin 0 480 0 0',
            '[ds3-audio] 1210000 fresh 5 480 0 0',
            '[ds3-audio] 1300000 mix_end 0 480 0 0',
            '[ds3-audio] 1500000 callback_end 0 480 48000 0',
            '[ds3-audio] 40000000 callback_begin 0 480 48000 0',
            '[ds3-audio] 40050000 fresh 5 480 300 300',
            '[ds3-audio] 40500000 callback_end 0 480 48000 0',
            '[ds3-audio] 50000000 callback_begin 0 480 48000 0',
            '[ds3-audio] 50050000 fresh 5 480 480 480',
            '[ds3-audio] 50500000 callback_end 0 480 48000 0',
            '[ds3-audio] 51000000 dropped 0 2 0 0',
        ]
        out = module.summarize(reversed(rows))
        self.assertEqual(out['fresh_shortage'], 2)
        self.assertEqual(out['callback_gaps'], 1)
        self.assertEqual(out['shortage_near_gap_ambiguous'], 1)
        self.assertEqual(out['sink_depth'], 'unavailable')
        self.assertEqual(out['dropped'], 2)
        self.assertEqual(out['max_callback_ms'], 0.5)
        self.assertEqual(out['max_mixer_ms'], 0.1)
        self.assertEqual(out['max_mixer_lock_wait_ms'], 0.1)


if __name__ == '__main__':
    unittest.main()
