"""Emulator Details (ChaOS-diatom 0.9.24): RUN's corelog=1 keeps a core's own
INFO messages for that game; without it they go nowhere once the game runs,
as before, since mGBA's per-DMA logging cost 16 fps. Against the stub core,
which says something every 10 frames when STUBCORE_LOG is set.

    python3 test/corelog.py
"""
import os, socket, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN, STUB = f"{ROOT}/build/desktop/diatom", f"{ROOT}/build/desktop/stubcore.so"
if not os.path.exists(BIN):
    print(f"  skip  core log: no {os.path.relpath(BIN, ROOT)}; run make first")
    sys.exit(0)

fails = []

def run(extra):
    sock = f"/tmp/diatom-corelog-{os.getpid()}.sock"
    if os.path.exists(sock): os.unlink(sock)
    env = dict(os.environ, STUBCORE_LOG="1")
    env.pop("DIATOM_CORE_LOG", None)
    with tempfile.TemporaryFile() as out:
        p = subprocess.Popen([BIN, "--socket", sock], stdout=out, stderr=out, env=env)
        for _ in range(100):
            if os.path.exists(sock): break
            time.sleep(0.05)
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); s.connect(sock)
        s.sendall(f"RUN\tcore={STUB}{extra}\n".encode()); time.sleep(1.5)
        s.sendall(b"STOP\n"); time.sleep(0.5)
        s.sendall(b"QUIT\n"); time.sleep(0.3)
        try: p.wait(timeout=5)
        except subprocess.TimeoutExpired: p.kill()
        out.seek(0)
        return out.read().decode(errors="replace").count("stub chatter")

def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + f"{name}\n         got  {got}")
    if not ok: fails.append(name)

quiet = run("")
check("without corelog, a game's INFO goes nowhere", quiet <= 1, quiet)
kept = run("\tcorelog=1")
check("corelog=1 keeps it (one every 10 frames)", kept >= 5, kept)
again = run("")
check("and the next game without it is quiet again", again <= 1, again)
print("core log:", "ok" if not fails else f"{len(fails)} failed")
sys.exit(1 if fails else 0)
