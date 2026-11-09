#!/usr/bin/env python3
"""Serial tool for microreader2 device.

Interactive mode (default):
    python serial_cmd.py [--port COM4] [--baud 115200]

Capture mode (non-interactive, saves output to file):
    python serial_cmd.py --capture bench.log [--reset] [--timeout 300] [--done-marker "=== DONE:"]

Benchmark mode (non-interactive, sends bench command and saves output):
    python serial_cmd.py --bench ohler.epub [--capture bench.log] [--timeout 300]

Image-size benchmark all books (non-interactive):
    python serial_cmd.py --imgbench-all [--timeout 120]

Set the device clock to this computer's local time (used by Reading Stats):
    python serial_cmd.py --sync-time

Interactive commands:
    btn <N>           Press button N (0=back, 1=select, 2=down/next, 3=up/prev, 4=up, 5=down, 6=power)
    back              Alias for btn 0
    select / sel      Alias for btn 1
    down / next       Alias for btn 2
    up / prev         Alias for btn 3
    status / st       Query heap status
    books / ls        List books on SD card (full paths)
    open <path>       Open book by full path
    test [filter] [--clean] [-v] [--timeout N]
                      Test books: open each and watch for BOOK_OK/BOOK_FAIL.
                      --clean deletes .mrb files first (tests full conversion).
                      filter narrows to books whose filename contains the string.
    clear             Delete all .mrb files on the device
    rm <path>         Delete a file by full path
    upload <file>     Upload an EPUB file to the device
    bench <book>      Run full EPUB conversion benchmark for a book
    imgsize <book>    Run image format+size benchmark for a book
    imgdecode <book>  Fully decode every image in a book (streaming decoder test)
    renderbench       Benchmark render_page_ on the currently open page (20 iterations)
    time              Set the device clock to this computer's local time
    help              Show this help
    quit / exit       Exit
"""

import argparse
import struct
import sys
import time
import zlib
from pathlib import Path
from typing import Optional

import serial

MAGIC = b"CMND"


def send_button(ser: serial.Serial, mask: int) -> str:
    ser.write(MAGIC + b"B" + bytes([mask & 0xFF]))
    return read_response(ser)


def send_status(ser: serial.Serial) -> str:
    ser.write(MAGIC + b"S")
    return read_response(ser)


def send_list_books(ser: serial.Serial) -> str:
    ser.write(MAGIC + b"L")
    return read_multiline_response(ser)


def send_open(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser)


def send_bench(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"X" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser, timeout=10.0)


def send_imgbench(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"I" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser, timeout=10.0)


def send_imgdecode(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"D" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser, timeout=10.0)


def send_flashbench(ser: serial.Serial) -> str:
    ser.write(MAGIC + b"G")
    return read_response(ser, timeout=5.0)


def send_renderbench(ser: serial.Serial, iterations: int = 20) -> str:
    """Send 'P' command to benchmark render_page_ on the currently open page.
    Returns the initial OK/ERR, then streams per-iteration and summary lines."""
    ser.write(MAGIC + b"P")
    return read_response(ser, timeout=5.0)


def local_epoch_seconds() -> int:
    """Local wall time as seconds since 1970-01-01 (the device clock has no time zone)."""
    now = time.time()
    offset = time.localtime(now).tm_gmtoff or 0
    return int(now) + int(offset)


def send_set_time(ser: serial.Serial, seconds: Optional[int] = None) -> str:
    """Set the device wall clock via CMND 'E' + 8-byte LE local seconds."""
    if seconds is None:
        seconds = local_epoch_seconds()
    ser.write(MAGIC + b"E" + struct.pack("<q", seconds))
    return read_response(ser)


def read_response(ser: serial.Serial, timeout: float = 3.0) -> str:
    """Read lines until we get OK, ERR:, or STATUS: response."""
    deadline = time.time() + timeout
    lines = []
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if (
            line.startswith("OK")
            or line.startswith("ERR:")
            or line.startswith("STATUS:")
        ):
            return line
        # Skip ESP log lines
        lines.append(line)
    return "TIMEOUT (no response)"


def read_multiline_response(ser: serial.Serial, timeout: float = 5.0) -> str:
    """Read lines until END marker."""
    deadline = time.time() + timeout
    result = []
    started = False
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("BOOKS:"):
            started = True
            continue
        if line == "END":
            break
        if line.startswith("ERR:"):
            return line
        if started:
            result.append(line)
        # Skip ESP log lines before BOOKS:
    return "\n".join(result) if result else "(no books found)"


