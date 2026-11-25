"""The dark-start hold (ChaOS-diatom 0.9.21), against the stub core.

A PlayStation game boots on black frames while its disc loads - sixteen
seconds of them for Castlevania - and presenting them replaced the launcher's
"Loading..." with a black panel that looked like a hang. Diatom now keeps the
launcher's frame until the first frame with something in it, for PCSX only.

    python3 test/holddark.py
"""
import os, socket, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN, STUB = f"{ROOT}/build/desktop/diatom", f"{ROOT}/build/desktop/stubcore.so"
if not os.path.exists(BIN):
    print(f"  skip  dark hold: no {os.path.relpath(BIN, ROOT)}; run make first")
    sys.exit(0)

fails = []

def run(dark):
    sock = f"/tmp/diatom-holddark-{os.getpid()}.sock"
    if os.path.exists(sock): os.unlink(sock)
    env = dict(os.environ)
    env.pop("STUBCORE_DARK", None)
    if dark is not None: env["STUBCORE_DARK"] = str(dark)
    with tempfile.TemporaryFile() as out:
        p = subprocess.Popen([BIN, "--socket", sock], stdout=out, stderr=subprocess.DEVNULL, env=env)
        for _ in range(100):
            if os.path.exists(sock): break
            time.sleep(0.05)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock)
        s.sendall(f"RUN\tcore={STUB}\n".encode()); time.sleep(2.0)
        s.sendall(b"STOP\n"); time.sleep(0.5)
        s.sendall(b"QUIT\n"); time.sleep(0.3)
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill()
        out.seek(0)
        return [l for l in out.read().decode(errors="replace").splitlines() if "first picture after" in l]

def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + f"{name}\n         got  {got}")
    if not ok: fails.append(name)

got = run(45)
check("45 black frames held, then the picture", len(got) == 1 and 0.5 < float(got[0].split()[-4]) < 1.5, got)
got = run(0)
check("a first frame with something in it: no wait", len(got) == 1 and float(got[0].split()[-4]) < 0.3, got)
got = run(None)
check("another core: never held", got == [], got)
print("dark hold:", "ok" if not fails else f"{len(fails)} failed")
sys.exit(1 if fails else 0)
