#!/usr/bin/env python3
"""Compile dialog source (JSON) into the engine's binary dialog bank (.dlg).

Usage:
    python3 tools/dialog_build.py assets/dialog/demo.json -o filesystem/dialog/demo.dlg
    python3 tools/dialog_build.py --selftest

The Makefile runs it for every assets/dialog/*.json. The source format is
documented in docs/DIALOG.md; in short:

    {
      "speakers": { "guide": { "name": "Old Guide", "color": "accent" } },
      "conversations": {
        "intro": [
          { "speaker": "guide", "text": "Hello, {player}. [c=accent]Look![/c]" },
          "A plain string is a line by the same speaker.",
          { "text": "Tour?", "choices": [ { "text": "Yes", "next": "tour" },
                                          { "text": "No", "next": "bye", "if": "polite" } ] },
          { "id": "tour", "text": "This way.", "event": "spawn_ball" },
          { "id": "bye", "text": "Farewell.", "end": true }
        ]
      }
    }

Lines run in order unless a node has "next", "choices" or "end". Targets are
a node id in the same conversation, "@conversation" (its first line) or
"conversation.id". Text markup: [c=colour]...[/c], [p=ms] (pause), [page]
(page break), {variable} (filled in by the game), [[ and {{ for literal
brackets. Colours are style palette names: text, accent, hilite, title, dim.

Binary format (big-endian), version 1:
    "DLG1" u16 version u16 flags
    u16 speakers u16 conversations u16 nodes u16 choices u32 string_bytes
    speakers:      u32 name  u8 colour  u8[3] pad
    conversations: u32 id    u16 first_node  u16 pad
    nodes:         u32 text  u32 event  u16 speaker  u16 next  u16 first_choice  u8 choices  u8 flags
    choices:       u32 text  u32 cond   u16 next  u16 pad
    strings:       NUL-terminated, referenced by byte offset (0xFFFFFFFF = none)
Node text is compiled for rdpq_text: colours become ^NN style switches (^ and
$ are escaped), pauses become 0x01 <units of 20 ms>, page breaks 0x02.
"""
import json
import re
import struct
import sys

VERSION = 1
NONE32 = 0xFFFFFFFF
NONE16 = 0xFFFF
# Style palette: name -> rdpq style id (text.c registers ids 2..6 per UiStyle)
PALETTE = {"text": 2, "accent": 3, "hilite": 4, "title": 5, "dim": 6}
PAUSE_UNIT_MS = 20
MAX_TEXT = 480          # bytes of compiled text per line (runtime buffer is 512)
MAX_CHOICES = 4
MAX_SHORT = 36          # choice texts and speaker names (runtime slots hold 40 bytes with the "> " cursor)
VAR_RE = re.compile(r"[a-z_][a-z0-9_]*$")
ID_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*$")


class DialogError(Exception):
    pass


