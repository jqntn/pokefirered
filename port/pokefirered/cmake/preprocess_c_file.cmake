if(NOT DEFINED PFR_PREPROC)
  message(FATAL_ERROR "PFR_PREPROC is required")
endif()

if(NOT DEFINED PFR_SOURCE)
  message(FATAL_ERROR "PFR_SOURCE is required")
endif()

if(NOT DEFINED PFR_CHARMAP)
  message(FATAL_ERROR "PFR_CHARMAP is required")
endif()

if(NOT DEFINED PFR_OUTPUT)
  message(FATAL_ERROR "PFR_OUTPUT is required")
endif()

if(NOT DEFINED PFR_WORKING_DIRECTORY)
  message(FATAL_ERROR "PFR_WORKING_DIRECTORY is required")
endif()

execute_process(
  COMMAND "${PFR_PREPROC}" "${PFR_SOURCE}" "${PFR_CHARMAP}"
  WORKING_DIRECTORY "${PFR_WORKING_DIRECTORY}"
  OUTPUT_FILE "${PFR_OUTPUT}"
  RESULT_VARIABLE pfr_result)

if(NOT pfr_result EQUAL 0)
  message(FATAL_ERROR "preproc failed for ${PFR_SOURCE}")
endif()
