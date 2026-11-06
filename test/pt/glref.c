/* glref: render a frame through the real PT_SkyWalker541.glsl, headless.
 *
 * Test tool only - never shipped. Runs the shader file unmodified under
 * GLES 2 (GLSL ES 1.00) on Mesa's software rasterizer, the way RetroArch and
 * NextUI drive a single-pass shader: one quad, Texture == OrigTexture,
 * TextureSize == InputSize == the game frame, OutputSize = frame * scale,
 * nearest sampling, clamp to edge.
 *
 * usage: glref shader.glsl in.ppm scale frame out.ppm [NAME=value ...]
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ppm.h"

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *s;
	if (!f) { perror(path); exit(1); }
	fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
	s = malloc((size_t)n + 1);
	if (fread(s, 1, (size_t)n, f) != (size_t)n) { perror(path); exit(1); }
	s[n] = 0; fclose(f);
	return s;
}

static GLuint compile(GLenum type, const char *stage, const char *body)
{
	char head[256];
	const char *srcs[2];
	GLuint s = glCreateShader(type);
	GLint ok;
	snprintf(head, sizeof head,
	         "%s\n#define %s\n#define PARAMETER_UNIFORM\n",
	         getenv("GLREF_ES3") ? "#version 300 es" : "#version 100", stage);
	srcs[0] = head; srcs[1] = body;
	glShaderSource(s, 2, srcs, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[4096];
		glGetShaderInfoLog(s, sizeof log, NULL, log);
		fprintf(stderr, "%s: %s\n", stage, log);
		exit(1);
	}
	return s;
}

int main(int argc, char **argv)
{
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLContext ctx;
	EGLint n;
	img in, out;
	int scale, frame, ow, oh, i;
	char *src;
	GLuint prog, tex, fbo, rb;
	GLint ok;
	static const EGLint cattr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT_KHR, EGL_NONE };
	EGLint xattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	/* Clip-space quad and the texcoords RetroArch hands a pass. v = 0 is the
	 * first uploaded row (the top of the picture), and glReadPixels returns
	 * the bottom row first, so reading back gives rows top-down with no flip. */
	static const GLfloat vert[] = { -1,-1,0,1,  1,-1,0,1,  -1,1,0,1,  1,1,0,1 };
	static const GLfloat texc[] = {  0, 0,0,0,  1, 0,0,0,   0,1,0,0,  1,1,0,0 };
	static const GLfloat mvp[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };

	if (argc < 6) {
		fprintf(stderr, "usage: glref shader.glsl in.ppm scale frame out.ppm [NAME=value ...]\n");
		return 2;
	}
	src = slurp(argv[1]);
	if (!ppm_read(argv[2], &in)) return 1;
	scale = atoi(argv[3]); frame = atoi(argv[4]);
	ow = in.w * scale; oh = in.h * scale;

	if (getenv("GLREF_ES3")) xattr[1] = 3;
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, NULL, NULL)) { fprintf(stderr, "eglInitialize failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, cattr, &cfg, 1, &n) || n < 1) { fprintf(stderr, "no config\n"); return 1; }
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xattr);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		fprintf(stderr, "context failed (0x%x)\n", eglGetError()); return 1;
	}

	prog = glCreateProgram();
	glAttachShader(prog, compile(GL_VERTEX_SHADER, "VERTEX", src));
	glAttachShader(prog, compile(GL_FRAGMENT_SHADER, "FRAGMENT", src));
	glBindAttribLocation(prog, 0, "VertexCoord");
	glBindAttribLocation(prog, 1, "TexCoord");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) { char log[4096]; glGetProgramInfoLog(prog, sizeof log, NULL, log); fprintf(stderr, "link: %s\n", log); return 1; }
	glUseProgram(prog);

	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, in.w, in.h, 0, GL_RGB, GL_UNSIGNED_BYTE, in.px);

	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, ow, oh);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { fprintf(stderr, "fbo incomplete\n"); return 1; }
	glViewport(0, 0, ow, oh);

	glUniform1i(glGetUniformLocation(prog, "Texture"), 0);
	glUniform1i(glGetUniformLocation(prog, "OrigTexture"), 0);
	glUniformMatrix4fv(glGetUniformLocation(prog, "MVPMatrix"), 1, GL_FALSE, mvp);
	glUniform2f(glGetUniformLocation(prog, "OutputSize"), (float)ow, (float)oh);
	glUniform2f(glGetUniformLocation(prog, "TextureSize"), (float)in.w, (float)in.h);
	glUniform2f(glGetUniformLocation(prog, "InputSize"), (float)in.w, (float)in.h);
	glUniform2f(glGetUniformLocation(prog, "OrigTextureSize"), (float)in.w, (float)in.h);
	glUniform2f(glGetUniformLocation(prog, "OrigInputSize"), (float)in.w, (float)in.h);
	glUniform1i(glGetUniformLocation(prog, "FrameCount"), frame);

	/* Every parameter starts at its #pragma default, the way RetroArch loads
	 * a shader, then NAME=value arguments override. */
	{
		const char *p = src;
		while ((p = strstr(p, "#pragma parameter ")) != NULL) {
			char name[64]; float def = 0;
			const char *q;
			p += 18;
			if (sscanf(p, "%63s", name) != 1) break;
			q = strchr(p, '"'); q = q ? strchr(q + 1, '"') : NULL;
			if (q && sscanf(q + 1, "%f", &def) == 1)
				glUniform1f(glGetUniformLocation(prog, name), def);
		}
	}
	for (i = 6; i < argc; i++) {
		char name[64]; float v;
		const char *eq = strchr(argv[i], '=');
		if (!eq || eq - argv[i] >= 64) continue;
		memcpy(name, argv[i], (size_t)(eq - argv[i])); name[eq - argv[i]] = 0;
		v = (float)atof(eq + 1);
		glUniform1f(glGetUniformLocation(prog, name), v);
		if (!strcmp(name, "PT_GLARE_AND_SHEEN"))
			glUniform1f(glGetUniformLocation(prog, "PT_GLARE_AND_SHEEN_F"), v);
	}

	if (getenv("GLREF_DEBUG")) {
		static const char *nm[] = { "PT_GLARE_AND_SHEEN", "PT_LIGHT_DRIFT_SPEED", "PT_LIGHT_HOME_X",
			"PT_LIGHT_HOME_Y", "PT_GLARE_STRENGTH", "PT_SHEEN_STRENGTH", "PT_GLARE_SPREAD" };
		unsigned k;
		for (k = 0; k < sizeof nm / sizeof nm[0]; k++) {
			GLint loc = glGetUniformLocation(prog, nm[k]);
			GLfloat v = -99;
			if (loc >= 0) glGetUniformfv(prog, loc, &v);
			fprintf(stderr, "%s loc %d = %g\n", nm[k], loc, v);
		}
	}
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, vert);
	glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, texc);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	{
		unsigned char *rgba = malloc((size_t)ow * oh * 4);
		int k;
		glReadPixels(0, 0, ow, oh, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		out.w = ow; out.h = oh; out.px = malloc((size_t)ow * oh * 3);
		for (k = 0; k < ow * oh; k++) memcpy(out.px + k * 3, rgba + k * 4, 3);
		if (!ppm_write(argv[5], &out)) return 1;
	}
	return 0;
}
