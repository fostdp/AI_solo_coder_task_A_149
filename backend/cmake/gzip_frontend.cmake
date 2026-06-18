file(GLOB_RECURSE FRONTEND_ASSETS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/frontend
    frontend/*.html
    frontend/*.css
    frontend/js/*.js
    frontend/config/*.json
)

foreach(asset ${FRONTEND_ASSETS})
    set(src "${CMAKE_CURRENT_SOURCE_DIR}/frontend/${asset}")
    set(dst "${CMAKE_CURRENT_SOURCE_DIR}/frontend/${asset}.gz")

    if(src IS_NEWER_THAN dst OR NOT EXISTS dst)
        execute_process(
            COMMAND gzip -k -f -9 "${src}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/frontend
            RESULT_VARIABLE GZIP_RESULT
        )
        if(NOT GZIP_RESULT EQUAL 0)
            message(WARNING "Failed to gzip ${asset}")
        else()
            message(STATUS "  gzipped: ${asset}")
        endif()
    endif()
endforeach()

message(STATUS "Frontend assets compressed: ${CMAKE_CURRENT_SOURCE_DIR}/frontend/")
