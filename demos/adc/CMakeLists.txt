include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_chibios_executable(adc
    chconf.h halconf.h mcuconf.h
    main.cc ${PHOBOS_SRC})
