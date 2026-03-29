file(READ "${PFR_ASSET_ROOT}/graphics/title_screen/border_bg.4bpp" BORDER_BG HEX)
file(SIZE "${PFR_ASSET_ROOT}/graphics/title_screen/border_bg.4bpp" BORDER_BG_SIZE)

if(NOT BORDER_BG_SIZE EQUAL 128)
  message(FATAL_ERROR "unexpected border_bg.4bpp size: ${BORDER_BG_SIZE}")
endif()

function(check_byte byte_offset expected_hex)
  math(EXPR hex_offset "${byte_offset} * 2")
  string(SUBSTRING "${BORDER_BG}" "${hex_offset}" 2 actual_hex)

  if(NOT actual_hex STREQUAL "${expected_hex}")
    message(
      FATAL_ERROR
        "border_bg.4bpp byte ${byte_offset} expected ${expected_hex}, got ${actual_hex}")
  endif()
endfunction()

check_byte(0 "ff")
check_byte(31 "ff")
check_byte(32 "66")
check_byte(63 "66")
check_byte(88 "bb")
check_byte(127 "bb")
