import os
import subprocess
import sys
import unittest

BINARY = sys.argv.pop(1)


class DesktopRoute(unittest.TestCase):
    def test_reject_invalid_explicit_route_before_sdl(self):
        for variable, value in [('DS3_WINDOW_SYSTEM', 'bogus'), ('DS3_RENDERER', 'zink')]:
            with self.subTest(variable=variable):
                env = dict(os.environ, DS3_WINDOW_SYSTEM='wayland', DS3_RENDERER='vulkan')
                env[variable] = value
                proc = subprocess.run([BINARY, '/nonexistent/game', '/nonexistent/profile'],
                                      env=env, capture_output=True, text=True, timeout=6)
                self.assertEqual(proc.returncode, 2)
                self.assertIn('invalid ' + variable, proc.stderr)


if __name__ == '__main__':
    unittest.main()
