file(GLOB UNIT_TESTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_*.c)

# In-memory pio backend, linked only into unit tests.
add_library(pio_fake STATIC ${CMAKE_SOURCE_DIR}/src/pio/pio_fake.c)
target_link_libraries(pio_fake minivr_dp)

foreach(src ${UNIT_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} pio_fake minivr_dp)
  add_test(NAME ${name} COMMAND ${name})
endforeach()
