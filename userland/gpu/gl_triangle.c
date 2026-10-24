/*
 * gl-triangle: glue plan U3 (main repo ios/gpu-glue-plan.md 5). Mesa's OSMesa (freedreno,
 * through redwood-drm and RedwoodGPU) draws on the Adreno 710 with nothing of iOS's graphics
 * in between: no CoreAnimation, no EAGL. Run over the agent while the GPU is kept
 * (kgpu_keep=1 kgpu>=21), with libOSMesa.8.dylib (userland/mesa/mk-mesa-mac.sh) put beside it:
 *
 *   gl-triangle [LIB] [soft] [debug=FD_MESA_DEBUG]      (LIB: /tmp/libOSMesa.8.dylib)
 *
 * The agent passes no environment, so the options set Mesa's before it is loaded: soft =
 * OSMESA_SOFTWARE=1 (softpipe, the same picture without the GPU, to compare with). Two
 * pictures of 256x256, each checked in this program and written as a PPM next to the library:
 *   1. an OpenGL (compatibility) context: a triangle with a colour per corner, glBegin/glEnd;
 *   2. an OpenGL ES 2 context (the fork's OSMESA_ES_PROFILE, as CoreAnimation will get through
 *      eagl.c): the same triangle from a vertex and a fragment shader and glDrawArrays, plus
 *      glReadPixels; then 100 frames, timed.
 * Exit 0 if the renderer is the GPU's (not with soft) and both pictures are right.
 */
#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLubyte;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef struct osmesa_context *OSMesaContext;

#define GL_TRIANGLES            0x0004
#define GL_COLOR_BUFFER_BIT     0x4000
#define GL_UNSIGNED_BYTE        0x1401
#define GL_FLOAT                0x1406
#define GL_RGBA                 0x1908
#define GL_VENDOR               0x1F00
#define GL_RENDERER             0x1F01
#define GL_VERSION              0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define OSMESA_RGBA             GL_RGBA
#define OSMESA_FORMAT           0x22
#define OSMESA_DEPTH_BITS       0x30
#define OSMESA_STENCIL_BITS     0x31
#define OSMESA_PROFILE          0x33
#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
#define OSMESA_ES_PROFILE       0x38    /* osmesa-es-profile.patch */

#define W 256
#define H 256

static OSMesaContext (*OSMesaCreateContextExt)(GLenum, GLint, GLint, GLint, OSMesaContext);
static OSMesaContext (*OSMesaCreateContextAttribs)(const int *, OSMesaContext);
static GLboolean (*OSMesaMakeCurrent)(OSMesaContext, void *, GLenum, GLsizei, GLsizei);
static void (*OSMesaDestroyContext)(OSMesaContext);
static void *(*OSMesaGetProcAddress)(const char *);

