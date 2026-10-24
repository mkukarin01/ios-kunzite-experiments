/*
 * libtopaz-inject.dylib (private build, local only): inserted into backboardd
 * (DYLD_INSERT_LIBRARIES in its launchd job). When the kernel has published the
 * GPU service (boot-arg gpu=1, TopazAdreno.cpp), it loads the OpenGL ES library
 * that stands behind EAGLContext, so CoreAnimation's window server draws with
 * the Adreno instead of its software renderer. gl=0 in the boot-args: never.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/ucontext.h>
#include <objc/runtime.h>
#include <mach/mach.h>
#include <stdint.h>

extern const void *_dyld_get_shared_cache_range(size_t *length);

#define TOPAZ_GL "/topaz/lib/libtopaz-gl.dylib"

/*
 * CoreAnimation's OpenGL ES renderer hands a "function"'s numbers (a colour matrix, ...) to
 * GLContext::function_uniform(); for the functions it has no shader for they go nowhere, and our
 * library draws those with programs of its own (eagl.c) once told them. Without that the lock
 * screen's vibrantColorMatrix layer is a red veil. (Scheme from darwin-redwood inject.c; addresses
 * are this iOS 16.7.16 cache's.)
 */
#define CACHE_BASE              0x180000000UL
#define VT_GLES_CONTEXT         0x1d5b2f6e8UL   /* vtable for CA::OGL::GLESContext */
#define VSLOT(vtable, offset)   ((vtable) + 0x10 + (offset))
#define FN_FUNCTION_SUPPORTED   0x1884b2164UL   /* CA::OGL::GLContext::function_supported() const */
#define FN_FUNCTION_UNIFORM     0x1884b21acUL   /* CA::OGL::GLContext::function_uniform(unsigned, unsigned long, unsigned long, float const*) */
#define TEX_FRAGMENT_SHADERS    0x1d5b48a10UL   /* CA::OGL::tex_fragment_shaders: a GLSL source per function, or NULL */

static uintptr_t slide;

static void *
replace_virtual(const char *what, uintptr_t slot, uintptr_t expected, void *replacement)
{
	uintptr_t *entry = (uintptr_t *)(slot + slide);
	vm_address_t page = (vm_address_t)entry & ~(vm_address_t)(vm_page_size - 1);
	void *original;

	if (*entry != expected + slide) {
		fprintf(stderr, "topaz-gl: %s: slot holds %#lx, expected %#lx: left alone\n", what, *entry - slide, expected);
		return NULL;
	}
	if (vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
		fprintf(stderr, "topaz-gl: %s: table not writable\n", what);
		return NULL;
	}
	original = (void *)*entry;
	*entry = (uintptr_t)replacement;
	vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
	return original;
}

static int (*orig_function_supported)(void *context);
static void (*gl_function_refused)(unsigned function);
static void (*orig_function_uniform)(void *context, unsigned index, size_t components, size_t count, const float *values);
static void (*gl_function_values)(void *context, unsigned function, unsigned index, size_t components, size_t count,
    const float *values);

static unsigned
context_function(void *context)
{
	return *(*(uint8_t **)((char *)context + 0x10) + 0x10);
}

/*
 * kunzite (boot #55's census): CoreAnimation's GL renderer has no function 40 ("sb3"), the blur it
 * puts over a keyboard's backdrop; refused, it draws that backdrop blocky (Settings) or not at
 * all (Safari's keyboard came out black). With the file /topaz/sb3 there when the window server
 * starts, it is told 40 is there: it then draws it with no program, which eagl.c draws (plainly,
 * or with its own sb3 program) and whose numbers it writes down.
 */
#define FUNCTION_SB3 40
#define SB3_SWITCH "/topaz/sb3"
static int sb3_yes;

/*
 * Told yes, CoreAnimation crashed (boot #55): GaussianBlurFilter::render -> Context::blur_surface ->
 * BlurState::single_pass_blur -> function_uniform -> update_shader -> create_shader, which
 * strdup()s CA::OGL::tex_fragment_shaders[40], and that entry is NULL: the GL renderer has no
 * source for "sb3" (nor for 38, 39, drawn by eagl.c), though create_shader expects one. 40's
 * vertex shader is the Gaussian blurs' (32-37, vertex_shaders[6]): texcoord6/7 = t0 -/+ offset[0],
 * texcoord5/8 = t0 -/+ offset[1], and so on. What CoreAnimation gives 40 (boot #55, Safari's
 * keyboard): offset = (-a, b), (a, b), (-a, -b), (a, -b), so texcoord5-8 are the four corners
 * (3, 4, 9 and 10 repeat them); weight = 0.25, 0, 0, 0; divide = 1. So here is a source for the
 * entry, in the style of the GL renderer's own blurs (32-37: sampler2DRect; eagl.c's
 * glShaderSource rewrites it as it does the others): the four corners averaged. Taking weight and
 * divide as the others do looked worse (black big-pixel lines behind the keys: the divide makes
 * every texel with any alpha opaque); without them Settings' keyboard is right. Safari's stays
 * wrong either way (parked: a pass around this one, not this one; NOTES, boot #55). CoreAnimation
 * still sets divide, which leaves a GL error 0x502 in the log, harmless.
 */
