#!/usr/bin/env python3

import csv, difflib, os, math, re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "ESPHamClock"
DOC = ROOT / "doc" / "eeprom"
CONTRIB = ROOT / "hamclock-contrib"

ENUM_H = SRC / "nvramenum.h"
SIZE_H = SRC / "nvramsize.h"
LEN_H = SRC / "nvramlen.h"
NVRAM_CPP = SRC / "nvram.cpp"
CSV_FILE = DOC / "hceeprom.csv"
GENERATE_SH = DOC / "generate.sh"


def fail(msg):
    print(f"EEPROM layout check failed: {msg}", file=sys.stderr)
    raise SystemExit(1)


def read_text(path):
    return path.read_text(encoding="utf-8")


def parse_macro_defines():
    defines = {}
    define_re = re.compile(r"^\s*#define\s+([A-Za-z0-9_]+)\s+(.+?)\s*(?://.*)?$")

    for path in (SRC / "HamClock.h", LEN_H, NVRAM_CPP):
        for raw_line in read_text(path).splitlines():
            line = raw_line.strip()
            if not line or line.startswith("//"): continue
            m = define_re.match(raw_line)
            if not m: continue

            name, value = m.groups()
            value = value.split("/*", 1)[0].strip()
            value = value.split("//", 1)[0].strip()
            if value:
                defines[name] = value


    if "NV_BASE" not in defines: fail(f"NV_BASE not found in {NVRAM_CPP}")



    return defines


def resolve_int(expr, defines, stack=()) -> int:
    expr = expr.strip()
    if not expr:
        raise ValueError("empty expression")

    expr = expr.strip("()")
    if re.fullmatch(r"\d+", expr):
        return int(expr)

    if expr in stack:
        chain = " -> ".join(stack + (expr,))
        raise ValueError(f"circular define: {chain}")

    if expr in defines:
        return resolve_int(defines[expr], defines, stack + (expr,))

    raise ValueError(f"unsupported integer expression: {expr}")


def parse_enum_entries():
    entries = []
    enum_re = re.compile(r"^\s*(NV_[A-Za-z0-9_]+)\s*,\s*//\s*(.*?)\s*$")

    for lineno, line in enumerate(read_text(ENUM_H).splitlines(), 1):
        m = enum_re.match(line)
        if not m: continue
        name, desc = m.groups()
        if name == "NV_N": continue
        entries.append((len(entries), name, desc, lineno))

    if not entries:
        fail(f"no NV_* enum entries found in {ENUM_H}")

    return entries


def parse_size_entries(defines):
    entries = []
    size_re = re.compile(r"^\s*([^,]+),\s*//\s*(NV_[A-Za-z0-9_]+)\b")

    for lineno, line in enumerate(read_text(SIZE_H).splitlines(), 1):
        m = size_re.match(line)
        if not m: continue
        size_expr, name = m.groups()
        try:
            size = resolve_int(size_expr, defines)
        except ValueError as exc:
            fail(f"{SIZE_H.name}:{lineno}: {exc}")
        entries.append((len(entries), name, size, lineno))

    if not entries:
        fail(f"no nv_sizes[] rows found in {SIZE_H}")

    return entries


def check_enum_matches_size_table(enum_entries, size_entries):
    if len(enum_entries) != len(size_entries):
        fail(
            f"count mismatch: {ENUM_H.name} has {len(enum_entries)} NV entries, "
            f"{SIZE_H.name} has {len(size_entries)} size rows"
        )

    for enum_entry, size_entry in zip(enum_entries, size_entries):
        if enum_entry[1] != size_entry[1]:
            fail(
                f"order mismatch at NV index {enum_entry[0]}: "
                f"{ENUM_H.name}:{enum_entry[3]} has {enum_entry[1]}, "
                f"{SIZE_H.name}:{size_entry[3]} has {size_entry[1]}"
            )


def parse_csv_entries():
    rows = []
    with CSV_FILE.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        expected_fields = ["Addr", "Name", "Len", "Type", "Description"]
        if reader.fieldnames != expected_fields:
            fail(f"{CSV_FILE.name} header mismatch: expected {expected_fields}, found {reader.fieldnames}")

        for lineno, row in enumerate(reader, 2):
            try:
                rows.append(( int(row["Addr"], 16), row["Name"], int(row["Len"]), row["Type"], row["Description"], lineno,))
            except Exception as exc:
                fail(f"csv:{lineno}: invalid CSV row: {exc}")

    if not rows:
        fail(f"{CSV_FILE.name} is empty")

    return rows


def csv_want(enum_entries, size_entries, defines):
    base_addr = resolve_int("NV_BASE", defines)
    value_addr = base_addr + 1
    enum_by_name = {entry[1]: entry for entry in enum_entries}
    expected = []

    for size_entry in size_entries:
        enum_entry = enum_by_name[size_entry[1]]
        if not size_entry[1].endswith("_OLD"):
            expected.append((value_addr, size_entry[1], size_entry[2], "", enum_entry[2], enum_entry[3]))
        value_addr += 1 + size_entry[2]

    return expected


