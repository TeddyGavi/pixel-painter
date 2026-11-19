import sys
from screens import *
import os
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockups") + "/"
jobs = {
  'draw_9.png':  lambda: draw_screen(9, 'A', False, 0),
  'draw_12.png': lambda: draw_screen(12, 'A', False, 0),
  'draw_15.png': lambda: draw_screen(15, 'A', True, 4),
  'draw_12_optB.png': lambda: draw_screen(12, 'B', False, 0),
  'draw_12_optC.png': lambda: draw_screen(12, 'C', False, 0),
}
for k in (sys.argv[1:] or jobs):
    jobs[k]().save(O + k); print(k)
