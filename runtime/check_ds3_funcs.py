#!/usr/bin/env python3
"""Cross-check the recompiled function table against the guest image and code.

The recompiler discovers functions from direct call/jump targets, exports and
vtables.  A target it did not discover has no ds3_<va> body, so the generated C
either fails to link (direct call) or the runtime stops with
"dispatch: no function at target" (indirect call through a pointer).

Exact checks (a failure is a build/runtime bug):
  * every ds3_funcs entry matches its ds3_<va> symbol and lies inside .text,
  * every ds3_<va>(c) reference in the generated C has a table entry,
  * every `call 0x...` target is a table entry or a local label of the same
    function (tail call the recompiler folded into the body).

Heuristic check (a warning, needs a human look):
  * a 4-byte value in .rdata/.data points into .text, is not a table entry, is
    not inside any generated function body and starts with a function prologue.
    That is what a function only reached through a vtable looks like when the
    recompiler missed it.

Usage: check_ds3_funcs.py IMAGE GUEST_DIR
       GUEST_DIR holds base/, wide/ and dispatch/ (module.c + the code_*.c).

Exit status: 0 clean, 1 a check failed or candidates were found, 2 bad input.
"""
import glob
import os
import re
import struct
import sys

TABLE_RE = re.compile(r"\{\s*0x([0-9a-fA-F]+)u\s*,\s*ds3_([0-9a-fA-F]+)\s*(?:,\s*0x[0-9a-fA-F]+u\s*)?\}")
FUNC_RE = re.compile(r"void ds3_([0-9a-f]+)\(CPU\* c\) \{(.*?)\n\}", re.S)
ADDR_RE = re.compile(r"/\* ([0-9a-f]{6,8}):")
CALL_RE = re.compile(r"/\* [0-9a-f]+: call 0x([0-9a-f]+) \*/")
REF_RE = re.compile(r"\bds3_([0-9a-f]{6,8})\(c\)")
LABEL_RE = re.compile(r"^L_([0-9a-f]+):", re.M)


def fail(msg):
    print("check_ds3_funcs: " + msg, file=sys.stderr)
    return 2


def sections(image):
    if len(image) < 0x40 or image[:2] != b"MZ":
        raise ValueError("not a PE image")
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    if image[pe:pe + 4] != b"PE\0\0":
        raise ValueError("no PE signature")
    nsec = struct.unpack_from("<H", image, pe + 6)[0]
    optsz = struct.unpack_from("<H", image, pe + 20)[0]
    opt = pe + 24
    base = struct.unpack_from("<I", image, opt + 28)[0]
    size = struct.unpack_from("<I", image, opt + 56)[0]
    if size != len(image):
        raise ValueError("SizeOfImage %#x does not match the file size %#x" % (size, len(image)))
    out = []
    sh = opt + optsz
    for i in range(nsec):
        s = sh + 40 * i
        name = image[s:s + 8].rstrip(b"\0").decode("latin-1")
        vsize, rva, rawsize, rawptr = struct.unpack_from("<IIII", image, s + 8)
        out.append((name, rva, base + rva, vsize, rawptr, rawsize))
    return base, size, out


def read_table(path):
    text = open(path, "r", encoding="utf-8", errors="replace").read()
    entries, bad = {}, []
    for va, sym in TABLE_RE.findall(text):
        va, sym = int(va, 16), int(sym, 16)
        if sym != va:
            bad.append((va, sym))
        entries[va] = entries.get(va, 0) + 1
    return entries, bad, sorted(va for va, n in entries.items() if n > 1)


def read_code(guest):
    """Per-function body span, local labels, call targets and ds3_ references."""
    spans, labels, calls, refs = {}, {}, {}, set()
    files = sorted(glob.glob(os.path.join(guest, "*", "*.c")))
    for path in files:
        text = open(path, "r", encoding="utf-8", errors="replace").read()
        refs.update(int(a, 16) for a in REF_RE.findall(text))
        for m in FUNC_RE.finditer(text):
            va, body = int(m.group(1), 16), m.group(2)
            addrs = [int(a, 16) for a in ADDR_RE.findall(body)]
            if addrs:
                spans[va] = max(addrs)
            labels[va] = {int(a, 16) for a in LABEL_RE.findall(body)}
            for a in CALL_RE.findall(body):
                calls.setdefault(int(a, 16), va)
    return files, spans, labels, calls, refs


