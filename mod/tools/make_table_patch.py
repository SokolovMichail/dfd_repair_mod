"""Write a DefianceLoader table-merge patch: what a mod changes in a game table, as a table of its own.

    python make_table_patch.py <stock.xml> <modded.xml> <out.patch.xml>

stock.xml is the game's table, modded.xml the same table with the mod's edits (as patch_table.py writes it). The
patch gets a `sysname` column and only the columns the mod changes:
  - a row the stock table lacks (a new vehicle, say) - in full, every non-empty cell;
  - a row the mod changed - only its changed cells; a cell the mod emptied - `$empty`.
Rows are matched on `sysname`, as the loader's defiance.table-merge plugin does (see its README: an empty patch cell
keeps the stock value, a row the table lacks is appended). A row the mod removed would need `$delete`; the mod
never removes rows, so that is refused.

The patch is an Excel-XML workbook like the game's own: the stock file's header (styles and all), one worksheet,
and the modded cells copied as they are (types, styles and escaping unchanged). Only the first sheet that has a
`sysname` column counts - the game keys its tables on it.

After writing, the patch is read back and merged into the stock table the way the loader merges it; the result
must have exactly the rows and cells of modded.xml, or the script fails. So a patch that would not reproduce the
mod's table never gets into a build.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from patch_table import CELL_RE, DATA_RE, INDEX_RE, ROW_RE, TABLE_RE, cell_text, cells  # noqa: E402

KEY = 'sysname'
EMPTY = '$empty'
WORKSHEET_RE = re.compile(r'<Worksheet\b[^>]*?ss:Name="([^"]*)"[^>]*>(.*?)</Worksheet>', re.S)
MERGE_RE = re.compile(r'ss:Merge(?:Across|Down)=')


class Table:
    """The first sheet of an Excel-XML workbook that has a `sysname` column: its name, column names (by 1-based
    column number), and its rows in order as (sysname, {column number: cell XML})."""

    def __init__(self, path):
        self.text = open(path, encoding='utf-8', newline='').read()
        for ws in WORKSHEET_RE.finditer(self.text):
            t = TABLE_RE.search(ws.group(2))
            rows = list(ROW_RE.finditer(t.group())) if t else []
            if not rows:
                continue
            header = {col: cell_text(m.group()) for col, m in cells(rows[0].group())}
            if KEY not in header.values():
                continue
            self.sheet = ws.group(1)
            self.columns = header
            key_col = next(c for c, n in header.items() if n == KEY)
            self.rows = []
            for r in rows[1:]:
                by_col = {col: m.group() for col, m in cells(r.group())}
                self.rows.append((cell_text(by_col.get(key_col, '')), by_col))
            return
        sys.exit(f'{path}: no sheet with a {KEY} column')

    def column(self, name):
        """Column number of the column called name, or None."""
        return next((c for c, n in self.columns.items() if n == name), None)

    def records(self):
        """{sysname: {column name: text}} of the rows with a sysname, non-empty cells only; a sysname that comes
        twice is refused (a patch could not tell the two rows apart)."""
        out = {}
        for key, by_col in self.rows:
            if not key:
                continue  # blank spreadsheet rows; the loader ignores them too
            if key in out:
                sys.exit(f'{KEY} {key!r} comes twice in sheet {self.sheet!r}; a patch cannot address such a row')
            out[key] = {self.columns[c]: cell_text(x) for c, x in by_col.items()
                        if c in self.columns and self.columns[c] and cell_text(x)}
        return out


def diff(stock, modded):
    """The patch as (column names, [(sysname, {column name: cell XML or None for $empty})]), in modded row order."""
    old = stock.records()
    new_rows = modded.records()
    gone = [k for k in old if k not in new_rows]
    if gone:
        sys.exit(f'rows removed by the mod: {gone}; patches here never delete rows')
    patch, used = [], set()
    xml_of = {key: by_col for key, by_col in modded.rows if key}
    for key, cells_now in new_rows.items():
        cells_then = old.get(key)
        changed = {}
        for name, text in cells_now.items():
            if name == KEY:
                continue
            if cells_then is None or cells_then.get(name, '') != text:
                changed[name] = xml_of[key][modded.column(name)]
        if cells_then is not None:
            for name in cells_then:
                if name != KEY and name not in cells_now:
                    changed[name] = None  # emptied by the mod
        if changed or cells_then is None:
            patch.append((key, changed))
            used.update(changed)
    columns = [KEY] + [n for c, n in sorted(modded.columns.items()) if n in used]
    return columns, patch


def placed(cell, col):
    """A copy of a cell's XML with ss:Index set to col (the patch has its own column order)."""
    head_end = cell.find('>')
    attrs = cell[len('<Cell'):head_end].rstrip()
    if MERGE_RE.search(attrs):
        sys.exit(f'merged cell cannot be copied into a patch: {cell[:80]}')
    closed = attrs.endswith('/')  # <Cell .../>
    attrs = re.sub(r'\s+', ' ', INDEX_RE.sub('', attrs.rstrip('/'))).strip()
    head = f'<Cell ss:Index="{col}"' + (' ' + attrs if attrs else '') + ('/' if closed else '')
    return head + cell[head_end:]


