function(pfr_define_native_tests)
  pfr_add_port_test_executable(pfr_smoke tests/smoke.c)
  pfr_add_port_test_executable(pfr_integration tests/integration.c)
  pfr_add_port_test_executable(
    pfr_startup_frame_capture tests/startup_frame_capture.c)

  add_test(NAME pfr_smoke COMMAND pfr_smoke)
  add_test(
    NAME pfr_asset_title_border_bg
    COMMAND ${CMAKE_COMMAND}
            -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_border_bg.cmake")
  add_test(
    NAME pfr_asset_title_press_start_blink
    COMMAND ${CMAKE_COMMAND}
            -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_press_start_blink.cmake")
  add_test(
    NAME pfr_asset_title_flames_palette
    COMMAND ${CMAKE_COMMAND}
            -DPFR_ASSET_ROOT=${PFR_ASSET_ROOT}
            -P
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_title_flames_palette.cmake")
  add_test(
    NAME pfr_headless_boot
    COMMAND pokefirered --mode game --headless --frames 1600 --quit-on-title)
  add_test(
    NAME pfr_headless_main_menu
    COMMAND pokefirered
            --mode
            game
            --headless
            --frames
            2300
            --auto-press-start-frame
            1860
            --quit-on-main-menu)
  add_test(
    NAME pfr_headless_demo
    COMMAND pokefirered --mode demo --headless --frames 3)
  add_test(
    NAME pfr_headless_sandbox
    COMMAND pokefirered --mode sandbox --headless --frames 3)
  add_test(NAME pfr_integration COMMAND pfr_integration)

  set(PFR_STARTUP_FRAME_MANIFEST
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/startup_frame_manifest.txt")
  set(PFR_STARTUP_CAPTURE_OUTPUT_DIR
      "${CMAKE_CURRENT_BINARY_DIR}/test-output/startup-capture")
  set(PFR_STARTUP_CAPTURE_OUTPUT_MANIFEST
      "${PFR_STARTUP_CAPTURE_OUTPUT_DIR}/capture_manifest.txt")

  add_test(
    NAME pfr_startup_capture
    COMMAND pfr_startup_frame_capture
            --mode
            game
            --output-dir
            ${PFR_STARTUP_CAPTURE_OUTPUT_DIR}
            --manifest-out
            ${PFR_STARTUP_CAPTURE_OUTPUT_MANIFEST}
            --frame-manifest
            ${PFR_STARTUP_FRAME_MANIFEST})
endfunction()
