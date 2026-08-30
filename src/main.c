#include <windows.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include <patch.h>
#include <global.h>
#include <input.h>
#include <config.h>
#include <script.h>
#include <thps3_ap/bridge_c_api.h>
void apEnsureDynamicGetGlobalFlagHook(void);
void apEnsureDynamicGetFlagHook(void);
void apEnsureDynamicSetGlobalFlagHook(void);
void apEnsureDynamicUpdateRecordsHook(void);
void apEnsureDynamicSaveOptionsHook(void);
void apEnsureDynamicGetGoalHook(void);
void apEnsureDynamicSetScoreGoalHook(void);
void apEnsureDynamicSetGoalHook(void);
void apEnsureDynamicJustGotGoalHook(void);
void apEnsureDynamicLaunchLocalMessageHook(void);
void apEnsureDynamicAwardStatPointHook(void);
void apEnsureDynamicJustGotFlagHook(void);
void apEnsureDynamicCareerStartLevelHook(void);
void apEnsureDynamicLoadNextUnlockedProHook(void);
void apEnsureObjectiveUnlockedCommand(void);
void apEnsureCustomSkaterAllowedCommand(void);
void apInstallAcceptedGapHook(void);
void apInstallGapTouchTraceHooks(void);
void apInstallLipEngageHook(void);
void apRefreshConnectionFooter(void);
uint32_t crc32(const void *buf, size_t size);
static void *apD3DDevice;
static volatile LONG apStagePassesThisFrame;
static volatile LONG apScriptConstructorLogCount;

#define VERSION_NUMBER_MAJOR 1
#define VERSION_NUMBER_MINOR 1
#define VERSION_NUMBER_PATCH 6

int __stdcall playIntroMovie(char *filename, int unk) {
	return 1;
}

void patchIntroMovie() {
	patchNop((void *)0x0040a1a0, 0x0040a33f - 0x0040a1a0 + 1);
	patchDWord(0x0040a1a0, 0x0824448b);	// MOV EAX, dword ptr [ESP + 0x08] (push second param back onto stack)
	patchByte(0x0040a1a0 + 4, 0x50);		// PUSH EAX
	patchDWord(0x0040a1a0 + 5, 0x0824448b);	// MOV EAX, dword ptr [ESP + 0x08] (push first param back onto stack)
	patchByte(0x0040a1a0 + 9, 0x50);		// PUSH EAX
	patchCall(0x0040a1a0 + 10, playIntroMovie);
	patchDWord(0x0040a1a0 + 15, 0x0824748b);		// MOV ESI, dword ptr [ESP + 0x08] (move second param into ESI, which future calls expect for some reason)
	patchByte(0x0040a1a0 + 19, 0xc3);	// RET
}

void patchNoMovie() {
	// nop out video playback for compatibility with systems that cannot handle directplay
	patchNop(0x004079a8, 0x004079f4 + 5 - 0x004079a8);
}

void ledgeWarpFix() {
	// clamp st(0) to [-1, 1]
	// replaces acos call, so we call that at the end
	__asm {
		ftst
		jl negative
		fld1
		fcom
		fstp st(0)
		jle end
		fstp st(0)
		fld1
		jmp end
	negative:
		fchs
		fld1
		fcom
		fstp st(0)
		fchs
		jle end
		fstp st(0)
		fld1
		fchs
	end:
		
	}

	callFunc(0x00577cd7);
}

void patchLedgeWarp() {
	patchCall(0x0049f1dc, ledgeWarpFix);
}

// fixed rendering function for the opaque side of the rendering pipeline
// respects a material flag 0x40 that appears to denote objects that require single sided rendering, while others draw un-culled
void __cdecl fixedDrawWorldAgain(int16_t param_1, int param_2) {
	int32_t *lastVertexBuffer = 0x00906760;
	int32_t *rwObj = 0x00970a8c;
	uint32_t *currentTexture = 0x0090675c;
	int *numMeshes = 0x005c94c8;
	int *meshList = 0x00901758;
	void (__cdecl *RwGetRenderState)(int, int *) = (void *)0x0055ce60;
	void (__cdecl *RwSetRenderState)(int, int) = (void *)0x0055ce10;
	void (__cdecl *SetCurrentTexture)(int **) = (void *)0x004f4320;
	void (__cdecl *SomeLightingStuff)(int, int) = (void *)0x0052fae0;
	void (__cdecl *DrawWorldMesh)(int32_t, int *) = (void *)0x004f4210;

	int32_t uVar1;
	int texture;
	int i;
	int *mesh;	// unknown struct
  
	uVar1 = *rwObj;
	int unkRenderstate;
	RwGetRenderState(8, &unkRenderstate);
	SomeLightingStuff(0,0);
	*lastVertexBuffer = 0xffffffff;
	*currentTexture = -1;
	if (param_2) {
		RwSetRenderState(0x14,1);
		i = 0;
		if (0 < *numMeshes) {
			mesh = meshList;
			texture = *currentTexture;
			while (i < *numMeshes) {
				if ((mesh[2] != 0) && ((*(uint16_t *)(*mesh + 0x1a) & param_1) == param_2)) {
					if (texture != *mesh) {
						SetCurrentTexture(mesh);
						if ((*(uint8_t *)(*mesh + 0x1a) & 0x40)) {	// if surface is marked as double sided
							//RwSetRenderState(0x14,2);	// draw single sided
						} else {
							//RwSetRenderState(0x14,1);	// draw double sided
						}
						if ((*(uint8_t *)(*mesh + 0x1a) & 0x10)) {
							if (unkRenderstate && param_2) {
								RwSetRenderState(0x8,1);
								param_2 = 0;
							}
						} else {
							RwSetRenderState(0x8,0);
							param_2 = 1;
						}
					}
					DrawWorldMesh(*rwObj, mesh[3]);
					texture = mesh[0];
					*currentTexture = texture;
				}
				i = i + 1;
				mesh = mesh + 5;
			}
		}
		RwSetRenderState(0x14,2);
		RwSetRenderState(0x8,1);
		
	} else {
		RwSetRenderState(0x14,2);
		i = 0;
		if (0 < *numMeshes) {
			mesh = meshList;
			texture = *currentTexture;
			while (i < *numMeshes) {
				if ((mesh[2] != 0) && ((*(uint16_t *)(*mesh + 0x1a) & param_1) == 0)) {
					int materialSingleSided = 0;
					if (texture != *mesh) {
						SetCurrentTexture(mesh);
						if ((*(uint8_t *)(*mesh + 0x1a) & 0x40)) {	// if surface is marked as single sided
							RwSetRenderState(0x14,2);	// draw single sided
							materialSingleSided = 1;
						} else {
							RwSetRenderState(0x14,1);	// draw double sided
						}
					}
					DrawWorldMesh(*rwObj, mesh[3]);
					texture = mesh[0];
					*currentTexture = texture;
				}
				i = i + 1;
				mesh = mesh + 5;
			}
		}
		RwSetRenderState(0x14,2);
		RwSetRenderState(0x8,1);
		if (param_1 == 8 && THPS3AP_HasActiveStageOverlay()) {
			InterlockedIncrement(&apStagePassesThisFrame);
			THPS3AP_CaptureStageRenderState(apD3DDevice);
		}
	}
	return;
}

struct rw3DVertex {
	float position[3];
	float normal[3];
	uint32_t color;
	float u;
	float v;
};

void transparentizeShadow(struct rw3DVertex *verts, uint32_t numVerts, void *matrix, uint32_t flags) {
	void (__cdecl *RwIm3DTransform)(struct rw3DVertex *, uint32_t, void *, uint32_t) = (void *)0x00561010;
	void (__cdecl *RwGetRenderState)(int, int *) = (void *)0x0055ce60;
	void (__cdecl *RwSetRenderState)(int, int) = (void *)0x0055ce10;

	//printf("DRAWING SHADOW START\n");

	for (int i = 0; i < numVerts; i++) {
		//printf("Vertex: POS: %f %f %f NORM: %f %f %f COLOR: 0x%08x UV: %f %f\n", verts[i].vertex[0], verts[i].vertex[1], verts[i].vertex[2], verts[i].normal[0], verts[i].normal[1], verts[i].normal[2], verts[i].color, verts[i].u, verts[i].v);
		verts[i].color = 0x60ffffff;
	}

	//printf("DRAWING SHADOW END\n");
	//printf("Drawing shadow!\n");

	// get original blend states
	int src, dst;

	RwGetRenderState(0x0a, &src);	// 0x0a - src function
	RwGetRenderState(0x0b, &dst);	// 0x0b - dst function

	// set new states
	RwSetRenderState(0x0a, 0x03);	// src color
	RwSetRenderState(0x0b, 0x09);	// dst color
	
	RwIm3DTransform(verts, numVerts, matrix, flags);

	RwSetRenderState(0x0a, src);
	RwSetRenderState(0x0b, dst);
}

void patchCullModeFix() {
	// sets the culling mode to always be NONE rather than BACK
	// fixes missing geometry in a few levels, fixes missing faces in shadows
	
	// transparent stuff
	//patchByte(0x004f4883+1, 0x01);
	// not sure
	//patchByte(0x004f4933+1, 0x01);
	// world?
	//patchByte(0x004f4953+1, 0x01);
	// not sure
	//patchByte(0x004f5c5d+1, 0x03);
	// UI text?
	//patchByte(0x004f5cae+1, 0x01);
	// UI Button Background
	//patchByte(0x004f60d8+1, 0x03);
	// Main UI Text?
	//patchByte(0x004f60f9+1, 0x01);
	// characters?
	//patchByte(0x004f9ca3+1, 0x03);
	// shadow - this doesn't seem like a graceful fix... but it seems to work
	patchByte(0x004f9d95+1, 0x01);

	patchCall(0x004f9ba2, fixedDrawWorldAgain);
	//patchCall(0x004f9bff, fixedDrawWorldAgain);
	patchCall(0x0042db88, fixedDrawWorldAgain);

	patchCall(0x005020a5, transparentizeShadow);
}

uint8_t compLevels[] = {
	0,
	0,
	0,
	1,
	0,
	0,
	1,
	0,
	1,
	0,
};

#define STATS_AND_BOARDS_MASK 0x01f80000

