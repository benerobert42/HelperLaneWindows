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
    TriangulationHelpers.cpp
    TriangulationHelpers.h
    SVGLoader.cpp
    SVGLoader.h
    ShaderTypes.h
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle/triangle.cpp
)

# Third-party headers (HEADER-ONLY)
target_include_directories(HelperLaneWindows PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/earcut.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/eigen
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/libigl/include
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/triangle
    ${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty
)

# Shaders
file(GLOB_RECURSE SHADER_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang"
)
target_sources(HelperLaneWindows PRIVATE ${SHADER_FILES})
target_copy_shaders(HelperLaneWindows Samples/HelperLaneWindows)

target_source_group(HelperLaneWindows "Samples")
