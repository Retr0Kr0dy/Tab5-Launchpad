idf_component_register(
  SRCS
    ${CMAKE_CURRENT_LIST_DIR}/../../src/konsole.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/line.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/cmd.c
    ${CMAKE_CURRENT_LIST_DIR}/../../src/npf.c
  INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../include
)
target_compile_definitions(${COMPONENT_LIB} PUBLIC KONSOLE_ENABLE_ANSI=1)
