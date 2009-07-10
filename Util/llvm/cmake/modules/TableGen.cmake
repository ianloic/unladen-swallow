# LLVM_TARGET_DEFINITIONS must contain the name of the .td file to process.
# Extra parameters for `tblgen' may come after `ofn' parameter.
# Adds the name of the generated file to TABLEGEN_OUTPUT.

macro(tablegen ofn)
  file(GLOB all_tds "*.td")

  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${ofn}.tmp
    COMMAND ${LLVM_TABLEGEN_EXE} ${ARGN} -I ${CMAKE_CURRENT_SOURCE_DIR}
    -I ${LLVM_MAIN_SRC_DIR}/lib/Target -I ${LLVM_MAIN_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/${LLVM_TARGET_DEFINITIONS} 
    -o ${CMAKE_CURRENT_BINARY_DIR}/${ofn}.tmp
    DEPENDS tblgen ${all_tds}
    COMMENT "Building ${ofn}.tmp..."
    )
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${ofn}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different 
    ${CMAKE_CURRENT_BINARY_DIR}/${ofn}.tmp ${CMAKE_CURRENT_BINARY_DIR}/${ofn}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${ofn}.tmp
    COMMENT "Building ${ofn}..."
    )
  set(TABLEGEN_OUTPUT ${TABLEGEN_OUTPUT} ${CMAKE_CURRENT_BINARY_DIR}/${ofn})
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${ofn} 
    PROPERTIES GENERATED 1)
endmacro(tablegen)
