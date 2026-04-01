if(QMDILIB_USE_MOLD)
    if(UNIX AND NOT APPLE)  # mold is mainly for Linux
        find_program(MOLD_EXECUTABLE mold)

        if(MOLD_EXECUTABLE)
            message(STATUS "Mold linker found: ${MOLD_EXECUTABLE}")

            # Set Mold as the linker
            set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=mold")
            set(CMAKE_SHARED_LINKER_FLAGS "-fuse-ld=mold")
            set(CMAKE_MODULE_LINKER_FLAGS "-fuse-ld=mold")

            # Optionally set Mold as the default linker for all targets
            set_property(GLOBAL PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "Mold linker not found. Falling back to default linker.")
        endif()
    endif()
endif()
