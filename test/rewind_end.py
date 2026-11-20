"""Rewinding to the end of the ring, then letting go (ChaOS).

With nothing left to go back to, no frame runs while F1 is held, and the
buttons are read from inside a frame - so F1 read as held for good, and the
game never came back: Aquilus found it 2026-11-20, rewinding right after a
start or a load, or past the oldest save. This holds F1 past the end, lets go,
and asks that the game is playing again (the log says F1 was let go).

Against the stub core on the desktop build, buttons held by a file
(DIATOM_TEST_BUTTONS). A few seconds; skipped with no binary."""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN, STUB = f"{ROOT}/build/desktop/diatom", f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("rewind-end: no desktop build, skipped"); sys.exit(0)
SOCK = "/tmp/diatom-rwend.sock"
def f1_bit():
    """F1's place in the button enum, which differs by branch (the Brick
    Pro's L3 and R3 come before it on testing only)"""
    import re
    src = open(f"{ROOT}/include/diatom_port.h").read()
    body = src[src.index("DIATOM_BTN_UP = 0"):src.index("DIATOM_BTN_COUNT")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    names = [n.split("=")[0].strip() for n in body.split(",") if n.strip()]
    return 1 << names.index("DIATOM_BTN_F1")
F1 = f1_bit()
fails = []

def run(wait_before):
    if os.path.exists(SOCK): os.unlink(SOCK)
    btn = tempfile.NamedTemporaryFile("w", delete=False, suffix=".btn")
    btn.write("0\n"); btn.close()
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", DIATOM_TEST_BUTTONS=btn.name)
    log = tempfile.NamedTemporaryFile("w+", delete=False, suffix=".log")
    p = subprocess.Popen([BIN, "--socket", SOCK], env=env,
                         stdout=log, stderr=log)
    try:
        for _ in range(100):
            if os.path.exists(SOCK): break
            time.sleep(0.05)
        s = socket.socket(socket.AF_UNIX); s.connect(SOCK); s.settimeout(0.2)
        def send(m): s.sendall((m + "\n").encode())
        def drain(t):
            out, end = b"", time.time() + t
            while time.time() < end:
                try:
                    d = s.recv(65536)
                    if not d: break
                    out += d
                except socket.timeout: pass
            return out.decode(errors="replace")
        drain(0.8)
        send(f"RUN\tcore={STUB}"); drain(1.0)
        send("SETREWIND\ton=1"); drain(0.3)
        time.sleep(wait_before)
        open(btn.name, "w").write(f"{F1}\n")
        drain(2.5)                              # far past the end of the ring
        open(btn.name, "w").write("0\n")
        drain(1.0)                              # let go
        send("MAP"); answer = drain(1.0)
        ok = "rewind: let go" in open(log.name).read() and "MAP\t" in answer
        print(("  ok    " if ok else "  FAIL  ") +
              f"held F1 past the end ({wait_before:.0f}s of saves), let go: playing again")
        if not ok: fails.append(wait_before)
    finally:
        p.kill(); os.unlink(btn.name); os.unlink(log.name)

run(0.0)     # straight after the start: nothing to go back to at all
run(1.0)     # a second of saves, rewound through and past
sys.exit(1 if fails else 0)