def upload_file(ser: serial.Serial, filepath: Path, magic: bytes) -> bool:
    """Upload a file to the device over the existing serial connection using the specifies magic."""
    data = filepath.read_bytes()
    name = filepath.name.encode("utf-8")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Uploading {filepath.name} ({len(data)} bytes, CRC32=0x{crc:08x})")

    header = magic
    header += struct.pack("<H", len(name))
    header += name
    header += struct.pack("<I", len(data))
    ser.write(header)

    # Wait for READY
    deadline = time.time() + 10
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "READY":
            break
        if resp.startswith("ERR:"):
            print(f"Upload failed: {resp!r}")
            return False
    else:
        print("Timeout waiting for READY")
        return False
    print("Device ready, sending data...")

    chunk_size = 2048
    sent = 0
    t0 = time.time()
    while sent < len(data):
        end = min(sent + chunk_size, len(data))
        ser.write(data[sent:end])
        sent = end
        deadline_ack = time.time() + 30
        got_ack = False
        while time.time() < deadline_ack:
            b = ser.read(1)
            if b == b"\x06":
                got_ack = True
                break
        if not got_ack:
            print("\nTimeout waiting for ACK")
            return False
        elapsed = time.time() - t0
        rate = sent / elapsed / 1024 if elapsed > 0 else 0
        pct = sent * 100 // len(data)
        print(
            f"\r  {sent}/{len(data)} bytes ({pct}%) {rate:.0f} KB/s", end="", flush=True
        )
    print()

    ser.write(struct.pack("<I", crc))

    deadline = time.time() + 30
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "OK":
            print("Upload successful!")
            return True
        if resp.startswith("ERR:"):
            print(f"Upload failed: {resp!r}")
            return False
    print("Timeout waiting for result")
    return False


def upload_epub(ser: serial.Serial, filepath: Path) -> bool:
    """Upload an EPUB file to the device over the existing serial connection."""
    return upload_file(ser, filepath, b"EPUB")


def upload_sleep(ser: serial.Serial, filepath: Path) -> bool:
    """Upload a sleep image file to the device over the existing serial connection."""
    return upload_file(ser, filepath, b"SIMG")


def upload_font_sd(ser: serial.Serial, filepath: Path) -> bool:
    """Upload an MBF font file to the device's SD card over the existing serial connection."""
    return upload_file(ser, filepath, b"SDFN")


def drain(ser: serial.Serial):
    """Drain any pending serial data."""
    ser.timeout = 0.1
    while ser.read(4096):
        pass
    ser.timeout = 1


def upload_font(ser: serial.Serial, filepath: Path) -> bool:
    """Upload an MBF font file to the device's flash partition."""
    data = filepath.read_bytes()
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Uploading font {filepath.name} ({len(data)} bytes, CRC32=0x{crc:08x})")

    # Send FONT magic + 4-byte size (LE)
    ser.write(b"FONT" + struct.pack("<I", len(data)))

    # Wait for READY
    deadline = time.time() + 10
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "READY":
            break
        if resp.startswith("ERR:"):
            print(f"Upload failed: {resp!r}")
            return False
    else:
        print("Timeout waiting for READY")
        return False
    print("Device ready, sending font data...")

    chunk_size = 2048
    sent = 0
    t0 = time.time()
    while sent < len(data):
        end = min(sent + chunk_size, len(data))
        ser.write(data[sent:end])
        sent = end
        deadline_ack = time.time() + 30
        got_ack = False
        while time.time() < deadline_ack:
            b = ser.read(1)
            if b == b"\x06":
                got_ack = True
                break
        if not got_ack:
            print("\nTimeout waiting for ACK")
            return False
        elapsed = time.time() - t0
        rate = sent / elapsed / 1024 if elapsed > 0 else 0
        pct = sent * 100 // len(data)
        print(
            f"\r  {sent}/{len(data)} bytes ({pct}%) {rate:.0f} KB/s", end="", flush=True
        )
    print()

    # Send CRC32
    ser.write(struct.pack("<I", crc))

    deadline = time.time() + 30
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "OK":
            print("Font upload successful!")
            return True
        if resp.startswith("ERR:"):
            print(f"Upload failed: {resp!r}")
            return False
    print("Timeout waiting for result")
    return False


def send_list_books_raw(ser: serial.Serial) -> list[str]:
    """Return book list as a list of filenames."""
    drain(ser)
    ser.write(MAGIC + b"L")
    deadline = time.time() + 5.0
    result = []
    started = False
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("BOOKS:"):
            started = True
            continue
        if line == "END":
            break
        if started:
            # Format is now "path|title|author"; take the first field as the path.
            result.append(line.split("|")[0].strip())
    return result


