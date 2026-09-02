#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <conio.h>

#include <incbin/incbin.h>
#include <patch.h>

INCBIN(patch, "executable.bps");

int main(int argc, char **argv) {
	int exitCode = 1;
	// open skate3.exe and dump contents
	FILE *f = fopen("Skate3.exe", "rb");

	if (f) {
		// get file length
		fseek(f, 0, SEEK_END);
		size_t filesize = ftell(f);
		fseek(f, 0, SEEK_SET);

		uint8_t *buffer = malloc(filesize);

		if (buffer) {
			printf("Patching Skate3.exe\n");
			if (fread(buffer, 1, filesize, f) != filesize) {
				printf("Failed to read Skate3.exe\n");
				free(buffer);
				fclose(f);
				goto end;
			}
			fclose(f);

			// patch
			uint8_t *patchedBuffer = NULL;
			size_t patchedLen = 0;
			int result = applyPatch(gpatchData, gpatchSize, buffer, filesize, &patchedBuffer, &patchedLen);
			if (result) {
				printf("Patching failed. Use the unmodified US English 1.01 Skate3.exe.\n");
				free(buffer);
				goto end;
			}

			// write to THPS3.exe
			printf("Creating THPS3.exe\n");
			FILE *fout = fopen("THPS3.exe", "wb");
			if (fout) {
				size_t written = fwrite(patchedBuffer, 1, patchedLen, fout);
				int closeResult = fclose(fout);
				if (written == patchedLen && closeResult == 0) {
					printf("Patch Successful!\n");
					exitCode = 0;
				} else {
					printf("Failed to write THPS3.exe\n");
					remove("THPS3.exe");
				}
			} else {
				printf("FAILED TO CREATE THPS3.exe: %s\n", strerror(errno));
			}
			free(patchedBuffer);
			free(buffer);
		} else {
			printf("Failed to allocate file buffer!\n");
			fclose(f);
		}
	} else {
		printf("FAILED TO OPEN EXECUTABLE %s: %s\n", "Skate3.exe", strerror(errno));
	}

end:
	printf("Press any key to continue\n");
	getch();

	return exitCode;
}
