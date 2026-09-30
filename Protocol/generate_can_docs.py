#!/usr/bin/env python3
"""Generate readable CAN protocol documents from the YAML message files.

    Protocol/.venv/bin/python Protocol/generate_can_docs.py          # write the .md files
    Protocol/.venv/bin/python Protocol/generate_can_docs.py --check  # validate only

Reads   can_messages.yaml        (PiAvionics messages, source of truth)
        mgl_can_messages.yaml    (MGL messages, transcribed from MGL's document)
Writes  CAN_PROTOCOL.md          (PiAvionics messages)
        MGL_CAN_PROTOCOL.md      (MGL messages)
        CAN_BUS_MAP.md           (ID map, conflicts and bus load for both)

The generated files carry no timestamp, so re-running gives no diff unless the
YAML changed.
"""

import argparse
import re
import sys
from pathlib import Path

import yaml

VERSION = "1.0.1"

HERE = Path(__file__).resolve().parent
PI_YAML = HERE / "can_messages.yaml"
MGL_YAML = HERE / "mgl_can_messages.yaml"

STATUS_LABEL = {
    "active": "✅ Active",
    "partial": "🟡 Partial",
    "planned": "⚪ Planned",
    "reserved": "🔘 Reserved",
}
STATUS_HELP = [
    ("✅ Active", "Sent by the module and used by the receiver."),
    ("🟡 Partial", "Exists, but something is missing or always zero. See the notes."),
    ("⚪ Planned", "Designed but not implemented in firmware or the EFIS yet."),
    ("🔘 Reserved", "ID is claimed or named in code, but nothing is sent on it."),
]
GENERATED_NOTE = (
    "> **Generated file. Do not edit.** Edit {src} and run "
    "`Protocol/.venv/bin/python Protocol/generate_can_docs.py`."
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def hx(n):
    """Format an 11-bit CAN ID."""
    return f"0x{n:03X}"


def parse_bytes(spec):
    """'0-1' -> (0, 1); '2' -> (2, 2)."""
    m = re.fullmatch(r"\s*(\d+)\s*(?:-\s*(\d+))?\s*", str(spec))
    if not m:
        raise ValueError(f"bad byte range {spec!r}")
    first = int(m.group(1))
    last = int(m.group(2)) if m.group(2) is not None else first
    if last < first:
        raise ValueError(f"byte range {spec!r} runs backwards")
    return first, last


def esc(text):
    """Escape a value for a Markdown table cell."""
    return str(text).replace("|", "\\|").replace("\n", " ").strip()


def fmt_scale(scale):
    if scale is None:
        return ""
    return f"{scale:g}"


def fmt_unit(field):
    unit = field.get("unit")
    scale = field.get("scale")
    if unit and scale not in (None, 1):
        return f"{unit} (raw × {fmt_scale(scale)})"
    if unit:
        return str(unit)
    if scale not in (None, 1):
        return f"raw × {fmt_scale(scale)}"
    return ""


def fmt_period(msg, short=False):
    if msg.get("period_ms") is None:
        return msg["period_note"] if msg.get("period_note") and not short else "—"
    ms = msg["period_ms"]
    if ms >= 1000 and ms % 1000 == 0:
        text = f"{ms // 1000} s"
    else:
        text = f"{ms} ms"
    if msg.get("period_note") and not short:
        text += f" ({msg['period_note']})"
    return text


def frame_bits(dlc):
    """Worst-case bits of a standard-ID data frame, stuffing and interframe space included."""
    n = int(str(dlc).split("-")[-1]) if dlc is not None else 8
    return 47 + 8 * n + (34 + 8 * n - 1) // 4


def byte_map(fields, dlc):
    """ASCII picture of which fields use which bytes."""
    cell = 9
    n = int(str(dlc).split("-")[-1])
    spans = []
    used = set()
    for f in fields:
        a, b = parse_bytes(f["bytes"])
        spans.append((a, b, f["name"]))
        used.update(range(a, b + 1))
    for i in range(n):
        if i not in used:
            spans.append((i, i, "-"))
    spans.sort()

    def box(width, text):
        text = text if len(text) <= width else text[: width - 1] + "…"
        return text.center(width)

    head = "byte  " + " ".join(str(i).center(cell) for i in range(n))
    rule = "      +" + "+".join("-" * (((b - a + 1) * (cell + 1)) - 1) for a, b, _ in spans) + "+"
    body = "      |" + "|".join(box(((b - a + 1) * (cell + 1)) - 1, name) for a, b, name in spans) + "|"
    return "\n".join(line.rstrip() for line in [head, rule, body, rule])


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def validate_pi(doc):
    errors, warnings = [], []
    nodes = doc.get("nodes", {})
    seen = {}
    for msg in doc["messages"]:
        ident = msg["id"]
        label = f"{hx(ident)} {msg.get('name', '?')}"
        if not 0 <= ident <= 0x7FF:
            errors.append(f"{label}: ID outside 0x000-0x7FF")
        if ident in seen:
            errors.append(f"{label}: duplicate ID (also {seen[ident]})")
        seen[ident] = msg.get("name")
        for key in ("name", "title", "sender", "dlc", "status", "fields"):
            if key not in msg:
                errors.append(f"{label}: missing '{key}'")
        if msg.get("status") not in STATUS_LABEL:
            errors.append(f"{label}: bad status {msg.get('status')!r}")
        if msg.get("sender") not in nodes:
            errors.append(f"{label}: unknown sender {msg.get('sender')!r}")
        for rx in msg.get("receivers") or []:
            if rx not in nodes:
                errors.append(f"{label}: unknown receiver {rx!r}")
        dlc = msg.get("dlc", 8)
        used = {}
        names = set()
        for f in msg.get("fields", []):
            try:
                a, b = parse_bytes(f["bytes"])
            except (ValueError, KeyError) as exc:
                errors.append(f"{label}: {exc}")
                continue
            if b >= int(dlc):
                errors.append(f"{label}: field {f.get('name')} uses byte {b} but DLC is {dlc}")
            for i in range(a, b + 1):
                if i in used:
                    errors.append(f"{label}: byte {i} used by both {used[i]} and {f.get('name')}")
                used[i] = f.get("name")
            if f.get("name") in names:
                errors.append(f"{label}: duplicate field name {f.get('name')}")
            names.add(f.get("name"))
        missing = [i for i in range(int(dlc)) if i not in used]
        if missing:
            warnings.append(f"{label}: bytes {missing} not described")
    return errors, warnings


def validate_mgl(doc):
    errors = []
    devices = doc["devices"]
    for key, dev in devices.items():
        if dev["last"] < dev["first"] or not 0 <= dev["first"] <= 127:
            errors.append(f"device {key}: bad address range")
    for msg in doc["messages"]:
        label = f"{msg.get('name', '?')}"
        if msg.get("address_of") not in devices:
            errors.append(f"{label}: unknown address_of {msg.get('address_of')!r}")
        if not 0 <= msg.get("type", -1) <= 15:
            errors.append(f"{label}: message type must be 0-15")
        for key in ("to", "from"):
            for dev in ([msg[key]] if isinstance(msg.get(key), str) else msg.get(key, [])):
                if dev not in devices:
                    errors.append(f"{label}: unknown device {dev!r} in '{key}'")
        for f in msg.get("fields", []):
            try:
                parse_bytes(f["bytes"])
            except (ValueError, KeyError) as exc:
                errors.append(f"{label}: {exc}")
    return errors


def mgl_id(doc, msg):
    """MGL CAN ID of a message: (first address of the owning device << 4) | type."""
    dev = doc["devices"][msg["address_of"]]
    address = 1 if msg["address_of"] == "host" else dev["first"]
    return (address << 4) | msg["type"]


# ---------------------------------------------------------------------------
# Rendering: message tables
# ---------------------------------------------------------------------------

def field_table(fields):
    lines = ["| Bytes | Field | Type | Unit | Description |",
             "|:-----:|-------|------|------|-------------|"]
    for f in fields:
        lines.append("| {} | `{}` | {} | {} | {} |".format(
            esc(f["bytes"]), esc(f["name"]), esc(f.get("type", "")),
            esc(fmt_unit(f)), esc(f.get("desc", ""))))
    return "\n".join(lines)


def render_pi(doc):
    proto, nodes, msgs = doc["protocol"], doc["nodes"], doc["messages"]
    out = [f"# {proto['name']} messages", "",
           GENERATED_NOTE.format(src="`Protocol/can_messages.yaml`"), "",
           f"Version {proto['version']} · reviewed {proto['last_reviewed']}", "",
           proto["summary"].strip(), ""]

    out += ["## Bus at a glance", "",
            "| | |", "|---|---|",
            f"| Bit rate | {proto['bitrate'] // 1000} kbit/s |",
            f"| Identifiers | {proto['identifier']} |",
            f"| Byte order | {proto['byte_order']} |",
            f"| Data length | {proto['data_length']} |",
            f"| Termination | {proto['termination']} |",
            f"| Hardware | {proto['hardware']} |", ""]
    for note in proto.get("notes", []):
        out.append(f"- {' '.join(note.split())}")
    out.append("")

    out += ["## Status key", ""]
    out += [f"- **{label}**: {text}" for label, text in STATUS_HELP]
    out.append("")

    out += ["## Message index", "",
            "| ID | Name | Sent by | Received by | Rate | Bytes | Status |",
            "|:--:|------|---------|-------------|------|:-----:|--------|"]
    for m in sorted(msgs, key=lambda m: m["id"]):
        rx = ", ".join(nodes[r]["name"] for r in m.get("receivers") or []) or "—"
        out.append("| `{}` | [{}](#{}) | {} | {} | {} | {} | {} |".format(
            hx(m["id"]), m["name"], anchor(m), nodes[m["sender"]]["name"], rx,
            fmt_period(m, short=True), m["dlc"], STATUS_LABEL[m["status"]]))
    out.append("")

    out += ["## Messages", ""]
    for m in sorted(msgs, key=lambda m: m["id"]):
        out += [f"### {hx(m['id'])} {m['name']}", "", f"**{m['title']}**", "",
                "| | |", "|---|---|",
                f"| Sent by | {nodes[m['sender']]['name']} |",
                f"| Received by | {', '.join(nodes[r]['name'] for r in m.get('receivers') or []) or '—'} |",
                f"| Rate | {fmt_period(m)} |",
                f"| Data length | {m['dlc']} bytes |",
                f"| Status | {STATUS_LABEL[m['status']]} |", "",
                field_table(m["fields"]), "",
                "```", byte_map(m["fields"], m["dlc"]), "```", ""]
        if m.get("notes"):
            out += ["**Notes**", ""] + [f"- {' '.join(n.split())}" for n in m["notes"]] + [""]
        if m.get("code"):
            out += ["**Code**", ""]
            out += [f"- {k.capitalize()}: {v}" for k, v in m["code"].items()]
            out.append("")

    out += ["## Nodes", "",
            "| Node | Role | Code |", "|------|------|------|"]
    for n in nodes.values():
        out.append(f"| {n['name']} | {esc(n['description'])} | {('`' + n['code'] + '`') if n.get('code') else '—'} |")
    out.append("")

    if doc.get("reserved_ids"):
        out += ["## IDs named in code but unused", "",
                "| ID | Name | Note |", "|:--:|------|------|"]
        for r in doc["reserved_ids"]:
            out.append(f"| `{hx(r['id'])}` | {r['name']} | {' '.join(r['note'].split())} |")
        out.append("")
    return "\n".join(out).rstrip() + "\n"


def anchor(msg):
    """GitHub-style heading anchor for '### 0x28 AIR_DATA'."""
    text = f"{hx(msg['id'])} {msg['name']}".lower()
    return re.sub(r"[^a-z0-9 _-]", "", text).replace(" ", "-")


def render_mgl(doc):
    proto, devices, msgs = doc["protocol"], doc["devices"], doc["messages"]
    out = [f"# {proto['name']} messages", "",
           GENERATED_NOTE.format(src="`Protocol/mgl_can_messages.yaml`"), "",
           f"Transcribed from {proto['source']}, {proto['version']}. "
           "Check MGL's document before relying on a detail.", "",
           proto["summary"].strip(), "",
           "## How IDs are built", "",
           "```", "CAN ID = (device address << 4) | message type", "```", "",
           "The device address is the upper 7 bits and the message type is the lower 4 bits, "
           "so every device owns a block of 16 IDs. Host (EFIS) commands use host address 1 "
           "(IDs 0x10 to 0x1F). Each device sends on its own block.", ""]
    for note in proto.get("notes", []):
        out.append(f"- {' '.join(note.split())}")
    out.append("")

    out += ["## Device addresses", "",
            "| Device | Addresses | CAN ID range | Selection |",
            "|--------|:---------:|:------------:|-----------|"]
    for d in devices.values():
        out.append("| {} | {}–{} | `{}`–`{}` | {} |".format(
            d["name"], d["first"], d["last"], hx(d["first"] << 4), hx((d["last"] << 4) | 0xF), d["selection"]))
    out.append("")

    out += ["## Message index", "",
            "| ID | Name | Direction | Rate | Bytes |", "|:--:|------|-----------|------|:-----:|"]
    ordered = sorted(msgs, key=lambda m: (mgl_id(doc, m), m.get("variant", "")))
    for m in ordered:
        out.append("| `{}` | {} | {} | {} | {} |".format(
            hx(mgl_id(doc, m)), m["name"], direction(doc, m), fmt_period(m, short=True), m["dlc"]))
    out.append("")

    out += ["## Messages", ""]
    for m in ordered:
        i = mgl_id(doc, m)
        dev = devices[m["address_of"]]
        first_instance = "" if m["address_of"] == "host" or dev["first"] == dev["last"] else (
            f" (first unit; other units add 0x10 each, up to `{hx(((dev['last']) << 4) | m['type'])}`)")
        out += [f"### {hx(i)} {m['name']}", "", f"**{m['title']}**", "",
                "| | |", "|---|---|",
                f"| CAN ID | `{hx(i)}`{first_instance} |",
                f"| Direction | {direction(doc, m)} |",
                f"| Rate | {fmt_period(m)} |",
                f"| Data length | {m['dlc']} bytes |", "",
                field_table(m["fields"]), ""]
        if m.get("notes"):
            out += ["**Notes**", ""] + [f"- {' '.join(n.split())}" for n in m["notes"]] + [""]
    return "\n".join(out).rstrip() + "\n"


def direction(doc, m):
    devs = doc["devices"]
    if "to" in m:
        return "Host → " + ", ".join(devs[d]["name"] for d in m["to"])
    return devs[m["from"]]["name"] + " → Host"


# ---------------------------------------------------------------------------
# Rendering: combined bus map
# ---------------------------------------------------------------------------

def render_map(pi, mgl):
    nodes = pi["nodes"]
    out = ["# CAN bus map: PiAvionics and MGL on one bus", "",
           GENERATED_NOTE.format(src="`Protocol/can_messages.yaml` and `Protocol/mgl_can_messages.yaml`"), "",
           "Both systems run at 250 kbit/s with standard 11-bit IDs, so they can share a bus "
           "electrically. This page shows where the IDs land and whether anything collides.", ""]

    # ---- where each PiAvionics ID falls in MGL's scheme
    out += ["## Where PiAvionics IDs fall in MGL's scheme", "",
            "MGL splits an ID into a device address (upper 7 bits) and a message type (lower 4 bits). "
            "Addresses 1 to 15 are MGL host addresses: only address 1 is in use today, and the rest are "
            "reserved for future hosts.", "",
            "| PiAvionics ID | Name | MGL address | MGL type | What MGL says about it |",
            "|:-------------:|------|:-----------:|:--------:|------------------------|"]
    host_types = {}
    for m in mgl["messages"]:
        if m["address_of"] == "host":
            host_types.setdefault(m["type"], []).append(m["name"])
    collisions, reserved_host = [], []
    for m in sorted(pi["messages"], key=lambda m: m["id"]):
        addr, typ = m["id"] >> 4, m["id"] & 0xF
        owner = owner_of(mgl, addr)
        if addr == 1 and typ in host_types:
            verdict = "⛔ **Collision**: host message " + ", ".join(host_types[typ])
            collisions.append(m)
        elif addr == 1:
            verdict = "⚠️ Host 1 block, but this type is not defined by MGL"
            reserved_host.append(m)
        elif owner and owner["first"] <= addr <= owner["last"] and owner is not None:
            verdict = f"⚠️ Reserved: {owner['name']} address space, no current use"
            reserved_host.append(m)
        else:
            verdict = "✅ Not used by MGL"
        out.append(f"| `{hx(m['id'])}` | {m['name']} | {addr} | {typ} | {verdict} |")
    out.append("")

    # ---- findings
    out += ["## Findings", ""]
    if collisions:
        for m in collisions:
            out.append(f"- ⛔ `{hx(m['id'])}` {m['name']} uses the ID MGL reserves for host message "
                       f"{', '.join(host_types[m['id'] & 0xF])}. See the note on `{hx(m['id'])}` in the PiAvionics document.")
    host_reserved = [m for m in pi["messages"] if 2 <= (m["id"] >> 4) <= 15]
    if host_reserved:
        ids = ", ".join(f"`{hx(m['id'])}`" for m in sorted(host_reserved, key=lambda m: m["id"]))
        out.append(f"- ⚠️ {len(host_reserved)} PiAvionics messages ({ids}) sit in MGL's reserved host address "
                   "space (addresses 2 to 15). No current MGL device uses them, but MGL may in future.")
    device_overlap = [m for m in pi["messages"] if (m["id"] >> 4) >= 16 and owner_of(mgl, m["id"] >> 4)]
    out.append("- ✅ No PiAvionics message overlaps an MGL **device** block (RDAC, compass, AHRS and the rest)."
               if not device_overlap else
               "- ⛔ Some PiAvionics messages overlap MGL device blocks: " +
               ", ".join(f"`{hx(m['id'])}`" for m in device_overlap))
    out.append("- ℹ️ Lower IDs win arbitration, so every PiAvionics ID (0x19 to 0x83) outranks all MGL device data "
               "(0x100 and up). That is harmless at this bus load.")
    out.append("")

    # ---- address space
    out += ["## Address space", "",
            "| CAN IDs | Addresses | Owner |", "|:-------:|:---------:|-------|",
            "| `0x000`–`0x00F` | 0 | Reserved by MGL |"]
    used = sorted(((d["first"], d["last"], d["name"]) for d in mgl["devices"].values()))
    for first, last, name in used:
        out.append(f"| `{hx(first << 4)}`–`{hx((last << 4) | 0xF)}` | {first}–{last} | {name} |")
    top = max(last for _, last, _ in used)
    out.append(f"| `{hx((top + 1) << 4)}`–`0x7FF` | {top + 1}–127 | **Free.** Not used by MGL |")
    out += ["", "PiAvionics IDs currently in use: " +
            ", ".join(f"`{hx(m['id'])}`" for m in sorted(pi["messages"], key=lambda m: m["id"])) + ".", ""]

    # ---- bus load
    out += ["## Estimated bus load", "",
            "Worst case (every frame at maximum bit stuffing) at 250 kbit/s. Only messages that are sent today "
            "(Active or Partial) are counted for PiAvionics. The MGL rows are one unit of each device.", "",
            "| Source | Frames/s | Bus load |", "|--------|---------:|---------:|"]
    pi_frames = pi_load = 0.0
    for key, node in nodes.items():
        frames = load = 0.0
        for m in pi["messages"]:
            if m["sender"] == key and m["status"] in ("active", "partial") and m.get("period_ms"):
                rate = 1000.0 / m["period_ms"]
                frames += rate
                load += rate * frame_bits(m["dlc"])
        if frames:
            out.append(f"| PiAvionics: {node['name']} | {frames:.1f} | {100 * load / pi['protocol']['bitrate']:.1f} % |")
        pi_frames += frames
        pi_load += load
    out.append(f"| **PiAvionics total** | **{pi_frames:.1f}** | **{100 * pi_load / pi['protocol']['bitrate']:.1f} %** |")
    total_frames, total_load = pi_frames, pi_load
    for key, dev in mgl["devices"].items():
        frames = load = 0.0
        for m in mgl["messages"]:
            if m.get("from") == key and m.get("period_ms") and m.get("variant") not in ("deviation_1",):
                if m["name"] in ("SP6_DEV_1", "SP6_DEV_2", "SP6_DEV_3", "SP6_CAL"):
                    continue
                rate = 1000.0 / m["period_ms"]
                frames += rate
                load += rate * frame_bits(m["dlc"])
        if frames:
            out.append(f"| MGL: {dev['name']} | {frames:.1f} | {100 * load / mgl['protocol']['bitrate']:.1f} % |")
            total_frames += frames
            total_load += load
    out.append(f"| **All of the above on one bus** | **{total_frames:.1f}** | **{100 * total_load / pi['protocol']['bitrate']:.1f} %** |")
    out += ["", "A healthy CAN bus stays below about 30 to 40 % load.", ""]
    return "\n".join(out).rstrip() + "\n"


def owner_of(mgl, address):
    """MGL device entry that owns this device address, if any."""
    for dev in mgl["devices"].values():
        if dev["first"] <= address <= dev["last"]:
            return dev
    return None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="validate the YAML and write nothing")
    args = parser.parse_args()

    pi = yaml.safe_load(PI_YAML.read_text(encoding="utf-8"))
    mgl = yaml.safe_load(MGL_YAML.read_text(encoding="utf-8"))

    errors, warnings = validate_pi(pi)
    errors += validate_mgl(mgl)
    for w in warnings:
        print(f"warning: {w}")
    for e in errors:
        print(f"ERROR: {e}")
    if errors:
        print(f"{len(errors)} error(s); nothing written.")
        return 1

    print(f"generate_can_docs {VERSION}: {len(pi['messages'])} PiAvionics and {len(mgl['messages'])} MGL messages OK")
    if args.check:
        return 0

    outputs = {
        HERE / "CAN_PROTOCOL.md": render_pi(pi),
        HERE / "MGL_CAN_PROTOCOL.md": render_mgl(mgl),
        HERE / "CAN_BUS_MAP.md": render_map(pi, mgl),
    }
    for path, text in outputs.items():
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(HERE.parent)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
