/*
 * eagl-check (main repo ios/gpu-glue-plan.md U2): libtopaz-gl behind iOS's own EAGLContext and
 * OpenGL ES, in a plain program, before the window server is given it (U4). It loads
 * libOSMesa.8.dylib, then libtopaz-gl.dylib (whose constructor takes over six of EAGLContext's
 * methods) from DIR; dyld takes the OSMesa already loaded for libtopaz-gl's
 * /topaz/lib/libOSMesa.8.dylib, by its install name. Then, as CoreAnimation does: an EAGLContext
 * of API 3 (-initWithAPI:properties:) and its table (-getMacroContextPrivate; eagl_gen.h, from
 * eagl/eagl_slots.txt), entries called as entry(table[0], arguments...).
 *
 * The drawing goes through OpenGLES.framework's own gl* functions: each reads the thread's OpenGL
 * slot (TSD 30, [tpidrro_el0 + 0xf0]) and jumps through [slot + its offset]; the slot + 0x20 is the
 * table. iOS's +setCurrentContext: fills that slot from the context's own fields, which ours do not
 * have (boot #53: it said YES and left the gl* functions doing nothing). CoreAnimation does not
 * need it, as it calls the table directly; so this points the slot at the table as iOS does for its
 * own contexts, and every gl* function lands on the entry CoreAnimation would call. If the table is
 * wrong, this crashes or draws nothing, not backboardd.
 *
 *   eagl-check [soft] [DIR]      DIR: default /tmp; soft: OSMESA_SOFTWARE=1 (softpipe, no GPU needed)
 *
 * The picture: a clear to (0, 0, 64) and a triangle with red, green and blue corners in a 256 x 256
 * texture framebuffer, read back and checked as gl-triangle checks its own; 100 frames timed.
 */
#define GLES_SILENCE_DEPRECATION 1
#import <OpenGLES/EAGL.h>
#import <OpenGLES/ES3/gl.h>
#include <dlfcn.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (what CoreAnimation calls; not in the public header) */
@interface EAGLContext (Private)
- (id)initWithAPI:(EAGLRenderingAPI)api properties:(id)properties;
- (void *)getMacroContextPrivate;
@end

enum { SIZE = 256 };

static unsigned failed;

/* the thread's OpenGL slot, where OpenGLES.framework's gl* functions find the current table */
static void **
gl_slot(void)
{
	void **tsd;

	__asm__ volatile ("mrs %0, tpidrro_el0" : "=r"(tsd));
	return &tsd[30];
}

static double
now(void)
{
	static mach_timebase_info_data_t base;

	if (base.denom == 0) {
		mach_timebase_info(&base);
	}
	return (double)mach_absolute_time() * base.numer / base.denom / 1e9;
}

static void
check(int ok, const char *what)
{
	printf("EAGL CHECK: %s: %s\n", ok ? "ok" : "FAILED", what);
	failed += !ok;
}

static GLuint
shader(GLenum kind, const char *text)
{
	GLuint s = glCreateShader(kind);
	GLint ok = 0;
	char log[512] = "";

	glShaderSource(s, 1, &text, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		printf("EAGL CHECK: shader: %s\n", log);
	}
	return s;
}

