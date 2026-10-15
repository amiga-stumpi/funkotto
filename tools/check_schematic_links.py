#!/usr/bin/env python3
"""Check complete PCB symbol paths against a KiCad-exported schematic netlist.

Export with: kicad-cli sch export netlist -o current.net hardware/AmiWiFi.kicad_sch
Then run: python3 tools/check_schematic_links.py current.net
Unlike a reference-only or UUID-suffix check, reject an extra root-sheet UUID.
"""
import argparse
from collections import Counter
from pathlib import Path

from check_board_contract import children, parse_sexp


def check(netlist_path, pcb_path):
    netlist = parse_sexp(netlist_path.read_text())
    board = parse_sexp(pcb_path.read_text())
    footprints = children(board, "footprint")
    refs = [next(p[2] for p in children(f, "property") if p[1] == "Reference")
            for f in footprints]
    duplicates = [ref for ref, count in Counter(refs).items() if count > 1]
    errors = [f"Duplicate PCB reference: {ref}" for ref in duplicates]
    paths = {}
    for ref, fp in zip(refs, footprints):
        for path in children(fp, "path"):
            paths.setdefault(path[1], []).append(ref)
    components = children(children(netlist, "components")[0], "comp")
    matched = 0
    expected_refs = set()
    for comp in components:
        ref = children(comp, "ref")[0][1]
        expected_refs.add(ref)
        sheet = children(children(comp, "sheetpath")[0], "tstamps")[0][1]
        ids = children(comp, "tstamps")[0][1:]
        candidates = [sheet.rstrip("/") + "/" + uid for uid in ids]
        hits = [r for path in candidates for r in paths.get(path, [])]
        if hits != [ref]:
            errors.append(f"{ref}: expected full path {candidates}, found {hits}")
        else:
            matched += 1
    errors.extend(f"Unexpected PCB reference: {ref}" for ref in set(refs) - expected_refs)
    return matched, len(components), errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("netlist", type=Path)
    parser.add_argument("--pcb", type=Path,
                        default=Path(__file__).resolve().parents[1] / "hardware/AmiWiFi.kicad_pcb")
    args = parser.parse_args()
    matched, total, errors = check(args.netlist, args.pcb)
    print(f"{'FAIL' if errors else 'PASS'}: {matched}/{total} complete symbol paths match")
    for error in errors:
        print(error)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
