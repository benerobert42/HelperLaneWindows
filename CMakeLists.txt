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
    HELPERLANE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle/triangle.cpp
    PROPERTIES
    COMPILE_FLAGS "/wd4311 /wd4302"
)

target_sources(HelperLaneWindows PRIVATE
    HelperLaneViz.cpp
    HelperLaneViz.h
    SampleSupport.cpp
    SampleSupport.h
    Rendering/LitPreview.h
    Rendering/LitPreview.cpp
    Rendering/PreviewCapture.cpp

    # Importers
    Import/ObjMeshLoader.cpp
    Import/ObjMeshLoader.h
    Import/ModelPresets.h
    Import/SVGLoader.cpp
    Import/SVGLoader.h

    # Shared triangulation types
    Triangulation/ShaderTypes.h
    Triangulation/GeometryMath.h
    Triangulation/SurfaceGeometryMath.h
    Triangulation/MeshValidation.cpp
    Triangulation/MeshValidation.h

    # Planar algorithms (also used by projected 3D patches)
    Triangulation/2D/TriangulationHelpers.cpp
    Triangulation/2D/TriangulationHelpers.h
    Triangulation/2D/ProjectedGeometry.h
    Triangulation/2D/ProjectedGeometry.cpp
    Triangulation/2D/ProjectedGraph.cpp
    Triangulation/2D/ProjectedPatchTriangulation.cpp
    Triangulation/2D/ProjectedPatchTriangulation.h

    # Surface algorithms, generators, and experimental flips
    Triangulation/3D/Triangulation3D.cpp
    Triangulation/3D/Triangulation3D.h
    Triangulation/3D/ProceduralGeometry3D.cpp
    Triangulation/3D/ProceduralGeometry3D.h
    Triangulation/3D/ReferenceSurface.cpp
    Triangulation/3D/ReferenceSurface.h
    Triangulation/3D/SurfaceFlipExperiment.cpp
    Triangulation/3D/SurfaceFlipExperiment.h
    Triangulation/3D/SurfaceStarExperiment.cpp
    Triangulation/3D/SurfaceStarExperiment.h

    # Benchmark orchestration and fixtures
    Experiments/BenchmarkOptions.h
    Experiments/BenchmarkOptions.cpp
    Experiments/Benchmark.h
    Experiments/Benchmark.cpp
    Experiments/BenchmarkSupport.h
    Experiments/InteractiveSmoke.cpp
    Experiments/InteractiveBenchmark.cpp
    Experiments/PerformanceBenchmark.cpp
    Experiments/RegressionChecks.cpp
    Experiments/BenchmarkGeometry.cpp
    Experiments/BenchmarkGeometry.h
    Experiments/RasterMetrics.h
    Experiments/RasterMetrics.cpp
    Experiments/PipelineStatistics.h
    Experiments/PipelineStatistics.cpp

    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle/triangle.cpp
    # meshoptimizer sources
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/allocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/vcacheoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/vfetchoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/overdrawoptimizer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/indexgenerator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/meshoptimizer/src/indexanalyzer.cpp
)

# Third-party headers (HEADER-ONLY)
target_include_directories(HelperLaneWindows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
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

file(GLOB_RECURSE MODEL_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Data/Models/*.obj")
set_source_files_properties(${MODEL_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(HelperLaneWindows PRIVATE ${MODEL_FILES})
target_source_group(HelperLaneWindows "Samples")

# Enable AVX2 on supported MSVC x64 targets, to enable compiler vectorizations. 
target_compile_options(HelperLaneWindows PRIVATE
  # Windows, x64 MSVC frontend (covers cl.exe and clang-cl) and not ARM
  $<$<AND:$<PLATFORM_ID:Windows>,$<CXX_COMPILER_ID:MSVC>,$<STREQUAL:${MSVC_CXX_ARCHITECTURE_ID},x64>>:/arch:AVX2>
  $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<CXX_COMPILER_ID:GNU>>:-mavx2 -mfma>
  $<$<AND:$<NOT:$<PLATFORM_ID:Windows>>,$<CXX_COMPILER_ID:Clang>>:-mavx2 -mfma>
)

# Record the actual configuration and source contents. Changes to a source trigger
# reconfiguration, so metadata describes what was compiled, including dirty edits.
get_target_property(_helper_sources HelperLaneWindows SOURCES)
list(APPEND _helper_sources "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Falcor/Core/API/GpuTimer.cpp")
set(_helper_source_manifest "")
foreach(_helper_source IN LISTS _helper_sources)
    if(NOT IS_ABSOLUTE "${_helper_source}")
        set(_helper_source "${CMAKE_CURRENT_SOURCE_DIR}/${_helper_source}")
    endif()
    if(EXISTS "${_helper_source}")
        file(SHA256 "${_helper_source}" _helper_hash)
        file(RELATIVE_PATH _helper_relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_helper_source}")
        string(APPEND _helper_source_manifest "${_helper_relative}:${_helper_hash}\n")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_helper_source}")
    endif()
endforeach()
string(SHA256 _helper_digest "${_helper_source_manifest}")
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _helper_revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
target_compile_definitions(HelperLaneWindows PRIVATE
    HELPERLANE_BUILD_CONFIG="$<CONFIG>"
    HELPERLANE_GIT_REVISION="${_helper_revision}"
    HELPERLANE_COMPILER_DESC="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}"
)
set_source_files_properties(Experiments/Benchmark.cpp
    PROPERTIES COMPILE_DEFINITIONS "HELPERLANE_SOURCE_DIGEST=\"${_helper_digest}\"")

# Deploy all OBJ presets beside the executable; no GraphEx runtime dependency.
add_custom_command(TARGET HelperLaneWindows POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Data/Models"
        "$<TARGET_FILE_DIR:HelperLaneWindows>/Data/HelperLaneWindows/Models"
)