int oldLevel = 0;
int oldProgress = 0;
int oldPickups = 0;

void retryHook() {
	int (__cdecl *IsCareerMode)(void) = (void *)0x00421540;
	if (IsCareerMode()) {
		// reset career progress
		uint32_t *career = 0x008e1e90;
		career = (*career) + 0x134;
		career = (*career) + 0x14;

		uint32_t *level = (*career) + 0x690;
		
		if (!compLevels[*level]) {
			uint32_t *goals = (*career + 0x564 + ((*level - 1) * 8));
			uint32_t *someflags = (*career + 0x5e4 + ((*level - 1) * 8));

			*someflags = 0;	// keeps the goal messages from disappearing sometimes.  don't know what that's about.

			if (*level != oldLevel) {
				oldLevel = *level;
				oldProgress = *goals;
				oldPickups = *(someflags + 1);
			}

			if (*goals & oldProgress != *goals) {
				oldProgress = *goals | oldProgress;
			}

			uint32_t pickups = *(someflags + 1) & STATS_AND_BOARDS_MASK;
			if (pickups & oldPickups != pickups) {
				oldPickups = pickups | oldPickups;
			}

			*goals = 0;
			*(someflags + 1) ^= pickups;
		}
	}

	callFunc(0x00422400);	// call retry
}

void loadRequestedLevelHook() {
	int (__cdecl *IsCareerMode)(void) = (void *)0x00421540;

	if (oldLevel != 0 && !compLevels[oldLevel]) {
		// restore career progress

		uint32_t *career = 0x008e1e90;
		career = (*career) + 0x134;
		career = (*career) + 0x14;

		uint32_t *goals = (*career + 0x564 + ((oldLevel - 1) * 8));

		*goals = *goals | oldProgress;

		uint32_t *pickups = (*career + 0x5e4 + ((oldLevel - 1) * 8)) + 1;

		*pickups = *pickups | oldPickups;
	}

	callFunc(0x004220c0);
}

void endRunHook() {
	int (__cdecl *IsCareerMode)(void) = (void *)0x00421540;

	if (IsCareerMode() && !compLevels[oldLevel]) {
		// restore career progress

		uint32_t *career = 0x008e1e90;
		career = (*career) + 0x134;
		career = (*career) + 0x14;

		uint32_t *goals = (*career + 0x564 + ((oldLevel - 1) * 8));

		*goals = *goals | oldProgress;

		uint32_t *pickups = (*career + 0x5e4 + ((oldLevel - 1) * 8)) + 1;

		*pickups = *pickups | oldPickups;
	}

	callFunc(0x004216f0);	// call end run
}

void patchILMode() {
	patchDWord(0x005b801c, retryHook);
	//patchDWord(0x005b7f8c, endRunHook);
	patchDWord(0x005b7ffc, loadRequestedLevelHook);
}

void patchTrickLimit() {
	patchByte(0x004355ad, 0xeb);
}

void patchUnlimitedCareerTime() {
	// Keep the explicit EndRunSelected flag and bypass only timer expiry.
	patchByte(0x0042207c, 0x74); // je cleanup_false
	patchByte(0x0042207d, 0x14);
	patchByte(0x0042207e, 0xeb); // jmp return_true
	patchByte(0x0042207f, 0x10);
}

void our_random(int out_of) {
	// first, call the original random so that we consume a value.  
	// juuust in case someone wants actual 100% identical behavior between partymod and the original game
	void (__cdecl *their_random)(int) = (void *)0x0040e4c0;

	their_random(out_of);

	return rand() % out_of;
}

void patchRandomMusic() {
	patchCall(0x004c6b37, our_random);
}

char domainStr[256];
char peerchatStr[266];
char masterServerStr[264];

void patchOnlineService(char *configFile) {
	GetPrivateProfileString("Miscellaneous", "OnlineDomain", "openspy.net", domainStr, 256, configFile);

	sprintf(peerchatStr, "peerchat.%s", domainStr);
	sprintf(masterServerStr, "master.%s", domainStr);

	patchDWord(0x0050b278 + 1, peerchatStr);
	patchDWord(0x00517845 + 1, masterServerStr);
	patchDWord(0x0051785d + 1, masterServerStr);
	patchDWord(0x0051959a + 1, masterServerStr);

	printf("Patched online server: %s\n", domainStr);
}

void safeWait(uint64_t endTime) {
	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	uint64_t safetyThreshold = (timerFreq / 1000) * 3;	// 3ms

	uint64_t currentTime = SDL_GetPerformanceCounter();

	while (currentTime < endTime) {
		currentTime = SDL_GetPerformanceCounter();

		//printf("%f\n", timerAccumulator);

		if (endTime - currentTime > safetyThreshold) {
			//uint64_t bsleep = SDL_GetPerformanceCounter();
			SDL_Delay(1);
			//uint64_t asleep = SDL_GetPerformanceCounter();
			//printf("BIG yawn! %f\n", (asleep - bsleep) / ((double)timerFreq / 1000.0));
		}
	}
}

uint64_t nextFrame = 0;
uint64_t frameStart = 0;
uint64_t safeTime = 0;

void do_frame_cap() {
	apEnsureDynamicGetGlobalFlagHook();
	apEnsureDynamicGetFlagHook();
	apEnsureDynamicSetGlobalFlagHook();
	apEnsureDynamicUpdateRecordsHook();
	apEnsureDynamicSaveOptionsHook();
	apEnsureDynamicGetGoalHook();
	apEnsureDynamicSetScoreGoalHook();
	apEnsureDynamicSetGoalHook();
	apEnsureDynamicJustGotGoalHook();
	apEnsureDynamicLaunchLocalMessageHook();
	apEnsureDynamicAwardStatPointHook();
	apEnsureDynamicJustGotFlagHook();
	apEnsureDynamicCareerStartLevelHook();
	apEnsureDynamicLoadNextUnlockedProHook();
	apEnsureObjectiveUnlockedCommand();
	apEnsureCustomSkaterAllowedCommand();
	THPS3AP_PumpMainThread();
	apRefreshConnectionFooter();
	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	uint64_t frameTarget = timerFreq / 60;

	if (!nextFrame || nextFrame < SDL_GetPerformanceCounter()) {
		nextFrame = SDL_GetPerformanceCounter() + frameTarget;
	} else {
		safeWait(nextFrame);
		nextFrame += frameTarget;
	}
}

void *origPresent = NULL;

static int moviePresentSourceRect(RECT *source) {
	int videoWidth;
	int videoHeight;
	int videoX;
	int videoY;
	int frameWidth;
	int frameHeight;
	int sourceWidth;
	int sourceHeight;

	if (*(void **)0x005d085c == NULL) {
		return 0;
	}
	videoWidth = *(int *)0x005d0848;
	videoHeight = *(int *)0x005d084c;
	videoX = *(int *)0x005d0840;
	videoY = *(int *)0x005d0844;
	frameWidth = videoWidth + videoX * 2;
	frameHeight = videoHeight + videoY * 2;
	if (videoWidth <= 0 || videoHeight <= 0 ||
		frameWidth <= 0 || frameHeight <= 0) {
		return 0;
	}

	if ((int64_t)frameWidth * videoHeight >=
		(int64_t)frameHeight * videoWidth) {
		sourceWidth = (int)(((int64_t)videoHeight * frameWidth +
			frameHeight / 2) / frameHeight);
		sourceHeight = videoHeight;
	} else {
		sourceWidth = videoWidth;
		sourceHeight = (int)(((int64_t)videoWidth * frameHeight +
			frameWidth / 2) / frameWidth);
	}
	source->left = videoX - (sourceWidth - videoWidth) / 2;
	source->top = videoY - (sourceHeight - videoHeight) / 2;
	source->right = source->left + sourceWidth;
	source->bottom = source->top + sourceHeight;
	return 1;
}

long __stdcall presentWrapper(
	void *device,
	const RECT *source,
	const RECT *destination,
	HWND window,
	const RGNDATA *dirtyRegion) {
	long (__stdcall *present)(void *, const RECT *, const RECT *, HWND, const RGNDATA *) =
		(void *)origPresent;
	if (InterlockedCompareExchange(&apStagePassesThisFrame, 0, 0) > 0) {
		THPS3AP_DrawCapturedStageOverlays(device);
	}
	THPS3AP_DrawHudOverlay(device);
	RECT movieSource;
	if (moviePresentSourceRect(&movieSource)) {
		source = &movieSource;
		destination = NULL;
	}
	long result = present(device, source, destination, window, dirtyRegion);
	InterlockedExchange(&apStagePassesThisFrame, 0);
	return result;
}

void *origCreateDevice = NULL;

int __fastcall createDeviceWrapper(void *id3d8, void *pad, void *id3d8again, uint32_t adapter, uint32_t type, void *hwnd, uint32_t behaviorFlags, uint32_t *presentParams, void *devOut) {
	int (__fastcall *createDevice)(void *, void *, void *, uint32_t, uint32_t, void *, uint32_t, uint32_t *, void *) = (void *)origCreateDevice;

	presentParams[3] = 1;
	presentParams[5] = 3; // D3DSWAPEFFECT_COPY permits scaled source rectangles.

	if (presentParams[7]) {	// if windowed
		presentParams[11] = 0;	// refresh rate
		presentParams[12] = 0;	// swap interval
	}
	else {
		presentParams[11] = 0;
		presentParams[12] = 1;
	}

	int result = createDevice(id3d8, pad, id3d8again, adapter, type, hwnd, behaviorFlags, presentParams, devOut);

	if (result == 0 && devOut != NULL) {
		apD3DDevice = *(void **)devOut;
		uint8_t *device = *(uint8_t **)apD3DDevice;
		origPresent = *(void **)(device + 0x3c);
		patchDWord(device + 0x3c, presentWrapper);
	}

	return result;
}

void * __stdcall createD3D8Wrapper(uint32_t version) {
	void *(__stdcall *created3d8)(uint32_t) = (void *)0x00569d20;

	void *result = created3d8(version);

	if (result) {
		uint8_t *iface = *(uint32_t *)result;

		origCreateDevice = *(uint32_t *)(iface + 0x3c);
		patchDWord(iface + 0x3c, createDeviceWrapper);
	}
	
	return result;
}

