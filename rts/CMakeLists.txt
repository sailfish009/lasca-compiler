add_library(objlib OBJECT runtime.c builtin.c lasca.h)
add_library (lascart SHARED $<TARGET_OBJECTS:objlib>)
add_library (lascartStatic  $<TARGET_OBJECTS:objlib>)
set_target_properties(lascartStatic PROPERTIES OUTPUT_NAME lascart)
set(CMAKE_SHARED_LIBRARY_SUFFIX ".so")

find_path(FFI_INCLUDE_PATH ffi.h PATHS ${FFI_INCLUDE_DIR})

if( EXISTS "${FFI_INCLUDE_PATH}/ffi.h" )
    message("Found lib ffi in ${FFI_INCLUDE_PATH}")
else()
    find_path(FFI_INCLUDE_PATH ffi/ffi.h PATHS ${FFI_INCLUDE_DIR})
    if( EXISTS "${FFI_INCLUDE_PATH}/ffi/ffi.h" )
        message("Searching ffi.h ${FFI_INCLUDE_PATH}")
        set(FFI_INCLUDE_PATH "${FFI_INCLUDE_PATH}/ffi" CACHE INTERNAL "")
    endif()
endif()

message("Found ffi.h in ${FFI_INCLUDE_PATH}")

find_library(FFI_LIBRARY ffi PATHS ${FFI_LIBRARY_DIR})
if( NOT FFI_LIBRARY )
    message(FATAL_ERROR "libffi is not found.")
endif()


find_path(GC_INCLUDE_PATH gc.h PATHS ${GC_INCLUDE_DIR})
find_library(GC_LIBRARY gc PATHS ${GC_LIBRARY_DIR})
message("Found gc.h in ${GC_INCLUDE_PATH}")

target_include_directories(objlib PRIVATE ${GC_INCLUDE_PATH} ${FFI_INCLUDE_PATH} )

target_link_libraries(lascart m ${GC_LIBRARY} ${FFI_LIBRARY})
target_link_libraries(lascartStatic m ${GC_LIBRARY} ${FFI_LIBRARY})

install(TARGETS lascart LIBRARY DESTINATION lib)