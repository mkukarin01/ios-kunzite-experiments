/*
 * redwood-check: RedwoodGPU from userland, the way redwood-drm (and so Mesa) talks to it,
 * without Mesa: glue plan K4 (main repo ios/gpu-glue-plan.md 4). Run over the agent while
 * the GPU is kept (kgpu_keep=1 kgpu>=21):
 *
 *   curl -s 'http://192.168.7.2:8022/run?a=/tmp/redwood-check'
 *
 * It opens the service, reads every MSM_PARAM_* the kernel answers and compares them
 * with glue plan 3.3's values, then makes two buffers (MSM_GEM_NEW), asks their GPU
 * addresses (MSM_GEM_INFO GET_IOVA: the user range from 0x40_0000_0000, the second one
 * 64K grain after the first's end), maps one into this process (client memory of the
 * buffer's handle, as redwood_drm_mmap does), writes it, reads it back, unmaps it, and
 * gives both back (DRM_GEM_CLOSE). A third buffer must get the first one's address
 * again. Exit status 0 if all of it went as expected.
 *
 * Build: userland/gpu/mk-redwood-check.sh (arm64 iOS 16, signed ad hoc like the agent).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <IOKit/IOKitLib.h>
#include <mach/mach.h>

/* RedwoodGPU.cpp: Linux's msm_drm.h requests as selectors, their structures as they are */
enum {
	kDrmGemClose    = 0x09,
	kMsmGetParam    = 0x40,
	kMsmGemNew      = 0x42,
	kMsmGemInfo     = 0x43,
};
struct msm_param {
	uint32_t pipe, param;
	uint64_t value;
	uint32_t len, pad;
};
struct msm_gem_new {
	uint64_t size;
	uint32_t flags, handle;
};
struct msm_gem_info {
	uint32_t handle, info;
	uint64_t value;
	uint32_t len, pad;
};
struct drm_gem_close {
	uint32_t handle, pad;
};
#define MSM_PIPE_3D0            0x10
#define MSM_BO_WC               0x00020000u
#define MSM_INFO_GET_OFFSET     0
#define MSM_INFO_GET_IOVA       1
#define MSM_INFO_GET_FLAGS      5
#define USER_VA_START           0x4000000000ull
#define USER_VA_END             0x8000000000ull
#define GRAIN                   0x10000ull

static io_connect_t gpu;
static unsigned failed;

static kern_return_t
call(uint32_t selector, void *data, size_t size)
{
	size_t back = size;

	return IOConnectCallStructMethod(gpu, selector, data, size, data, &back);
}

/* glue plan 3.3: what the kernel should answer; TIMESTAMP only has to move */
static const struct {
	uint32_t param;
	const char *name;
	uint64_t want;
	bool any;
} params[] = {
	{ 0x01, "GPU_ID", 710, false },
	{ 0x02, "GMEM_SIZE", 0x80000, false },
	{ 0x03, "CHIP_ID", 0x07010000, false },
	{ 0x04, "MAX_FREQ", 345000000, false },
	{ 0x05, "TIMESTAMP", 0, true },
	{ 0x06, "GMEM_BASE", 0x1000000, false },
	{ 0x07, "PRIORITIES", 1, false },
	{ 0x08, "PP_PGTABLE", 0, false },
	{ 0x09, "FAULTS", 0, false },
	{ 0x0a, "SUSPENDS", 0, false },
	{ 0x10, "HIGHEST_BANK_BIT", 14, false },
	{ 0x11, "RAYTRACING", 0, false },
	{ 0x12, "UBWC_SWIZZLE", 6, false },
	{ 0x13, "MACROTILE_MODE", 1, false },
};

static bool
param(uint32_t which, uint64_t *value)
{
	struct msm_param p = { .pipe = MSM_PIPE_3D0, .param = which };
	kern_return_t kr = call(kMsmGetParam, &p, sizeof(p));

	*value = p.value;
	return kr == KERN_SUCCESS;
}

/* a buffer of `size` bytes: its handle, 0 if none */
static uint32_t
buffer(uint64_t size)
{
	struct msm_gem_new n = { .size = size, .flags = MSM_BO_WC };
	kern_return_t kr = call(kMsmGemNew, &n, sizeof(n));

	if (kr != KERN_SUCCESS || n.handle == 0) {
		printf("REDWOOD CHECK: no buffer of %" PRIu64 " KB: %#x\n", size >> 10, kr);
		failed++;
		return 0;
	}
	return n.handle;
}

static uint64_t
info(uint32_t handle, uint32_t what)
{
	struct msm_gem_info i = { .handle = handle, .info = what };

	if (call(kMsmGemInfo, &i, sizeof(i)) != KERN_SUCCESS) {
		printf("REDWOOD CHECK: buffer %u: info %u not answered\n", handle, what);
		failed++;
		return 0;
	}
	return i.value;
}

