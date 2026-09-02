#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <patch.h>

int main(void) {
	uint8_t patch[] = {
		0x42, 0x50, 0x53, 0x31, 0x83, 0x83, 0x80, 0x89,
		0x61, 0x62, 0x64, 0xc2, 0x41, 0x24, 0x35, 0x61,
		0xd4, 0x40, 0xab, 0x8f, 0x67, 0x28, 0x1e,
	};
	uint8_t source[] = {'a', 'b', 'c'};
	uint8_t wrongSource[] = {'a', 'b', 'x'};
	uint8_t *output = NULL;
	size_t outputLen = 0;

	assert(applyPatch(patch, sizeof(patch), source, sizeof(source),
		&output, &outputLen) == 0);
	assert(outputLen == 3 && memcmp(output, "abd", 3) == 0);
	free(output);

	output = NULL;
	outputLen = 0;
	assert(applyPatch(patch, sizeof(patch), wrongSource, sizeof(wrongSource),
		&output, &outputLen) != 0);
	assert(output == NULL && outputLen == 0);
	return 0;
}
