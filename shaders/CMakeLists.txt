# Shader compilation using glslc from shaderc
find_program(GLSLC glslc HINTS ${shaderc_DIR}/../../tools/shaderc)

if(NOT GLSLC)
    message(FATAL_ERROR "glslc not found — required for shader compilation")
endif()

set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})

set(SHADERS
    sprite.vert
    sprite.frag
    blit.vert
    blit.frag
)

set(SPIRV_OUTPUTS "")
foreach(SHADER ${SHADERS})
    set(SHADER_SRC ${SHADER_SOURCE_DIR}/${SHADER})
    set(SHADER_SPV ${SHADER_BINARY_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_SPV}
        COMMAND ${GLSLC} ${SHADER_SRC} -o ${SHADER_SPV}
        DEPENDS ${SHADER_SRC}
        COMMENT "Compiling shader ${SHADER}"
    )
    list(APPEND SPIRV_OUTPUTS ${SHADER_SPV})
endforeach()

add_custom_target(xebble_shaders ALL DEPENDS ${SPIRV_OUTPUTS})
