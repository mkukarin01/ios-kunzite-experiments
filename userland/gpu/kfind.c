/*
 * kfind DIR [MINUTES]: the files under DIR (not following links) that are pictures (.png .jpg
 * .jpeg .heic .tif .tiff) or, with MINUTES, were changed in the last MINUTES minutes by the phone's
 * clock: one line each, size, age in minutes and path. The phone has no find(1); this is for
 * looking from the agent (boot #53: where the screenshots went while assetsd kept aborting).
 * Built by mk-gl-triangle.sh.
 */
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static time_t now;
static long minutes = -1;
static unsigned found;

static int
picture(const char *path)
{
	static const char *const endings[] = { ".png", ".jpg", ".jpeg", ".heic", ".tif", ".tiff" };
	size_t length = strlen(path);

	for (unsigned i = 0; i < sizeof(endings) / sizeof(endings[0]); i++) {
		size_t n = strlen(endings[i]);
		if (length > n && strcasecmp(path + length - n, endings[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

static int
visit(const char *path, const struct stat *st, int kind, struct FTW *ftw)
{
	(void)ftw;
	if (kind != FTW_F || !S_ISREG(st->st_mode)) {
		return 0;
	}
	long age = (long)(now - st->st_mtime) / 60;
	if (picture(path) || (minutes >= 0 && age <= minutes)) {
		printf("%10lld %6ld min  %s\n", (long long)st->st_size, age, path);
		found++;
	}
	return 0;
}

int
main(int argc, char **argv)
{
	if (argc < 2) {
		printf("usage: kfind DIR [MINUTES]\n");
		return 2;
	}
	if (argc > 2) {
		minutes = strtol(argv[2], NULL, 10);
	}
	now = time(NULL);
	nftw(argv[1], visit, 32, FTW_PHYS);
	printf("kfind: %u files\n", found);
	return 0;
}
