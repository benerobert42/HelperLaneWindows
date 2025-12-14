add_falcor_executable(HelperLaneWindows)

target_sources(HelperLaneWindows PRIVATE
    HelperLaneViz.cpp
    HelperLaneViz.h
    TriangulationHelpers.cpp
    TriangulationHelpers.h
    SVGLoader.cpp
    SVGLoader.h
    ShaderTypes.h
)

file(GLOB_RECURSE SHADER_FILES
	"${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.slang"
)
target_sources(HelperLaneWindows PRIVATE ${SHADER_FILES})
target_copy_shaders(HelperLaneWindows Samples/HelperLaneWindows)

# add_custom_command(TARGET HelperLaneWindows PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/data ${FALCOR_OUTPUT_DIRECTORY}/data)
# target_copy_shaders(HelperLaneWindows Samples/HelperLaneWindows)

target_source_group(HelperLaneWindows "Samples")