void patchFramerateCap() {
	patchByte(0x004c0507, 0xEB);	// skip original framerate cap logic
	patchNop(0x004c04ef, 24);
	patchCall(0x004c04ef, do_frame_cap);

	patchCall(0x0054e433, createD3D8Wrapper);

	// remove sleep from main loop
	patchNop((void *)0x004c05eb, 2);
	patchNop((void *)0x004c067a, 8);
	//patchByte((void *)(0x004c067a + 1), 0x01);
}

void __fastcall doSoundCleanup(void *skatemod) {
	void (__fastcall *origCleanup)(void *) = (void *)0x004397a0;
	void (__cdecl *cleanupSound)(int) = (void *)0x00408e40;

	origCleanup(skatemod);
	for (int i = 0; i < 48; i++) {
		cleanupSound(i);
	}
}

#define PRESERVE_MUSIC_STOP 0x01
static volatile LONG preserveMusicActions;

static int __cdecl preserveMusicForRetry(void *params, void *script) {
	InterlockedExchange(&preserveMusicActions, PRESERVE_MUSIC_STOP);
	return ((int (__cdecl *)(void *, void *))0x00422400)(params, script);
}

static int __cdecl preserveMusicForEndRun(void *params, void *script) {
	InterlockedExchange(&preserveMusicActions, PRESERVE_MUSIC_STOP);
	return ((int (__cdecl *)(void *, void *))0x004216f0)(params, script);
}

static void __cdecl stopMusicHook(void) {
	if (InterlockedAnd(&preserveMusicActions, ~PRESERVE_MUSIC_STOP) &
		PRESERVE_MUSIC_STOP) {
		return;
	}
	*(uint32_t *)0x005c6974 = 1;
	((void (__cdecl *)(int))0x00407c30)(1);
}

void patchSoundFix() {
	// Keep SFX cleanup without resetting the active soundtrack on transitions.
	patchCall(0x00439b1c, doSoundCleanup);
	patchDWord((void *)0x005b801c, (uint32_t)preserveMusicForRetry);
	patchDWord((void *)0x005b7f8c, (uint32_t)preserveMusicForEndRun);
	patchNop((void *)0x004c5c90, 10);
	patchByte((void *)0x004c5c90, 0xe9);
	patchDWord((void *)0x004c5c91,
		(uint32_t)stopMusicHook - 0x004c5c90 - 5);
}

uint32_t rng_seed = 0;
int ILMode;
int disableTrickLimit;

// MSVC's C frontend cannot declare __thiscall function pointers. A
// __fastcall pointer with a dummy EDX argument produces the same x86 call:
// params in ECX, followed by the three real arguments on the stack.
typedef unsigned char (__fastcall *GetChecksumParamFn)(
	void *params,
	void *unused_edx,
	const char *name,
	uint32_t *value,
	int required
);

static volatile LONG apGetFlagOriginal = 0;
static volatile LONG apSetGlobalFlagOriginal = 0x00420fa0;
static volatile LONG apUpdateRecordsOriginal = 0;
static volatile LONG apSaveOptionsOriginal = 0;
static int apCurrentCareerLevel(void);
static volatile LONG apSetGoalOriginal = 0;
static volatile LONG apGetGoalOriginal = 0x00420ab0;
static volatile LONG apLoadNextUnlockedProOriginal = 0;

static void apLogDynamicHookChange(
	const char *name, void *header, uint32_t oldCallback, void *newCallback) {
	THPS3AP_DebugLog(
		"dynamic-hook name=%s header=%p old=%08x new=%p",
		name, header, oldCallback, newCallback);
}

int __cdecl apLoadNextUnlockedProHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apLoadNextUnlockedProOriginal, 0, 0);
	if (THPS3AP_IsSelectedSkaterActive()) {
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicLoadNextUnlockedProHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header =
		(uint8_t *)resolveQbKey(0x9c7410be); // LoadNextUnlockedPro
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apLoadNextUnlockedProHook) {
			apLogDynamicHookChange("LoadNextUnlockedPro", header, callback, apLoadNextUnlockedProHook);
			if (callback != 0) {
				InterlockedExchange(
					&apLoadNextUnlockedProOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apLoadNextUnlockedProHook);
		}
	}
}
static volatile LONG apSetScoreGoalOriginal = 0x00421630;
static volatile LONG apJustGotGoalOriginal = 0x00420b50;
static volatile LONG apLaunchLocalMessageOriginal = 0;
static volatile LONG apLastSetFlag = 0;
static volatile LONG apAwardStatPointOriginal = 0;
static volatile LONG apJustGotFlagOriginal = 0x00420dd0;
static volatile LONG apCareerStartLevelOriginal = 0x004205e0;
static uint32_t apPendingGapChecksums[64];
static uint32_t apPendingGapSkaterStates[64];
static uint32_t apPendingGapTriggerTypes[64];
static int apPendingGapCount = 0;
static int apPendingGapLevel = 0;

static uint32_t apCurrentScriptChecksum(void *script);

static int apLevelForUnlockFlag(uint32_t flag) {
	switch (flag) {
		// GetChecksumParam resolves these QB constants to the numeric career
		// flag indices defined by THPS3, rather than returning their checksums.
		case 10: return 1; // LEVEL_UNLOCKED_FOUNDRY (AP mainmenu patch)
		case 11: return 2; // LEVEL_UNLOCKED_CANADA
		case 12: return 3; // LEVEL_UNLOCKED_RIO
		case 13: return 4; // LEVEL_UNLOCKED_SUBURBIA
		case 14: return 5; // LEVEL_UNLOCKED_AIRPORT
		case 15: return 6; // LEVEL_UNLOCKED_SKATERISLAND
		case 16: return 7; // LEVEL_UNLOCKED_LOSANGELES
		case 17: return 8; // LEVEL_UNLOCKED_TOKYO
		case 18: return 9; // LEVEL_UNLOCKED_SHIP
		default: return 0;
	}
}

int __cdecl apGetGlobalFlagHook(void *params, void *script) {
	if (THPS3AP_HasStateSnapshot()) {
		GetChecksumParamFn getChecksumParam =
			(GetChecksumParamFn)0x00429920;
		uint32_t flag = 0;

		if (getChecksumParam(params, NULL, "flag", &flag, 1)) {
			if (flag == 1 && apCurrentScriptChecksum(script) == 0xff46456eu &&
				((int (__cdecl *)(void))0x00421540)()) {
				// AP owns career persistence; skip GameFlow_End's automatic
				// save confirmation without changing unrelated global flags.
				return 0;
			}
			int level = apLevelForUnlockFlag(flag);
			if (level != 0) {
				uint32_t scriptChecksum = apCurrentScriptChecksum(script);
				int rewardBypass =
					// Skip vanilla's post-run level reward scripts completely.
					// The cassette/menu paths still receive the AP decision.
					scriptChecksum == 0x868b1c6au || // EndRun_CheckForLevelsOpen
					scriptChecksum == 0x31097598u;   // Goal_CheckProVideoUnlock
				int result = rewardBypass || THPS3AP_IsLevelUnlocked(level);
				return result;
			}

			// The vanilla career menu hides Cruise Ship entirely until this
			// separate discovery flag is set. AP's ship item owns both states.
			if (flag == 159) { // SPECIAL_HAS_SEEN_SHIP
				// Keep the Ship cassette present; LEVEL_UNLOCKED_SHIP is
				// the independent AP-controlled access .decision.
				int result = 1;
				return result;
			}
		}
	}

	return ((int (__cdecl *)(void *, void *))0x00421110)(params, script);
}

static int __cdecl apResolveCassetteUnlocked(void *cassette) {
	uint8_t *element = (uint8_t *)cassette;
	int vanillaUnlocked = element[0x1d4] != 0;
	if (!THPS3AP_HasStateSnapshot()) {
		return vanillaUnlocked;
	}

	int level = *(int *)(element + 0x1d0);
	if (level < 1 || level > 9) {
		return vanillaUnlocked;
	}
	THPS3AP_RecordHighlightedCassetteLevel(level);

	return THPS3AP_IsLevelUnlocked(level) != FALSE;
}

// Replaces `mov cl, byte ptr [esi+0x1d4]` at the two native cassette refresh
// paths. ESI is the highlighted cassette and EAX is the shared goals panel.
// Preserve the surrounding routine's volatile registers while returning the
// AP decision in CL, exactly as the displaced instruction did.
__declspec(naked) void apCassetteUnlockStateHook(void) {
	__asm {
		push eax
		push edx
		push ecx
		push esi
		call apResolveCassetteUnlocked
		add esp, 4
		test eax, eax
		setne al
		mov byte ptr [esi + 0x1d4], al
		mov dl, al
		pop ecx
		mov cl, dl
		pop edx
		pop eax
		ret
	}
}

void apEnsureDynamicGetGlobalFlagHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x8e55af45); // GetGlobalFlag
	if (header != NULL && *(uint32_t *)(header + 4) == 8 &&
		*(uint32_t *)(header + 12) != (uint32_t)apGetGlobalFlagHook) {
		apLogDynamicHookChange(
			"GetGlobalFlag", header, *(uint32_t *)(header + 12), apGetGlobalFlagHook);
		patchDWord(header + 12, (uint32_t)apGetGlobalFlagHook);
	}
}

static int apReadDirectIntParam(
	void *params, uint32_t key, uint32_t *value) {
	typedef struct ApStructHeader {
		uint32_t type;
		uint32_t key;
		uint32_t value;
		struct ApStructHeader *next;
	} ApStructHeader;
	ApStructHeader *header = params != NULL
		? *(ApStructHeader **)params
		: NULL;

	while (header != NULL) {
		if (header->type == 1 && header->key == key) {
			*value = header->value;
			return 1;
		}
		header = header->next;
	}
	return 0;
}