static const GLubyte *(*glGetString)(GLenum);
static void (*glViewport)(GLint, GLint, GLsizei, GLsizei);
static void (*glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*glClear)(GLbitfield);
static void (*glFinish)(void);
static GLenum (*glGetError)(void);
static void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (*glBegin)(GLenum);
static void (*glEnd)(void);
static void (*glColor3f)(GLfloat, GLfloat, GLfloat);
static void (*glVertex2f)(GLfloat, GLfloat);
static GLuint (*glCreateShader)(GLenum);
static void (*glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*glCompileShader)(GLuint);
static void (*glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static GLuint (*glCreateProgram)(void);
static void (*glAttachShader)(GLuint, GLuint);
static void (*glBindAttribLocation)(GLuint, GLuint, const GLchar *);
static void (*glLinkProgram)(GLuint);
static void (*glGetProgramiv)(GLuint, GLenum, GLint *);
static void (*glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static void (*glUseProgram)(GLuint);
static void (*glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
static void (*glEnableVertexAttribArray)(GLuint);
static void (*glDrawArrays)(GLenum, GLint, GLsizei);

static unsigned failed;
static char dir[512];

#define SYM(lib, name) (*(void **)&name = dlsym(lib, #name))
#define GL(name) (*(void **)&name = OSMesaGetProcAddress(#name))

static double
now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

/* the corners: red at the bottom left, green at the bottom right, blue at the top */
static const GLfloat corner[3][2] = { { -0.8f, -0.8f }, { 0.8f, -0.8f }, { 0.0f, 0.8f } };
static const GLfloat colour[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };

/*
 * A picture of the triangle over the clear colour (0, 0, 63 or 64), rows bottom up: pixels in it,
 * pixels of the clear colour outside, mostly-red near the red corner, mostly-blue near the top.
 * Then written as a PPM. The number of things wrong.
 */
static unsigned
check(const char *what, const uint8_t *p, const char *ppm)
{
	unsigned inside = 0, outside_wrong = 0, bad = 0;

	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			const uint8_t *q = p + 4 * (y * W + x);
			bool clear = q[0] == 0 && q[1] == 0 && (q[2] == 63 || q[2] == 64);     /* 0.25 * 255 */
			/* the triangle in pixels: from (25.6, 25.6)-(230.4, 25.6) up to (128, 230.4) */
			float fx = x + 0.5f, fy = y + 0.5f;
			bool in = fy > 25.6f + 2 && fy < 230.4f - 2 &&
			    fx > 25.6f + (fy - 25.6f) / 2 + 2 && fx < 230.4f - (fy - 25.6f) / 2 - 2;
			bool out = fy < 25.6f - 2 || fy > 230.4f + 2 || fx < 25.6f + (fy - 25.6f) / 2 - 2 ||
			    fx > 230.4f - (fy - 25.6f) / 2 + 2;

			inside += !clear;
			if (in && clear) {
				bad++;
			}
			if (out && !clear) {
				outside_wrong++;
			}
		}
	}
	const uint8_t *red = p + 4 * (30 * W + 40), *blue = p + 4 * (220 * W + 128);
	bool corners = red[0] > 200 && red[1] < 60 && red[2] < 60 && blue[2] > 200 && blue[0] < 60 && blue[1] < 60;
	printf("GL TRIANGLE: %s: %u pixels drawn (about 21000 wanted), %u missing inside, %u drawn outside; near the red "
	    "corner %u %u %u, near the top %u %u %u: %s\n", what, inside, bad, outside_wrong, red[0], red[1], red[2], blue[0],
	    blue[1], blue[2], corners ? "colours right" : "COLOURS WRONG");

	char path[600];
	snprintf(path, sizeof(path), "%s/%s", dir, ppm);
	FILE *f = fopen(path, "wb");
	if (f != NULL) {
		fprintf(f, "P6\n%d %d\n255\n", W, H);
		for (int y = H - 1; y >= 0; y--) {
			for (int x = 0; x < W; x++) {
				fwrite(p + 4 * (y * W + x), 1, 3, f);
			}
		}
		fclose(f);
		printf("GL TRIANGLE: %s written\n", path);
	}
	return (inside < 19000 || inside > 23000) + (bad != 0) + (outside_wrong != 0) + !corners;
}

static bool
current(OSMesaContext ctx, void *buffer, const char *what)
{
	if (ctx == NULL || !OSMesaMakeCurrent(ctx, buffer, GL_UNSIGNED_BYTE, W, H)) {
		printf("GL TRIANGLE: no %s context\n", what);
		failed++;
		return false;
	}
	printf("GL TRIANGLE: %s: %s / %s / %s\n", what, (const char *)glGetString(GL_VENDOR),
	    (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
	return true;
}

static GLuint
shader(GLenum kind, const char *source)
{
	GLuint s = glCreateShader(kind);
	GLint ok = 0;
	char log[512] = "";

	glShaderSource(s, 1, &source, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("GL TRIANGLE: a shader does not compile: %s\n", log);
		failed++;
	}
	return s;
}

int
main(int argc, char **argv)
{
	const char *lib = "/tmp/libOSMesa.8.dylib";
	bool soft = false;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "soft") == 0) {
			soft = true;
			setenv("OSMESA_SOFTWARE", "1", 1);
		} else if (strncmp(argv[i], "debug=", 6) == 0) {
			setenv("FD_MESA_DEBUG", argv[i] + 6, 1);
		} else {
			lib = argv[i];
		}
	}
	snprintf(dir, sizeof(dir), "%s", lib);
	char *slash = strrchr(dir, '/');
	if (slash != NULL) {
		*slash = 0;
	} else {
		strcpy(dir, ".");
	}

	double t0 = now();
	void *mesa = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
	if (mesa == NULL) {
		printf("GL TRIANGLE: %s does not load: %s\n", lib, dlerror());
		return 2;
	}
	if (!SYM(mesa, OSMesaCreateContextExt) || !SYM(mesa, OSMesaCreateContextAttribs) || !SYM(mesa, OSMesaMakeCurrent) ||
	    !SYM(mesa, OSMesaDestroyContext) || !SYM(mesa, OSMesaGetProcAddress)) {
		printf("GL TRIANGLE: %s has no OSMesa entry points\n", lib);
		return 2;
	}
	GL(glGetString);
	GL(glViewport);
	GL(glClearColor);
	GL(glClear);
	GL(glFinish);
	GL(glGetError);
	GL(glReadPixels);
	GL(glBegin);
	GL(glEnd);
	GL(glColor3f);
	GL(glVertex2f);
	GL(glCreateShader);
	GL(glShaderSource);
	GL(glCompileShader);
	GL(glGetShaderiv);
	GL(glGetShaderInfoLog);
	GL(glCreateProgram);
	GL(glAttachShader);
	GL(glBindAttribLocation);
	GL(glLinkProgram);
	GL(glGetProgramiv);
	GL(glGetProgramInfoLog);
	GL(glUseProgram);
	GL(glVertexAttribPointer);
	GL(glEnableVertexAttribArray);
	GL(glDrawArrays);
	printf("GL TRIANGLE: %s loaded in %.0f ms%s\n", lib, (now() - t0) * 1e3, soft ? " (OSMESA_SOFTWARE=1)" : "");

	/* 1. OpenGL, glBegin/glEnd */
	uint8_t *picture = calloc(W * H, 4);
	t0 = now();
	OSMesaContext gl = OSMesaCreateContextExt(OSMESA_RGBA, 0, 0, 0, NULL);
	if (current(gl, picture, "OpenGL")) {
		bool gpu = strstr((const char *)glGetString(GL_RENDERER), "FD710") != NULL;
		printf("GL TRIANGLE: OpenGL: the renderer is %s\n", gpu ? "the Adreno 710 (freedreno)" : "NOT the GPU's");
		failed += !soft && !gpu;
		glViewport(0, 0, W, H);
		glClearColor(0, 0, 0.25f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_TRIANGLES);
		for (int i = 0; i < 3; i++) {
			glColor3f(colour[i][0], colour[i][1], colour[i][2]);
			glVertex2f(corner[i][0], corner[i][1]);
		}
		glEnd();
		glFinish();
		printf("GL TRIANGLE: OpenGL: context, draw and finish in %.1f ms, error %#x\n", (now() - t0) * 1e3, glGetError());
		failed += check("OpenGL", picture, "gl-triangle-gl.ppm");
		OSMesaDestroyContext(gl);
	}

	/* 2. OpenGL ES 2, shaders and glDrawArrays, then glReadPixels and 100 frames */
	const int attribs[] = { OSMESA_FORMAT, OSMESA_RGBA, OSMESA_DEPTH_BITS, 0, OSMESA_STENCIL_BITS, 0,
		                OSMESA_PROFILE, OSMESA_ES_PROFILE, OSMESA_CONTEXT_MAJOR_VERSION, 2, 0 };
	uint8_t *es_picture = calloc(W * H, 4), *read = calloc(W * H, 4);
	t0 = now();
	OSMesaContext es = OSMesaCreateContextAttribs(attribs, NULL);
	if (current(es, es_picture, "OpenGL ES 2")) {
		static const char *vs = "attribute vec2 position;\nattribute vec3 colour;\nvarying vec3 v;\n"
		    "void main() { v = colour; gl_Position = vec4(position, 0.0, 1.0); }\n";
		static const char *fs = "precision mediump float;\nvarying vec3 v;\n"
		    "void main() { gl_FragColor = vec4(v, 1.0); }\n";
		GLuint program = glCreateProgram();
		GLint linked = 0;

		glAttachShader(program, shader(GL_VERTEX_SHADER, vs));
		glAttachShader(program, shader(GL_FRAGMENT_SHADER, fs));
		glBindAttribLocation(program, 0, "position");
		glBindAttribLocation(program, 1, "colour");
		glLinkProgram(program);
		glGetProgramiv(program, GL_LINK_STATUS, &linked);
		if (!linked) {
			char log[512] = "";
			glGetProgramInfoLog(program, sizeof(log), NULL, log);
			printf("GL TRIANGLE: the program does not link: %s\n", log);
			failed++;
		}
		glUseProgram(program);
		glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, corner);
		glVertexAttribPointer(1, 3, GL_FLOAT, 0, 0, colour);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glViewport(0, 0, W, H);
		glClearColor(0, 0, 0.25f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glFinish();
		printf("GL TRIANGLE: OpenGL ES 2: context, shaders, draw and finish in %.1f ms, error %#x\n",
		    (now() - t0) * 1e3, glGetError());
		failed += check("OpenGL ES 2", es_picture, "gl-triangle-es.ppm");
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, read);
		printf("GL TRIANGLE: glReadPixels %s the picture\n", memcmp(read, es_picture, W * H * 4) == 0 ? "matches" :
		    "does NOT match");
		failed += memcmp(read, es_picture, W * H * 4) != 0;
		t0 = now();
		for (int n = 0; n < 100; n++) {
			glClear(GL_COLOR_BUFFER_BIT);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glFinish();
		}
		printf("GL TRIANGLE: 100 frames (clear, draw, finish) in %.1f ms, error %#x\n", (now() - t0) * 1e3, glGetError());
		OSMesaDestroyContext(es);
	}

	printf("GL TRIANGLE: %s (%u failed)\n", failed != 0 ? "SOMETHING IS NOT AS IT SHOULD BE" :
	    soft ? "softpipe drew both pictures right" : "THE ADRENO 710 DREW BOTH PICTURES, THROUGH MESA", failed);
	return failed == 0 ? 0 : 1;
}