def string_cell(col, text):
    return f'<Cell ss:Index="{col}"><Data ss:Type="String">{text}</Data></Cell>'


def write(stock, modded, columns, patch, out):
    """The patch workbook: the stock file up to its first worksheet, then one sheet with the patch rows."""
    nl = '\r\n' if '\r\n' in stock.text else '\n'
    prefix = stock.text[:stock.text.find('<Worksheet')]
    rows = ['   <Row>' + ''.join(string_cell(i + 1, name) for i, name in enumerate(columns)) + '</Row>']
    key_xml = {key: by_col for key, by_col in modded.rows if key}
    for key, changed in patch:
        parts = [placed(key_xml[key][modded.column(KEY)], 1)]
        for i, name in enumerate(columns[1:], start=2):
            if name not in changed:
                continue
            parts.append(placed(changed[name], i) if changed[name] is not None else string_cell(i, EMPTY))
        rows.append('   <Row>' + ''.join(parts) + '</Row>')
    sheet = (f' <Worksheet ss:Name="{modded.sheet}">{nl}'
             f'  <Table ss:ExpandedColumnCount="{len(columns)}" ss:ExpandedRowCount="{len(rows)}"'
             f' x:FullColumns="1" x:FullRows="1">{nl}' + nl.join(rows) + f'{nl}  </Table>{nl} </Worksheet>{nl}')
    with open(out, 'w', encoding='utf-8', newline='') as f:
        f.write(prefix + sheet + '</Workbook>' + nl)


def merge(stock, patch_table):
    """stock records merged with a patch the way defiance.table-merge does it: a row the stock has gets the patch's
    non-empty cells ($empty clears one), a row it lacks is added."""
    out = {k: dict(v) for k, v in stock.records().items()}
    names = patch_table.columns
    for key, by_col in patch_table.rows:
        if not key:
            continue
        row = out.setdefault(key, {KEY: key})
        for c, x in by_col.items():
            name, text = names.get(c, ''), cell_text(x)
            if not name or name == KEY or not text:
                continue
            if text == EMPTY:
                row.pop(name, None)
            else:
                row[name] = text
    return out


def main(stock_path, modded_path, out):
    stock, modded = Table(stock_path), Table(modded_path)
    columns, patch = diff(stock, modded)
    write(stock, modded, columns, patch, out)
    merged = merge(stock, Table(out))
    expected = modded.records()
    if merged != expected:
        bad = sorted(k for k in set(merged) | set(expected) if merged.get(k) != expected.get(k))
        sys.exit(f'{out}: stock + patch does not give the modded table; rows that differ: {bad[:10]}')
    print(f'{os.path.basename(out)}: {len(patch)} rows, columns {len(columns) - 1}; stock + patch = modded table')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
