add_falcor_executable(HelperLaneWindows)

# Silence Eigen / STL C++17 deprecation warnings
target_compile_definitions(HelperLaneWindows PRIVATE
    _SILENCE_CXX17_NEGATORS_DEPRECATION_WARNING
)

# Triangle flags
target_compile_definitions(HelperLaneWindows PRIVATE
    TRILIBRARY
    NO_TIMER
    ANSI_DECLARATORS
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle/triangle.cpp
    PROPERTIES
    COMPILE_FLAGS "/wd4311 /wd4302"
)

target_sources(HelperLaneWindows PRIVATE
    HelperLaneViz.cpp
    HelperLaneViz.h
    ExtendedMeasurementAssets.cpp
    ExtendedMeasurementAssets.h
    TriangulationHelpers.cpp
    TriangulationHelpers.h
    SVGLoader.cpp
    SVGLoader.h
    ShaderTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle/triangle.cpp
    # meshoptimizer sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/allocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/vcacheoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/vfetchoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/overdrawoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/indexgenerator.cpp
)

# Third-party headers (HEADER-ONLY)
target_include_directories(HelperLaneWindows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/earcut.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libigl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/nanosvg/src
)

# Shaders
file(GLOB_RECURSE SHADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang"
)
target_sources(HelperLaneWindows PRIVATE ${SHADER_FILES})
target_copy_shaders(HelperLaneWindows Samples/HelperLaneWindows)

target_source_group(HelperLaneWindows "Samples")

# Enable AVX2 on supported MSVC x64 targets, to enable compiler vectorizations. 
target_compile_options(HelperLaneWindows PRIVATE
  # Windows, x64 MSVC frontend (covers cl.exe and clang-cl) and not ARM
  $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:${MSVC_CXX_ARCHITECTURE_ID},x64>>:/arch:AVX2>
  $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<CXX_COMPILER_ID:GNU>>:-mavx2 -mfma>
  $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<CXX_COMPILER_ID:Clang>>:-mavx2 -mfma>
)