static void
release(uint32_t handle)
{
	struct drm_gem_close c = { .handle = handle };
	kern_return_t kr = call(kDrmGemClose, &c, sizeof(c));

	if (kr != KERN_SUCCESS) {
		printf("REDWOOD CHECK: buffer %u not given back: %#x\n", handle, kr);
		failed++;
	}
}

int
main(void)
{
	io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("RedwoodGPU"));
	kern_return_t kr;
	uint64_t v, t0 = 0;

	if (service == 0) {
		printf("REDWOOD CHECK: no RedwoodGPU service (is the GPU kept: kgpu_keep=1 kgpu>=21?)\n");
		return 2;
	}
	kr = IOServiceOpen(service, mach_task_self(), 0, &gpu);
	IOObjectRelease(service);
	if (kr != KERN_SUCCESS) {
		printf("REDWOOD CHECK: RedwoodGPU does not open: %#x\n", kr);
		return 2;
	}
	printf("REDWOOD CHECK: RedwoodGPU open\n");

	for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); i++) {
		bool ok = param(params[i].param, &v);
		bool right = ok && (params[i].any ? v != 0 : v == params[i].want);

		if (params[i].param == 0x05) {
			t0 = v;
		}
		printf("REDWOOD CHECK: param %#04x %-16s %s %#" PRIx64 "%s\n", params[i].param, params[i].name,
		    ok ? "=" : "not answered,", v, right ? "" : " (NOT as planned)");
		failed += !right;
	}
	if (param(0x05, &v)) {
		printf("REDWOOD CHECK: TIMESTAMP again %#" PRIx64 ": %s\n", v, v > t0 ? "moving" : "NOT moving");
		failed += !(v > t0);
	}
	if (param(0x0e, &v)) {
		printf("REDWOOD CHECK: VA_START answered (%#" PRIx64 "): the kernel was to keep the addresses its own\n", v);
		failed++;
	}

	uint32_t a = buffer(0x10000), b = buffer(0x100000);
	uint64_t va_a = a ? info(a, MSM_INFO_GET_IOVA) : 0, va_b = b ? info(b, MSM_INFO_GET_IOVA) : 0;
	bool range = va_a >= USER_VA_START && va_a < USER_VA_END && va_b >= USER_VA_START && va_b < USER_VA_END;

	printf("REDWOOD CHECK: buffer %u (64 KB) at GPU %#" PRIx64 ", buffer %u (1 MB) at %#" PRIx64 ": %s; %s\n", a, va_a, b,
	    va_b, range ? "in the user range" : "NOT in the user range",
	    va_b == va_a + 0x10000 + GRAIN ? "the second after the first and one 64K grain" : "the second NOT where expected");
	failed += !range || va_b != va_a + 0x10000 + GRAIN;
	if (a != 0) {
		printf("REDWOOD CHECK: buffer %u: offset %#" PRIx64 ", flags %#" PRIx64 "\n", a, info(a, MSM_INFO_GET_OFFSET),
		    info(a, MSM_INFO_GET_FLAGS));
	}

	mach_vm_address_t at = 0;
	mach_vm_size_t size = 0;
	kr = b != 0 ? IOConnectMapMemory64(gpu, b, mach_task_self(), &at, &size, kIOMapAnywhere) : KERN_FAILURE;
	if (kr == KERN_SUCCESS) {
		volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)at;
		unsigned zero = 0, wrong = 0;

		for (size_t i = 0; i < size / 4; i++) {
			zero += w[i] != 0;
		}
		for (size_t i = 0; i < size / 4; i++) {
			w[i] = 0x6b670000u ^ (uint32_t)i;
		}
		for (size_t i = 0; i < size / 4; i++) {
			wrong += w[i] != (0x6b670000u ^ (uint32_t)i);
		}
		printf("REDWOOD CHECK: buffer %u mapped at %#llx, %llu bytes: %u words not zero when made, %u of %llu wrong "
		    "after writing\n", b, (unsigned long long)at, (unsigned long long)size, zero, wrong,
		    (unsigned long long)(size / 4));
		failed += zero != 0 || wrong != 0 || size < 0x100000;
		IOConnectUnmapMemory64(gpu, b, mach_task_self(), at);
	} else {
		printf("REDWOOD CHECK: buffer %u does not map: %#x\n", b, kr);
		failed++;
	}

	if (a != 0) {
		release(a);
	}
	if (b != 0) {
		release(b);
	}
	uint32_t c = buffer(0x10000);
	uint64_t va_c = c ? info(c, MSM_INFO_GET_IOVA) : 0;
	printf("REDWOOD CHECK: buffer %u (64 KB) at GPU %#" PRIx64 ": %s\n", c, va_c,
	    va_c == va_a ? "the first one's address again" : "NOT the first one's address");
	failed += va_c != va_a;
	if (c != 0) {
		release(c);
	}

	IOServiceClose(gpu);
	printf("REDWOOD CHECK: %s (%u failed)\n", failed == 0 ? "ALL AS PLANNED" : "SOMETHING IS NOT AS PLANNED", failed);
	return failed == 0 ? 0 : 1;
}