int __cdecl apSetGlobalFlagHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apSetGlobalFlagOriginal, 0, 0);
	uint32_t scriptChecksum = apCurrentScriptChecksum(script);
	uint32_t flag = 0;

	if (!apReadDirectIntParam(params, 0x2e0b1465u, &flag)) {
		((GetChecksumParamFn)0x00429920)(
			params, NULL, "flag", &flag, 1);
	}
	if (flag == 0x7ffffffeu) { // APGapList
		if (THPS3AP_HasStateSnapshot()) {
			THPS3AP_QueueGapListRequest(apCurrentCareerLevel());
		}
		return 1;
	}
	if (flag == 0x7ffffffdu) { // AP collectible-marker toggle
		THPS3AP_ToggleCollectibleMarkers();
		return 1;
	}
	if (flag > 0x7ffe0000u && flag <= 0x7ffe006au) {
		THPS3AP_SelectGapMenuRow(flag - 0x7ffe0001u);
		return 1;
	}
	// Hidden decks are AP locations. Preserve the pickup message and its
	// career collectible bit, but do not arm THPS3's disk-save prompt or its
	// vanilla skateshop reward notification from Got_Deck_Icon.
	if (THPS3AP_HasStateSnapshot() && scriptChecksum == 0x605313fdu) {
		return 1;
	}

	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicSetGlobalFlagHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header =
		(uint8_t *)resolveQbKey(0xfaeb6309); // SetGlobalFlag
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apSetGlobalFlagHook) {
			apLogDynamicHookChange("SetGlobalFlag", header, callback, apSetGlobalFlagHook);
			if (callback != 0) {
				InterlockedExchange(&apSetGlobalFlagOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apSetGlobalFlagHook);
		}
	}
}

int __cdecl apUpdateRecordsHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apUpdateRecordsOriginal, 0, 0);
	if (THPS3AP_HasStateSnapshot() &&
		((int (__cdecl *)(void))0x00421540)()) {
		// Avoid the career high-score initials flow; records are not AP state.
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicUpdateRecordsHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x9987c6d4); // UpdateRecords
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apUpdateRecordsHook) {
			apLogDynamicHookChange("UpdateRecords", header, callback, apUpdateRecordsHook);
			if (callback != 0) {
				InterlockedExchange(&apUpdateRecordsOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apUpdateRecordsHook);
		}
	}
}

int __cdecl apSaveOptionsHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apSaveOptionsOriginal, 0, 0);
	if (THPS3AP_HasStateSnapshot()) {
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicSaveOptionsHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header =
		(uint8_t *)resolveQbKey(0x2c53b12f); // SaveOptionsAndPros
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apSaveOptionsHook) {
			apLogDynamicHookChange("SaveOptionsAndPros", header, callback, apSaveOptionsHook);
			if (callback != 0) {
				InterlockedExchange(&apSaveOptionsOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apSaveOptionsHook);
		}
	}
}

static int apCurrentCareerLevel(void) {
	typedef void *(__cdecl *AcquireProfileManagerFn)(int);
	typedef void (__cdecl *ReleaseProfileManagerFn)(void);
	AcquireProfileManagerFn acquireProfileManager =
		(AcquireProfileManagerFn)0x004367e0;
	ReleaseProfileManagerFn releaseProfileManager =
		(ReleaseProfileManagerFn)0x00436830;
	uint8_t *manager = (uint8_t *)acquireProfileManager(0);
	uint8_t *selector = NULL;
	uint8_t *profile = NULL;
	int level = 0;

	if (manager != NULL) {
		selector = *(uint8_t **)(manager + 0x134);
	}
	if (selector != NULL) {
		uint32_t count = *(uint32_t *)(selector + 0x00);
		uint32_t current = *(uint32_t *)(selector + 0x1c);
		if (current < count) {
			profile = *(uint8_t **)(selector + 0x14 + current * sizeof(void *));
		}
	}
	if (profile != NULL) {
		level = *(int *)(profile + 0x690);
	}
	releaseProfileManager();
	return level;
}

static const char *apStatPointScriptLevelPrefix(int level) {
	switch (level) {
		case 1: return "foun";
		case 2: return "can";
		case 3: return "rio";
		case 4: return "sub";
		case 5: return "ap";
		case 6: return "si";
		case 7: return "la";
		case 8: return "tok";
		case 9: return "shp";
		default: return NULL;
	}
}

static int apStatPointIdFromScript(void *script, int level) {
	const char *prefix = apStatPointScriptLevelPrefix(level);
	uint32_t scriptChecksum;
	char scriptName[64];
	char layout;
	int pointId;

	if (script == NULL || prefix == NULL) {
		return 0;
	}

	// THPS3 1.01 CScript stores the currently executing script checksum at
	// +0x390. Collectible scripts are named, for example,
	// FounTRG_Stat_Point_A01Script. A-D select the skater-dependent physical
	// layout while the 01-05 suffix is the stable level-scoped logical ID.
	scriptChecksum = *(uint32_t *)((uint8_t *)script + 0x390);
	for (layout = 'a'; layout <= 'd'; ++layout) {
		for (pointId = 1; pointId <= 5; ++pointId) {
			sprintf(
				scriptName,
				"%strg_stat_point_%c%02dscript",
				prefix,
				layout,
				pointId);
			if ((crc32(scriptName, strlen(scriptName)) ^ 0xffffffffu) ==
				scriptChecksum) {
				return pointId;
			}
		}
	}

	return 0;
}

static int apStatPointIdFromNode(void *script) {
	uint8_t *node;
	uint32_t nodeFlags;
	int pointId;

	if (script == NULL) {
		return 0;
	}

	// AwardStatPoint runs inside Got_STAT_POINT. Its CScript object points at
	// the collected Node at +0x38c, and Node::flags lives at +0x0c. The
	// Goal_STAT_POINT setup script assigns exactly one of STAT_POINT1..5,
	// whose numeric flag values are 0..4 in the stock goal scripts.
	node = *(uint8_t **)((uint8_t *)script + 0x38c);
	if (node == NULL) {
		return 0;
	}
	nodeFlags = *(uint32_t *)(node + 0x0c);
	for (pointId = 1; pointId <= 5; ++pointId) {
		if ((nodeFlags & (1u << (pointId - 1))) != 0) {
			return pointId;
		}
	}

	return 0;
}

void __fastcall apCareerSetFlagHook(
	void *flagBank,
	void *unusedEdx,
	uint32_t flag,
	int level
) {
	typedef void (__fastcall *SetCareerFlagFn)(
		void *flagBank,
		void *unusedEdx,
		uint32_t flag,
		int level
	);
	SetCareerFlagFn setCareerFlag = (SetCareerFlagFn)0x004bb260;

	// Observe the resolved career flag at QScript SetFlag's career-bank setter.
	// ECX is the flag bank and the stack carries flag plus level (-1 here).
	setCareerFlag(flagBank, unusedEdx, flag, level);
	InterlockedExchange(&apLastSetFlag, (LONG)flag);
	if (THPS3AP_HasStateSnapshot() && THPS3AP_HiddenDecksAreLocations() && flag == 56) { // GOAL_DECK
		THPS3AP_RecordDeckCollection(apCurrentCareerLevel());
	}

	// AwardStatPoint immediately follows this setter for collectible stats.
	// Keep the resolved numeric flag here and correlate it in that command.
}

static uint32_t apCareerGoalBits(int level) {
	typedef void *(__cdecl *AcquireProfileManagerFn)(int);
	typedef void (__cdecl *ReleaseProfileManagerFn)(void);
	AcquireProfileManagerFn acquireProfileManager =
		(AcquireProfileManagerFn)0x004367e0;
	ReleaseProfileManagerFn releaseProfileManager =
		(ReleaseProfileManagerFn)0x00436830;
	uint8_t *manager = (uint8_t *)acquireProfileManager(0);
	uint8_t *selector = NULL;
	uint8_t *profile = NULL;
	uint32_t goals = 0;

	if (manager != NULL) {
		selector = *(uint8_t **)(manager + 0x134);
	}
	if (selector != NULL) {
		uint32_t count = *(uint32_t *)(selector + 0x00);
		uint32_t current = *(uint32_t *)(selector + 0x1c);
		if (current < count) {
			profile = *(uint8_t **)(selector + 0x14 + current * sizeof(void *));
		}
	}
	if (profile != NULL && level >= 1 && level <= 9) {
		goals = *(uint32_t *)(profile + 0x564 + (level - 1) * 8);
	}
	releaseProfileManager();
	return goals;
}

static uint32_t apCurrentScriptChecksum(void *script) {
	return script != NULL ? *(uint32_t *)((uint8_t *)script + 0x390) : 0;
}

static int apLockedGoalQuerySkipsProgress(uint32_t scriptChecksum) {
	// Stock QB scripts whose true GetGoal branch means "already handled".
	// Keep status/aggregate scripts out: they must see locked goals incomplete.
	switch (scriptChecksum) {
	case 0x58128349u: // APTRGP_Create_TerroristsScript
	case 0x2bbc1e01u: // unnamed Airport objective helper
	case 0x7f9e07aau: // APTRGP_Create_TerroristsStartScript
	case 0x15fe7186u: // APTRGP_KillFamilyScript
	case 0xb004db09u: // ALF_SUB_GameStartCam
	case 0xb487c3f6u: // ALF_SUB_Trickspot_Check_Vert
	case 0x1fc0adb7u: // ALF_SUB_Trickspot_Check_Street
	case 0x757c57c4u: // ALF_SUB_Axe_Gap
	case 0xdf336efdu: // BDJ_SHP_GotVertTrick
	case 0x2a9d08acu: // BDJ_SHP_GotStreetTrick2
	case 0x7ac9f973u: // Got_TrickSpot
	case 0x1fb3dd38u: // Got_Scripted1
	case 0x86ba8c82u: // Got_Scripted2
	case 0xf1bdbc14u: // Got_Scripted3
	case 0x781840a5u: // CreatePhotoGuy
	case 0xe290b46fu: // LAGO_PershBall_01Script
	case 0xd378aef2u: // LAGO_PershBall_02Script
	case 0xa444e8bcu: // CJR_Foun_Valve_Goal_Counter
	case 0x04812eccu: // FounObj_Valve_Goal_CounterScript
		return 1;
	default:
		return 0;
	}
}

static int apReadGoalParam(void *params, uint32_t *goal) {
	GetChecksumParamFn getChecksumParam = (GetChecksumParamFn)0x00429920;
	return getChecksumParam(params, NULL, "goal", goal, 1) != 0;
}

