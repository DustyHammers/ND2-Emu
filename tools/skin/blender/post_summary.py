# Prints the calibration result as CI notices (one per light and view transform), so a run's summary shows it.
#   python3 post_summary.py phase2_calibration.json
import json
import sys

r = json.load(open(sys.argv[1]))
for light, v in sorted(r['lights'].items()):
    for t in ('Standard', 'AgX', 'AgX HC'):
        e = v.get(t, {})
        inks = '; '.join(f"{k} {[round(x) for x in d['shown']]} albedo {[round(x, 3) for x in d['albedo']]}"
                         f"{'' if d['reachable'] else ' OUT OF REACH'}" for k, d in e.items() if isinstance(d, dict))
        print(f"::notice title=Light {light} {t}::{e.get('exposure_ev')} EV | {inks} | targets {r['targets_srgb']}")
    print(f"::notice title=Light {light} render times::{v.get('render_s')} | s/MP/sample {v.get('s_per_mp_per_sample')}")
