"""Edits cells in a game table (Excel XML: cars.xml, shop_presets.xml, etc.),
keeping the rest of the file byte-for-byte (including CRLF and formatting).

Usage:
    python patch_table.py <input.xml> <output.xml> <row sysname> [--clone <new sysname>] <edits> ...

    column=value       -- replace the cell value
    column+=suffix     -- append to the end of the cell (for example, ",repair")
    column,+=item      -- add an item to a comma-separated list (a comma only if needed:
                          the cell is not empty and does not end with a comma)
    --clone NEW        -- copy the row under the name NEW (inserted right after
                          the original) and apply the edits to the copy; the original row is unchanged

Columns are looked up by name in the first row of the sheet, taking ss:Index into account.
In multi-sheet files the first row found with that sysname is edited.
"""
import re
import sys

ROW_RE = re.compile(r"<Row\b[^>]*?(?:/>|>.*?</Row>)", re.S)
CELL_RE = re.compile(r"<Cell\b[^>]*?(?:/>|>.*?</Cell>)", re.S)
DATA_RE = re.compile(r"(<Data\b[^>]*>)(.*?)(</Data>)", re.S)
INDEX_RE = re.compile(r'ss:Index="(\d+)"')
TABLE_RE = re.compile(r"<Table\b.*?</Table>", re.S)
ROW_COUNT_RE = re.compile(r'ss:ExpandedRowCount="(\d+)"')
ROW_INDEX_RE = re.compile(r'(<Row\b[^>]*?ss:Index=")(\d+)(")')


def cells(row):
    """[(column_number, match)] taking ss:Index into account."""
    out, col = [], 0
    for m in CELL_RE.finditer(row):
        idx = INDEX_RE.search(m.group()[: m.group().find(">")])
        col = int(idx.group(1)) if idx else col + 1
        out.append((col, m))
    return out


def cell_text(cell):
    """Cell text (the contents of <Data>), an empty string if there is no data."""
    d = DATA_RE.search(cell)
    return d.group(2) if d else ""


def parse_edits(args):
    """Parse the command-line edits into (column, value, mode): False - replace, True - append,
    "list" - add an item to a comma-separated list."""
    edits = []
    for a in args:
        if ",+=" in a:
            name, value = a.split(",+=", 1)
            edits.append((name, value, "list"))
        elif "+=" in a:
            name, value = a.split("+=", 1)
            edits.append((name, value, True))
        elif "=" in a:
            name, value = a.split("=", 1)
            edits.append((name, value, False))
        else:
            sys.exit(f"Unrecognized edit: {a}")
    return edits


def find_row(text, sysname):
    """(table_offset, header, row match) for the first sheet that has the row."""
    for t in TABLE_RE.finditer(text):
        rows = list(ROW_RE.finditer(t.group()))
        if not rows:
            continue
        header = {cell_text(m.group()): col for col, m in cells(rows[0].group())}
        hits = [r for r in rows[1:] if cells(r.group()) and cell_text(cells(r.group())[0][1].group()) == sysname]
        if len(hits) > 1:
            sys.exit(f"Rows {sysname} on the sheet: {len(hits)}, expected 1")
        if hits:
            return t.start(), header, hits[0]
    sys.exit(f"Row {sysname} not found")


def clone_row(text, sysname, new_sysname):
    """Inserts a copy of row sysname named new_sysname right after it.
    Increments the table's ss:ExpandedRowCount and shifts ss:Index of the following rows."""
    base, _, row_m = find_row(text, sysname)
    table_m = TABLE_RE.search(text, base)
    table = table_m.group()
    row_start, row_end = row_m.start(), row_m.end()
    row = row_m.group()

    first = cells(row)[0][1]
    d = DATA_RE.search(first.group())
    new_first = first.group()[: d.start(2)] + new_sysname + first.group()[d.end(2):]
    clone = row[: first.start()] + new_first + row[first.end():]
    # Repeat the indent before the row (line break + spaces) for the copy
    indent = re.search(r"\r?\n[ \t]*$", table[:row_start]).group()

    tail = ROW_INDEX_RE.sub(lambda m: m.group(1) + str(int(m.group(2)) + 1) + m.group(3), table[row_end:])
    table = table[:row_end] + indent + clone + tail
    head_end = table.find(">")
    count = ROW_COUNT_RE.search(table[:head_end])
    if count:
        table = table[: count.start(1)] + str(int(count.group(1)) + 1) + table[count.end(1):]
    print(f"{sysname} -> copied as {new_sysname}")
    return text[: table_m.start()] + table + text[table_m.end():]


def main(src, dst, sysname, edit_args):
    """Edit one table row: with --clone first insert its copy, then change the cells and
    write the file (the rest of the text byte-for-byte)."""
    text = open(src, encoding="utf-8", newline="").read()
    if edit_args[:1] == ["--clone"]:
        new_sysname = edit_args[1]
        text = clone_row(text, sysname, new_sysname)
        sysname, edit_args = new_sysname, edit_args[2:]
    base, header, row_m = find_row(text, sysname)
    edits = parse_edits(edit_args)
    missing = [n for n, _, _ in edits if n not in header]
    if missing:
        sys.exit(f"No columns {missing} in the header")

    row = row_m.group()
    by_col = {col: m for col, m in cells(row)}
    # Edit from the end of the row so the offsets do not shift
    for name, value, append in sorted(edits, key=lambda e: -header[e[0]]):
        col = header[name]
        m = by_col.get(col)
        if m is None:
            sys.exit(f"Row {sysname} has no cell for column {name} (col {col}); a manual edit is needed")
        cell = m.group()
        d = DATA_RE.search(cell)
        old = d.group(2) if d else ""
        if append == "list":
            new = old + ("," if old.strip() and not old.rstrip().endswith(",") else "") + value
        else:
            new = old + value if append else value
        if d:
            new_cell = cell[: d.start()] + d.group(1) + new + d.group(3) + cell[d.end():]
        else:  # an empty cell <Cell .../>
            new_cell = cell[:-2] + '><Data ss:Type="String">' + new + "</Data></Cell>"
        row = row[: m.start()] + new_cell + row[m.end():]
        print(f"{sysname}.{name}: '{old}' -> '{new}'")

    start, end = base + row_m.start(), base + row_m.end()
    text = text[:start] + row + text[end:]
    open(dst, "w", encoding="utf-8", newline="").write(text)


if __name__ == "__main__":
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:])
