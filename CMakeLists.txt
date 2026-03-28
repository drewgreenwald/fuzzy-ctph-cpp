cmake_minimum_required(VERSION 3.20)

project(fuzzy-hash-tool LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 1. Standard Requirements (C++20 is required for std::execution and std::filesystem)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# 2. Find Dependencies via vcpkg
# ---------------------------------------------------------------------------
find_package(argparse CONFIG REQUIRED)
find_package(Threads REQUIRED)

# On Linux/Unix, std::execution::par often requires TBB
if(UNIX)
    find_package(TBB REQUIRED)
endif()

# ---------------------------------------------------------------------------
# 3. Source files shared by both library targets
# ---------------------------------------------------------------------------
set(CTPH_SOURCES
    fuzzy-ctph/fuzzy.cpp
    fuzzy-ctph/fuzzy.h
)

# ---------------------------------------------------------------------------
# 4. Static Library  (ctph_static)
#    Consumers: link with ctph_static; no import lib needed, no DLL to ship.
#    CTPH_STATIC_DEFINE suppresses dllimport decorations on Windows.
# ---------------------------------------------------------------------------
add_library(ctph_static STATIC ${CTPH_SOURCES})

target_include_directories(ctph_static PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fuzzy-ctph>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(ctph_static PUBLIC CTPH_STATIC_DEFINE)

set_target_properties(ctph_static PROPERTIES
    OUTPUT_NAME "ctph"          # produces libctph.a / ctph.lib
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(ctph_static PRIVATE /W4 /utf-8)
else()
    target_compile_options(ctph_static PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 5. Shared Library  (ctph_shared)
#    On Windows  → ctph.dll + ctph.lib (import lib)
#    On Linux    → libctph.so
#
#    CTPH_EXPORTS is defined only for this target so the header emits
#    dllexport / visibility("default") during the build of the DLL itself.
# ---------------------------------------------------------------------------
add_library(ctph_shared SHARED ${CTPH_SOURCES})

target_include_directories(ctph_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/fuzzy-ctph>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(ctph_shared PRIVATE CTPH_EXPORTS)

set_target_properties(ctph_shared PROPERTIES
    OUTPUT_NAME "ctph"          # produces ctph.dll / libctph.so
    VERSION     "1.0.0"
    SOVERSION   "1"
)

# On GCC/Clang, hide symbols by default so only CTPH_API symbols are exported
if(NOT MSVC)
    target_compile_options(ctph_shared PRIVATE
        -Wall -Wextra -fvisibility=hidden -fvisibility-inlines-hidden
    )
endif()

if(MSVC)
    target_compile_options(ctph_shared PRIVATE /W4 /utf-8)
endif()

# Linux-specific TBB link for shared lib (needed if fuzzy.cpp uses parallel algos)
if(UNIX)
    target_link_libraries(ctph_shared PRIVATE TBB::tbb Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# 6. CLI Executable  (fuzzy-ctph)
#    Links the static lib so the EXE is fully self-contained (no DLL needed).
# ---------------------------------------------------------------------------
add_executable(fuzzy-ctph fuzzy-ctph/fuzzy-ctph.cpp)

target_link_libraries(fuzzy-ctph
    PRIVATE
        ctph_static
        argparse::argparse
        Threads::Threads
)

target_include_directories(fuzzy-ctph PRIVATE fuzzy-ctph)

if(UNIX)
    target_link_libraries(fuzzy-ctph PRIVATE TBB::tbb)
endif()

if(MSVC)
    target_compile_options(fuzzy-ctph PRIVATE /W4 /utf-8)
else()
    target_compile_options(fuzzy-ctph PRIVATE -Wall -Wextra -pthread)
endif()

# ---------------------------------------------------------------------------
# 7. Install rules
#    cmake --install build --prefix /some/dir
#    will place:
#      bin/       fuzzy-ctph(.exe)
#      lib/       libctph.a  libctph.so  (or ctph.lib  ctph.dll)
#      include/   fuzzy.h
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS ctph_static ctph_shared fuzzy-ctph
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES fuzzy-ctph/fuzzy.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ctph
)
