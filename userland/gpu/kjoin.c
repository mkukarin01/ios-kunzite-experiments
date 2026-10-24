/*
 * kjoin OUT PART...: the parts, in order, into OUT (mode 0644), then OUT's size and FNV-1a 64-bit
 * hash printed, to compare with the Mac's copy. For files the agent cannot take in one /put: it
 * holds a body in memory, and jetsam kills it near 5 MB (boot #52, a 15 MB libOSMesa: SIGKILL,
 * reason namespace 1 code 7). Built by mk-gl-triangle.sh.
 */
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	uint64_t hash = 0xcbf29ce484222325ull, size = 0;
	static unsigned char buf[1 << 16];
	int out;

	if (argc < 3) {
		printf("usage: kjoin OUT PART...\n");
		return 2;
	}
	out = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0) {
		perror(argv[1]);
		return 1;
	}
	for (int i = 2; i < argc; i++) {
		int in = open(argv[i], O_RDONLY);
		ssize_t n;

		if (in < 0) {
			perror(argv[i]);
			return 1;
		}
		while ((n = read(in, buf, sizeof(buf))) > 0) {
			for (ssize_t k = 0; k < n; k++) {
				hash = (hash ^ buf[k]) * 0x100000001b3ull;
			}
			if (write(out, buf, (size_t)n) != n) {
				perror(argv[1]);
				return 1;
			}
			size += (uint64_t)n;
		}
		close(in);
	}
	if (close(out) != 0) {
		perror(argv[1]);
		return 1;
	}
	printf("kjoin: %s %" PRIu64 " bytes fnv1a64 %016" PRIx64 "\n", argv[1], size, hash);
	return 0;
}
