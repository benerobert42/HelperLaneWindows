add_falcor_executable(HelperLaneViz)

target_sources(HelperLaneViz PRIVATE
    HelperLaneViz.cpp
    HelperLaneViz.h
    TriangulationHelpers.cpp
    TriangulationHelpers.h
    SVGLoader.cpp
    SVGLoader.h
    ShaderTypes.h
)

target_copy_shaders(HelperLaneViz Samples/HelperLaneViz)

target_source_group(HelperLaneViz "Samples")
