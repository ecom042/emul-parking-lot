# Coverage builds only (CONFIG_COVERAGE_GCOV, see scripts/grading/gcov.py).
if(CONFIG_COVERAGE_GCOV)
  zephyr_library()
  zephyr_library_sources(dump_irq.c)

  # Zephyr instruments every library (zephyr_interface). Instrument only the
  # application, so the kernel and drivers keep their timing and the dump
  # stays small: at the end of the application's CMakeLists, move the flags
  # from zephyr_interface to the app target.
  function(course_gcov_app_only)
    get_property(options TARGET zephyr_interface PROPERTY INTERFACE_COMPILE_OPTIONS)
    list(REMOVE_ITEM options "$<TARGET_PROPERTY:compiler,coverage>")
    set_property(TARGET zephyr_interface PROPERTY INTERFACE_COMPILE_OPTIONS "${options}")
    target_compile_options(app PRIVATE $<TARGET_PROPERTY:compiler,coverage>)
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL course_gcov_app_only)
endif()