static const char sb3_fragment[] =
	"uniform lowp sampler2DRect texture0;\n"
	"varying highp vec2 texcoord5, texcoord6, texcoord7, texcoord8;\n"
	"void main() {\n"
	"  highp vec4 acc;\n"
	"  acc = texture2DRect(texture0, texcoord6);\n"
	"  acc += texture2DRect(texture0, texcoord7);\n"
	"  acc += texture2DRect(texture0, texcoord5);\n"
	"  acc += texture2DRect(texture0, texcoord8);\n"
	"  gl_FragData[0] = acc * 0.25;\n"
	"}\n";

/* tex_fragment_shaders[40] made to point at sb3_fragment, if it is still NULL; 0 if not done */
static int
sb3_source(void)
{
	uintptr_t *entry = (uintptr_t *)(TEX_FRAGMENT_SHADERS + slide + FUNCTION_SB3 * sizeof(uintptr_t));
	vm_address_t page = (vm_address_t)entry & ~(vm_address_t)(vm_page_size - 1);

	if (*entry != 0) {
		fprintf(stderr, "topaz-gl: tex_fragment_shaders[40] holds %#lx, not NULL: left alone, sb3 not offered\n", *entry);
		return 0;
	}
	if (vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) != KERN_SUCCESS) {
		fprintf(stderr, "topaz-gl: tex_fragment_shaders not writable: sb3 not offered\n");
		return 0;
	}
	*entry = (uintptr_t)sb3_fragment;
	vm_protect(mach_task_self(), page, vm_page_size, FALSE, VM_PROT_READ);
	return 1;
}

static int
my_function_supported(void *context)
{
	static uint8_t said[256];
	int supported = orig_function_supported(context);
	unsigned function = context_function(context);

	if (!supported && function == FUNCTION_SB3 && sb3_yes) {
		if (!said[function & 255]) {
			said[function & 255] = 1;
			fprintf(stderr, "topaz-gl: CoreAnimation's GL renderer has no function %u (\"sb3\"): told it has (%s)\n", function,
			    SB3_SWITCH);
		}
		return 1;
	}
	if (!supported) {
		if (gl_function_refused != NULL) {
			gl_function_refused(function);
		}
		if (!said[function & 255]) {
			said[function & 255] = 1;
			fprintf(stderr, "topaz-gl: CoreAnimation's GL renderer has no function %u\n", function);
		}
	}
	return supported;
}

static void
my_function_uniform(void *context, unsigned index, size_t components, size_t count, const float *values)
{
	static unsigned sb3_told;

	if (context_function(context) == FUNCTION_SB3 && sb3_told < 6) {
		char text[400];
		size_t used = 0;

		sb3_told++;
		for (size_t i = 0; i < components * count && i < 28 && used < sizeof(text) - 16; i++) {
			used += (size_t)snprintf(text + used, sizeof(text) - used, " %.4g", (double)values[i]);
		}
		fprintf(stderr, "topaz-gl: sb3 uniform %u: %zu of %zu:%s\n", index, count, components, text);
	}
	gl_function_values(context, context_function(context), index, components, count, values);
	orig_function_uniform(context, index, components, count, values);
}

static void
gl_hooks(void *lib)
{
	size_t length = 0;
	const void *cache = _dyld_get_shared_cache_range(&length);

	if (cache == NULL || lib == NULL) {
		return;
	}
	slide = (uintptr_t)cache - CACHE_BASE;
	sb3_yes = access(SB3_SWITCH, F_OK) == 0 && sb3_source();
	gl_function_refused = (void (*)(unsigned))dlsym(lib, "redwood_gl_function_refused");
	gl_function_values = (void (*)(void *, unsigned, unsigned, size_t, size_t, const float *))dlsym(lib,
	    "redwood_gl_function_values");
	orig_function_supported = replace_virtual("function_supported", VSLOT(VT_GLES_CONTEXT, 0x378), FN_FUNCTION_SUPPORTED,
	    my_function_supported);
	if (gl_function_values != NULL) {
		orig_function_uniform = replace_virtual("function_uniform", VSLOT(VT_GLES_CONTEXT, 0x388), FN_FUNCTION_UNIFORM,
		    my_function_uniform);
	}
	fprintf(stderr, "topaz-gl: GLESContext hooks: function_supported %s, function_uniform %s; sb3 %s\n",
	    orig_function_supported ? "on" : "OFF", orig_function_uniform ? "on" : "OFF", sb3_yes ? "said to be there" : "as it is");
}

