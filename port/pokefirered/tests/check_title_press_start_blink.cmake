file(
  READ
  "${PFR_ASSET_ROOT}/graphics/title_screen/copyright_press_start.4bpp"
  TITLE_COPYRIGHT_GFX
  HEX)
file(
  SIZE
  "${PFR_ASSET_ROOT}/graphics/title_screen/copyright_press_start.4bpp"
  TITLE_COPYRIGHT_GFX_SIZE)

if(NOT TITLE_COPYRIGHT_GFX_SIZE EQUAL 2048)
  message(
    FATAL_ERROR
      "unexpected copyright_press_start.4bpp size: ${TITLE_COPYRIGHT_GFX_SIZE}")
endif()

function(get_tile_indices tile_index out_var)
  math(EXPR byte_offset "${tile_index} * 32")
  math(EXPR hex_offset "${byte_offset} * 2")
  set(tile_indices)

  foreach(byte_index RANGE 0 31)
    math(EXPR nibble_offset "${hex_offset} + (${byte_index} * 2)")
    string(SUBSTRING "${TITLE_COPYRIGHT_GFX}" "${nibble_offset}" 2 byte_hex)
    math(EXPR byte_value "0x${byte_hex}")
    math(EXPR low_nibble "${byte_value} & 0x0F")
    math(EXPR high_nibble "(${byte_value} >> 4) & 0x0F")
    list(APPEND tile_indices "${low_nibble}" "${high_nibble}")
  endforeach()

  list(REMOVE_DUPLICATES tile_indices)
  set(${out_var} "${tile_indices}" PARENT_SCOPE)
endfunction()

function(assert_tile_group group_name required_indices forbidden_indices)
  foreach(tile_index IN LISTS ARGN)
    get_tile_indices(${tile_index} tile_indices)
    set(has_required FALSE)

    foreach(required_index IN LISTS required_indices)
      list(FIND tile_indices "${required_index}" found_index)
      if(NOT found_index EQUAL -1)
        set(has_required TRUE)
      endif()
    endforeach()

    if(NOT has_required)
      message(
        FATAL_ERROR
          "${group_name} tile ${tile_index} is missing required palette indices: ${tile_indices}")
    endif()

    foreach(forbidden_index IN LISTS forbidden_indices)
      list(FIND tile_indices "${forbidden_index}" found_index)
      if(NOT found_index EQUAL -1)
        message(
          FATAL_ERROR
            "${group_name} tile ${tile_index} uses forbidden palette index ${forbidden_index}: ${tile_indices}")
      endif()
    endforeach()
  endforeach()
endfunction()

set(PRESS_START_REQUIRED 1 2 3 4 5)
set(PRESS_START_FORBIDDEN 7 8 9 10)
set(COPYRIGHT_REQUIRED 7 8 9 10)
set(COPYRIGHT_FORBIDDEN 1 2 3 4 5)

assert_tile_group(
  "press_start"
  "${PRESS_START_REQUIRED}"
  "${PRESS_START_FORBIDDEN}"
  21
  22
  23
  24
  25
  26
  53
  54
  55
  56
  57
  58)
assert_tile_group(
  "copyright"
  "${COPYRIGHT_REQUIRED}"
  "${COPYRIGHT_FORBIDDEN}"
  33
  34
  35
  36
  37
  38
  39
  40
  41
  42
  43
  44
  45
  46
  47
  48
  49
  50
  51
  52)