int __cdecl apObjectiveUnlockedCommand(void *params, void *script) {
	uint32_t goal = 0;
	int level;
	(void)script;

	// Outside an active AP session, preserve the stock game's behavior.
	if (!THPS3AP_HasStateSnapshot()) {
		return 1;
	}
	level = apCurrentCareerLevel();
	return apReadGoalParam(params, &goal) && goal < 9 &&
		THPS3AP_IsObjectiveUnlocked(level, (int)goal);
}

void apEnsureObjectiveUnlockedCommand(void) {
	typedef uint8_t *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	typedef uint8_t *(__cdecl *AddQbKeyHeaderFn)(uint32_t checksum, int headerId);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	AddQbKeyHeaderFn addQbKeyHeader = (AddQbKeyHeaderFn)0x0042b900;
	uint8_t *header = (uint8_t *)resolveQbKey(0x405ef9de); // APObjectiveUnlocked

	if (header == NULL) {
		header = addQbKeyHeader(0x405ef9de, 1);
	}
	if (header != NULL &&
		(*(uint32_t *)(header + 4) != 8 ||
		 *(uint32_t *)(header + 12) != (uint32_t)apObjectiveUnlockedCommand)) {
		apLogDynamicHookChange(
			"APObjectiveUnlocked", header,
			*(uint32_t *)(header + 12), apObjectiveUnlockedCommand);
		// This is the header allocated for our native command, not a stock
		// scripted-function header. It owns no QB bytecode body.
		*(uint32_t *)(header + 4) = 8; // COMPILED_FUNCTION
		*(uint32_t *)(header + 12) = (uint32_t)apObjectiveUnlockedCommand;
	}
}

int __cdecl apCustomSkaterAllowedCommand(void *params, void *script) {
	int allowed = THPS3AP_IsCustomSkaterSelected() != FALSE;
	(void)params;
	(void)script;
	THPS3AP_DebugLog("cas-entry allowed=%d", allowed);
	return allowed;
}

void apEnsureCustomSkaterAllowedCommand(void) {
	typedef uint8_t *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	typedef uint8_t *(__cdecl *AddQbKeyHeaderFn)(uint32_t checksum, int headerId);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	AddQbKeyHeaderFn addQbKeyHeader = (AddQbKeyHeaderFn)0x0042b900;
	uint8_t *header = (uint8_t *)resolveQbKey(0xdf7627ebu); // APCustomSkaterAllowed

	if (header == NULL) {
		header = addQbKeyHeader(0xdf7627ebu, 1);
	}
	if (header != NULL &&
		(*(uint32_t *)(header + 4) != 8 ||
		 *(uint32_t *)(header + 12) != (uint32_t)apCustomSkaterAllowedCommand)) {
		apLogDynamicHookChange(
			"APCustomSkaterAllowed", header,
			*(uint32_t *)(header + 12), apCustomSkaterAllowedCommand);
		*(uint32_t *)(header + 4) = 8; // COMPILED_FUNCTION
		*(uint32_t *)(header + 12) = (uint32_t)apCustomSkaterAllowedCommand;
	}
}

int __cdecl apGetFlagHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apGetFlagOriginal, 0, 0);
	GetChecksumParamFn getChecksumParam = (GetChecksumParamFn)0x00429920;
	uint32_t flag = 0;

	// Foundry's normal level setup uses this transient flag to choose the open
	// generator-door state. Mirror the checked valve objective into only that
	// exact query; do not replay the valve completion or reward scripts.
	if (THPS3AP_HasStateSnapshot() && apCurrentCareerLevel() == 1 &&
		apCurrentScriptChecksum(script) == 0xa8475bebu &&
		getChecksumParam(params, NULL, "flag", &flag, 1) && flag == 15 &&
		THPS3AP_IsObjectiveChecked(1, 7)) {
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicGetFlagHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x8859be8a); // GetFlag
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apGetFlagHook) {
			apLogDynamicHookChange("GetFlag", header, callback, apGetFlagHook);
			if (callback != 0) {
				InterlockedExchange(&apGetFlagOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apGetFlagHook);
		}
	}
}

int __cdecl apGetGoalHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apGetGoalOriginal, 0, 0);
	uint32_t goal = 0;
	int level = apCurrentCareerLevel();
	uint32_t scriptChecksum = apCurrentScriptChecksum(script);

	if (THPS3AP_HasStateSnapshot() &&
		THPS3AP_UseIntactLosAngelesGeometry() &&
		scriptChecksum == 0x2878d09fu && // CPF_LA_LoadingScript
		apReadGoalParam(params, &goal) && goal == 6) { // GOAL_SCRIPTED1
		// Preserve LA's CreatedAtStart geometry without changing completion.
		return 0;
	}
	if (THPS3AP_HasStateSnapshot() && apReadGoalParam(params, &goal) &&
		goal < 9 && !THPS3AP_IsObjectiveUnlocked(level, (int)goal)) {
		// GameStart owns the goal text, camera movie, and the level-specific
		// presentation scripts. None of those scripts is reusable world setup,
		// so suppress the whole branch while locked. The CreatedAtStart world
		// controllers remain alive and their destructive entries use the
		// APObjectiveUnlocked guards patched into the level scripts.
		if (scriptChecksum == 0xd6b6b647u) { // GameStart
			return 1;
		}
		// These setup scripts query their goal independently after GameStart.
		// Treating only the matching locked collectible as complete preserves
		// the vanilla checked-goal behavior while preventing its objects from
		// being created in the level.
		if ((goal == 3 && scriptChecksum == 0xa2018666u) ||
			(goal == 5 && scriptChecksum == 0xb51ebabdu)) {
			return 1;
		}
		if (apLockedGoalQuerySkipsProgress(scriptChecksum)) {
			return 1;
		}
		return 0;
	}

	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicGetGoalHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0xa571be3e); // GetGoal
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apGetGoalHook) {
			apLogDynamicHookChange("GetGoal", header, callback, apGetGoalHook);
			if (callback != 0) {
				InterlockedExchange(&apGetGoalOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apGetGoalHook);
		}
	}
}

int __cdecl apSetScoreGoalHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apSetScoreGoalOriginal, 0, 0);
	uint32_t goal = 0;
	int level = apCurrentCareerLevel();

	// SetScoreGoal registers native thresholds before GameStart. Do not let a
	// locked threshold exist: its GoalScript launches completion feedback as
	// soon as the score is reached, before the career mirror can clear the bit.
	if (THPS3AP_HasStateSnapshot() && apReadGoalParam(params, &goal) &&
		goal < 3 && !THPS3AP_IsObjectiveUnlocked(level, (int)goal)) {
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicSetScoreGoalHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header =
		(uint8_t *)resolveQbKey(0x29cb718d); // SetScoreGoal
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apSetScoreGoalHook) {
			apLogDynamicHookChange("SetScoreGoal", header, callback, apSetScoreGoalHook);
			if (callback != 0) {
				InterlockedExchange(&apSetScoreGoalOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apSetScoreGoalHook);
		}
	}
}

int __cdecl apSetGoalHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apSetGoalOriginal, 0, 0);
	int level = apCurrentCareerLevel();
	uint32_t before = apCareerGoalBits(level);
	int result = original != NULL ? original(params, script) : 0;
	uint32_t completed = apCareerGoalBits(level) & ~before;
	int goal;
	int blocked = 0;

	// Do not read SetGoal's QScript parameters here. The original callback must
	// consume them exactly once; duplicate reads previously broke SetFlag-based
	// collectible setup. Observe only the native career-word transition.
	if (result && THPS3AP_HasStateSnapshot()) {
		for (goal = 2; goal >= 0; --goal) {
			if ((completed & (1u << goal)) != 0) {
				THPS3AP_RecordMedalResult(level, goal);
				break;
			}
		}
		for (goal = 0; goal < 9; ++goal) {
			if ((completed & (1u << goal)) != 0) {
				THPS3AP_RecordGoalCompletion(level, goal);
				if (!THPS3AP_IsObjectiveUnlocked(level, goal)) {
					blocked = 1;
				}
			}
		}
		if (blocked) {
			// Remove the vanilla bit before the calling script can query
			// JustGotGoal or award completion/progression side effects.
			THPS3AP_ReconcileCareerBits();
		}
	}
	return result;
}

void apEnsureDynamicSetGoalHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x36e006e3); // SetGoal
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apSetGoalHook) {
			apLogDynamicHookChange("SetGoal", header, callback, apSetGoalHook);
			if (callback != 0) {
				InterlockedExchange(&apSetGoalOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apSetGoalHook);
		}
	}
}

int __cdecl apJustGotGoalHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apJustGotGoalOriginal, 0, 0);
	uint32_t goal = 0;
	int level = apCurrentCareerLevel();

	if (THPS3AP_HasStateSnapshot() && apReadGoalParam(params, &goal) &&
		goal < 9 && !THPS3AP_IsObjectiveUnlocked(level, (int)goal)) {
		return 0;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicJustGotGoalHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0xb12e13d0); // JustGotGoal
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apJustGotGoalHook) {
			apLogDynamicHookChange("JustGotGoal", header, callback, apJustGotGoalHook);
			if (callback != 0) {
				InterlockedExchange(&apJustGotGoalOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apJustGotGoalHook);
		}
	}
}

typedef struct ApQbStructHeader {
	uint32_t type;
	uint32_t key;
	union {
		uint32_t raw;
		void *pointer;
	} value;
	struct ApQbStructHeader *next;
} ApQbStructHeader;

static ApQbStructHeader *apFindParam(void *params, uint32_t key) {
	ApQbStructHeader *header = params != NULL
		? *(ApQbStructHeader **)params : NULL;
	while (header != NULL && header->key != key) {
		header = header->next;
	}
	return header;
}

static int apGoalIdForListMessage(uint32_t id) {
	switch (id) {
		case 0x0a205557u: return 0; // mess01: High Score
		case 0x932904edu: return 1; // mess02: Pro Score
		case 0xe42e347bu: return 2; // mess03: Sick Score
		case 0x7a4aa1d8u: return 3; // mess04: S-K-A-T-E
		case 0x0d4d914eu: return 5; // mess05: Secret Tape
		case 0x9444c0f4u: return 4; // mess06: Trick Spot
		case 0xe343f062u: return 8; // mess07: Scripted Goal 3
		case 0x73fcedf3u: return 6; // mess08: Scripted Goal 1
		case 0x04fbdd65u: return 7; // mess09: Scripted Goal 2
		default: return -1;
	}
}

