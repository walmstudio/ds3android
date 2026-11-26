import json
import math
import unittest
from pathlib import Path

ASSETS = Path(__file__).resolve().parents[1] / 'assets' / 'controls'


class StockInputProfileTest(unittest.TestCase):
    def test_stock_is_a_complete_pad_without_kbm(self):
        profile = json.loads((ASSETS / 'dead_space_3_xinput.icp').read_text())
        elements = profile['elements']
        buttons = {'GAMEPAD_BUTTON_' + b for b in (
            'A', 'B', 'X', 'Y', 'L1', 'R1', 'L2', 'R2', 'L3', 'R3', 'START', 'SELECT')}
        directions = ('UP', 'RIGHT', 'DOWN', 'LEFT')
        sticks = {tuple('GAMEPAD_' + side + '_THUMB_' + d for d in directions)
                  for side in ('LEFT', 'RIGHT')}
        self.assertEqual(buttons, {e['bindings'][0] for e in elements if e['type'] == 'BUTTON'})
        self.assertEqual(sticks, {tuple(e['bindings']) for e in elements if e['type'] == 'STICK'})
        self.assertEqual([tuple('GAMEPAD_DPAD_' + d for d in directions)],
                         [tuple(e['bindings']) for e in elements if e['type'] == 'D_PAD'])
        self.assertEqual(15, len(elements))
        for element in elements:
            self.assertFalse(element.get('hidden') or element.get('toggleSwitch') or element.get('zone'))
            self.assertTrue(all(b == 'NONE' or b.startswith('GAMEPAD_') for b in element['bindings']))

    def test_compact_stock_controls_do_not_lose_centers_or_overlap_toggle(self):
        elements = json.loads((ASSETS / 'dead_space_3_xinput.icp').read_text())['elements']
        for w, h in ((640, 360), (568, 320), (540, 270), (600, 320), (640, 320), (720, 360)):
            for mirror in (False, True):
                # Default-size ControlsOverlay and CustomControls hit geometry in dp.
                toggle_x = w - 32 if mirror else 32
                placed = [(e, (1 - e['x'] if mirror else e['x']) * w, e['y'] * h,
                           {'BUTTON': 3, 'STICK': 6, 'D_PAD': 7}[e['type']] * max(w, h) / 100 * e['scale'] + 4)
                          for e in elements]
                for e, x, y, r in placed:
                    with self.subTest(size=(w, h), mirror=mirror, control=e['bindings'][0]):
                        self.assertGreater(math.hypot(x - toggle_x, y - 32), 28,
                                           'toggle steals control center')
                        if e['type'] != 'D_PAD':
                            self.assertGreater(math.hypot(x - toggle_x, y - 32), 28 + r,
                                               'control hit area intersects toggle')
                        hit = next(q for q, qx, qy, qr in reversed(placed)
                                   if ((max(abs(x - qx), abs(y - qy)) if q['type'] == 'D_PAD'
                                        else math.hypot(x - qx, y - qy)) <= qr))
                        self.assertIs(hit, e, 'another control steals center')

    def test_existing_explicit_kbm_profile_still_present(self):
        profile = json.loads((ASSETS / 'dead_space_3.icp').read_text())
        bindings = {b for e in profile['elements'] for b in e['bindings']}
        self.assertTrue({'KEY_E', 'MOUSE_LEFT_BUTTON'} <= bindings)


if __name__ == '__main__':
    unittest.main()
