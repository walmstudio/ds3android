import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

BIN = Path(sys.argv.pop(1)).resolve()


class RenderSettings(unittest.TestCase):
    def test_edits_only_two_keys_and_rejects_duplicates(self):
        with tempfile.TemporaryDirectory() as p:
            directory = Path(p) / 'drive_c/Users/Shepard/AppData/Local/EA Games/Dead Space 3'
            directory.mkdir(parents=True)
            config = directory / 'system.txt'
            baseline = b'Window.Height = 720\r\nWindow.Fullscreen = true\r\nWindow.Width=1280\r\n'
            config.write_bytes(baseline)
            run = lambda: subprocess.run([BIN, p, '1600x900'], capture_output=True)
            self.assertEqual(run().returncode, 0)
            self.assertEqual(config.read_bytes(), baseline.replace(b'720', b'900').replace(b'1280', b'1600'))
            duplicate = baseline + b'Window.Width=1280\n'
            config.write_bytes(duplicate)
            self.assertNotEqual(run().returncode, 0)
            self.assertEqual(config.read_bytes(), duplicate)


if __name__ == '__main__':
    unittest.main()
