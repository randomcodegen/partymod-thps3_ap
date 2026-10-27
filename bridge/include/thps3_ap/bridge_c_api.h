#pragma once

#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void THPS3AP_StartBridge(HINSTANCE module_handle);
void THPS3AP_DebugLog(const char *format, ...);
void THPS3AP_StartEmbeddedClient(const char *server, const char *slot,
                                 const char *password, BOOL debug);
BOOL THPS3AP_HasStateSnapshot(void);
BOOL THPS3AP_StatPointsAreLocations(void);
BOOL THPS3AP_HiddenDecksAreLocations(void);
void THPS3AP_BeginLevelTransition(void);
void THPS3AP_EndLevelTransition(int level_number);
BOOL THPS3AP_IsLevelUnlocked(int level_number);
BOOL THPS3AP_IsObjectiveUnlocked(int level_number, int goal_id);
BOOL THPS3AP_IsObjectiveChecked(int level_number, int goal_id);
int THPS3AP_ObjectiveListPosition(int level_number, int goal_id);
BOOL THPS3AP_IsTrickCategoryUnlocked(int category);
#define THPS3AP_TRICK_FLIP 0
#define THPS3AP_TRICK_GRAB 1
#define THPS3AP_TRICK_GRIND 2
#define THPS3AP_TRICK_REVERT 3
#define THPS3AP_TRICK_MANUAL 4
#define THPS3AP_TRICK_LIP 5
#define THPS3AP_TRICK_SPECIAL 6
#define THPS3AP_TRICK_WALLRIDE 7
void THPS3AP_RequestLevelMenuRefresh(void);
void THPS3AP_RecordHighlightedCassetteLevel(int level_number);
void __fastcall THPS3AP_RefreshCassetteMenu(void *cassette_menu,
                                            void *unused_edx);
void THPS3AP_PumpMainThread(void);
uint32_t THPS3AP_ReconcileStatCareerBits(void);
uint32_t THPS3AP_ReconcileCareerBits(void);
void THPS3AP_ApplyCareerTimerBonus(void);
void THPS3AP_RecordGoalCompletion(int level_number, int goal_id);
void THPS3AP_RecordMedalResult(int level_number, int medal_id);
void THPS3AP_RecordDeckCollection(int level_number);
void THPS3AP_QueueGoalEvent(int level_number, int goal_id);
void THPS3AP_QueueStatPointEvent(int level_number, int point_id);
void THPS3AP_QueueDeckEvent(int level_number);
void THPS3AP_QueueGapListRequest(int level_number);
void THPS3AP_SetGapMenuData(const char *title, const char *const *rows,
                            const uint32_t *checksums, uint32_t row_count);
void THPS3AP_SelectGapMenuRow(uint32_t row_index);
void THPS3AP_AdvanceGapHighlight(void);
void THPS3AP_ToggleAllGapHighlights(void);
void THPS3AP_ToggleLosAngelesGeometry(void);
BOOL THPS3AP_UseIntactLosAngelesGeometry(void);
BOOL THPS3AP_HasActiveStageOverlay(void);
void THPS3AP_CaptureStageRenderState(void *d3d_device);
void THPS3AP_DrawCapturedStageOverlays(void *d3d_device);
void THPS3AP_DrawGapTintOverlay(void *d3d_device);
void THPS3AP_DrawGapMarkers(void *d3d_device);
void THPS3AP_ToggleCollectibleMarkers(void);
void THPS3AP_SetCollectibleMarkersEnabled(BOOL enabled);
BOOL THPS3AP_CollectibleMarkersEnabled(void);
void THPS3AP_DrawHudOverlay(void *d3d_device);
BOOL THPS3AP_ConsoleCapturesInput(void);
void THPS3AP_InitializeConsole(const char *config_path);
BOOL THPS3AP_ConsoleHudVisible(void);
BOOL THPS3AP_ProcessConsoleEvent(const void *event);
void THPS3AP_OpenConsole(void);
void THPS3AP_DrawConsole(void *d3d_device);
void THPS3AP_ClearMessagePopups(void);
void THPS3AP_ConsoleMainMenuReady(void);
void THPS3AP_ClearGapHighlights(void);
void THPS3AP_RequestGapMenuRefresh(void);
void THPS3AP_QueueGapEvent(int level_number, uint32_t checksum,
                           uint32_t skater_state, uint32_t trigger_type);
BOOL THPS3AP_IsSelectedSkaterActive(void);
BOOL THPS3AP_IsCustomSkaterSelected(void);
BOOL THPS3AP_RestoreCustomSkaterPreset(void);

#ifdef __cplusplus
}
#endif