/* Where a crash happened, printed to stderr (the console): image + offset + symbol. */
static void
topaz_where(const char *what, void *addr)
{
	Dl_info info;

	if (dladdr(addr, &info) && info.dli_fname != NULL) {
		fprintf(stderr, "topaz-crash: %s %p = %s+0x%lx (%s)\n", what, addr, info.dli_fname,
		    (unsigned long)((char *)addr - (char *)info.dli_fbase), info.dli_sname ? info.dli_sname : "?");
	} else {
		fprintf(stderr, "topaz-crash: %s %p (no image)\n", what, addr);
	}
}

static void
topaz_crash(int sig, siginfo_t *si, void *uc_)
{
	ucontext_t *uc = (ucontext_t *)uc_;
	void *frames[16];
	int n;

	fprintf(stderr, "topaz-crash: signal %d at address %p\n", sig, si ? si->si_addr : NULL);
	if (uc != NULL && uc->uc_mcontext != NULL) {
		topaz_where("pc", (void *)uc->uc_mcontext->__ss.__pc);
		topaz_where("lr", (void *)uc->uc_mcontext->__ss.__lr);
	}
	n = backtrace(frames, 16);
	for (int i = 0; i < n; i++) {
		topaz_where("bt", frames[i]);
	}
	signal(sig, SIG_DFL);
	raise(sig);
}

static void
topaz_crash_handlers(void)
{
	struct sigaction sa;
	int sigs[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGTRAP, SIGFPE };

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = topaz_crash;
	sa.sa_flags = SA_SIGINFO;
	for (unsigned i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		sigaction(sigs[i], &sa, NULL);
	}
}

/*
 * containermanagerd (both): they ask the user manager for the user's personas, of which this
 * system has none, again and again before giving up, and everything that needs a container
 * waits with them -- extensions (posters, widgets) are not registered at all. Told personas are
 * not supported, they do not ask. (darwin-redwood inject.c; address of this 16.7.16 cache.)
 */
#define FN_PERSONAS_ARE_SUPPORTED 0x1c4c699f8UL /* +[MCMUserIdentityCache personasAreSupported] */

static BOOL
personas_are_supported(__unused id self, __unused SEL selector)
{
	return NO;
}

static void
container_manager_fixes(void)
{
	size_t length = 0;
	const void *cache = _dyld_get_shared_cache_range(&length);
	Class cls = objc_getClass("MCMUserIdentityCache");
	Method method = cls ? class_getClassMethod(cls, sel_registerName("personasAreSupported")) : NULL;
	IMP original = method ? method_getImplementation(method) : NULL;

	if (cache == NULL || original == NULL ||
	    (uintptr_t)original != FN_PERSONAS_ARE_SUPPORTED + ((uintptr_t)cache - CACHE_BASE)) {
		fprintf(stderr, "topaz-cm: +[MCMUserIdentityCache personasAreSupported] not as expected: left alone\n");
		return;
	}
	method_setImplementation(method, (IMP)personas_are_supported);
	fprintf(stderr, "topaz-cm: container manager: no personas\n");
	FILE *f = fopen("/var/mobile/topaz-logs/inject.log", "a");      /* (launchd sends its stderr to /dev/null) */
	if (f != NULL) {
		fprintf(f, "%s[%d]: no personas\n", getprogname(), getpid());
		fclose(f);
	}
}

__attribute__((constructor))
static void
topaz_inject(void)
{
	const char *name = getprogname();
	char args[1024] = "";
	size_t size = sizeof(args) - 1;
	io_service_t gpu;
	void *lib;

	if (name != NULL && strncmp(name, "containermanagerd", 17) == 0) {
		container_manager_fixes();
		return;
	}
	if (name == NULL || strcmp(name, "backboardd") != 0 || access(TOPAZ_GL, R_OK) != 0) {
		return;
	}
	if (sysctlbyname("kern.bootargs", args, &size, NULL, 0) == 0 && strstr(args, " gl=0") != NULL) {
		syslog(LOG_NOTICE, "topaz: gl=0: CoreAnimation keeps its software renderer");
		return;
	}
	/* the kernel starts the GPU once the root filesystem is there: give it a moment */
	for (int i = 0; i < 50; i++) {
		gpu = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RedwoodGPU"));
		if (gpu != 0) {
			break;
		}
		usleep(100000);
	}
	if (gpu == 0) {
		syslog(LOG_NOTICE, "topaz: no GPU service: software renderer");
		return;
	}
	IOObjectRelease(gpu);
	topaz_crash_handlers();
	lib = dlopen(TOPAZ_GL, RTLD_NOW);
	gl_hooks(lib);
	fprintf(stderr, "topaz-crash: handlers set, GL %s\n", lib ? "loaded" : dlerror());
	syslog(LOG_NOTICE, "topaz: %s %s", TOPAZ_GL, lib ? "loaded" : dlerror());
}