def send_remove_file(ser: serial.Serial, path: str) -> str:
    path_bytes = path.encode("utf-8")
    ser.write(MAGIC + b"R" + struct.pack("<H", len(path_bytes)) + path_bytes)
    return read_response(ser, timeout=10.0)


def send_write_file(ser: serial.Serial, path: str, filepath: Path) -> bool:
    """Upload a file to an arbitrary /sdcard/ path via the CMND 'W' command.

    This mirrors what the Web Manager does (cmdWriteFile in docs/index.html),
    so it exercises the same firmware code path: file write + conditional
    index update if the path is a .epub under /sdcard/.
    """
    data = filepath.read_bytes()
    name = path.encode("utf-8")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    print(f"Writing {filepath.name} → {path} ({len(data)} bytes, CRC32=0x{crc:08x})")
    ser.write(MAGIC + b"W" + struct.pack("<H", len(name)) + name + struct.pack("<I", len(data)))

    # Wait for READY
    deadline = time.time() + 10
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "READY":
            break
        if resp.startswith("ERR:"):
            print(f"Write failed: {resp!r}")
            return False
    else:
        print("Timeout waiting for READY")
        return False

    chunk_size = 2048
    sent = 0
    while sent < len(data):
        end = min(sent + chunk_size, len(data))
        ser.write(data[sent:end])
        sent = end
        deadline_ack = time.time() + 30
        got_ack = False
        while time.time() < deadline_ack:
            b = ser.read(1)
            if b == b"\x06":
                got_ack = True
                break
        if not got_ack:
            print("\nTimeout waiting for ACK")
            return False
        pct = sent * 100 // len(data)
        print(f"\r  {sent}/{len(data)} bytes ({pct}%)", end="", flush=True)
    print()

    ser.write(struct.pack("<I", crc))
    deadline = time.time() + 30
    while time.time() < deadline:
        resp = ser.readline().decode("utf-8", errors="replace").strip()
        if not resp:
            continue
        if resp == "OK":
            print("Write successful!")
            return True
        if resp.startswith("ERR:"):
            print(f"Write failed: {resp!r}")
            return False
    print("Timeout waiting for result")
    return False


def send_rename(ser: serial.Serial, src: str, dst: str) -> str:
    """Rename/move a file via the CMND 'N' command (same path the Web Manager uses)."""
    src_bytes = src.encode("utf-8")
    dst_bytes = dst.encode("utf-8")
    ser.write(
        MAGIC
        + b"N"
        + struct.pack("<H", len(src_bytes))
        + src_bytes
        + struct.pack("<H", len(dst_bytes))
        + dst_bytes
    )
    return read_response(ser, timeout=10.0)


def send_clear_mrb(ser: serial.Serial) -> str:
    """Send 'C' command to delete all .mrb files. Returns 'CLEARED:N' or error."""
    drain(ser)
    ser.write(MAGIC + b"C")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("CLEARED:") or line.startswith("ERR:"):
            return line
    return "TIMEOUT"


def send_clear_sleep(ser: serial.Serial) -> str:
    """Send 'Z' command to delete all sleep images. Returns 'CLEARED_SLEEP:N' or error."""
    drain(ser)
    ser.write(MAGIC + b"Z")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("CLEARED_SLEEP:") or line.startswith("ERR:"):
            return line
    return "TIMEOUT"


def send_clear_sd_fonts(ser: serial.Serial) -> str:
    """Send 'Y' command to delete all SD fonts. Returns 'CLEARED_SDFONTS:N' or error."""
    drain(ser)
    ser.write(MAGIC + b"Y")
    deadline = time.time() + 10.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("CLEARED_SDFONTS:") or line.startswith("ERR:"):
            return line
    return "TIMEOUT"


def send_invalidate_font(ser: serial.Serial) -> str:
    """Send 'F' command to zero the font partition CRC, forcing re-provisioning."""
    drain(ser)
    ser.write(MAGIC + b"F")
    deadline = time.time() + 5.0
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if line.startswith("FONT_INVALIDATED") or line.startswith("ERR:"):
            return line
    return "TIMEOUT"