def prologue(image, base, va):
    b = image[va - base:va - base + 6]
    if len(b) < 3:
        return False
    return ((b[0] == 0x55 and b[1] == 0x8B and b[2] == 0xEC) or          # push ebp; mov ebp, esp
            (b[0] == 0x8B and b[1] == 0xFF and b[2] == 0x55) or          # mov edi, edi; push ebp
            (b[0] in (0x53, 0x56, 0x57) and b[1] == 0x8B) or             # push ebx/esi/edi; mov
            (b[0] == 0x83 and b[1] == 0xEC) or (b[0] == 0x81 and b[1] == 0xEC))  # sub esp, imm


def pointer_targets(image, secs, text_va, text_end):
    found = {}
    for name, rva, va, vsize, rawptr, rawsize in secs:
        if name == ".text" or not rawsize:
            continue
        lo = rawptr
        hi = min(rawptr + rawsize, rawptr + vsize, len(image))
        for p in range(lo, hi - 3, 4):
            v = struct.unpack_from("<I", image, p)[0]
            if text_va <= v < text_end:
                found.setdefault(v, va + (p - rawptr))
    return found


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    seeds_out = next((a.split("=", 1)[1] for a in argv[1:] if a.startswith("--seeds-out=")), None)
    if len(args) != 2:
        return fail("usage: check_ds3_funcs.py IMAGE GUEST_DIR [--seeds-out=FILE]")
    image_path, guest = args
    try:
        image = open(image_path, "rb").read()
    except OSError as e:
        return fail("cannot read %s: %s" % (image_path, e))
    try:
        base, size, secs = sections(image)
    except (ValueError, struct.error) as e:
        return fail("%s: %s" % (image_path, e))
    module = os.path.join(guest, "dispatch", "module.c")
    try:
        entries, bad, dup = read_table(module)
    except OSError as e:
        return fail("cannot read %s: %s" % (module, e))
    if not entries:
        return fail("%s: no ds3_funcs entries found" % module)
    try:
        files, spans, labels, calls, refs = read_code(guest)
    except OSError as e:
        return fail("cannot read %s: %s" % (guest, e))
    if not files:
        return fail("%s: no generated code_*.c files" % guest)
    text = next((s for s in secs if s[0] == ".text"), None)
    if text is None:
        return fail("%s: no .text section" % image_path)
    text_va, text_end = text[2], text[2] + text[3]

    outside = sorted(va for va in entries if not (text_va <= va < text_end))
    miss_refs = sorted(refs - entries.keys())
    miss_calls = sorted(t for t, owner in calls.items()
                        if t not in entries and t not in labels.get(owner, ()))
    ptrs = pointer_targets(image, secs, text_va, text_end)
    starts = sorted(spans)
    import bisect
    cands = []
    for va in sorted(ptrs):
        if va in entries:
            continue
        i = bisect.bisect_right(starts, va) - 1
        if i >= 0 and starts[i] <= va <= spans[starts[i]]:
            continue
        if prologue(image, base, va):
            cands.append((va, ptrs[va]))

    print("image      %s  base %#x  size %#x" % (image_path, base, size))
    print("table      %s  %d entries" % (module, len(entries)))
    print("code       %d files, %d functions, %d call targets, %d ds3_ references"
          % (len(files), len(spans), len(calls), len(refs)))
    print("pointers   %d unique .text addresses in .rdata/.data" % len(ptrs))
    hard = False
    for label, items in (("symbol/address mismatch", bad), ("duplicate address", dup),
                         ("entry outside .text", outside), ("ds3_ reference without entry", miss_refs),
                         ("call target without entry", miss_calls)):
        if not items:
            continue
        hard = True
        print("FAILED     %d %s" % (len(items), label))
        for item in items[:20]:
            print("           %s" % (("%#x -> ds3_%x" % item) if isinstance(item, tuple) else "%#x" % item))
        if len(items) > 20:
            print("           ... %d more" % (len(items) - 20))
    if cands:
        print("WARNING    %d possible missed function(s): data pointer, no entry, no body, prologue"
              % len(cands))
        for va, site in cands[:40]:
            print("           %#x  (pointer at %#x, bytes %s)"
                  % (va, site, image[va - base:va - base + 6].hex()))
        if len(cands) > 40:
            print("           ... %d more" % (len(cands) - 40))
        if seeds_out:
            with open(seeds_out, "w", encoding="utf-8") as f:
                f.write("".join("%x\n" % va for va, _ in cands))
            print("seeds      %s  (%d addresses for recomp.py --seeds)" % (seeds_out, len(cands)))
    ok = not hard and not cands
    print("result     %s" % ("OK: every reachable .text target is covered" if ok else "FAILED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
