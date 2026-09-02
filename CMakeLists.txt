cmake_minimum_required(VERSION 3.15)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()

project(thps3patch LANGUAGES C CXX)

include(CTest)

set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(THIRDPARTY "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty")
set(THPS3_AP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(THPS3_APCPP_ROOT "${THPS3_AP_ROOT}/thirdparty/APCpp" CACHE PATH "APCpp source directory")

set(SOURCE_FILES
	"${SRC}/main.c"
	"${SRC}/patch.c"
	"${SRC}/config.c"
	"${SRC}/input.c"
	"${SRC}/script.c"
	"${SRC}/patches.c"
)

set(HEADER_FILES
	"${SRC}/global.h"
	"${SRC}/patch.h"
	"${SRC}/config.h"
	"${SRC}/input.h"
	"${SRC}/script.h"
)

list(APPEND INCLUDES ${THIRDPARTY} "${THIRDPARTY}/incbin")

# add sdl2
find_package(SDL2 REQUIRED)
list(APPEND LIBS 
	SDL2::SDL2
	SDL2::SDL2main
)
list(APPEND INCLUDES ${SDL2_INCLUDE_DIRS})

add_library(partymod SHARED)

target_sources(partymod PRIVATE
	${SOURCE_FILES}
	${HEADER_FILES}
)

find_package(ixwebsocket CONFIG REQUIRED)
find_package(jsoncpp CONFIG REQUIRED)
target_sources(partymod PRIVATE
	"${THPS3_AP_ROOT}/bridge/src/partymod_version.rc"
	"${THPS3_AP_ROOT}/bridge/src/bridge_c_api.cpp"
	"${THPS3_AP_ROOT}/bridge/src/bridge_runtime.cpp"
	"${THPS3_AP_ROOT}/bridge/src/game_state.cpp"
	"${THPS3_AP_ROOT}/native_client/main.cpp"
	"${THPS3_APCPP_ROOT}/Archipelago.cpp"
)

target_link_libraries(partymod PUBLIC ${LIBS})
target_link_libraries(partymod PRIVATE
	advapi32 bcrypt crypt32 ws2_32
	ixwebsocket::ixwebsocket
	JsonCpp::JsonCpp
)

target_include_directories(partymod PUBLIC
	"${PROJECT_SOURCE_DIR}/src"
	${INCLUDES}
	"${THPS3_AP_ROOT}/bridge/include"
	"${THPS3_APCPP_ROOT}"
)

target_compile_definitions(partymod PRIVATE
	NOMINMAX
)
target_compile_features(partymod PRIVATE cxx_std_20)
if(MSVC)
	target_compile_options(partymod PRIVATE /Od)
	target_link_options(partymod PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
endif()

if(BUILD_TESTING)
	add_executable(partypatcher_validation_test
		"${CMAKE_CURRENT_SOURCE_DIR}/tests/partypatcher_validation_test.c"
		"${SRC}/patch.c"
	)
	target_include_directories(partypatcher_validation_test PRIVATE "${SRC}")
	add_test(NAME partypatcher_validation COMMAND partypatcher_validation_test)

	add_executable(thps3_ap_bridge_runtime_test
		"${THPS3_AP_ROOT}/bridge/tests/bridge_runtime_live_event_test.cpp"
		"${THPS3_AP_ROOT}/bridge/src/bridge_runtime.cpp"
		"${THPS3_AP_ROOT}/bridge/src/game_state.cpp"
	)
	target_include_directories(thps3_ap_bridge_runtime_test PRIVATE
		"${THPS3_AP_ROOT}/bridge/include"
	)
	target_compile_features(thps3_ap_bridge_runtime_test PRIVATE cxx_std_20)
	target_compile_definitions(thps3_ap_bridge_runtime_test PRIVATE
		WIN32_LEAN_AND_MEAN
		NOMINMAX
	)
	target_link_libraries(thps3_ap_bridge_runtime_test PRIVATE
		advapi32
		JsonCpp::JsonCpp
	)
	add_test(NAME bridge_runtime_live_event COMMAND thps3_ap_bridge_runtime_test)
endif()

add_executable(partyconfig)

target_sources(partyconfig PUBLIC config/main.c)
target_link_libraries(partyconfig PRIVATE 
	${LIBS}
	comctl32.lib
	UxTheme.lib
)
target_include_directories(partyconfig PUBLIC
	"${PROJECT_SOURCE_DIR}/src"
	${INCLUDES}
)

add_executable(incbin)

target_sources(incbin PUBLIC 
	"${THIRDPARTY}/incbin/incbin.c"
	"${THIRDPARTY}/incbin/incbin.h"
)

add_custom_command(
	OUTPUT "${SRC}/patches.c"
	COMMAND $<TARGET_FILE:incbin> src/script.c -o src/patches.c
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
	DEPENDS
		incbin
		"${SRC}/script.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/optionsmenu.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/levels.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/levelmenu_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/mainmenu_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/casmenu_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/memcard_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/game_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/gameflow_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/goal_scripts_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/judges_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/netmessages_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/shp_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/cjr_scripts_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/ajc_scripts_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/alf_scripts_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/cpf_scripts_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/la_objectives_ap.bps"
		"${CMAKE_CURRENT_SOURCE_DIR}/patches/bdj_scripts_ap.bps"
	VERBATIM
)
add_custom_command(TARGET partymod POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
		"${CMAKE_CURRENT_SOURCE_DIR}/partymod.ini"
		"$<TARGET_FILE_DIR:partymod>/partymod.ini"
)

add_executable(partypatcher)

add_custom_command(
	OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/patcher/data.c"
	COMMAND $<TARGET_FILE:incbin> main.c -o data.c
	WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/patcher"
	DEPENDS
		incbin
		"${CMAKE_CURRENT_SOURCE_DIR}/patcher/main.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/patcher/executable.bps"
	VERBATIM
)

target_sources(partypatcher PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/patcher/main.c"
	"${CMAKE_CURRENT_SOURCE_DIR}/patcher/data.c"
	"${SRC}/patch.c"
)

target_include_directories(partypatcher PUBLIC
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	${THIRDPARTY}
	"${THIRDPARTY}/incbin"
)

install(TARGETS partymod partypatcher partyconfig
	RUNTIME DESTINATION .
	LIBRARY DESTINATION .
)
install(FILES partymod.ini gamecontrollerdb.txt DESTINATION .)
