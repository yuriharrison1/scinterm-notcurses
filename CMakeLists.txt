# CMakeLists.txt for Scinterm NotCurses
# Scintilla-based text editor widget for NotCurses terminal environment
# Copyright 2012-2026 Mitchell. See LICENSE.

cmake_minimum_required(VERSION 3.10)
project(scinterm_notcurses VERSION 1.0.0 LANGUAGES C CXX)

# ============================================================================
# Project configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ============================================================================
# Build options
# ============================================================================

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(ENABLE_DEBUG "Enable debug output and assertions" OFF)
option(ENABLE_LEXERS "Enable lexers for syntax highlighting" OFF)
option(BUILD_TESTING "Build tests" OFF)
option(ENABLE_SCINTILLUA "Enable Scintillua Lua-based syntax highlighting" OFF)
set(SCINTILLUA_DIR "" CACHE PATH "Path to compiled Scintillua directory")

# ============================================================================
# Target names
# ============================================================================

set(TARGET_NAME scinterm_notcurses)
set(TARGET_STATIC_NAME scinterm_notcurses_static)

# ============================================================================
# Find required packages
# ============================================================================

find_package(PkgConfig REQUIRED)
pkg_check_modules(NOTCURSES REQUIRED notcurses>=3.0.0)

# ============================================================================
# Scintilla paths
# ============================================================================

set(SCINTILLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/scintilla")
set(SCINTILLA_INCLUDE_DIR "${SCINTILLA_DIR}/include")
set(SCINTILLA_SRC_DIR "${SCINTILLA_DIR}/src")

# ============================================================================
# Include directories
# ============================================================================

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plat
    ${SCINTILLA_INCLUDE_DIR}
    ${SCINTILLA_SRC_DIR}
    ${NOTCURSES_INCLUDE_DIRS}
)

# ============================================================================
# Scintilla core sources
# ============================================================================

set(SCINTILLA_SOURCES
    ${SCINTILLA_SRC_DIR}/AutoComplete.cxx
    ${SCINTILLA_SRC_DIR}/CallTip.cxx
    ${SCINTILLA_SRC_DIR}/CaseConvert.cxx
    ${SCINTILLA_SRC_DIR}/CaseFolder.cxx
    ${SCINTILLA_SRC_DIR}/CellBuffer.cxx
    ${SCINTILLA_SRC_DIR}/ChangeHistory.cxx
    ${SCINTILLA_SRC_DIR}/CharacterCategoryMap.cxx
    ${SCINTILLA_SRC_DIR}/CharacterType.cxx
    ${SCINTILLA_SRC_DIR}/CharClassify.cxx
    ${SCINTILLA_SRC_DIR}/ContractionState.cxx
    ${SCINTILLA_SRC_DIR}/DBCS.cxx
    ${SCINTILLA_SRC_DIR}/Decoration.cxx
    ${SCINTILLA_SRC_DIR}/Document.cxx
    ${SCINTILLA_SRC_DIR}/EditModel.cxx
    ${SCINTILLA_SRC_DIR}/Editor.cxx
    ${SCINTILLA_SRC_DIR}/EditView.cxx
    ${SCINTILLA_SRC_DIR}/Geometry.cxx
    ${SCINTILLA_SRC_DIR}/Indicator.cxx
    ${SCINTILLA_SRC_DIR}/KeyMap.cxx
    ${SCINTILLA_SRC_DIR}/LineMarker.cxx
    ${SCINTILLA_SRC_DIR}/MarginView.cxx
    ${SCINTILLA_SRC_DIR}/PerLine.cxx
    ${SCINTILLA_SRC_DIR}/PositionCache.cxx
    ${SCINTILLA_SRC_DIR}/RESearch.cxx
    ${SCINTILLA_SRC_DIR}/RunStyles.cxx
    ${SCINTILLA_SRC_DIR}/ScintillaBase.cxx
    ${SCINTILLA_SRC_DIR}/Selection.cxx
    ${SCINTILLA_SRC_DIR}/Style.cxx
    ${SCINTILLA_SRC_DIR}/UndoHistory.cxx
    ${SCINTILLA_SRC_DIR}/UniConversion.cxx
    ${SCINTILLA_SRC_DIR}/UniqueString.cxx
    ${SCINTILLA_SRC_DIR}/ViewStyle.cxx
    ${SCINTILLA_SRC_DIR}/XPM.cxx
)