def check_csv_matches_headers(enum_entries, size_entries, defines):
    expected = csv_want(enum_entries, size_entries, defines)
    actual = parse_csv_entries()

    if len(expected) != len(actual):
        # this _dumbly_ assumes the changes are at the end of the eeprom file
        # Usually, that's how the eeprom evolves, but it can be made more user-friendly, to show all eeprom changes and errors
        delta = abs(len(expected) - len(actual))
        delta = 5 if delta > 5 else delta

        headers=['addr', 'name', 'type', 'len', 'description']
        print("C layout")
        print_ascii_table(expected[-delta:], headers)

        print("\nCSV layout")
        print_ascii_table(actual[-delta:], headers)
        
        fail(
            f"{CSV_FILE.name} row count mismatch: expected {len(expected)} entries, " f"found {len(actual)}\n"
            f"Remember to run generate.sh if you change the EEPROM layout"
        )

    for idx, (exp, got) in enumerate(zip(expected, actual)):
        if exp[1] != got[1]:
            fail(f"{CSV_FILE.name}:{got[5]}: name mismatch at row {idx}: expected {exp[1]}, found {got[1]}")
        if exp[0] != got[0]:
            fail(f"{CSV_FILE.name}:{got[5]}: address mismatch for {got[1]}: expected 0x{exp[0]:03X}, found 0x{got[0]:03X}")
        if exp[2] != got[2]:
            fail(f"{CSV_FILE.name}:{got[5]}: length mismatch for {got[1]}: expected {exp[2]}, found {got[2]}")
        if exp[4] != got[4]:
            fail(f"{CSV_FILE.name}:{got[5]}: description mismatch for {got[1]}: expected {exp[4]!r}, found {got[4]!r}")

    total_end = expected[-1][0] + expected[-1][2]
    return total_end


def write_blank_eeprom(path, nbytes):
    with path.open("w", encoding="utf-8") as f:
        for addr in range(nbytes):
            f.write(f"{addr:08X} 00\n")


def check_csv_is_generated(blank_eeprom_bytes):
    with tempfile.TemporaryDirectory(prefix="hamclock-eeprom-") as outdir, tempfile.TemporaryDirectory(
        prefix="hamclock-home-"
    ) as homedir:
        home = Path(homedir)
        eeprom = home / ".hamclock" / "eeprom"
        eeprom.parent.mkdir()
        write_blank_eeprom(eeprom, blank_eeprom_bytes)

        env = os.environ.copy()
        env["HOME"] = str(home)

        proc = subprocess.run(
            [ "bash", str(GENERATE_SH), "-s", str(SRC), "-c", str(CONTRIB), "-e", outdir, ],
            cwd=ROOT, env=env, text=True, capture_output=True, check=False,
        )

        if proc.returncode != 0:
            fail(f"generate.sh failed with code {proc.returncode}\n\nstdout:\n{proc.stdout}\n\nstderr:\n{proc.stderr}")

        if proc.stderr.strip():
            fail(f"generate.sh got stderr:\n{proc.stderr}")

        generated = Path(outdir) / "hceeprom.csv"
        if not generated.exists():
            fail("generate.sh did not create hceeprom.csv")

        committed_text = read_text(CSV_FILE)
        generated_text = read_text(generated)

        if committed_text != generated_text:
            diff = "".join(
                difflib.unified_diff(
                    committed_text.splitlines(keepends=True),
                    generated_text.splitlines(keepends=True),
                    fromfile=str(CSV_FILE),
                    tofile="generated hceeprom.csv",
                )
            )
            fail("hceeprom.csv is old; run doc/eeprom/generate.sh\n" + diff)

def print_ascii_table(rows, headers, order=None):
    type_names = {'i': 'int', 'f': 'flt', 's': 'str', 'RGB': 'rgb'}

    if order:
        rows    = [[row[i] for i in order] for row in rows]
        headers = [headers[i] for i in order]

    rows = [
        [type_names.get(v, v) if isinstance(v, str) and v in type_names else v for v in row]
        for row in rows
    ]

    col_widths = [
        max(len(str(v)) for v in [headers[col]] + [row[col] for row in rows])
        for col in range(len(headers))
    ]

    budget = 80 - (3 * len(headers) + 1)
    while sum(col_widths) > budget:
        col_widths[col_widths.index(max(col_widths))] -= 1

    def cell(v, w):
        s = str(v)
        return (s[:w - 2] + '..') if len(s) > w else s.ljust(w)

    sep = "+-" + "-+-".join("-" * w for w in col_widths) + "-+"
    fmt = lambda row: "| " + " | ".join(cell(v, w) for v, w in zip(row, col_widths)) + " |"

    print(sep)
    print(fmt(headers))
    print(sep)
    for row in rows:
        print(fmt(row))
    print(sep)

def main() -> int: # not void
    defines = parse_macro_defines()
    enum_entries = parse_enum_entries()
    size_entries = parse_size_entries(defines)

    check_enum_matches_size_table(enum_entries, size_entries)
    blank_eeprom_bytes = check_csv_matches_headers(enum_entries, size_entries, defines)
    check_csv_is_generated(blank_eeprom_bytes)

    print("All EEPROM checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