int __cdecl apLaunchLocalMessageHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apLaunchLocalMessageOriginal, 0, 0);
	ApQbStructHeader *id;
	int goal;
	int position;

	if (!THPS3AP_HasStateSnapshot() ||
		apCurrentScriptChecksum(script) != 0x0a82beebu) { // LaunchLocalMessage
		return original != NULL ? original(params, script) : 0;
	}

	id = apFindParam(params, 0x40c698afu); // id
	goal = id != NULL ? apGoalIdForListMessage(id->value.raw) : -1;
	position = THPS3AP_ObjectiveListPosition(apCurrentCareerLevel(), goal);
	if (position < 0) {
		return goal >= 0 ? 1 : (original != NULL ? original(params, script) : 0);
	}

	{
		ApQbStructHeader *basePos = apFindParam(params, 0x15406719u); // base_pos
		if (basePos != NULL && basePos->type == 5 && basePos->value.pointer != NULL) {
			((float *)basePos->value.pointer)[1] = (float)(position * 35);
		}
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicLaunchLocalMessageHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(
		0x33ef5062u); // LaunchLocalPanelMessage (LaunchLocalMessage backend)
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apLaunchLocalMessageHook) {
			apLogDynamicHookChange("LaunchLocalPanelMessage", header, callback, apLaunchLocalMessageHook);
			if (callback != 0) {
				InterlockedExchange(
					&apLaunchLocalMessageOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apLaunchLocalMessageHook);
		}
	}
}

int __cdecl apAwardStatPointHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apAwardStatPointOriginal, 0, 0);
	int level = apCurrentCareerLevel();
	int scriptPointId = apStatPointIdFromScript(script, level);
	int nodePointId = apStatPointIdFromNode(script);
	int careerFlag = (int)InterlockedCompareExchange(&apLastSetFlag, 0, 0);
	int pointId = 0;
	if (careerFlag >= 51 && careerFlag <= 55) {
		// GOAL_STAT_POINT1..5 are career flags 55..51 respectively.
		pointId = 56 - careerFlag;
	}
	if (pointId == 0) {
		pointId = nodePointId;
	}
	if (pointId == 0) {
		pointId = scriptPointId;
	}
    if (pointId >= 1 && pointId <= 5 && THPS3AP_HasStateSnapshot() &&
        THPS3AP_StatPointsAreLocations()) {
		if (level >= 1 && level <= 9) {
			THPS3AP_QueueStatPointEvent(level, pointId);
		}
	}
    if (THPS3AP_HasStateSnapshot() && THPS3AP_StatPointsAreLocations()) {
		// AP owns the stat item total. Preserve pickup identification/event
		// delivery, but suppress vanilla's physical AwardStatPoint mutation.
		return 1;
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicAwardStatPointHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x18659121); // AwardStatPoint
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apAwardStatPointHook) {
			apLogDynamicHookChange("AwardStatPoint", header, callback, apAwardStatPointHook);
			if (callback != 0) {
				InterlockedExchange(&apAwardStatPointOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apAwardStatPointHook);
		}
	}
}

int __cdecl apJustGotFlagHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apJustGotFlagOriginal, 0, 0);
    if (THPS3AP_HasStateSnapshot() && THPS3AP_StatPointsAreLocations()) {
		GetChecksumParamFn getChecksumParam = (GetChecksumParamFn)0x00429920;
		uint32_t flag = 0;
		if (getChecksumParam(params, NULL, "flag", &flag, 1) &&
			flag >= 51 && flag <= 55) {
			// Physical stat pickups must not trigger the end-run stats menu;
			// received AP stat items are applied directly to the profile.
			return 0;
		}
	}
	return original != NULL ? original(params, script) : 0;
}

void apEnsureDynamicJustGotFlagHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0x9c061364); // JustGotFlag
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apJustGotFlagHook) {
			apLogDynamicHookChange("JustGotFlag", header, callback, apJustGotFlagHook);
			if (callback != 0) {
				InterlockedExchange(&apJustGotFlagOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apJustGotFlagHook);
		}
	}
}

static void apDiscardPendingGaps(void) {
	apPendingGapCount = 0;
	apPendingGapLevel = 0;
}

static void apCommitPendingGaps(void) {
	int i;
	if (!THPS3AP_HasStateSnapshot()) {
		apDiscardPendingGaps();
		return;
	}
	for (i = 0; i < apPendingGapCount; ++i) {
		THPS3AP_QueueGapEvent(
			apPendingGapLevel,
			apPendingGapChecksums[i],
			apPendingGapSkaterStates[i],
			apPendingGapTriggerTypes[i]);
	}
	apDiscardPendingGaps();
}

static void __fastcall apScoreLandedHook(void *score, void *unused) {
	typedef void (__fastcall *ScoreFn)(void *, void *);
	((ScoreFn)0x00435eb0)(score, NULL);
	apCommitPendingGaps();
	if (THPS3AP_HasStateSnapshot()) {
		THPS3AP_QueueGapListRequest(apCurrentCareerLevel());
	}
}

static void __fastcall apScoreBailedHook(void *score, void *unused) {
	typedef void (__fastcall *ScoreFn)(void *, void *);
	((ScoreFn)0x00436140)(score, NULL);
	apDiscardPendingGaps();
}

static void __cdecl apQueueScoringGap(const char *name, void *skater) {
	char normalized[128];
	size_t length = 0;
	uint32_t checksum;
	int i;
	int level;

	if (name == NULL) {
		return;
	}
	while (name[length] != '\0' && length < sizeof(normalized) - 1) {
		char c = name[length];
		normalized[length] = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
		++length;
	}
	if (length == 0) {
		return;
	}
	normalized[length] = '\0';
	checksum = crc32(normalized, length) ^ 0xffffffffu;

	if (THPS3AP_HasStateSnapshot()) {
		level = apCurrentCareerLevel();
		if (apPendingGapCount != 0 && apPendingGapLevel != level) {
			apDiscardPendingGaps();
		}
		for (i = 0; i < apPendingGapCount; ++i) {
			if (apPendingGapChecksums[i] == checksum) {
				return;
			}
		}
		if (apPendingGapCount < sizeof(apPendingGapChecksums) / sizeof(apPendingGapChecksums[0])) {
			apPendingGapLevel = level;
			apPendingGapChecksums[apPendingGapCount] = checksum;
			apPendingGapSkaterStates[apPendingGapCount] = skater != NULL
				? *(uint32_t *)((uint8_t *)skater + 0x8498)
				: 0xffffffffu;
			apPendingGapTriggerTypes[apPendingGapCount] = skater != NULL
				? *(uint32_t *)((uint8_t *)skater + 0x84c0)
				: 0xffffffffu;
			++apPendingGapCount;
		}
	}
}

static int apIsAtriumGapScript(uint32_t checksum) {
	switch (checksum) {
		case 0x52126583u: // BDJ_SHP_GapAtriumBox
		case 0x78c6ceeeu: // BDJ_SHP_GapAtrium808
		case 0xafe017d5u: // BDJ_SHP_StartGap808
		case 0x1c862fcdu: // BDJ_SHP_EndGap808
		case 0x46134e4eu: // BDJ_SHP_EndGapBoxTo808
		case 0x6cc7e523u: // BDJ_SHP_EndGapBoxToBox
			return 1;
		default:
			return 0;
	}
}

static const char apStartGapTraceText[] = "START";
static const char apEndGapTraceText[] = "END";

static uint32_t apAtriumNodeChecksum(void *node) {
	typedef void *(__cdecl *GetArrayFn)(uint32_t checksum, int required);
	typedef uint32_t (__cdecl *GetNodeIndexFn)(uint32_t checksum);
	typedef void *(__fastcall *GetArrayElementFn)(
		void *array, void *unusedEdx, uint32_t index);
	static const uint32_t checksums[] = {
		0xb4d61f4bu, 0x98968891u, 0xe8fc7c1eu, 0x7843618fu,
		0xf68a8946u, 0x68ee1ce5u, 0x165f6058u, 0xe81bc433u,
		0x987130bcu, 0x01786106u, 0x6809a4c8u, 0xf66d316bu,
		0x7d2a4759u, 0x93242675u,
	};
	void *array;
	size_t i;

	if (node == NULL) {
		return 0;
	}
	array = ((GetArrayFn)0x00426590)(0xc472ecc5u, 1); // NodeArray
	if (array == NULL) {
		return 0;
	}
	for (i = 0; i < sizeof(checksums) / sizeof(checksums[0]); ++i) {
		uint32_t index = ((GetNodeIndexFn)0x0042b2b0)(checksums[i]);
		if (((GetArrayElementFn)0x00427360)(array, NULL, index) == node) {
			return checksums[i];
		}
	}
	return 0;
}

static uint8_t *apFindActiveGap(void *skater, uint32_t gapId) {
	uint8_t *gap;
	if (skater == NULL) {
		return NULL;
	}
	gap = *(uint8_t **)((uint8_t *)skater + 0x885c);
	while (gap != NULL && *(int32_t *)(gap + 4) != -1) {
		if (*(uint32_t *)(gap + 0x14) == gapId) {
			return gap;
		}
		gap = *(uint8_t **)(gap + 0x0c);
	}
	return NULL;
}

static void __cdecl apTraceAtriumGapCommand(
	const char *command, uint32_t gapId, void *script, void *skater) {
	char path[MAX_PATH];
	FILE *log;
	uint32_t scriptChecksum = apCurrentScriptChecksum(script);
	uint32_t nodeName = 0;
	uint8_t *activeGap;
	uint32_t flags = 0;
	void *node;

	if (apCurrentCareerLevel() != 9 ||
		(!apIsAtriumGapScript(scriptChecksum) &&
		 gapId != 0x2c76c4c2u)) { // CommLip
		return;
	}
	node = script != NULL ? *(void **)((uint8_t *)script + 0x38c) : NULL;
	nodeName = apAtriumNodeChecksum(node);
	activeGap = apFindActiveGap(skater, gapId);
	if (activeGap != NULL) {
		flags = *(uint32_t *)(activeGap + 0x18);
	}
	if (GetTempPathA(sizeof(path), path) == 0 ||
		strcat_s(path, sizeof(path), "thps3-ap-gap-touch.log") != 0) {
		return;
	}
	log = fopen(path, "a");
	if (log == NULL) {
		return;
	}
	fprintf(
		log, "%lu pid=%lu %s gap=%08x script=%08x active=%u flags=%08x state=%08x trigger=%08x node=%08x ptr=%p\n",
		(unsigned long)GetTickCount(), (unsigned long)GetCurrentProcessId(),
		command, gapId, scriptChecksum, activeGap != NULL, flags,
		skater != NULL ? *(uint32_t *)((uint8_t *)skater + 0x8498) : 0,
		skater != NULL ? *(uint32_t *)((uint8_t *)skater + 0x84c0) : 0,
		nodeName, node);
	fclose(log);
}

