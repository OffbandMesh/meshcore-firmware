"""The capture's emit() keeps capturing through anything it cannot display (#1514).
Run: python scripts/test_cap_serial_emit.py
  or python -m pytest scripts/test_cap_serial_emit.py -q

A live bench capture on qcc-badge-1 ended mid-session when a node name carrying an
emoji (U+1F331) reached print(): the console is cp1252 on Windows, print() raised
UnicodeEncodeError out of emit(), and the process died -- so the line never reached the
log file even though the file is opened UTF-8 and would have accepted it.

The defect was the ORDER. A lossy display path was gating the durable one. These tests
pin the order and the swallow, because the failure they prevent is silent: a capture that
stops without saying so reads as "nothing happened" to whoever uses the log afterwards.

No serial hardware is touched -- emit()'s behaviour is reproduced against a file and a
deliberately hostile stdout.
"""
import io
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CAP = ROOT / "scripts" / "_cap_serial.py"

FAILED = []


def check(cond, name, detail=""):
    if cond:
        print("PASS %s" % name)
    else:
        FAILED.append(name)
        print("FAIL %s%s" % (name, (": " + detail) if detail else ""))


class Cp1252Stdout(io.TextIOBase):
    """A console that refuses anything outside cp1252, like a default Windows terminal."""

    def __init__(self):
        self.written = []
        self.reconfigured = False

    def write(self, s):
        s.encode("cp1252")   # raises UnicodeEncodeError on an emoji
        self.written.append(s)
        return len(s)

    def flush(self):
        pass

    def reconfigure(self, **kw):
        # Model a stream that cannot be reconfigured: the guarantee must not depend on it.
        raise io.UnsupportedOperation("cannot reconfigure")


def emit_like_cap_serial(line, out_fh, stdout):
    """emit()'s body, in the order _cap_serial.py uses it.

    Kept as a copy rather than imported: _cap_serial.py builds emit() as a closure inside
    main(), after it has opened a serial port. Importing would mean faking pyserial and a
    port to reach three lines. The ORDER is what these tests pin, and the source is
    asserted to still have it by test_source_writes_file_before_printing below.
    """
    out_fh.write(line + "\n")
    out_fh.flush()
    try:
        stdout.write(line + "\n")
        stdout.flush()
    except (UnicodeEncodeError, OSError, ValueError):
        pass


def test_an_unencodable_line_still_reaches_the_file():
    """The regression itself: an emoji must not cost the capture."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "cap.log"
        with open(p, "w", encoding="utf-8", newline="\n") as fh:
            stdout = Cp1252Stdout()
            emit_like_cap_serial("advert from Seedling \U0001f331", fh, stdout)
            emit_like_cap_serial("the next line, which used to be lost", fh, stdout)
        text = p.read_text(encoding="utf-8")
    check("\U0001f331" in text, "the unencodable line is in the log",
          "log was: %r" % text)
    check("the next line, which used to be lost" in text,
          "capture continues after an unencodable line",
          "a crash here is the original bug")


def test_the_console_keeps_what_it_can_render():
    """Swallowing the console error must not silence the console generally."""
    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "cap.log"
        with open(p, "w", encoding="utf-8", newline="\n") as fh:
            stdout = Cp1252Stdout()
            emit_like_cap_serial("plain ascii line", fh, stdout)
            emit_like_cap_serial("emoji \U0001f331", fh, stdout)
            emit_like_cap_serial("another plain line", fh, stdout)
    check(any("plain ascii line" in w for w in stdout.written),
          "a renderable line still prints")
    check(any("another plain line" in w for w in stdout.written),
          "printing resumes after a refused line",
          "the except must not latch")


def test_a_dead_stdout_does_not_end_the_capture():
    """A closed or redirected console is the same class of problem as an emoji."""
    class DeadStdout(io.TextIOBase):
        def write(self, s):
            raise OSError("stdout is gone")

        def flush(self):
            raise OSError("stdout is gone")

        def close(self):
            # Without this, TextIOBase's finaliser flushes at garbage-collection time,
            # the flush above raises, and Python prints an "Exception ignored in:"
            # traceback. The test still passed -- but a suite that prints a traceback
            # while reporting PASS teaches people to skim its output.
            pass

    with tempfile.TemporaryDirectory() as d:
        p = Path(d) / "cap.log"
        with open(p, "w", encoding="utf-8", newline="\n") as fh:
            emit_like_cap_serial("line one", fh, DeadStdout())
            emit_like_cap_serial("line two", fh, DeadStdout())
        text = p.read_text(encoding="utf-8")
    check("line one" in text and "line two" in text,
          "a dead stdout costs nothing from the log")


def test_source_writes_file_before_printing():
    """Pin the ORDER in the real source, since the body above is a copy.

    Without this, someone could reorder emit() back and these tests would still pass.
    """
    src = CAP.read_text(encoding="utf-8")
    start = src.find("def emit(line):")
    check(start != -1, "emit() found in _cap_serial.py")
    if start == -1:
        return
    body = src[start:start + 1200]
    write_at = body.find("out_fh.write")
    print_at = body.find("print(line)")
    check(write_at != -1 and print_at != -1, "emit() still writes and prints")
    if write_at == -1 or print_at == -1:
        return
    check(write_at < print_at,
          "emit() writes the FILE before it prints",
          "print() before the write is the #1514 bug: a console error kills the capture "
          "and the line never lands")
    check("except (UnicodeEncodeError" in body,
          "emit()'s print is wrapped so it cannot raise")


def main():
    test_an_unencodable_line_still_reaches_the_file()
    test_the_console_keeps_what_it_can_render()
    test_a_dead_stdout_does_not_end_the_capture()
    test_source_writes_file_before_printing()
    print()
    print("%d failure(s)" % len(FAILED))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
