# The browser build: the same `games` program, linked by Emscripten into
# games.js + games.wasm, with GUI.Forms' web host. The page (web/index.html)
# must be cross-origin isolated (COOP same-origin, COEP require-corp) because
# the program's threads and the audio worklet share its memory.
#
# Fonts are preloaded with the program at /gui_forms/fonts. The runtime assets
# are packed once, apart from the program, into assets.data + assets.js
# (target games_web_assets), which the page loads first and mounts at /assets,
# so relinking never rewrites them. Saves live in IndexedDB at /state
# (web/shell.js mounts and synchronizes it).
set(GAMES_WEB_OUTPUT_DIR "${CMAKE_BINARY_DIR}/web" CACHE PATH "Where the browser build is assembled")
# A thread beyond the pool needs the main thread to start a worker, which a
# main-thread join would block, so the pool covers what the games start at
# once: the lawn and the r3d bands each use about one thread per core.
set(GAMES_WEB_THREAD_POOL "Math.max(16,Math.min(48,2*navigator.hardwareConcurrency+4))"
    CACHE STRING "Worker threads started with the page (a JavaScript expression)")

set_target_properties(games PROPERTIES
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${GAMES_WEB_OUTPUT_DIR}")
target_link_options(games PRIVATE
    -sENVIRONMENT=web,worker
    -sEXIT_RUNTIME=0
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=512MB
    -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=8MB
    -sDEFAULT_PTHREAD_STACK_SIZE=4MB
    "SHELL:-sPTHREAD_POOL_SIZE='${GAMES_WEB_THREAD_POOL}'"
    -sFORCE_FILESYSTEM=1
    -lidbfs.js
    -sINVOKE_RUN=0
    -sEXPORTED_RUNTIME_METHODS=callMain,FS,ENV,IDBFS,UTF8ToString
    "--preload-file=${GUIForms_FONT_DIR}@/gui_forms/fonts"
    # No game shows Japanese text; the 16 MB CJK fallback face stays out.
    "--exclude-file=*NotoSansCJK*")
option(GAMES_WEB_DEBUG "Keep function names and Emscripten's runtime assertions" OFF)
# Function names alone cost nothing at run time; profiles need them.
option(GAMES_WEB_FUNCTION_NAMES "Keep function names (for profiles)" OFF)
if(GAMES_WEB_DEBUG)
    target_link_options(games PRIVATE --profiling-funcs -sASSERTIONS=1 -sSTACK_OVERFLOW_CHECK=2)
else()
    target_link_options(games PRIVATE -sASSERTIONS=0)
    if(GAMES_WEB_FUNCTION_NAMES)
        target_link_options(games PRIVATE --profiling-funcs)
    endif()
endif()
set_property(TARGET games APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.js")

add_custom_command(TARGET games POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.js"
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.css"
        "${GAMES_WEB_OUTPUT_DIR}"
    VERBATIM)

# Test programs run under Node (node <test>.js), reading files straight from
# the disk; main runs on a worker so it may block like a native test. Deferred
# to the end of the directory, since CMakeLists.txt adds most tests after this
# file is included.
function(games_web_link_tests_for_node)
    get_property(games_web_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(test_target IN LISTS games_web_targets)
        get_target_property(test_type ${test_target} TYPE)
        if(test_type STREQUAL "EXECUTABLE" AND NOT test_target STREQUAL "games")
            set_target_properties(${test_target} PROPERTIES SUFFIX ".js")
            target_link_options(${test_target} PRIVATE -sENVIRONMENT=node -sNODERAWFS=1
                -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8MB -sDEFAULT_PTHREAD_STACK_SIZE=4MB
                -sPROXY_TO_PTHREAD=1 -sEXIT_RUNTIME=1 -sPTHREAD_POOL_SIZE=8 -sASSERTIONS=1 --profiling-funcs)
        endif()
    endforeach()
endfunction()
cmake_language(DEFER CALL games_web_link_tests_for_node)

set(GAMES_WEB_SHARED_ASSETS "" CACHE PATH "Directory of an already packed assets.data + assets.js to link to instead")
if(GAMES_WEB_SHARED_ASSETS)
    file(MAKE_DIRECTORY "${GAMES_WEB_OUTPUT_DIR}")
    foreach(pack assets.data assets.js)
        file(CREATE_LINK "${GAMES_WEB_SHARED_ASSETS}/${pack}" "${GAMES_WEB_OUTPUT_DIR}/${pack}" SYMBOLIC)
    endforeach()
else()
add_custom_command(
    OUTPUT "${GAMES_WEB_OUTPUT_DIR}/assets.data" "${GAMES_WEB_OUTPUT_DIR}/assets.js"
    COMMAND "${EMSCRIPTEN_ROOT_PATH}/tools/file_packager" assets.data
        "--preload" "${GAMES_RUNTIME_ASSET_DIR}@/assets" "--js-output=assets.js" "--no-node"
        # The web build plays only sound generated live; recorded audio stays out.
        "--exclude" "*.ogg" "--exclude" "*.wav" "--exclude" "*.m4a"
    DEPENDS "${GAMES_RUNTIME_ASSET_DIR}/audio/portable_manifest.json"
    WORKING_DIRECTORY "${GAMES_WEB_OUTPUT_DIR}"
    COMMENT "Packing the runtime assets"
    VERBATIM)
add_custom_target(games_web_assets ALL DEPENDS "${GAMES_WEB_OUTPUT_DIR}/assets.data")
add_dependencies(games games_web_assets)
endif()

# One pack per browser exhibit (tools/web/exhibits.json): packs/base plus
# packs/<game>, loaded by the page for ?game=<id>&standalone.
add_custom_target(games_web_packs
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/web/pack_exhibits.py"
        --runtime "${GAMES_RUNTIME_ASSET_DIR}" --output "${GAMES_WEB_OUTPUT_DIR}/packs"
        --packager "${EMSCRIPTEN_ROOT_PATH}/tools/file_packager"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/web/exhibits.json" "${CMAKE_CURRENT_SOURCE_DIR}/tools/web/pack_exhibits.py"
    COMMENT "Packing each exhibit's runtime files"
    VERBATIM)