def compile_text(src, where, allow_markup=True):
    """Source markup -> runtime text. Raises DialogError with a location."""
    out = []
    i = 0
    colour_open = False
    while i < len(src):
        ch = src[i]
        if ch == "\n":
            out.append("\n")
            i += 1
            continue
        if ord(ch) < 0x20 or ord(ch) > 0x7E:
            raise DialogError(f"{where}: character {ch!r} is outside printable ASCII (the UI fonts cover 0x20-0x7E)")
        if ch == "[":
            if src.startswith("[[", i):
                out.append("[")
                i += 2
                continue
            end = src.find("]", i)
            if end < 0:
                raise DialogError(f"{where}: unclosed '[' at column {i + 1} (write [[ for a literal bracket)")
            tag = src[i + 1:end].strip()
            if not allow_markup:
                raise DialogError(f"{where}: markup [{tag}] is not allowed here")
            if tag.startswith("c="):
                name = tag[2:].strip()
                if name not in PALETTE:
                    raise DialogError(f"{where}: unknown colour '{name}' (use one of {', '.join(PALETTE)})")
                out.append("^%02X" % PALETTE[name])
                colour_open = True
            elif tag == "/c":
                out.append("^%02X" % PALETTE["text"])
                colour_open = False
            elif tag.startswith("p="):
                try:
                    ms = int(tag[2:])
                except ValueError:
                    raise DialogError(f"{where}: pause [p={tag[2:]}] needs milliseconds, e.g. [p=400]")
                units = max(1, min(255, (ms + PAUSE_UNIT_MS // 2) // PAUSE_UNIT_MS))
                out.append("\x01" + chr(units))
            elif tag == "page":
                out.append("\x02")
            else:
                raise DialogError(f"{where}: unknown markup [{tag}] (known: [c=colour] [/c] [p=ms] [page])")
            i = end + 1
            continue
        if ch == "]":
            if src.startswith("]]", i):
                i += 1
            out.append("]")
            i += 1
            continue
        if ch == "{":
            if src.startswith("{{", i):
                out.append("{{")          # the runtime turns {{ into {
                i += 2
                continue
            end = src.find("}", i)
            if end < 0:
                raise DialogError(f"{where}: unclosed '{{' (write {{{{ for a literal brace)")
            name = src[i + 1:end]
            if not VAR_RE.match(name):
                raise DialogError(f"{where}: bad variable name '{{{name}}}' (lowercase letters, digits, _)")
            out.append("{" + name + "}")
            i = end + 1
            continue
        if ch in "^$":
            out.append(ch + ch)           # rdpq_text escape
            i += 1
            continue
        out.append(ch)
        i += 1
    if colour_open:
        out.append("^%02X" % PALETTE["text"])
    text = "".join(out)
    if len(text) > MAX_TEXT:
        raise DialogError(f"{where}: line is too long ({len(text)} bytes compiled, max {MAX_TEXT}); split it")
    return text


def short_text(src, where):
    """A choice text or speaker name: no markup, at most MAX_SHORT bytes."""
    text = compile_text(src, where, allow_markup=False)
    if len(text) > MAX_SHORT:
        raise DialogError(f"{where}: '{src}' is too long ({len(text)} bytes, max {MAX_SHORT})")
    return text


class Strings:
    def __init__(self):
        self.blob = bytearray()
        self.index = {}

    def add(self, s):
        if s is None:
            return NONE32
        if s in self.index:
            return self.index[s]
        off = len(self.blob)
        self.blob += s.encode("latin-1") + b"\0"     # pause lengths are raw bytes up to 0xFF
        self.index[s] = off
        return off


def build(doc, name="<dialog>"):
    if not isinstance(doc, dict):
        raise DialogError(f"{name}: the top level must be an object")
    unknown = set(doc) - {"speakers", "conversations"}
    if unknown:
        raise DialogError(f"{name}: unknown top-level keys {sorted(unknown)}")
    speakers_src = doc.get("speakers", {})
    convs_src = doc.get("conversations")
    if not isinstance(convs_src, dict) or not convs_src:
        raise DialogError(f"{name}: 'conversations' must be a non-empty object")

    strings = Strings()
    speaker_ids = {}
    speakers = []
    for sid, sp in speakers_src.items():
        if not ID_RE.match(sid):
            raise DialogError(f"{name}: speaker id '{sid}' must be letters, digits and _")
        if isinstance(sp, str):
            sp = {"name": sp}
        colour = sp.get("color", sp.get("colour", "title"))
        if colour not in PALETTE:
            raise DialogError(f"{name}: speaker '{sid}': unknown colour '{colour}'")
        disp = sp.get("name", sid)
        short_text(disp, f"{name}: speaker '{sid}' name")
        speaker_ids[sid] = len(speakers)
        speakers.append((strings.add(disp), PALETTE[colour]))

    # First pass: assign node indices so targets can be resolved
    nodes = []            # dicts with resolved fields filled in pass 2
    conv_first = {}
    conv_ids = {}         # conv -> {node id -> index}
    for conv, lines in convs_src.items():
        if not ID_RE.match(conv):
            raise DialogError(f"{name}: conversation id '{conv}' must be letters, digits and _")
        if not isinstance(lines, list) or not lines:
            raise DialogError(f"{name}: conversation '{conv}' must be a non-empty list of lines")
        conv_first[conv] = len(nodes)
        conv_ids[conv] = {}
        for i, line in enumerate(lines):
            where = f"{name}: {conv}[{i}]"
            if isinstance(line, str):
                line = {"text": line}
            if not isinstance(line, dict):
                raise DialogError(f"{where}: a line is a string or an object")
            unknown = set(line) - {"id", "speaker", "text", "next", "end", "choices", "event"}
            if unknown:
                raise DialogError(f"{where}: unknown keys {sorted(unknown)}")
            if "text" not in line:
                raise DialogError(f"{where}: missing 'text'")
            nid = line.get("id")
            if nid is not None:
                if not ID_RE.match(nid):
                    raise DialogError(f"{where}: id '{nid}' must be letters, digits and _")
                if nid in conv_ids[conv]:
                    raise DialogError(f"{where}: duplicate id '{nid}' in conversation '{conv}'")
                conv_ids[conv][nid] = len(nodes)
            nodes.append({"conv": conv, "pos": i, "last": i == len(lines) - 1, "src": line, "where": where})

    def resolve(target, conv, where):
        if target.startswith("@"):
            c = target[1:]
            if c not in conv_first:
                raise DialogError(f"{where}: unknown conversation '{c}' in '{target}'")
            return conv_first[c]
        if "." in target:
            c, nid = target.split(".", 1)
            if c not in conv_ids or nid not in conv_ids[c]:
                raise DialogError(f"{where}: unknown target '{target}'")
            return conv_ids[c][nid]
        if target not in conv_ids[conv]:
            raise DialogError(f"{where}: unknown target '{target}' (ids in '{conv}': {sorted(conv_ids[conv]) or 'none'})")
        return conv_ids[conv][target]

    choices = []
    out_nodes = []
    prev_speaker = {}
    for idx, n in enumerate(nodes):
        line, conv, where = n["src"], n["conv"], n["where"]
        if n["pos"] == 0:
            prev_speaker[conv] = NONE16
        sp = line.get("speaker")
        if sp is not None:
            if sp not in speaker_ids:
                raise DialogError(f"{where}: unknown speaker '{sp}' (declare it under 'speakers')")
            spk = speaker_ids[sp]
        else:
            spk = prev_speaker[conv]
        prev_speaker[conv] = spk

        text = compile_text(line["text"], where)
        event = line.get("event")
        if event is not None and (not isinstance(event, str) or not event):
            raise DialogError(f"{where}: 'event' must be a non-empty string")

        ch = line.get("choices")
        first_choice, n_choices = 0, 0
        if ch is not None:
            if "next" in line or line.get("end"):
                raise DialogError(f"{where}: a line with 'choices' cannot also have 'next' or 'end'")
            if not isinstance(ch, list) or not 1 <= len(ch) <= MAX_CHOICES:
                raise DialogError(f"{where}: 'choices' needs 1 to {MAX_CHOICES} entries")
            first_choice, n_choices = len(choices), len(ch)
            for k, c in enumerate(ch):
                cw = f"{where}.choices[{k}]"
                if not isinstance(c, dict) or "text" not in c:
                    raise DialogError(f"{cw}: a choice is an object with 'text'")
                unknown = set(c) - {"text", "next", "if", "end"}
                if unknown:
                    raise DialogError(f"{cw}: unknown keys {sorted(unknown)}")
                ctext = short_text(c["text"], cw)
                if c.get("end"):
                    cnext = NONE16
                elif "next" in c:
                    cnext = resolve(c["next"], conv, cw)
                else:
                    raise DialogError(f"{cw}: a choice needs 'next' or \"end\": true")
                cond = c.get("if")
                choices.append((strings.add(ctext), strings.add(cond), cnext))

        if line.get("end"):
            nxt = NONE16
        elif "next" in line:
            nxt = resolve(line["next"], conv, where)
        elif n_choices:
            nxt = NONE16
        else:
            nxt = NONE16 if n["last"] else idx + 1
        out_nodes.append((strings.add(text), strings.add(event), spk, nxt, first_choice, n_choices, 0))

    if len(out_nodes) >= NONE16:
        raise DialogError(f"{name}: too many lines ({len(out_nodes)})")

    conv_names = [(strings.add(conv), first) for conv, first in conv_first.items()]

    body = bytearray()
    body += b"DLG1" + struct.pack(">HH", VERSION, 0)                       # 8 bytes
    body += struct.pack(">HHHHI", len(speakers), len(conv_names), len(out_nodes),
                        len(choices), len(strings.blob))                    # 12 bytes
    for nm, col in speakers:
        body += struct.pack(">IB3x", nm, col)                               # 8 bytes each
    for nm, first in conv_names:
        body += struct.pack(">IHxx", nm, first)                             # 8 bytes each
    for t, e, spk, nxt, fc, nc, fl in out_nodes:
        body += struct.pack(">IIHHHBB", t, e, spk, nxt, fc, nc, fl)         # 16 bytes each
    for t, cond, nxt in choices:
        body += struct.pack(">IIHxx", t, cond, nxt)                         # 12 bytes each
    body += strings.blob
    return bytes(body)


def selftest():
    doc = {
        "speakers": {"guide": {"name": "Guide", "color": "accent"}},
        "conversations": {
            "a": [
                {"speaker": "guide", "text": "Hi {player} [c=hilite]x[/c] ^ $ [[b]] {{c}} [p=100]."},
                "Second.",
                {"text": "Pick", "choices": [{"text": "One", "next": "one"}, {"text": "Two", "next": "@b", "if": "flag"}]},
                {"id": "one", "text": "End.", "end": True},
            ],
            "b": ["B line"],
        },
    }
    data = build(doc, "selftest")
    assert data[:4] == b"DLG1"
    nsp, ncv, nnd, nch, nstr = struct.unpack_from(">HHHHI", data, 8)
    assert (nsp, ncv, nnd, nch) == (1, 2, 5, 2), (nsp, ncv, nnd, nch)
    assert len(data) == 20 + 8 * nsp + 8 * ncv + 16 * nnd + 12 * nch + nstr
    assert compile_text("a[c=accent]b[/c]", "t") == "a^03b^02"
    assert compile_text("100%^", "t") == "100%^^"
    assert compile_text("[p=400]", "t") == "\x01\x14"
    for bad, msg in [({"conversations": {"a": [{"text": "x", "next": "nope"}]}}, "unknown target"),
                     ({"conversations": {"a": [{"text": "[c=pink]"}]}}, "unknown colour"),
                     ({"conversations": {"a": [{"text": "x", "speaker": "who"}]}}, "unknown speaker"),
                     ({"conversations": {"a": [{"id": "x", "text": "1"}, {"id": "x", "text": "2"}]}}, "duplicate id"),
                     ({"conversations": {"a": [{"text": "q", "choices": [{"text": "x" * 40, "end": True}]}]}}, "too long")]:
        try:
            build(bad, "bad")
        except DialogError as e:
            assert msg in str(e), (msg, str(e))
        else:
            raise AssertionError("expected an error: " + msg)
    print("dialog_build selftest OK")


def main(argv):
    if len(argv) == 2 and argv[1] == "--selftest":
        selftest()
        return 0
    if len(argv) != 4 or argv[2] != "-o":
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    src, dst = argv[1], argv[3]
    try:
        with open(src, encoding="utf-8") as f:
            doc = json.load(f)
        data = build(doc, src)
    except json.JSONDecodeError as e:
        print(f"{src}:{e.lineno}:{e.colno}: JSON error: {e.msg}", file=sys.stderr)
        return 1
    except DialogError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    with open(dst, "wb") as f:
        f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