static int __cdecl apSkipBrokenBoxTo808Cancellation(
	uint32_t gapId, void *script) {
	return apCurrentCareerLevel() == 9 && gapId == 0xa115eef5u &&
		apCurrentScriptChecksum(script) == 0x6cc7e523u;
}

__declspec(naked) void apStartGapTraceHook(void) {
	__asm {
		pushfd
		pushad
		mov eax, dword ptr [esp + 0x1f0]
		mov ecx, dword ptr [esp + 0x40]
		push esi
		push eax
		push ecx
		push offset apStartGapTraceText
		call apTraceAtriumGapCommand
		add esp, 16
		popad
		popfd
		mov eax, dword ptr [esi + 0x885c]
		push 0x004afd0f
		ret
	}
}

__declspec(naked) void apEndGapTraceHook(void) {
	__asm {
		pushfd
		pushad
		mov eax, dword ptr [esp + 0x1f0]
		mov ecx, dword ptr [esp + 0x38]
		push esi
		push eax
		push ecx
		push offset apEndGapTraceText
		call apTraceAtriumGapCommand
		add esp, 16
		mov eax, dword ptr [esp + 0x1f0]
		mov ecx, dword ptr [esp + 0x38]
		push eax
		push ecx
		call apSkipBrokenBoxTo808Cancellation
		add esp, 8
		test eax, eax
		jnz skipBrokenCancellation
		popad
		popfd
		mov al, byte ptr [esi + 0x83b0]
		push 0x004b1ae1
		ret
	skipBrokenCancellation:
		popad
		popfd
		push 0x004b1fb9
		ret
	}
}

void apInstallGapTouchTraceHooks(void) {
	uint8_t *startSite = (uint8_t *)0x004afd09;
	uint8_t *endSite = (uint8_t *)0x004b1adb;
	patchNop(startSite, 6);
	patchByte(startSite, 0xe9);
	patchDWord(startSite + 1, (uint32_t)apStartGapTraceHook - (uint32_t)startSite - 5);
	patchNop(endSite, 6);
	patchByte(endSite, 0xe9);
	patchDWord(endSite + 1, (uint32_t)apEndGapTraceHook - (uint32_t)endSite - 5);
}

// EndGap is handled inside the skater's native command dispatcher rather than
// through a resolvable QB callback. This site is reached after the active gap
// and cancellation checks and after the scoring gap has been awarded. Its
// displayed name is at [esp+0x1c], so intermediate trigger IDs are excluded.
__declspec(naked) void apAcceptedGapHook(void) {
	__asm {
		pushfd
		pushad
		mov eax, dword ptr [esp + 0x40]
		mov ecx, dword ptr [esp + 0x04]
		push ecx
		push eax
		call apQueueScoringGap
		add esp, 8
		popad
		popfd
		push 0
		push 0
		push -1
		push 0x004b1bbe
		ret
	}
}

void apInstallAcceptedGapHook(void) {
	uint8_t *site = (uint8_t *)0x004b1bb8;
	patchNop(site, 6);
	patchByte(site, 0xe9);
	patchDWord(site + 1, (uint32_t)apAcceptedGapHook - (uint32_t)site - 5);
}

static int __cdecl apLipTricksAllowed(void) {
	return THPS3AP_IsTrickCategoryUnlocked(THPS3AP_TRICK_LIP);
}

// Both successful native lip-detection branches converge here, immediately
// before THPS3 clears balance state and changes the skater to LIP. When lip
// tricks are locked, resume at the engine's normal rail-detection fallback.
__declspec(naked) void apLipEngageHook(void) {
	__asm {
		pushfd
		pushad
		call apLipTricksAllowed
		test eax, eax
		jnz allowLip
		popad
		popfd
		push 0x004a655e
		ret
	allowLip:
		popad
		popfd
		mov edi, 0x3f800000
		push 0x004a6e8d
		ret
	}
}

void apInstallLipEngageHook(void) {
	uint8_t *site = (uint8_t *)0x004a6e88;
	patchByte(site, 0xe9);
	patchDWord(site + 1, (uint32_t)apLipEngageHook - (uint32_t)site - 5);
}

static void *__fastcall apScriptConstructorHook(
	void *object,
	void *unusedEdx,
	uint32_t checksum,
	void *params,
	void *node) {
	typedef void *(__fastcall *ScriptConstructorFn)(
		void *, void *, uint32_t, void *, void *);
	void *result = ((ScriptConstructorFn)0x00427440)(
		object, unusedEdx, checksum, params, node);
	LONG event = InterlockedIncrement(&apScriptConstructorLogCount);
	if (event <= 20000) {
		THPS3AP_DebugLog(
			"script-ctor event=%ld checksum=%08x object=%p result=%p "
			"params=%p node=%p",
			event, checksum, object, result, params, node);
	} else if (event == 20001) {
		THPS3AP_DebugLog("script-ctor log-cap=20000");
	}
	return result;
}

static int apQbBatchLoad(const char *site, void *arg1, void *arg2) {
	typedef int (__cdecl *QbBatchLoadFn)(void *, void *);
	int result;
	THPS3AP_DebugLog(
		"qb-batch-enter site=%s arg1=%p arg2=%p", site, arg1, arg2);
	result = ((QbBatchLoadFn)0x00419870)(arg1, arg2);
	THPS3AP_DebugLog(
		"qb-batch-exit site=%s result=%d", site, result);
	return result;
}

static int __cdecl apQbBatchLoad419d07(void *arg1, void *arg2) {
	return apQbBatchLoad("419d07", arg1, arg2);
}

static int __cdecl apQbBatchLoad4397f2(void *arg1, void *arg2) {
	return apQbBatchLoad("4397f2", arg1, arg2);
}

static int __cdecl apQbBatchLoad46a6cd(void *arg1, void *arg2) {
	return apQbBatchLoad("46a6cd", arg1, arg2);
}

int __cdecl apCareerStartLevelHook(void *params, void *script) {
	typedef int (__cdecl *ScriptCommandFn)(void *params, void *script);
	ScriptCommandFn original = (ScriptCommandFn)InterlockedCompareExchange(
		&apCareerStartLevelOriginal, 0, 0);
	uint32_t writes = 0;
	int result;

	THPS3AP_DebugLog(
		"career-start-enter script=%p checksum=%08x current=%d",
		script, apCurrentScriptChecksum(script), apCurrentCareerLevel());
	apDiscardPendingGaps();
	// CareerStartLevel copies persisted per-level words into active career
	// state before node loading and spawn. Reconcile AP-owned objective and stat
	// bits at that boundary; do not run the full per-frame pump here.
	if (THPS3AP_HasStateSnapshot()) {
		writes = THPS3AP_ReconcileCareerBits();
		THPS3AP_ApplyCareerTimerBonus();
	}
	result = original != NULL ? original(params, script) : 0;
	THPS3AP_DebugLog(
		"career-start-original-return result=%d current=%d",
		result, apCurrentCareerLevel());
	THPS3AP_EndLevelTransition(apCurrentCareerLevel());
	if (THPS3AP_HasStateSnapshot()) {
		THPS3AP_QueueGapListRequest(apCurrentCareerLevel());
	}
	THPS3AP_DebugLog("career-start-exit result=%d", result);
	return result;
}

void apEnsureDynamicCareerStartLevelHook(void) {
	typedef void *(__cdecl *ResolveQbKeyFn)(uint32_t checksum);
	ResolveQbKeyFn resolveQbKey = (ResolveQbKeyFn)0x00426340;
	uint8_t *header = (uint8_t *)resolveQbKey(0xed9644e7); // CareerStartLevel
	if (header != NULL && *(uint32_t *)(header + 4) == 8) {
		uint32_t callback = *(uint32_t *)(header + 12);
		if (callback != (uint32_t)apCareerStartLevelHook) {
			apLogDynamicHookChange("CareerStartLevel", header, callback, apCareerStartLevelHook);
			if (callback != 0) {
				InterlockedExchange(&apCareerStartLevelOriginal, (LONG)callback);
			}
			patchDWord(header + 12, (uint32_t)apCareerStartLevelHook);
		}
	}
}

int __cdecl apLoadRequestedLevelHook() {
	uint32_t game = *(uint32_t *)0x008e1e90;
	int apLevel = 0;
	uint32_t requestedLevel = 0;
	if (game != 0) {
		requestedLevel = *(uint32_t *)(game + 0x48c);
		switch (requestedLevel) {
			case 0x0b099cdf: apLevel = 1; break; // Load_Foun
			case 0xc06ed828: apLevel = 2; break; // Load_Can
			case 0x6254abf1: apLevel = 3; break; // Load_Rio
			case 0xfb50e026: apLevel = 4; break; // Load_Sub
			case 0x8c32c761: apLevel = 5; break; // Load_Ap
			case 0x90ad1f72: apLevel = 6; break; // Load_Si
			case 0x532c99de: apLevel = 7; break; // Load_La
			case 0x37eeb4dc: apLevel = 8; break; // Load_Tok
			case 0xf785fd72: apLevel = 9; break; // Load_Shp
			default: break;
		}
	}

	THPS3AP_DebugLog(
		"load-request-enter game=%08x requested=%08x ap_level=%d",
		game, requestedLevel, apLevel);
	if (apLevel != 0) {
		THPS3AP_BeginLevelTransition();
	}
	{
		int result = ((int (__cdecl *)(void))0x004220c0)();
		THPS3AP_DebugLog(
			"load-request-native-return requested=%08x ap_level=%d result=%d",
			requestedLevel, apLevel, result);
		return result;
	}
}

