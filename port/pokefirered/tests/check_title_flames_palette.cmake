file(
  READ
  "${PFR_ASSET_ROOT}/graphics/title_screen/firered/flames.gbapal"
  TITLE_FLAMES_PAL
  HEX)
file(
  SIZE
  "${PFR_ASSET_ROOT}/graphics/title_screen/firered/flames.gbapal"
  TITLE_FLAMES_PAL_SIZE)

if(NOT TITLE_FLAMES_PAL_SIZE EQUAL 32)
  message(
    FATAL_ERROR "unexpected flames.gbapal size: ${TITLE_FLAMES_PAL_SIZE}")
endif()

function(check_palette_entry entry_index expected_hex)
  math(EXPR hex_offset "${entry_index} * 4")
  string(SUBSTRING "${TITLE_FLAMES_PAL}" "${hex_offset}" 4 actual_hex)

  if(NOT actual_hex STREQUAL "${expected_hex}")
    message(
      FATAL_ERROR
        "flames.gbapal entry ${entry_index} expected ${expected_hex}, got ${actual_hex}")
  endif()
endfunction()

check_palette_entry(0 "2a5b")
check_palette_entry(1 "0000")
check_palette_entry(7 "ce46")
check_palette_entry(8 "ec4e")
check_palette_entry(9 "0b53")
check_palette_entry(15 "2a5b")
