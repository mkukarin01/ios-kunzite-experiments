/*
 * kshot [OUTDIR]: the window server's screen pictures, as PNG files, to look at from the Mac (the
 * phone's own screenshots go to Photos, whose assetsd aborts here). It looks IOSurfaces up by ID,
 * keeps those of the screen's size in 'BGRA' (backboardd draws its frames into two of them, in
 * turn), and writes each as OUTDIR/kshot-ID.png (default /tmp). The GPU draws into them behind
 * the CPU's cache, so a line or two may be a frame old.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurfaceRef.h>
#include <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_W 1125
#define SCREEN_H 2436
#define BGRA 0x42475241

static int
save(IOSurfaceRef surface, uint32_t id, const char *dir)
{
	char path[512];
	size_t row = IOSurfaceGetBytesPerRow(surface);
	CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
	int ok = 0;

	IOSurfaceLock(surface, kIOSurfaceLockReadOnly, NULL);
	CGDataProviderRef data = CGDataProviderCreateWithData(NULL, IOSurfaceGetBaseAddress(surface), row * SCREEN_H, NULL);
	CGImageRef image = CGImageCreate(SCREEN_W, SCREEN_H, 8, 32, row, space,
	    kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, data, NULL, false, kCGRenderingIntentDefault);
	snprintf(path, sizeof(path), "%s/kshot-%u.png", dir, id);
	CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
	CGImageDestinationRef out = url != NULL ? CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, NULL) : NULL;
	if (image != NULL && out != NULL) {
		CGImageDestinationAddImage(out, image, NULL);
		ok = CGImageDestinationFinalize(out);
	}
	IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, NULL);
	printf("kshot: surface %u -> %s: %s\n", id, path, ok ? "written" : "FAILED");
	if (out != NULL) {
		CFRelease(out);
	}
	if (url != NULL) {
		CFRelease(url);
	}
	if (image != NULL) {
		CGImageRelease(image);
	}
	CGDataProviderRelease(data);
	CGColorSpaceRelease(space);
	return ok;
}

int
main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp";
	unsigned found = 0;

	for (uint32_t id = 1; id < 4096 && found < 4; id++) {
		IOSurfaceRef surface = IOSurfaceLookup(id);

		if (surface == NULL) {
			continue;
		}
		if (IOSurfaceGetWidth(surface) == SCREEN_W && IOSurfaceGetHeight(surface) == SCREEN_H &&
		    IOSurfaceGetPixelFormat(surface) == BGRA) {
			found += save(surface, id, dir);
		}
		CFRelease(surface);
	}
	printf("kshot: %u screen pictures\n", found);
	return found == 0;
}