void patchArchipelagoHooks() {
	// Rewrite AP panel colors after construction but before inline tags parse.
	patchCall((void *)0x00456fad, THPS3AP_TextLayoutHook);

	// GetGlobalFlag QScript callback. This is what levelmenu.qb uses to
	// decide whether each career level entry is selectable or static.
	patchDWord((void *)0x005b7f6c, apGetGlobalFlagHook);
	apEnsureDynamicGetGlobalFlagHook();
	apEnsureDynamicGetFlagHook();
	apEnsureDynamicSetGlobalFlagHook();
	apEnsureDynamicUpdateRecordsHook();
	apEnsureDynamicSaveOptionsHook();

	// QScript SetFlag resolves its numeric career flag before this setter call.
	// Hooking the career-bank path preserves the script VM's parameter reads.
	patchCall((void *)0x00420c53, apCareerSetFlagHook);
	apEnsureDynamicGetGoalHook();
	apEnsureDynamicSetScoreGoalHook();
	apEnsureDynamicSetGoalHook();
	apEnsureDynamicJustGotGoalHook();
	apEnsureDynamicLaunchLocalMessageHook();
	apEnsureDynamicAwardStatPointHook();
	apEnsureDynamicJustGotFlagHook();
	apEnsureDynamicCareerStartLevelHook();
	apEnsureDynamicLoadNextUnlockedProHook();
	apEnsureObjectiveUnlockedCommand();
	apEnsureCustomSkaterAllowedCommand();
	apInstallLipEngageHook();
	apInstallGapTouchTraceHooks();
	apInstallAcceptedGapHook();
	patchCall(0x004af027, apScoreLandedHook);
	patchCall(0x004b0d96, apScoreBailedHook);
	patchDWord(0x005b7ffc, apLoadRequestedLevelHook);
	patchCall(0x00419d07, apQbBatchLoad419d07);
	patchCall(0x004397f2, apQbBatchLoad4397f2);
	patchCall(0x0046a6cd, apQbBatchLoad46a6cd);

	{
		static const uint32_t constructorCalls[] = {
			0x00422708, 0x004281d1, 0x004285ab, 0x00433a83,
			0x00449dee, 0x004c27ef, 0x004cd410, 0x004cfc60,
			0x004d0086, 0x004d02f6, 0x004d0556, 0x004d07e8,
			0x004d0aa4, 0x004d0d48, 0x004d109c, 0x004d20e2,
		};
		int i;
		for (i = 0; i < sizeof(constructorCalls) / sizeof(constructorCalls[0]); ++i) {
			patchCall(constructorCalls[i], apScriptConstructorHook);
		}
	}

	// Career progress is calculated natively and can overwrite the QB/global
	// flag state immediately before the selector updates its lock message and
	// TV image. Override the final cached read in both career refresh paths.
	patchNop((void *)0x004459e0, 6);
	patchCall(0x004459e0, apCassetteUnlockStateHook);
	patchNop((void *)0x00445b74, 6);
	patchCall(0x00445b74, apCassetteUnlockStateHook);
	patchCall(0x00444575, THPS3AP_RefreshCassetteMenu);
	patchCall(0x00444675, THPS3AP_RefreshCassetteMenu);
	patchCall(0x0044469d, THPS3AP_RefreshCassetteMenu);
	patchCall(0x004446ac, THPS3AP_RefreshCassetteMenu);
	patchCall(0x0044483e, THPS3AP_RefreshCassetteMenu);

}

void initPatch() {
	GetModuleFileName(NULL, &executableDirectory, filePathBufLen);

	// find last slash
	char *exe = strrchr(executableDirectory, '\\');
	if (exe) {
		*(exe + 1) = '\0';
	}

	char configFile[1024];
	sprintf(configFile, "%s%s", executableDirectory, CONFIG_FILE_NAME);

	int isDebug = getIniBool("Miscellaneous", "Debug", 0, configFile);

	if (isDebug) {
		AllocConsole();

		FILE *fDummy;
		freopen_s(&fDummy, "CONIN$", "r", stdin);
		freopen_s(&fDummy, "CONOUT$", "w", stderr);
		freopen_s(&fDummy, "CONOUT$", "w", stdout);
	}
	printf("PARTYMOD for THPS3 %d.%d.%d\n", VERSION_NUMBER_MAJOR, VERSION_NUMBER_MINOR, VERSION_NUMBER_PATCH);

	printf("DIRECTORY: %s\n", executableDirectory);

	//patchResolution();

	initScriptPatches();

	int disableMovies = getIniBool("Miscellaneous", "NoMovie", 0, configFile);
	if (disableMovies) {
		printf("Disabling movies\n");
		patchNoMovie();
	}

	ILMode = getIniBool("Miscellaneous", "ILMode", 0, configFile);
	if (ILMode) {
		printf("Disabling IL Mode for Archipelago\n");
		ILMode = 0;
	}
	if (ILMode) {
		printf("Enabling IL Mode\n");
		patchILMode();
	}

	disableTrickLimit = getIniBool("Miscellaneous", "NoTrickLimit", 0, configFile);
	if (disableTrickLimit) {
		printf("Disabling trick limit\n");
		patchTrickLimit();
	}

	if (getIniBool("Miscellaneous", "UnlimitedCareerTime", 0, configFile)) {
		printf("Enabling unlimited career time\n");
		patchUnlimitedCareerTime();
	}

	patchOnlineService(configFile);

	patchArchipelagoHooks();

	// get some source of entropy for the music randomizer
	rng_seed = time(NULL) & 0xffffffff;
	srand(rng_seed);

	// PartyMod's post-launch initialization hook runs outside the loader lock.
	// Starting worker threads from DllMain destabilizes THPS3's early Direct3D
	// initialization on current Windows versions.
	THPS3AP_StartBridge(NULL);
	{
		char server[256] = {0};
		char slot[256] = {0};
		char password[256] = {0};
		GetPrivateProfileString("Archipelago", "Server", "", server, sizeof(server), configFile);
		GetPrivateProfileString("Archipelago", "Slot", "", slot, sizeof(slot), configFile);
		GetPrivateProfileString("Archipelago", "Password", "", password, sizeof(password), configFile);
		if (server[0] != '\0' && slot[0] != '\0') {
			printf("Starting embedded Archipelago client for slot %s\n", slot);
			THPS3AP_StartEmbeddedClient(server, slot, password, isDebug);
		} else {
			printf("Archipelago Server/Slot not configured; waiting for an external client\n");
		}
	}

	printf("Patch Initialized\n");
}

static void *apVersionNumberTarget;
static int apFooterConnected = -1;

void apRefreshConnectionFooter(void) {
	int connected = THPS3AP_HasStateSnapshot() != FALSE;
	if (apVersionNumberTarget == NULL || connected == apFooterConnected) {
		return;
	}
	((void (__cdecl *)(void *, char *))0x004ce940)(
		apVersionNumberTarget,
		connected ? " Archipelago - Active" : " Archipelago - Connecting");
	apFooterConnected = connected;
}

int getVersionNumberHook(void *param) {
	void (__fastcall *unk_func1)(void *, void *, void *, void *) = (void *)0x00429dc0;

	void *unkp;
	unk_func1(param, NULL, 0x005b6220, &unkp);

	apVersionNumberTarget = unkp;
	apFooterConnected = -1;
	apRefreshConnectionFooter();
	return 1;
}

void patchVersionNumber() {
	patchDWord(0x005b83d4, getVersionNumberHook);
}

void patchNoLauncher() {
	patchInst((void *)0x0040b57c, JMP8);    // skipcd
	// prevent the setup utility from starting
	// NOTE: if you need some free bytes, there's a lot to work with here, 0x0040b9da to 0x0040ba02 can be rearranged to free up like 36 bytes.  easily enough for a function call
	// the function to load config is in there too, so that can also be taken care of now
	patchNop((void *)0x0040b9da, 7);    // remove call to run launcher
	patchInst((void *)0x0040b9e1, JMP8);    // change launcher condition jump from JZ to JMP
	patchNop((void *)0x0040b9fc, 12);   // remove call to change registry

	// TODO: rename function to make it clear that this adds patch init
	patchCall((void *)0x0040b9da, &(initPatch));
}

void patchNotCD() {
	// Make "notCD" cfunc return true to enable debug features (mostly boring)
	patchByte((void *)0x00404350, 0xb8);
	patchDWord((void *)(0x00404350 + 1), 0x01);
	patchByte((void *)(0x00404350 + 5), 0xc3);
}

void patchPrintf() {
	patchDWord((void *)0x0058d10c, printf);
}

int (__stdcall *theirbind)(SOCKET, const struct sockaddr_in *, int) = NULL;

int __stdcall bindWrapper(SOCKET socket, struct sockaddr_in *address, int namelen) {
	address->sin_addr.S_un.S_addr = INADDR_ANY;	// bind too all interfaces instead of just one

	int result = theirbind(socket, address, namelen);

	return result;
}

void patchNetworkBind() {
	uint32_t *bindaddr = 0x00519500 + 1;

	theirbind = ((int)bindaddr) + 4 + *bindaddr;

	patchCall(0x004d9f3e, bindWrapper);	// i think this is the relevant one, but it doesn't seem to do harm to patch the rest
	patchCall(0x004d9f75, bindWrapper); 
	patchCall(0x00518a32, bindWrapper);
	patchCall(0x00519500, bindWrapper);
}

__declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	// Perform actions based on the reason for calling.
	switch(fdwReason) { 
		case DLL_PROCESS_ATTACH:
			// Initialize once for each new process.
			// Return FALSE to fail DLL load.

			// install patches
			//patchNotCD();
			patchWindow();
			patchNoLauncher();
			patchIntroMovie();
			patchLedgeWarp();
			patchCullModeFix();
			patchInput();
			patchLoadConfig();
			patchScriptHook();
			patchRandomMusic();
			patchVersionNumber();
			patchFramerateCap();
			//patchPrintf();
			patchSoundFix();
			patchNetworkBind();

			break;

		case DLL_THREAD_ATTACH:
			// Do thread-specific initialization.
			break;

		case DLL_THREAD_DETACH:
			// Do thread-specific cleanup.
			break;

		case DLL_PROCESS_DETACH:
			// Perform any necessary cleanup.
			break;
	}
	return TRUE;
}
