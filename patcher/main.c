#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <conio.h>

#include <incbin/incbin.h>
#include <patch.h>

INCBIN(patch, "executable.bps");

int main(int argc, char **argv) {
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
			fread(buffer, 1, filesize, f);

			// check input crc (not using the one in the bps due to multiple valid executables)
			uint32_t inputcrc = crc32(buffer, filesize);
			if (inputcrc != 0xdda4822f && inputcrc != 0x045925e8 && inputcrc != 0xa1414bba) {
				printf("INPUT CRC DOES NOT MATCH EXPECTED: %08x\n", inputcrc);
				printf("Make sure THPS3 Patch 1.01 is installed\n");
				printf("Patch Failed!\n");
			}

			// patch
			uint8_t *patchedBuffer = NULL;
			size_t patchedLen = 0;
			int result = applyPatch(gpatchData, gpatchSize, buffer, filesize, &patchedBuffer, &patchedLen);
			if (result) {
				printf("Patching Failed!\n");

				goto end;
			}

			// check crc (again, not using the one in the bps due to multiple valid executables)
			uint32_t outputcrc = crc32(patchedBuffer, patchedLen);
			if (outputcrc != 0xbb5e5c48 && outputcrc != 0x69133ccb && outputcrc != 0xff4861b5) {
				printf("OUTPUT CRC DOES NOT MATCH EXPECTED: %08x\n", outputcrc);
				printf("Make sure THPS3 Patch 1.01 is installed\n");
				printf("Patch may not work!\n");
			}

			// write to THPS3.exe
			printf("Creating THPS3.exe\n");
			FILE *fout = fopen("THPS3.exe", "wb");
			if (fout) {
				fwrite(patchedBuffer, 1, patchedLen, fout);
				fclose(fout);
				printf("Patch Successful!\n");
			}
		} else {
			printf("Failed to allocate file buffer!\n");
		}
	} else {
		printf("FAILED TO OPEN EXECUTABLE %s: %s\n", "Skate3.exe", strerror(errno));
	}

end:
	printf("Press any key to continue\n");
	getch();

	return 0;
}
