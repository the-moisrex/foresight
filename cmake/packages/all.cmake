set(current_dir "${CMAKE_SOURCE_DIR}/cmake/packages/")

list(APPEND CMAKE_MODULE_PATH "${current_dir}")

include(threads)
if (IS_DEBUG)
    include(googletest)
    # include(fuzztest)
endif ()
include(libevdev)
