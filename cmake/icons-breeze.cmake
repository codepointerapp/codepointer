include(ExternalProject)

function(download_breeze_icons VERSION)
    set(URL "https://github.com/KDE/breeze-icons/archive/refs/tags/v${VERSION}.zip")
    set(ZIP_FILE "${CMAKE_BINARY_DIR}/breeze-icons-${VERSION}.zip")
    set(EXTRACT_DIR "${CMAKE_BINARY_DIR}/breeze-icons-${VERSION}")
    set(breeze_icons_install_dir "${CMAKE_BINARY_DIR}/share/icons/breeze")

    file(DOWNLOAD "${URL}" "${ZIP_FILE}" SHOW_PROGRESS INACTIVITY_TIMEOUT 10 STATUS download_result)
    list(GET download_result 0 status_code)
    list(GET download_result 1 error_message)
    if (NOT status_code EQUAL 0)
        file(REMOVE "${ZIP_FILE}")
        message(FATAL_ERROR "Failed to download ${URL}: ${error_message}")
    endif()

    file(ARCHIVE_EXTRACT INPUT "${ZIP_FILE}" DESTINATION "${CMAKE_BINARY_DIR}")
    file(COPY
       "${CMAKE_BINARY_DIR}/breeze-icons-${VERSION}/icons/"
        DESTINATION "${CMAKE_BINARY_DIR}/share/icons/breeze/"
    )
endfunction()
