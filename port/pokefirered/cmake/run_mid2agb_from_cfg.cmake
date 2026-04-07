if(NOT DEFINED PFR_MID2AGB)
  message(FATAL_ERROR "PFR_MID2AGB is required")
endif()

if(NOT DEFINED PFR_MIDI_CFG)
  message(FATAL_ERROR "PFR_MIDI_CFG is required")
endif()

if(NOT DEFINED PFR_MIDI_FILE)
  message(FATAL_ERROR "PFR_MIDI_FILE is required")
endif()

if(NOT DEFINED PFR_OUTPUT_FILE)
  message(FATAL_ERROR "PFR_OUTPUT_FILE is required")
endif()

if(NOT DEFINED PFR_REPO_ROOT)
  message(FATAL_ERROR "PFR_REPO_ROOT is required")
endif()

get_filename_component(midi_name "${PFR_MIDI_FILE}" NAME)
file(STRINGS "${PFR_MIDI_CFG}" midi_cfg_lines)

set(midi_cfg_line "")
foreach(line IN LISTS midi_cfg_lines)
  string(FIND "${line}" "${midi_name}:" line_prefix_pos)
  if(line_prefix_pos EQUAL 0)
    set(midi_cfg_line "${line}")
    break()
  endif()
endforeach()

if(midi_cfg_line STREQUAL "")
  message(FATAL_ERROR "No midi.cfg entry found for ${midi_name}")
endif()

string(REGEX REPLACE "^[^:]+:[ \t]*" "" midi_args_string "${midi_cfg_line}")
set(midi_args)
if(NOT midi_args_string STREQUAL "")
  separate_arguments(midi_args NATIVE_COMMAND "${midi_args_string}")
endif()

execute_process(
  COMMAND "${PFR_MID2AGB}" "${PFR_MIDI_FILE}" "${PFR_OUTPUT_FILE}" ${midi_args}
  WORKING_DIRECTORY "${PFR_REPO_ROOT}"
  COMMAND_ERROR_IS_FATAL ANY)
