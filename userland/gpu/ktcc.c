/*
 * ktcc [SERVICE]: what tccd answers this process for a TCC service (default kTCCServicePhotos):
 * TCCAccessPreflight() and TCCAccessCheckAuditToken() on our own audit token, from the private
 * TCC framework (dlopen: it is not in the SDK). Built twice by mk-gl-triangle.sh: ktcc signed with
 * com.apple.private.tcc.allow = kTCCServicePhotos (as assetsd is) and ktcc-plain without, to tell
 * whether tccd honours that entitlement on this port (assetsd: "assetsd requires TCCPhotosService
 * access", though its signature carries it).
 */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* sys/codesign.h is not in the iOS SDK: the audit-token form tccd's XPC peers are checked with */
extern int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);
extern int csops_audittoken(pid_t pid, unsigned int ops, void *useraddr, size_t usersize, audit_token_t *token);

int
main(int argc, char **argv)
{
	const char *name = argc > 1 ? argv[1] : "kTCCServicePhotos";
	void *tcc = dlopen("/System/Library/PrivateFrameworks/TCC.framework/TCC", RTLD_NOW);
	CFStringRef service = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);

	if (tcc == NULL) {
		printf("ktcc: %s\n", dlerror());
		return 1;
	}
	int (*preflight)(CFStringRef, CFDictionaryRef) = (int (*)(CFStringRef, CFDictionaryRef))dlsym(tcc, "TCCAccessPreflight");
	Boolean (*check)(CFStringRef, audit_token_t, CFDictionaryRef) =
	    (Boolean (*)(CFStringRef, audit_token_t, CFDictionaryRef))dlsym(tcc, "TCCAccessCheckAuditToken");
	if (preflight != NULL) {
		/* 0 granted, 1 denied, 2 would prompt (unknown) */
		int r = preflight(service, NULL);
		printf("ktcc: TCCAccessPreflight(%s) = %d (%s)\n", name, r, r == 0 ? "granted" : r == 1 ? "denied" : "unknown");
	} else {
		printf("ktcc: no TCCAccessPreflight\n");
	}
	if (check != NULL) {
		audit_token_t token;
		mach_msg_type_number_t count = TASK_AUDIT_TOKEN_COUNT;

		if (task_info(mach_task_self(), TASK_AUDIT_TOKEN, (task_info_t)&token, &count) == KERN_SUCCESS) {
			static unsigned char blob[64 * 1024];
			uint32_t status = 0;
			int r;

			printf("ktcc: TCCAccessCheckAuditToken(%s, ours) = %s\n", name, check(service, token, NULL) ? "true" : "false");
			/* the kernel's side of it: by pid, and by our audit token (pid %u, pid version %u) */
			printf("ktcc: our audit token: pid %u, pid version %u (getpid %d)\n", token.val[5], token.val[7], getpid());
			r = csops(getpid(), 16, blob, sizeof(blob));
			printf("ktcc: csops(DER entitlements) = %d%s%s\n", r, r ? ", " : "", r ? strerror(errno) : "");
			r = csops_audittoken(getpid(), 0, &status, sizeof(status), &token);
			printf("ktcc: csops_audittoken(STATUS) = %d%s%s, status %#x\n", r, r ? ", " : "", r ? strerror(errno) : "", status);
			r = csops_audittoken(getpid(), 16, blob, sizeof(blob), &token);
			printf("ktcc: csops_audittoken(DER entitlements) = %d%s%s\n", r, r ? ", " : "", r ? strerror(errno) : "");
		}
	} else {
		printf("ktcc: no TCCAccessCheckAuditToken\n");
	}
	return 0;
}
