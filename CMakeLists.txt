cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

option(ENABLE_OBS_WEBSOCKET "Enable obs-websocket vendor request integration" ON)

# When the submodule is missing, only this configure run is downgraded to OFF (a non-cache local variable). Writing OFF
# to the cache here would persist past `git submodule update --init` until the user remembers to pass `--fresh` or to
# delete the build dir.
if(ENABLE_OBS_WEBSOCKET AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/obs-websocket/lib/obs-websocket-api.h")
  message(
    WARNING
      "ENABLE_OBS_WEBSOCKET is ON but lib/obs-websocket submodule is missing; disabling integration for this configure run. Run: git submodule update --init lib/obs-websocket then re-run cmake with --fresh (or delete the build dir) to enable obs-websocket vendor request integration."
  )
  set(ENABLE_OBS_WEBSOCKET OFF)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header
                                  -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

if(ENABLE_OBS_WEBSOCKET)
  # Consumed surface is `obs-websocket-api.h` only; the whole `lib/obs-websocket/lib` directory is intentionally exposed
  # as a SYSTEM include path because that header lives there and SYSTEM suppresses upstream warnings.
  target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/obs-websocket/lib)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE ENABLE_OBS_WEBSOCKET)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/plugin-websocket.cpp)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin-main.cpp
          src/plugin-streaming.cpp
          src/plugin-stream-recording.cpp
          src/plugin-replay-buffer.cpp
          src/plugin-ui.cpp
          src/utils.cpp
          src/audio/audio-capture.cpp
          src/video/filter-video-capture.cpp
          src/video/crop-rect-preview-renderer.cpp
          src/UI/output-status-dock.cpp
          src/UI/resources.qrc)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
