"""Build the test copies of the GLSL, and check the light point on its own.

Writes, from ../../shaders/pt/PT_SkyWalker541.glsl:
  flat.glsl        the grain hash returns 0.5 (for exact comparisons)
  fraglight.glsl   flat.glsl, with the glare's light worked out in the
                   fragment stage using the vertex stage's exact code
  fraglight_real.glsl  the same light change on the unmodified grain, for
                   pictures
  light.glsl       outputs the vertex stage's light point as a colour

Why the light change: Mesa's software driver hands this shader's vertex-only
uniforms wrong values once its fragment stage uses many uniforms, which moves
the glare. The vertex code is the shader's own, so the check below renders
light.glsl (where the fragment stage uses almost nothing and Mesa is right)
and compares it with the formula.
"""
import math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "shaders", "pt", "PT_SkyWalker541.glsl")

FRAG_LIGHT = """uniform COMPAT_PRECISION float PT_LIGHT_DRIFT_SPEED;
uniform COMPAT_PRECISION float PT_LIGHT_HOME_X;
uniform COMPAT_PRECISION float PT_LIGHT_HOME_Y;
uniform COMPAT_PRECISION float PT_SHEEN_HUE_TURN;
uniform COMPAT_PRECISION float PT_GLARE_AND_SHEEN_F;
uniform highp int FrameCount;
vec3 fragLight()
{
    vec3 L = vec3(PT_LIGHT_HOME_X, PT_LIGHT_HOME_Y, PT_SHEEN_HUE_TURN);
    if (PT_GLARE_AND_SHEEN_F > 0.5 && PT_LIGHT_DRIFT_SPEED > 0.001) {
        float t = float(FrameCount) * PT_LIGHT_DRIFT_SPEED * 0.01;
        L += vec3(0.6 * sin(t - 0.34), 0.4 * sin(t * 0.73 - 0.52), t * 0.15);
    }
    return L;
}
"""

def once(s, old, new):
    assert s.count(old) == 1, "shader changed: %r" % old[:60]
    return s.replace(old, new)

def flat(s):
    return once(s, "float noiseHash(vec2 p)\n{", "float noiseHash(vec2 p)\n{\n    return 0.5;")

def fraglight(s):
    decl = "COMPAT_VARYING vec3 shimmerLight;\n"     # the fragment stage's
    s = once(s, decl, decl + FRAG_LIGHT)
    return once(s, "    vec2  d  = (g - shimmerLight.xy)",
                "    vec3 shimmerLight = fragLight();\n    vec2  d  = (g - shimmerLight.xy)")

def light(s):
    return once(s, "void main()\n{\n    // Sample corrected frame and raw input frame.",
                "void main()\n{\n    FragColor = vec4(shimmerLight, 1.0); return;\n"
                "    // Sample corrected frame and raw input frame.")

def write(name, text):
    open(os.path.join(HERE, name), "w").write(text)

def check_light():
    env = dict(os.environ, EGL_PLATFORM="surfaceless", LIBGL_ALWAYS_SOFTWARE="1")
    ok = True
    for frame, speed, hue in [(0, 0.0, 0.0), (0, 0.5, 0.0), (300, 0.1, 0.2), (40, 0.3, 0.6)]:
        subprocess.run([os.path.join(HERE, "glref"), os.path.join(HERE, "light.glsl"),
                        os.path.join(HERE, "white.ppm"), "1", str(frame), "/tmp/claude-0/light.ppm",
                        "PT_GLARE_AND_SHEEN=1", "PT_LIGHT_DRIFT_SPEED=%g" % speed,
                        "PT_SHEEN_HUE_TURN=%g" % hue], check=True, env=env)
        d = open("/tmp/claude-0/light.ppm", "rb").read()
        px = d[-160 * 144 * 3:][:3]
        got = [v / 255 for v in px]
        L = [0.5, 0.45, hue]
        if speed > 0.001:
            t = frame * speed * 0.01
            L = [L[0] + 0.6 * math.sin(t - 0.34), L[1] + 0.4 * math.sin(t * 0.73 - 0.52), L[2] + t * 0.15]
        want = [min(max(v, 0.0), 1.0) for v in L]
        good = all(abs(a - b) <= 1.5 / 255 for a, b in zip(got, want))
        ok &= good
        print("light frame %4d speed %.1f: GLSL %s, formula %s %s" %
              (frame, speed, [round(v, 3) for v in got], [round(v, 3) for v in want], "ok" if good else "DIFFERS"))
    return ok

if __name__ == "__main__":
    s = open(SRC).read()
    write("flat.glsl", flat(s))
    write("fraglight.glsl", fraglight(flat(s)))
    write("fraglight_real.glsl", fraglight(s))
    write("light.glsl", light(s))
    if "--check" in sys.argv:
        sys.exit(0 if check_light() else 1)
