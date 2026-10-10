#!/usr/bin/env python3
"""Fails if the built plugin's identity differs from the fixed one (see CLAUDE.md).

Hosts recognise the plugin in saved projects by these values; they must never change.
Usage: scripts/check_plugin_identity.py <JUCE artefacts dir, e.g. build/plugin/plugin/ND2_artefacts/Release> [--mac]
--mac also requires the macOS bundle metadata and the AU.
"""
import json
import os
import plistlib
import re
import sys

BUNDLE_ID = "com.dhammers.nd2"
VST3_CLASS_ID = "ABCDEF019182FAEB44486D724E643245"     # from DHmr + Nd2E, JUCE_VST3_CAN_REPLACE_VST2=0
AU = {"type": "aumu", "subtype": "Nd2E", "manufacturer": "DHmr", "name": "DHammers: ND2"}

errors = []


def expect(what, got, want):
    if got != want:
        errors.append("%s is %r, must be %r" % (what, got, want))


def main():
    root, mac = sys.argv[1], "--mac" in sys.argv[2:]
    vst3 = os.path.join(root, "VST3", "ND2.vst3", "Contents")
    with open(os.path.join(vst3, "Resources", "moduleinfo.json")) as f:
        text = re.sub(r",(\s*[}\]])", r"\1", f.read())       # the VST3 SDK writes trailing commas
    info = json.loads(text)
    expect("VST3 name", info["Name"], "ND2")
    expect("VST3 vendor", info["Factory Info"]["Vendor"], "DHammers")
    audio = [c for c in info["Classes"] if c["Category"] == "Audio Module Class"]
    expect("VST3 audio class count", len(audio), 1)
    if audio:
        expect("VST3 class ID", audio[0]["CID"], VST3_CLASS_ID)
        expect("VST3 class name", audio[0]["Name"], "ND2")
        expect("VST3 categories", audio[0]["Sub Categories"], ["Instrument", "Drum"])

    plists = [(os.path.join(vst3, "Info.plist"), "VST3"),
              (os.path.join(root, "AU", "ND2.component", "Contents", "Info.plist"), "AU"),
              (os.path.join(root, "Standalone", "ND2.app", "Contents", "Info.plist"), "Standalone")]
    for path, kind in plists:
        if not os.path.exists(path):
            if mac:
                errors.append("missing " + path)
            continue
        with open(path, "rb") as f:
            p = plistlib.load(f)
        expect(kind + " bundle ID", p.get("CFBundleIdentifier"), BUNDLE_ID)
        if kind == "AU":
            comps = p.get("AudioComponents", [])
            expect("AU component count", len(comps), 1)
            for key, want in AU.items():
                expect("AU " + key, comps[0].get(key) if comps else None, want)

    if errors:
        print("Plugin identity check failed (hosts find ND2 in saved projects by these values):")
        print("\n".join("  - " + e for e in errors))
        return 1
    print("plugin identity OK: VST3 class %s, AU %s/%s/%s, bundle %s" %
          (VST3_CLASS_ID, AU["type"], AU["subtype"], AU["manufacturer"], BUNDLE_ID))
    return 0


if __name__ == "__main__":
    sys.exit(main())
