"""Compare two PPMs: how many pixels differ, and by how much (0-255 levels)."""
import sys

def load(path):
    d = open(path, "rb").read()
    # header: P6, width, height, maxval, then exactly one whitespace byte
    fields, i = [], 0
    while len(fields) < 4:
        while d[i:i+1].isspace():
            i += 1
        j = i
        while not d[j:j+1].isspace():
            j += 1
        fields.append(d[i:j])
        i = j
    return int(fields[1]), int(fields[2]), d[i+1:]

def main():
    wa, ha, a = load(sys.argv[1])
    wb, hb, b = load(sys.argv[2])
    assert (wa, ha) == (wb, hb), "sizes differ"
    hist = [0] * 256
    worst = (0, 0, 0)
    for i in range(wa * ha):
        d = max(abs(a[i*3+k] - b[i*3+k]) for k in range(3))
        hist[d] += 1
        if d > worst[0]:
            worst = (d, i % wa, i // wa)
    n = wa * ha
    exact = hist[0] / n * 100
    within1 = (hist[0] + hist[1]) / n * 100
    print(f"exact {exact:6.2f}%  within 1 level {within1:6.2f}%  "
          f"max diff {worst[0]} at ({worst[1]},{worst[2]})")
    return 0 if worst[0] <= int(sys.argv[3] if len(sys.argv) > 3 else 255) else 1

if __name__ == "__main__":
    sys.exit(main())
