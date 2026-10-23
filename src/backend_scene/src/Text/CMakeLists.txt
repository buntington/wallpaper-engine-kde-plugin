set(LIB_NAME wpText)

pkg_check_modules(FONTCONFIG REQUIRED fontconfig)

add_library(${LIB_NAME}
STATIC
TextLayers.cpp
)

target_link_libraries(${LIB_NAME}
PUBLIC
    wpUtils
PRIVATE
    wpScene
    ${FONTCONFIG_LIBRARIES}
)
target_include_directories(${LIB_NAME} PRIVATE ${FONTCONFIG_INCLUDE_DIRS})
target_compile_options(${LIB_NAME} PRIVATE ${warn_opts})
set_property(TARGET ${LIB_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
