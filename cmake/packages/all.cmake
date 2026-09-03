set(current_dir "${CMAKE_CURRENT_LIST_DIR}/")

list(APPEND CMAKE_MODULE_PATH "${current_dir}")

include(threads)
if (IS_DEBUG)
    include(googletest)
    # include(fuzztest)
endif ()
include(google_benchmark)
include(libevdev)
