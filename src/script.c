#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <incbin/incbin.h>
#include <Windows.h>

#include <global.h>
#include <patch.h>

INCBIN(OptionsMenu, "patches/optionsmenu.bps");
INCBIN(Levels, "patches/levels.bps");
INCBIN(LevelMenuAP, "patches/levelmenu_ap.bps");
INCBIN(MainMenuAP, "patches/mainmenu_ap.bps");
INCBIN(CasMenuAP, "patches/casmenu_ap.bps");
INCBIN(MemCardAP, "patches/memcard_ap.bps");
INCBIN(GameAP, "patches/game_ap.bps");
INCBIN(GameFlowAP, "patches/gameflow_ap.bps");
INCBIN(GoalScriptsAP, "patches/goal_scripts_ap.bps");
INCBIN(JudgesAP, "patches/judges_ap.bps");
INCBIN(NetMessagesAP, "patches/netmessages_ap.bps");
INCBIN(ShpAP, "patches/shp_ap.bps");
INCBIN(CjrScriptsAP, "patches/cjr_scripts_ap.bps");
INCBIN(AjcScriptsAP, "patches/ajc_scripts_ap.bps");
INCBIN(AlfScriptsAP, "patches/alf_scripts_ap.bps");
INCBIN(CpfScriptsAP, "patches/cpf_scripts_ap.bps");
INCBIN(LaObjectivesAP, "patches/la_objectives_ap.bps");
INCBIN(BdjScriptsAP, "patches/bdj_scripts_ap.bps");

struct scriptPatch {
	const char *name;
	unsigned int size;
	unsigned char *data;
};

static struct scriptPatch scriptPatches[18];
static size_t scriptPatchCount;
static volatile LONG apMainMenuScriptLoadCount;

static struct scriptPatch *findPatch(const char *name) {
	for (size_t index = 0; index < scriptPatchCount; ++index) {
		if (strcmp(scriptPatches[index].name, name) == 0) {
			return &scriptPatches[index];
		}
	}
	return NULL;
}

void __stdcall dumpScript(char *filename, int unk) {
	//printf("LOADING SCRIPT %s\n", filename);

	char filenameBuffer[1024];
	int isMainMenuScript = _stricmp(filename, "scripts\\mainmenu.qb") == 0;
	sprintf(filenameBuffer, "%sdata\\%s", executableDirectory, filename);

	FILE *f = fopen(filenameBuffer, "rb");

	if (f) {
		// get file length
		fseek(f, 0, SEEK_END);
		size_t filesize = ftell(f);
		fseek(f, 0, SEEK_SET);

		uint8_t *buffer = malloc(filesize);

		if (buffer) {
			fread(buffer, 1, filesize, f);

			void (__stdcall *unknown1)(char *, uint8_t *, int) = (void*)0x0042b3d0;

			struct scriptPatch *patch = findPatch(filename);
			if (patch) {
				printf("Applying patch for %s\n", filename);
				uint8_t *patchedBuffer = NULL;
				size_t patchedBufferLen = 0;

				int result = applyPatch(patch->data, patch->size, buffer, filesize, &patchedBuffer, &patchedBufferLen);
				
				if (!result) {
					unknown1(filename, patchedBuffer, unk);
				} else {
					unknown1(filename, buffer, unk);
					printf("Patch failed! Loading original script\n");
				}

				free(patchedBuffer);
			} else {
				unknown1(filename, buffer, unk);
			}
			if (isMainMenuScript) {
				InterlockedIncrement(&apMainMenuScriptLoadCount);
			}
			free(buffer);
		}

		fclose(f);
	} else {
		printf("FAILED TO OPEN SCRIPT %s: %s\n", filenameBuffer, strerror(errno));
	}
};

void registerPatch(char *name, unsigned int sz, char *data) {
	scriptPatches[scriptPatchCount++] = (struct scriptPatch){name, sz, data};
	printf("Registered patch for %s\n", name);
}

void initScriptPatches() {
	scriptPatchCount = 0;
	registerPatch("scripts\\optionsmenu.qb", gOptionsMenuSize, gOptionsMenuData);
	registerPatch("scripts\\levels.qb", gLevelsSize, gLevelsData);
	registerPatch("scripts\\levelmenu.qb", gLevelMenuAPSize, gLevelMenuAPData);
	registerPatch("scripts\\mainmenu.qb", gMainMenuAPSize, gMainMenuAPData);
	registerPatch("scripts\\casmenu.qb", gCasMenuAPSize, gCasMenuAPData);
	registerPatch("scripts\\memcard.qb", gMemCardAPSize, gMemCardAPData);
	registerPatch("scripts\\game.qb", gGameAPSize, gGameAPData);
	registerPatch("scripts\\gameflow.qb", gGameFlowAPSize, gGameFlowAPData);
	registerPatch("scripts\\goal_scripts.qb", gGoalScriptsAPSize, gGoalScriptsAPData);
	registerPatch("scripts\\judges.qb", gJudgesAPSize, gJudgesAPData);
	registerPatch("scripts\\netmessages.qb", gNetMessagesAPSize, gNetMessagesAPData);
	registerPatch("levels\\shp\\shp.qb", gShpAPSize, gShpAPData);
	registerPatch("scripts\\cjr_scripts.qb", gCjrScriptsAPSize, gCjrScriptsAPData);
	registerPatch("scripts\\ajc_scripts.qb", gAjcScriptsAPSize, gAjcScriptsAPData);
	registerPatch("scripts\\alf_scripts.qb", gAlfScriptsAPSize, gAlfScriptsAPData);
	registerPatch("scripts\\cpf_scripts.qb", gCpfScriptsAPSize, gCpfScriptsAPData);
	registerPatch("levels\\la\\la.qb", gLaObjectivesAPSize, gLaObjectivesAPData);
	registerPatch("scripts\\bdj_scripts.qb", gBdjScriptsAPSize, gBdjScriptsAPData);
}

uint32_t THPS3AP_MainMenuScriptLoadCount(void) {
	return (uint32_t)InterlockedCompareExchange(&apMainMenuScriptLoadCount, 0, 0);
}

void patchScriptHook() {
	// patch the function that loads scripts
	patchNop((void *)0x0042b354, 115);
	// push param1
	// 8b 5c 24 18	MOV dword ptr [ESP + 0x18],EBX
	patchByte(0x0042b354 + 0, 0x8b);
	patchByte(0x0042b354 + 1, 0x5c);
	patchByte(0x0042b354 + 2, 0x24);
	patchByte(0x0042b354 + 3, 0x18);
	// 53	PUSH EBX
	patchByte(0x0042b354 + 4, 0x53);

	// push param2
	// 8b 5c 24 18	MOV dword ptr [ESP + 0x18],EBX
	patchByte(0x0042b354 + 5, 0x8b);
	patchByte(0x0042b354 + 6, 0x5c);
	patchByte(0x0042b354 + 7, 0x24);
	patchByte(0x0042b354 + 8, 0x18);
	// 53	PUSH EBX
	patchByte(0x0042b354 + 9, 0x53);

	// call our file load
	patchCall(0x0042b354 + 10, dumpScript);

	// the stack should be clean at this point
}
