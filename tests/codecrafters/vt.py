"""A small terminal screen, enough to judge what a line-oriented shell shows.

CodeCrafters' tester feeds the shell's output to a virtual terminal and asserts on the rows of
the screen. This is the same idea reduced to what this shell (and the programs the tests run)
produce: printable characters, \\r, \\n, \\b and the bell. Escape sequences are skipped.
"""

import threading


class Screen:
    def __init__(self, columns=80):
        self.columns = columns
        self._cells = [[]]  # One list of characters per row; rows only grow
        self._row = 0
        self._col = 0
        self._escape = None  # None, "esc" after ESC, "csi" inside a CSI sequence
        self._pending_utf8 = b""
        self._lock = threading.Lock()
        self.bells = 0

    # ---- input ------------------------------------------------------------------------

    def feed(self, data):
        with self._lock:
            text = (self._pending_utf8 + data).decode("utf-8", errors="ignore")
            self._pending_utf8 = b""
            for char in text:
                self._put(char)

    def _put(self, char):
        if self._escape == "esc":
            self._escape = "csi" if char == "[" else None
            return
        if self._escape == "csi":
            if "@" <= char <= "~":  # The final byte ends the sequence
                self._escape = None
            return

        if char == "\x1b":
            self._escape = "esc"
        elif char == "\a":
            self.bells += 1
        elif char == "\r":
            self._col = 0
        elif char == "\n":
            self._row += 1
            self._ensure_row(self._row)
        elif char == "\b":
            self._col = max(0, self._col - 1)
        elif char == "\t":
            self._col = (self._col // 8 + 1) * 8
        elif char >= " ":
            if self._col >= self.columns:  # Auto wrap
                self._row += 1
                self._col = 0
                self._ensure_row(self._row)
            row = self._cells[self._row]
            while len(row) <= self._col:
                row.append(" ")
            row[self._col] = char
            self._col += 1

    def _ensure_row(self, index):
        while len(self._cells) <= index:
            self._cells.append([])

    # ---- output -----------------------------------------------------------------------

    def row(self, index):
        """The text of a row as the tester sees it: blanks right of the cursor are dropped, and
        the blanks before the cursor are kept (`$ ` is a prompt, not an empty line)."""
        with self._lock:
            if index >= len(self._cells):
                return ""
            cells = self._cells[index]
            if index == self._row:
                before = "".join(cells[: self._col])
                before = before.ljust(self._col)
                after = "".join(cells[self._col:]).rstrip(" ")
                return before + after
            return "".join(cells).rstrip(" ")

    def cursor_row(self):
        with self._lock:
            return self._row

    def rows(self):
        with self._lock:
            count = len(self._cells)
        return [self.row(i) for i in range(count)]

    def dump(self):
        return "\n".join("%3d| %s" % (i, text) for i, text in enumerate(self.rows()))