int
main(int argc, char **argv)
{
	const char *dir = "/tmp";
	char path[512];
	int soft = 0;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "soft") == 0) {
			soft = 1;
			setenv("OSMESA_SOFTWARE", "1", 1);
		} else {
			dir = argv[i];
		}
	}
	double t0 = now();
	snprintf(path, sizeof(path), "%s/libOSMesa.8.dylib", dir);
	if (dlopen(path, RTLD_NOW | RTLD_GLOBAL) == NULL) {
		printf("EAGL CHECK: %s\n", dlerror());
		return 1;
	}
	snprintf(path, sizeof(path), "%s/libtopaz-gl.dylib", dir);
	if (dlopen(path, RTLD_NOW) == NULL) {
		printf("EAGL CHECK: %s\n", dlerror());
		return 1;
	}
	printf("EAGL CHECK: libraries from %s loaded in %.0f ms%s\n", dir, (now() - t0) * 1e3,
	    soft ? " (OSMESA_SOFTWARE=1)" : "");

	EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3 properties:nil];
	check(context != nil, "an EAGLContext of API 3 (-initWithAPI:properties:)");
	if (context == nil) {
		return 1;
	}
	check([context API] == kEAGLRenderingAPIOpenGLES3, "-API says 3");
	void **table = [context getMacroContextPrivate];
	check(table != NULL && table[0] != NULL, "-getMacroContextPrivate: a table, its context in entry 0");
	if (table == NULL) {
		return 1;
	}
	check([EAGLContext setCurrentContext:context], "+setCurrentContext:");
	void **slot = gl_slot(), *before = *slot;
	printf("EAGL CHECK: table %p; the thread's OpenGL slot after +setCurrentContext: %p, %s; +currentContext %s\n",
	    (void *)table, before, before == (char *)table - 0x20 ? "the table already" : "pointed at the table now",
	    [EAGLContext currentContext] == context ? "ours" : "not ours");
	*slot = (char *)table - 0x20;

	/* from here on every gl* call is OpenGLES.framework's, through the table */
	const char *renderer = (const char *)glGetString(GL_RENDERER);
	const char *version = (const char *)glGetString(GL_VERSION);
	printf("EAGL CHECK: renderer \"%s\", version \"%s\"\n", renderer ? renderer : "(none)", version ? version : "(none)");
	check(renderer != NULL && version != NULL, "glGetString through the table");

	GLuint texture, framebuffer;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &framebuffer);
	glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "a texture framebuffer");

	GLuint program = glCreateProgram();
	glAttachShader(program, shader(GL_VERTEX_SHADER,
	    "attribute vec2 position;\nattribute vec3 colour;\nvarying vec3 c;\n"
	    "void main() { c = colour; gl_Position = vec4(position, 0.0, 1.0); }\n"));
	glAttachShader(program, shader(GL_FRAGMENT_SHADER,
	    "precision mediump float;\nvarying vec3 c;\nvoid main() { gl_FragColor = vec4(c, 1.0); }\n"));
	glBindAttribLocation(program, 0, "position");
	glBindAttribLocation(program, 1, "colour");
	glLinkProgram(program);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	check(linked, "shaders compiled and linked");

	static const GLfloat position[] = { -0.8f, -0.8f, 0.8f, -0.8f, 0.0f, 0.8f };
	static const GLfloat colour[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	double t1 = now();
	glViewport(0, 0, SIZE, SIZE);
	glClearColor(0.0f, 0.0f, 0.25f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(program);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, position);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, colour);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();
	printf("EAGL CHECK: clear + triangle + finish in %.1f ms\n", (now() - t1) * 1e3);

	static unsigned char pixels[SIZE * SIZE * 4];
	glReadPixels(0, 0, SIZE, SIZE, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	check(glGetError() == GL_NO_ERROR, "no GL error");
	unsigned drawn = 0, missing = 0, outside = 0;
	for (int y = 0; y < SIZE; y++) {
		for (int x = 0; x < SIZE; x++) {
			const unsigned char *p = pixels + 4 * (y * SIZE + x);
			int background = p[0] == 0 && p[1] == 0 && (p[2] == 63 || p[2] == 64);
			/* (the triangle in pixels, a pixel's centre: well inside, well outside, or the edge) */
			double fx = (x + 0.5) / SIZE * 2 - 1, fy = (y + 0.5) / SIZE * 2 - 1;
			double left = 2 * fx + 0.8 - fy, right = -2 * fx + 0.8 - fy;     /* (the slanted edges: y = +-2x + 0.8) */
			double inside = fmin(fmin(left, right), fy + 0.8);
			drawn += !background;
			missing += background && inside > 0.02;
			outside += !background && inside < -0.02;
		}
	}
	const unsigned char *red = pixels + 4 * (30 * SIZE + 40), *top = pixels + 4 * (220 * SIZE + 128);
	printf("EAGL CHECK: %u pixels drawn, %u missing inside, %u outside; near red %u %u %u, near the top %u %u %u; "
	    "corner %u %u %u\n", drawn, missing, outside, red[0], red[1], red[2], top[0], top[1], top[2], pixels[0], pixels[1],
	    pixels[2]);
	check(pixels[0] == 0 && pixels[1] == 0 && (pixels[2] == 63 || pixels[2] == 64), "the clear colour");
	check(drawn >= 19000 && drawn <= 23000 && missing == 0 && outside == 0, "the triangle's pixels");
	check(red[0] > 150 && red[1] < 80 && red[2] < 80 && top[2] > 150 && top[0] < 80, "the corners' colours");

	double t2 = now();
	for (int i = 0; i < 100; i++) {
		glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glFinish();
	}
	printf("EAGL CHECK: 100 frames in %.1f ms\n", (now() - t2) * 1e3);

	*slot = before;
	[EAGLContext setCurrentContext:nil];
	[context release];
	printf("EAGL CHECK: %s (%u failed)\n", failed ? "SOMETHING IS WRONG" : "THE TABLE WORKS, THROUGH iOS's OWN OpenGL ES", failed);
	return failed != 0;
}
