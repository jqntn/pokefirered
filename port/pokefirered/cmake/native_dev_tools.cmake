function(pfr_define_native_dev_tools)
  if(WIN32)
    set(PFR_CLANG_FORMAT
        "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe")
  else()
    find_program(PFR_CLANG_FORMAT NAMES clang-format REQUIRED)
  endif()

  file(
    GLOB_RECURSE PFR_FORMAT_FILES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp)
  file(
    GLOB PFR_FORMAT_FILES_TOP CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
  list(APPEND PFR_FORMAT_FILES ${PFR_FORMAT_FILES_TOP})

  add_custom_target(
    format-native
    COMMAND ${PFR_CLANG_FORMAT} -i --style=file:${CMAKE_CURRENT_SOURCE_DIR}/.clang-format ${PFR_FORMAT_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM)
endfunction()
