"""A game Diatom can't open is said, in words, over the protocol (ChaOS-diatom
0.9.22). A zip packed a way it can't read used to report "core refused the
rom", and the launcher, reading that as a dead emulator, ran it again the
slow way, failed the same way, and showed the shelf with no reason
(2026-11-25: every Master System game in one set, zipped with Zstandard).

    python3 test/romerror.py
"""
import os, socket, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN, STUB = f"{ROOT}/build/desktop/diatom", f"{ROOT}/build/desktop/stubcore.so"
if not os.path.exists(BIN):
    print(f"  skip  rom errors: no {os.path.relpath(BIN, ROOT)}; run make first")
    sys.exit(0)
fails = []

def run(rom):
    sock = f"/tmp/diatom-romerror-{os.getpid()}.sock"
    if os.path.exists(sock): os.unlink(sock)
    p = subprocess.Popen([BIN, "--socket", sock], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock); s.settimeout(3)
    s.sendall(f"RUN\tcore={STUB}\trom={rom}\n".encode())
    got, end = b"", time.time() + 3
    while time.time() < end and b"ERROR" not in got and b"RUNNING" not in got:
        try: got += s.recv(4096)
        except socket.timeout: break
    s.sendall(b"QUIT\n"); time.sleep(0.2)
    p.kill(); p.wait()
    return [l for l in got.decode(errors="replace").splitlines() if l.startswith(("ERROR", "RUNNING"))]

with tempfile.TemporaryDirectory() as d:
    raw = bytearray(open(f"{ROOT}/test/fixtures/zstd.zip", "rb").read())
    odd = bytearray(raw)
    # method 12 (bzip2) in both headers: a zip Diatom can't read
    odd[8] = 12; odd[raw.index(b"PK\x01\x02") + 10] = 12
    open(f"{d}/odd.zip", "wb").write(odd)
    bad = bytearray(raw); bad[70] ^= 0x5a
    open(f"{d}/bad.zip", "wb").write(bad)
    for name, want in (("odd.zip", "its zip is packed a way ChaOS can't read yet (method 12)"),
                       ("bad.zip", "its zip is damaged")):
        got = run(f"{d}/{name}")
        ok = got == [f"ERROR\tcode=rom_unreadable\tmsg={want}"]
        print(("  ok   " if ok else "  FAIL ") + f"{name}: said why\n         got  {got}")
        if not ok: fails.append(name)
print("rom errors:", "ok" if not fails else f"{len(fails)} failed")
sys.exit(1 if fails else 0)
