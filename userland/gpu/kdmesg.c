/*
 * kdmesg [TEXT]: the kernel's message buffer (as dmesg(8) reads it: its size from sysctl
 * kern.msgbuf, its text from proc_kmsgbuf(); root),
 * or only its lines that contain TEXT. The phone has no dmesg; the USB live log starts only once
 * USB networking is up, so this is where the lines before that are (boot #54: the kept GPU run
 * was over before USB came up). Built by mk-gl-triangle.sh.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

extern int proc_kmsgbuf(void *buffer, uint32_t size);   /* libproc.h, not in the iOS SDK */

int
main(int argc, char **argv)
{
	int bufsize = 0;
	size_t len = sizeof(bufsize);
	char *buffer, *line;
	int size;

	if (sysctlbyname("kern.msgbuf", &bufsize, &len, NULL, 0) != 0 || bufsize <= 0) {
		perror("kern.msgbuf");
		return 1;
	}
	if ((buffer = calloc(1, (size_t)bufsize + 1)) == NULL || (size = proc_kmsgbuf(buffer, (uint32_t)bufsize)) <= 0) {
		perror("proc_kmsgbuf");
		return 1;
	}
	buffer[size] = 0;
	for (line = strtok(buffer, "\n"); line != NULL; line = strtok(NULL, "\n")) {
		if (line[0] != 0 && (argc < 2 || strstr(line, argv[1]) != NULL)) {
			puts(line);
		}
	}
	printf("kdmesg: %d bytes of a %d-byte message buffer\n", size, bufsize);
	return 0;
}
