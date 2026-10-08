#!/usr/bin/env python3
"""Read-only verification of the firmware pin contract against the actual PCB."""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def parse_sexp(text):
    stack = []
    root = None
    for token in re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', text):
        if token == "(":
            node = []
            if stack:
                stack[-1].append(node)
            else:
                root = node
            stack.append(node)
        elif token == ")":
            stack.pop()
        else:
            stack[-1].append(json.loads(token) if token.startswith('"') else token)
    if stack:
        raise ValueError("Unbalanced KiCad file")
    return root

def children(node, name):
    return [x for x in node if isinstance(x, list) and x and x[0] == name]

def check():
    pcb = parse_sexp((ROOT / "hardware/AmiWiFi.kicad_pcb").read_text())
    footprints = {next(x[2] for x in children(f, "property") if x[1] == "Reference"): f
                  for f in children(pcb, "footprint")}
    nets = {ref: {p[1]: children(p, "net")[0][1] for p in children(f, "pad") if children(p, "net")}
            for ref, f in footprints.items()}
    # Mapping is physical Pico module pin -> PCB net, independent of implementation.
    data_pins = [1, 2, 4, 5, 6, 7, 9, 10]
    for bit, pin in enumerate(data_pins):
        assert nets["U7"][str(pin)] == f"/PIO_D{bit}", (pin, bit)
    signals = {"FO_STROBE_N": (8, 11, "PIO_STROBE_N"), "FO_SEL": (9, 12, "PIO_SEL"),
               "FO_BUSY": (10, 14, "PIO_BUSY"), "FO_POUT": (11, 15, "PIO_POUT"),
               "FO_DATA_DIR": (20, 26, "DATA_DIR"), "FO_DATA_EN": (21, 27, "FW_DATA_EN"),
               "FO_CTRL_EN": (22, 29, "FW_CTRL_EN"), "FO_ACK_N": (26, 31, "PIO_ACK_N")}
    header = (ROOT / "firmware/include/funkotto/board.h").read_text()
    constants = {k: int(v) for k, v in re.findall(r"\b(FO_\w+)\s*=\s*(\d+)", header)}
    assert constants["FO_DATA_FIRST"] == 0 and constants["FO_DATA_LAST"] == 7
    for symbol, (gpio, pin, net) in signals.items():
        assert constants[symbol] == gpio, symbol
        assert nets["U7"][str(pin)] == "/" + net, (symbol, pin)
    assert nets["U7"]["30"] == "/PICO_RUN_N"
    assert nets["U7"]["39"] == "/PICO_VSYS"
    for ref, enable, output in [("U5", "FW_DATA_EN", "DATA_OE_N"), ("U6", "FW_CTRL_EN", "CTRL_OE_N")]:
        assert nets[ref] == {"1": "/" + enable, "2": "/GND", "3": "/HOST_PRESENT",
                             "4": "/" + output, "5": "/+3V3_IF", "6": "/RESET_IN"}
    assert nets["U8"] == {"1": "/RESET_IN", "2": "/GND", "3": "/HOST_PRESENT",
                          "4": "/PICO_RUN_N", "5": "/+3V3_IF", "6": "/PICO_RUN_N"}
    for ref, enable in [("R28", "FW_DATA_EN"), ("R29", "FW_CTRL_EN")]:
        assert set(nets[ref].values()) == {"/" + enable, "/GND"}
        assert next(p[2] for p in children(footprints[ref], "property") if p[1] == "Value") == "4.7k"
    print("PASS: firmware GPIOs, enable gates, RUN and pulldowns match current PCB")

if __name__ == "__main__":
    check()