def wait_for_book_result(
    ser: serial.Serial, timeout: float, verbose: bool = False
) -> "Optional[str]":
    """Watch serial output for BOOK_OK or BOOK_FAIL."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue
        if verbose:
            print(f"\n    {line}", end="", flush=True)
        if "BOOK_OK:" in line:
            return "OK"
        if "BOOK_FAIL:" in line:
            return "FAIL"
    return None


def run_test(
    ser: serial.Serial,
    filter_str: str,
    clean: bool,
    verbose: bool,
    per_book_timeout: int,
):
    """Test books on the device, reporting OK/FAIL/TIMEOUT for each."""
    if clean:
        print("Clearing .mrb files ... ", end="", flush=True)
        result = send_clear_mrb(ser)
        print(result)
        if result.startswith("ERR"):
            return
        time.sleep(1)

    books = send_list_books_raw(ser)
    if not books:
        print("No books found on device!")
        return

    if clean:
        test_books = sorted(b for b in books if b.endswith(".epub"))
        mode = "conversion"
    else:
        mrb_set = {b for b in books if b.endswith(".mrb")}
        epub_set = {b for b in books if b.endswith(".epub")}
        test_books = []
        for epub in sorted(epub_set):
            mrb = epub.rsplit(".", 1)[0] + ".mrb"
            if mrb in mrb_set:
                test_books.append(mrb)
            else:
                test_books.append(epub)
        for mrb in sorted(mrb_set):
            epub = mrb.rsplit(".", 1)[0] + ".epub"
            if epub not in epub_set:
                test_books.append(mrb)
        mode = "open"

    if filter_str:
        test_books = [b for b in test_books if filter_str in b]

    if not test_books:
        print(
            f"No books matching '{filter_str}'." if filter_str else "No books to test."
        )
        return

    print(
        f"\nTesting {len(test_books)} books ({mode} mode, {per_book_timeout}s timeout):\n"
    )
    results: dict[str, list[str]] = {"OK": [], "FAIL": [], "TIMEOUT": []}

    for i, book in enumerate(test_books, 1):
        path = book
        display = book.rsplit("/", 1)[-1]
        print(f"[{i}/{len(test_books)}] {display} ... ", end="", flush=True)
        drain(ser)
        path_bytes = path.encode("utf-8")
        ser.write(MAGIC + b"O" + struct.pack("<H", len(path_bytes)) + path_bytes)
        outcome = wait_for_book_result(ser, per_book_timeout, verbose=verbose)
        if outcome == "OK":
            print("OK")
            results["OK"].append(book)
        elif outcome == "FAIL":
            print("FAIL")
            results["FAIL"].append(book)
        else:
            print("TIMEOUT")
            results["TIMEOUT"].append(book)
        time.sleep(0.5)
        ser.write(MAGIC + b"B" + bytes([1]))  # back button
        time.sleep(2.0)
        drain(ser)

    print(f"\n{'='*50}")
    print(
        f"Results: {len(results['OK'])} OK, {len(results['FAIL'])} FAIL, "
        f"{len(results['TIMEOUT'])} TIMEOUT"
    )
    if results["FAIL"]:
        print("\nFailed:")
        for b in results["FAIL"]:
            print(f"  - {b}")
    if results["TIMEOUT"]:
        print("\nTimed out:")
        for b in results["TIMEOUT"]:
            print(f"  - {b}")


BUTTON_ALIASES = {
    "back": 0,
    "select": 1,
    "sel": 1,
    "down": 2,
    "next": 2,
    "up": 3,
    "prev": 3,
}


def main():
    parser = argparse.ArgumentParser(description="Serial tool for microreader2")
    parser.add_argument("--port", default="COM4", help="Serial port (default: COM4)")
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--capture",
        metavar="FILE",
        default=None,
        help="Non-interactive: capture all serial output to FILE and exit",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        help="Toggle DTR/RTS to reset the device before capturing (use with --capture)",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=300,
        help="Capture timeout in seconds (default: 300, use with --capture)",
    )
    parser.add_argument(
        "--done-marker",
        default="=== DONE:",
        help="Stop capturing when this string appears in a line (default: '=== DONE:')",
    )
    parser.add_argument(
        "--upload",
        metavar="FILE",
        default=None,
        help="Non-interactive: upload an EPUB file then exit",
    )
    parser.add_argument(
        "--upload-dir",
        metavar="DIR",
        default=None,
        help="Non-interactive: upload all .epub files in DIR then exit",
    )
    parser.add_argument(
        "--upload-sleep",
        metavar="FILE",
        default=None,
        help="Non-interactive: upload a sleep image file (.mgr) then exit",
    )
    parser.add_argument(
        "--clear-sleep",
        action="store_true",
        default=False,
        help="Non-interactive: delete all images in the /sdcard/sleep/ folder on the device",
    )
    parser.add_argument(
        "--clear-sd-fonts",
        action="store_true",
        default=False,
        help="Non-interactive: delete all fonts in the /sdcard/fonts/ folder on the device",
    )
    parser.add_argument(
        "--upload-font",
        metavar="FILE",
        default=None,
        help="Non-interactive: upload an MBF font file to the flash partition then exit",
    )
    parser.add_argument(
        "--upload-sd-font",
        metavar="FILE",
        default=None,
        help="Non-interactive: upload an MBF font file to the /sdcard/fonts/ folder on the device then exit",
    )
    parser.add_argument(
        "--bench",
        metavar="BOOK",
        default=None,
        help="Non-interactive: send bench command for BOOK (filename or full path), stream output until done",
    )
    parser.add_argument(
        "--imgbench",
        metavar="BOOK",
        default=None,
        help="Non-interactive: send image-size benchmark for BOOK, stream output until done",
    )
    parser.add_argument(
        "--imgdecode",
        metavar="BOOK",
        default=None,
        help="Non-interactive: fully decode every image in BOOK (streaming decoder test)",
    )
    parser.add_argument(
        "--invalidate-font",
        action="store_true",
        default=False,
        help="Non-interactive: zero the font partition CRC so it re-provisions on next book open, then exit",
    )
    parser.add_argument(
        "--imgbench-all",
        action="store_true",
        default=False,
        help="Non-interactive: run image-size benchmark on every .epub on the device",
    )
    parser.add_argument(
        "--bench-all",
        action="store_true",
        default=False,
        help="Non-interactive: run conversion benchmark on every .epub on the device",
    )
    parser.add_argument(
        "--sync-time",
        action="store_true",
        default=False,
        help="Non-interactive: set the device clock to this computer's local time, then exit",
    )
    parser.add_argument(
        "--renderbench",
        action="store_true",
        default=False,
        help="Non-interactive: benchmark render_page_ on the currently open page (20 iterations)",
    )
    args = parser.parse_args()

    try:
        if "://" in args.port:
            # URL-form: socket://localhost:4444 (QEMU raw TCP) or rfc2217://host:port
            ser = serial.serial_for_url(args.port, baudrate=args.baud, timeout=1)
        else:
            ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Cannot open {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    if args.upload:
        fp = Path(args.upload)
        if not fp.exists():
            print(f"File not found: {fp}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        ok = upload_epub(ser, fp)
        ser.close()
        sys.exit(0 if ok else 1)

    if args.upload_dir:
        dir_path = Path(args.upload_dir)
        if not dir_path.is_dir():
            print(f"Not a directory: {dir_path}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        epubs = sorted(dir_path.glob("*.epub"))
        if not epubs:
            print(f"No .epub files found in {dir_path}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        existing = set(send_list_books_raw(ser))
        existing_names = {p.rsplit("/", 1)[-1] for p in existing}
        to_upload = [fp for fp in epubs if fp.name not in existing_names]
        skipped = len(epubs) - len(to_upload)
        if skipped:
            print(f"Skipping {skipped} file(s) already on device")
        if not to_upload:
            print("Nothing to upload")
            ser.close()
            sys.exit(0)
        print(f"Found {len(to_upload)} .epub file(s) to upload")
        ok = 0
        for i, fp in enumerate(to_upload, 1):
            print(f"\n[{i}/{len(to_upload)}] ", end="", flush=True)
            drain(ser)
            try:
                if upload_epub(ser, fp):
                    ok += 1
            except OSError as e:
                print(f"ERROR: {e}")
        print(f"\nDone: {ok}/{len(to_upload)} uploaded successfully")
        ser.close()
        sys.exit(0 if ok == len(to_upload) else 1)

    if args.upload_sleep:
        fp = Path(args.upload_sleep)
        if not fp.exists():
            print(f"File not found: {fp}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        ok = upload_sleep(ser, fp)
        ser.close()
        sys.exit(0 if ok else 1)

    if args.clear_sleep:
        print(send_clear_sleep(ser))
        ser.close()
        sys.exit(0)

    if args.clear_sd_fonts:
        print(send_clear_sd_fonts(ser))
        ser.close()
        sys.exit(0)

    if args.upload_font:
        fp = Path(args.upload_font)
        if not fp.exists():
            print(f"File not found: {fp}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        ok = upload_font(ser, fp)
        ser.close()
        sys.exit(0 if ok else 1)

    if args.upload_sd_font:
        fp = Path(args.upload_sd_font)
        if not fp.exists():
            print(f"File not found: {fp}", file=sys.stderr)
            ser.close()
            sys.exit(1)
        ok = upload_font_sd(ser, fp)
        ser.close()
        sys.exit(0 if ok else 1)

    if args.sync_time:
        result = send_set_time(ser)
        print(result)
        ser.close()
        sys.exit(0 if result.startswith("OK") else 1)

    if args.invalidate_font:
        result = send_invalidate_font(ser)
        print(result)
        ser.close()
        sys.exit(0 if result.startswith("FONT_INVALIDATED") else 1)

    if args.renderbench:
        # --- Render benchmark mode ---
        timeout = args.timeout
        print(f"Sending renderbench (timeout={timeout}s) ...")
        resp = send_renderbench(ser)
        print(resp)
        if not resp.startswith("OK"):
            ser.close()
            sys.exit(1)
        done_marker = "RENDER_BENCH:"
        t0 = time.time()
        while time.time() - t0 < timeout:
            line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
            if not line:
                continue
            print(line)
            sys.stdout.flush()
            if done_marker in line:
                print("--- Done ---")
                break
        else:
            print("--- Timeout ---")
        ser.close()
        return

    if args.bench:
        # --- Benchmark mode ---
        path = args.bench
        capture_file = args.capture  # optional, may be None
        timeout = args.timeout
        done_marker = "BENCHMARK DONE"
        print(f"Sending bench for {path} (timeout={timeout}s)")
        resp = send_bench(ser, path)
        print(resp)
        if not resp.startswith("OK") and not resp.endswith("OK"):
            ser.close()
            sys.exit(1)
        out_f = open(capture_file, "w", encoding="utf-8") if capture_file else None
        try:
            t0 = time.time()
            while time.time() - t0 < timeout:
                line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                print(line)
                sys.stdout.flush()
                if out_f:
                    out_f.write(line + "\n")
                    out_f.flush()
                if done_marker in line:
                    print("--- Done ---")
                    break
            else:
                print("--- Timeout ---")
        finally:
            if out_f:
                out_f.close()
                print(f"Saved to {capture_file}")
        ser.close()
        return

    if args.bench_all:
        # --- Conversion benchmark for all books ---
        import re

        # Give device time to boot if port open triggered a reset.
        time.sleep(4)
        books = send_list_books_raw(ser)
        epubs = sorted(b for b in books if b.endswith(".epub"))
        if not epubs:
            print("No .epub files found on device.")
            ser.close()
            sys.exit(1)
        print(f"Found {len(epubs)} epub(s), benchmarking each...\n")

        # Collect summary rows: (name, open, conv, seek, decomp, build, write)
        summary = []
        done_marker = "BENCHMARK DONE"
        for epub in epubs:
            path = epub
            name = epub.rsplit("/", 1)[-1]
            print(f"=== {name} ===")
            drain(ser)
            resp = send_bench(ser, path)
            print(f"  {resp}")
            if not resp.startswith("OK") and not resp.endswith("OK"):
                print(f"  SKIP (no OK response)")
                summary.append((name, "SKIP", "", "", "", "", ""))
                continue
            # Collect output until done marker
            opn = conv = seek = decomp = build = write = ""
            t0 = time.time()
            while time.time() - t0 < args.timeout:
                line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                print(f"  {line}")
                sys.stdout.flush()
                # Parse BENCH_ lines
                m = re.search(r"BENCH_OPEN: (\d+)ms", line)
                if m:
                    opn = m.group(1)
                m = re.search(r"BENCH_CONV: (\d+)ms", line)
                if m:
                    conv = m.group(1)
                m = re.search(r"BENCH_SEEK: (\d+)ms", line)
                if m:
                    seek = m.group(1)
                m = re.search(r"BENCH_DECOMP: (\d+)ms", line)
                if m:
                    decomp = m.group(1)
                m = re.search(r"BENCH_BUILD: (\d+)ms", line)
                if m:
                    build = m.group(1)
                m = re.search(r"BENCH_WRITE: (\d+)ms", line)
                if m:
                    write = m.group(1)
                if done_marker in line:
                    break
            else:
                print("  --- Timeout ---")
            summary.append((name, opn, conv, seek, decomp, build, write))
            print()

        # Print summary table
        print("=" * 110)
        print("SUMMARY")
        print("=" * 110)
        hdr = f"{'Book':<35} {'Open':>8} {'Conv':>8} {'Seek':>8} {'Decomp':>8} {'Build':>8} {'Write':>8}"
        print(hdr)
        print("-" * len(hdr))
        for name, opn, conv, seek, decomp, build, write in summary:

            def fmt(v):
                return f"{v}ms" if v and v != "SKIP" else v

            print(
                f"{name:<35} {fmt(opn):>8} {fmt(conv):>8} {fmt(seek):>8} {fmt(decomp):>8} {fmt(build):>8} {fmt(write):>8}"
            )
        print()
        ser.close()
        return

    if args.imgbench_all:
        # --- Image-size benchmark for all books ---
        books = send_list_books_raw(ser)
        epubs = sorted(b for b in books if b.endswith(".epub"))
        if not epubs:
            print("No .epub files found on device.")
            ser.close()
            sys.exit(1)
        print(f"Found {len(epubs)} epub(s): {', '.join(epubs)}\n")
        for epub in epubs:
            path = epub
            name = epub.rsplit("/", 1)[-1]
            print(f"--- imgbench: {name} ---")
            drain(ser)
            resp = send_imgbench(ser, path)
            print(resp)
            if not resp.startswith("OK") and not resp.endswith("OK"):
                print(f"SKIP (no OK response)")
                continue
            done_marker = "IMAGE SIZE BENCH DONE"
            t0 = time.time()
            while time.time() - t0 < args.timeout:
                line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                print(line)
                sys.stdout.flush()
                if done_marker in line:
                    break
            else:
                print("--- Timeout ---")
            print()
        ser.close()
        return

    if args.imgbench:
        # --- Image-size benchmark mode ---
        path = args.imgbench
        capture_file = args.capture
        timeout = args.timeout
        done_marker = "IMAGE SIZE BENCH DONE"
        print(f"Sending imgbench for {path} (timeout={timeout}s)")
        resp = send_imgbench(ser, path)
        print(resp)
        if not resp.startswith("OK") and not resp.endswith("OK"):
            ser.close()
            sys.exit(1)
        out_f = open(capture_file, "w", encoding="utf-8") if capture_file else None
        try:
            t0 = time.time()
            while time.time() - t0 < timeout:
                line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                print(line)
                sys.stdout.flush()
                if out_f:
                    out_f.write(line + "\n")
                    out_f.flush()
                if done_marker in line:
                    print("--- Done ---")
                    break
            else:
                print("--- Timeout ---")
        finally:
            if out_f:
                out_f.close()
                if capture_file:
                    print(f"Saved to {capture_file}")
        ser.close()
        return

    if args.imgdecode:
        # --- Image decode test mode ---
        path = args.imgdecode
        capture_file = args.capture
        timeout = args.timeout
        done_marker = "IMAGE DECODE TEST DONE"
        print(f"Sending imgdecode for {path} (timeout={timeout}s)")
        resp = send_imgdecode(ser, path)
        print(resp)
        if not resp.startswith("OK") and not resp.endswith("OK"):
            ser.close()
            sys.exit(1)
        out_f = open(capture_file, "w", encoding="utf-8") if capture_file else None
        try:
            t0 = time.time()
            while time.time() - t0 < timeout:
                line = ser.readline().decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                print(line)
                sys.stdout.flush()
                if out_f:
                    out_f.write(line + "\n")
                    out_f.flush()
                if done_marker in line:
                    print("--- Done ---")
                    break
            else:
                print("--- Timeout ---")
        finally:
            if out_f:
                out_f.close()
                if capture_file:
                    print(f"Saved to {capture_file}")
        ser.close()
        return

    if args.capture:
        # --- Capture mode ---
        print(f"Capturing {args.port} → {args.capture} (timeout={args.timeout}s)")
        if args.reset:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.1)
            ser.rts = False
            time.sleep(0.1)
            ser.reset_input_buffer()
        with open(args.capture, "w", encoding="utf-8") as out:
            t0 = time.time()
            while time.time() - t0 < args.timeout:
                line = ser.readline().decode("utf-8", errors="replace")
                if not line:
                    continue
                line = line.rstrip("\r\n")
                print(line)
                out.write(line + "\n")
                out.flush()
                if args.done_marker and args.done_marker in line:
                    print("\n--- Done marker reached ---")
                    break
            else:
                print("\n--- Timeout ---")
        ser.close()
        print(f"Output saved to {args.capture}")
        return

    # --- Interactive mode ---
    print(f"Connected to {args.port} @ {args.baud}")
    print("Type 'help' for commands, 'quit' to exit.\n")

    try:
        while True:
            try:
                cmd = input("> ").strip()
            except EOFError:
                break
            if not cmd:
                continue

            parts = cmd.split(maxsplit=1)
            verb = parts[0].lower()
            arg = parts[1] if len(parts) > 1 else ""

            if verb in ("quit", "exit", "q"):
                break
            elif verb == "help":
                print(__doc__)
            elif verb == "btn":
                try:
                    mask = 1 << int(arg)
                    print(send_button(ser, mask))
                except (ValueError, IndexError):
                    print("Usage: btn <0-6>")
            elif verb in BUTTON_ALIASES:
                mask = 1 << BUTTON_ALIASES[verb]
                print(send_button(ser, mask))
            elif verb in ("status", "st"):
                print(send_status(ser))
            elif verb == "time":
                print(send_set_time(ser))
            elif verb in ("books", "ls"):
                print(send_list_books(ser))
            elif verb == "open":
                if not arg:
                    print("Usage: open <path>")
                    continue
                print(send_open(ser, arg.strip('"\'')))
            elif verb == "rm":
                if not arg:
                    print("Usage: rm <path>")
                    continue
                print(send_remove_file(ser, arg.strip('"\'')))
            elif verb == "test":
                targs = arg.split()
                clean = "--clean" in targs
                verbose_test = "-v" in targs or "--verbose" in targs
                default_timeout = 120 if clean else 30
                per_book_timeout = default_timeout
                filter_parts = []
                skip_next = False
                for j, tok in enumerate(targs):
                    if skip_next:
                        skip_next = False
                        continue
                    if tok == "--timeout" and j + 1 < len(targs):
                        try:
                            per_book_timeout = int(targs[j + 1])
                            skip_next = True
                        except ValueError:
                            pass
                    elif not tok.startswith("-"):
                        filter_parts.append(tok)
                run_test(
                    ser, " ".join(filter_parts), clean, verbose_test, per_book_timeout
                )
            elif verb == "clear":
                print(send_clear_mrb(ser))
            elif verb == "clear-sleep":
                print(send_clear_sleep(ser))
            elif verb == "clear-sd-fonts":
                print(send_clear_sd_fonts(ser))
            elif verb == "upload":
                if not arg:
                    print("Usage: upload <file>")
                    continue
                fp = Path(arg)
                if not fp.exists():
                    print(f"File not found: {fp}")
                    continue
                upload_epub(ser, fp)
            elif verb == "upload-sleep":
                if not arg:
                    print("Usage: upload-sleep <file>")
                    continue
                fp = Path(arg)
                if not fp.exists():
                    print(f"File not found: {fp}")
                    continue
                upload_sleep(ser, fp)
            elif verb == "uploadfont":
                if not arg:
                    print("Usage: uploadfont <file.mbf>")
                    continue
                fp = Path(arg)
                if not fp.exists():
                    print(f"File not found: {fp}")
                    continue
                upload_font(ser, fp)
            elif verb == "uploadsdfont":
                if not arg:
                    print("Usage: uploadsdfont <file.mbf>")
                    continue
                fp = Path(arg)
                if not fp.exists():
                    print(f"File not found: {fp}")
                    continue
                upload_font_sd(ser, fp)
            elif verb == "bench":
                if not arg:
                    print("Usage: bench <path>")
                    continue
                print(f"Sending bench request for {arg} ...")
                resp = send_bench(ser, arg)
                print(resp)
                if resp.startswith("OK"):
                    print(
                        "Benchmark running — watch serial output for BENCH1..BENCH5 results."
                    )
                    print("(Use --capture to save logs, or just watch the terminal.)")
            elif verb == "imgsize":
                if not arg:
                    print("Usage: imgsize <path>")
                    continue
                print(f"Sending image-size benchmark request for {arg} ...")
                resp = send_imgbench(ser, arg)
                print(resp)
                if resp.startswith("OK"):
                    print(
                        "Image-size benchmark running — watch serial output for results."
                    )
                    print("Done when: '=== IMAGE SIZE BENCH DONE' appears in the log.")
            elif verb == "imgdecode":
                if not arg:
                    print("Usage: imgdecode <path>")
                    continue
                print(f"Sending image decode test for {arg} ...")
                resp = send_imgdecode(ser, arg)
                print(resp)
                if resp.startswith("OK"):
                    print(
                        "Image decode test running — watch serial output for per-image results."
                    )
                    print("Done when: '=== IMAGE DECODE TEST DONE' appears in the log.")
            elif verb == "flashbench":
                print("Sending flash erase+write benchmark ...")
                resp = send_flashbench(ser)
                print(resp)
                if resp.startswith("OK"):
                    print(
                        "Flash benchmark running — watch serial output for FLASH_BENCH lines."
                    )
                    print("Done when: '=== FLASH BENCH DONE' appears in the log.")
            elif verb in ("renderbench", "rb"):
                print("Sending render benchmark ...")
                resp = send_renderbench(ser)
                print(resp)
                if resp.startswith("OK"):
                    done_marker = "RENDER_BENCH:"
                    deadline = time.time() + 60.0
                    while time.time() < deadline:
                        line = ser.readline().decode("utf-8", errors="replace").strip()
                        if not line:
                            continue
                        print(line)
                        if done_marker in line:
                            break
                    else:
                        print("(timeout waiting for RENDER_BENCH: summary)")
            else:
                print(f"Unknown command: {verb!r}. Type 'help' for commands.")
    except KeyboardInterrupt:
        print()
    finally:
        ser.close()
        print("Disconnected.")


if __name__ == "__main__":
    main()
