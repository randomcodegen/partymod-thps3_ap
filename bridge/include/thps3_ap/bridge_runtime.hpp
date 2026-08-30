#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <windows.h>

namespace thps3_ap {

inline constexpr int kProtocolVersion = 1;
inline constexpr wchar_t kPipeName[] = LR"(\\.\pipe\thps3_archipelago)";

DWORD WINAPI RunBridgeRuntime(void* module_handle);
DWORD RunBridgeRuntimeForTest(const wchar_t* pipe_name);
void StartupBreadcrumb(const char* phase);
void RequestBridgeShutdown();
void QueueStatAssignment(const std::array<std::uint32_t, 10>& assignment);
void QueueAppearance(std::string appearance);
void QueueCollectibleMarkersEnabled(bool enabled);
void QueueScoreBonusItemsApplied(std::uint32_t count);

}  // namespace thps3_ap
