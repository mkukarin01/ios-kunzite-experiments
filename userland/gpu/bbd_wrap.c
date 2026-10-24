/*
 * /usr/libexec/backboardd (topaz, private build): launchd does not pass
 * DYLD_* variables from a job to a system daemon, so this stands in for the
 * window server: it adds our loader to DYLD_INSERT_LIBRARIES and execs the
 * real one (/usr/libexec/real/backboardd: same name, same pid, same Mach
 * services launchd prepared).
 *
 * kunzite: the GPU is kept some time into the boot (boot #53: RedwoodGPU was published 3 min after
 * `fastboot boot`, before fork 0996 started keep mode at the root filesystem's mount), and
 * topaz_inject waits only 5 s for it. So with the GPU path on (boot-arg kgpu_keep, no gl=0) this
 * waits for RedwoodGPU before it starts the window server, up to WAIT_S, while the kernel shows
 * the bring-up on the boot logo (amber per step, green once RedwoodGPU is there). If the GPU is
 * still not there by then, the window server starts on the CPU, and a child of ours goes on
 * waiting; once the GPU comes, it stops the window server once, and launchd starts it again,
 * through here, onto the GPU.
 */
#include <IOKit/IOKitLib.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sysctl.h>

extern int proc_name(int pid, void *buffer, uint32_t size);    /* libproc.h, not in the iOS SDK */

#ifndef REAL                                /* kunzite: -DREAL='"/usr/libexec/backboardd"' (mk-topaz-gl.sh) */
#define REAL "/usr/libexec/real/backboardd"
#endif
#define LOADER "/topaz/lib/libtopaz-inject.dylib"
#define ENVFILE "/topaz/backboardd.env"      /* KEY=VALUE lines (tests: FD_MESA_DEBUG=sysmem ...) */
#define WAIT_S 90                           /* how long the window server's start waits for the GPU */
#define WATCH_S 900                         /* how long the child then goes on waiting */

static int
gpu_there(void)
{
	io_service_t gpu = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("RedwoodGPU"));

	if (gpu == IO_OBJECT_NULL) {
		return 0;
	}
	IOObjectRelease(gpu);
	return 1;
}

/* the child: waits for RedwoodGPU, then stops the window server (our pid, after the exec) once */
static void
gpu_watch(void)
{
	pid_t server = getpid();

	if (fork() != 0) {
		return;                     /* (the parent, or no child: the boot stays on the CPU) */
	}
	for (int s = 0; s < WATCH_S && getppid() == server; s++) {
		if (gpu_there()) {
			char name[64] = "";

			sleep(2);
			if (getppid() == server && proc_name(server, name, sizeof(name)) > 0 && strcmp(name, "backboardd") == 0) {
				fprintf(stderr, "topaz: the GPU is there after %d s: the window server restarts onto it\n", s);
				kill(server, SIGTERM);
			}
			_exit(0);
		}
		sleep(1);
	}
	_exit(0);
}

int
main(int argc, char *argv[])
{
	(void)argc;
	if (access(LOADER, R_OK) == 0) {
		setenv("DYLD_INSERT_LIBRARIES", LOADER, 1);
	}
	char bootargs[1024] = "";
	size_t blen = sizeof(bootargs) - 1;
	(void)sysctlbyname("kern.bootargs", bootargs, &blen, NULL, 0);
	const char *keep = strstr(bootargs, "kgpu_keep=");
	if (access(LOADER, R_OK) == 0 && keep != NULL && keep[10] != '0' && strstr(bootargs, " gl=0") == NULL &&
	    !gpu_there()) {
		int s = 0;

		fprintf(stderr, "topaz: the window server waits for the GPU (up to %d s)\n", WAIT_S);
		while (s < WAIT_S && !gpu_there()) {
			sleep(1);
			s++;
		}
		if (gpu_there()) {
			fprintf(stderr, "topaz: the GPU is there after %d s: the window server starts on it\n", s);
		} else {
			fprintf(stderr, "topaz: no GPU after %d s: the window server starts on the CPU, and moves once the GPU "
			    "comes\n", s);
			gpu_watch();
		}
	}
	/* only with boot-arg bbdenv=1: a RAM-booted test, the normal boot stays as it is */
	FILE *f = strstr(bootargs, "bbdenv=1") != NULL ? fopen(ENVFILE, "r") : NULL;
	if (f != NULL) {
		char line[256];
		while (fgets(line, sizeof(line), f) != NULL) {
			char *eq = strchr(line, '='), *nl = strchr(line, '\n');
			if (nl != NULL) {
				*nl = 0;
			}
			if (eq != NULL && line[0] != '#' && eq != line) {
				*eq = 0;
				setenv(line, eq + 1, 1);
			}
		}
		fclose(f);
	}
	argv[0] = (char *)REAL;
	execv(REAL, argv);
	return 127;
}