# ============================================================================
# Optional lexers for syntax highlighting
# ============================================================================

if(ENABLE_LEXERS)
    file(GLOB LEXER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/lexers/*.cxx")
    set(SCINTILLA_SOURCES ${SCINTILLA_SOURCES} ${LEXER_SOURCES})
endif()

# ============================================================================
# Platform-specific sources for NotCurses
# ============================================================================

set(PLATFORM_SOURCES
    src/plat/scinterm_plat.cpp
    src/plat/scinterm_notcurses.cpp
    src/plat/scinterm_wcwidth.c
)

set(ALL_SOURCES
    ${SCINTILLA_SOURCES}
    ${PLATFORM_SOURCES}
)

# ============================================================================
# Public headers
# ============================================================================

set(PUBLIC_HEADERS
    ${SCINTILLA_INCLUDE_DIR}/Scintilla.h
    ${SCINTILLA_INCLUDE_DIR}/ScintillaTypes.h
    ${SCINTILLA_INCLUDE_DIR}/ScintillaMessages.h
    ${SCINTILLA_INCLUDE_DIR}/ScintillaStructures.h
    ${SCINTILLA_INCLUDE_DIR}/ILoader.h
    ${SCINTILLA_INCLUDE_DIR}/ILexer.h
    include/scinterm_notcurses.h
)

set(PRIVATE_HEADERS
    src/plat/scinterm_plat.h
    src/plat/scinterm_wcwidth.h
)

# ============================================================================
# Scintillua (Lua-based syntax highlighting)
# ============================================================================

if(ENABLE_SCINTILLUA)
    if(NOT SCINTILLUA_DIR)
        message(FATAL_ERROR "SCINTILLUA_DIR must be set when ENABLE_SCINTILLUA=ON")
    endif()
    if(NOT EXISTS "${SCINTILLUA_DIR}/Scintillua.h")
        message(FATAL_ERROR "Scintillua.h not found in SCINTILLUA_DIR=${SCINTILLUA_DIR}")
    endif()
    message(STATUS "Scintillua: ${SCINTILLUA_DIR}")
    add_definitions(-DENABLE_SCINTILLUA)
    include_directories(${SCINTILLUA_DIR})
    set(SCINTILLUA_LIB "${SCINTILLUA_DIR}/libscintillua.so")
    set(SCINTILLUA_LEXERS_DIR "${SCINTILLUA_DIR}/lexers")
endif()

# ============================================================================
# Compiler flags
# ============================================================================

if(ENABLE_DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DDEBUG -g -O0 -Wall -Wextra")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DDEBUG -g -O0 -Wall -Wextra")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DNDEBUG -O2 -Wall -Wextra")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DNDEBUG -O2 -Wall -Wextra")
endif()

# Use CURSES platform define so Scintilla Platform.h sets PLAT_CURSES=1
add_definitions(-DCURSES)

# Add NotCurses compile flags safely using separate_arguments
if(NOTCURSES_CFLAGS_OTHER)
    separate_arguments(NOTCURSES_CFLAGS_OTHER_LIST UNIX_COMMAND "${NOTCURSES_CFLAGS_OTHER}")
    add_compile_options(${NOTCURSES_CFLAGS_OTHER_LIST})
endif()

if(WIN32)
    add_definitions(-D_WIN32 -D_UNICODE -DUNICODE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWIN32_LEAN_AND_MEAN")
elseif(UNIX AND NOT APPLE)
    add_definitions(-D_GNU_SOURCE)
endif()

# ============================================================================
# Build shared library
# ============================================================================

if(BUILD_SHARED_LIBS)
    add_library(${TARGET_NAME} SHARED ${ALL_SOURCES} ${PUBLIC_HEADERS} ${PRIVATE_HEADERS})

    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${SCINTILLA_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include/scinterm>
    )
    target_include_directories(${TARGET_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/plat
        ${SCINTILLA_SRC_DIR}
        ${NOTCURSES_INCLUDE_DIRS}
    )

    target_link_libraries(${TARGET_NAME}
        ${NOTCURSES_LIBRARIES}
        m
        pthread
        $<$<BOOL:${ENABLE_SCINTILLUA}>:${SCINTILLUA_LIB}>
    )

    set_target_properties(${TARGET_NAME} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME scinterm_notcurses
        PUBLIC_HEADER "${PUBLIC_HEADERS}"
    )

    install(TARGETS ${TARGET_NAME}
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        PUBLIC_HEADER DESTINATION include/scinterm
    )
endif()

# ============================================================================
# Build static library
# ============================================================================

add_library(${TARGET_STATIC_NAME} STATIC ${ALL_SOURCES})

target_include_directories(${TARGET_STATIC_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${SCINTILLA_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include/scinterm>
)
target_include_directories(${TARGET_STATIC_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/plat
    ${SCINTILLA_SRC_DIR}
    ${NOTCURSES_INCLUDE_DIRS}
)

target_link_libraries(${TARGET_STATIC_NAME}
    ${NOTCURSES_LIBRARIES}
    m
    pthread
    $<$<BOOL:${ENABLE_SCINTILLUA}>:${SCINTILLUA_LIB}>
)

set_target_properties(${TARGET_STATIC_NAME} PROPERTIES
    OUTPUT_NAME scinterm_notcurses_static
)

install(TARGETS ${TARGET_STATIC_NAME}
    ARCHIVE DESTINATION lib
)

install(FILES ${PUBLIC_HEADERS}
    DESTINATION include/scinterm
)

# ============================================================================
# Build examples
# ============================================================================

if(BUILD_EXAMPLES)
    add_executable(scinterm_example examples/simple_editor.c)
    target_include_directories(scinterm_example PRIVATE
        include ${SCINTILLA_INCLUDE_DIR} ${NOTCURSES_INCLUDE_DIRS})
    target_link_libraries(scinterm_example
        ${TARGET_NAME}
        ${NOTCURSES_LIBRARIES}
        m
    )
    set_target_properties(scinterm_example PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_executable(scinterm_multi examples/multi_window.c)
    target_include_directories(scinterm_multi PRIVATE
        include ${SCINTILLA_INCLUDE_DIR} ${NOTCURSES_INCLUDE_DIRS})
    target_link_libraries(scinterm_multi
        ${TARGET_NAME}
        ${NOTCURSES_LIBRARIES}
        m
    )
    set_target_properties(scinterm_multi PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    add_executable(scinterm_syntax examples/syntax_highlighting.c)
    target_include_directories(scinterm_syntax PRIVATE
        include ${SCINTILLA_INCLUDE_DIR} ${NOTCURSES_INCLUDE_DIRS})
    target_link_libraries(scinterm_syntax
        ${TARGET_NAME}
        ${NOTCURSES_LIBRARIES}
        m
    )
    if(ENABLE_SCINTILLUA)
        target_compile_definitions(scinterm_syntax PRIVATE
            SCINTILLUA_LEXERS_DIR="${SCINTILLUA_LEXERS_DIR}")
    endif()
    set_target_properties(scinterm_syntax PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    install(TARGETS scinterm_example scinterm_multi scinterm_syntax
        RUNTIME DESTINATION bin
    )
endif()

# ============================================================================
# Build tests
# ============================================================================

if(BUILD_TESTING)
    enable_testing()

    add_executable(scinterm_test tests/test_editor.c)
    target_include_directories(scinterm_test PRIVATE
        include ${SCINTILLA_INCLUDE_DIR} ${NOTCURSES_INCLUDE_DIRS})
    target_link_libraries(scinterm_test
        ${TARGET_NAME}
        ${NOTCURSES_LIBRARIES}
        m
    )
    add_test(NAME scinterm_test COMMAND scinterm_test)
endif()

# ============================================================================
# Generate package configuration
# ============================================================================

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/scinterm_notcurses-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "cmake/scinterm_notcurses-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/scinterm_notcurses-config.cmake"
    INSTALL_DESTINATION lib/cmake/scinterm_notcurses
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/scinterm_notcurses-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/scinterm_notcurses-config-version.cmake"
    DESTINATION lib/cmake/scinterm_notcurses
)

# ============================================================================
# Build summary
# ============================================================================

message(STATUS "========================================")
message(STATUS "Scinterm NotCurses Build Configuration")
message(STATUS "========================================")
message(STATUS "Project Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Install Prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Build Shared Library: ${BUILD_SHARED_LIBS}")
message(STATUS "Build Examples: ${BUILD_EXAMPLES}")
message(STATUS "Enable Lexers: ${ENABLE_LEXERS}")
message(STATUS "Debug Mode: ${ENABLE_DEBUG}")
message(STATUS "NotCurses Version: ${NOTCURSES_VERSION}")
message(STATUS "Scintilla Dir: ${SCINTILLA_DIR}")
message(STATUS "========================================")
