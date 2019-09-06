file(GLOB concord_storage_src "*.cpp" "*.hpp")

add_library(concord_storage ${concord_storage_src})

get_property(thresh_include GLOBAL PROPERTY thresh_include_folder)
target_include_directories(concord_storage PUBLIC ${thresh_include})

target_link_libraries(concord_storage PUBLIC corebft concord_consensus)

if(USE_ROCKSDB)
find_library(ROCKSDB rocksdb)
find_library(LIBBZ2 bz2)
find_library(LIBLZ4 lz4)
find_library(LIBZSTD zstd)
find_library(LIBZ z)
find_library(LIBSNAPPY snappy)
target_compile_definitions(concord_storage PRIVATE USE_ROCKSDB=1)
target_link_libraries(concord_storage PUBLIC ${ROCKSDB} ${LIBBZ2} ${LIBLZ4} ${LIBZSTD} ${LIBZ} ${LIBSNAPPY})
endif()

target_compile_definitions(concord_storage PUBLIC __BASE=1 SPARSE_STATE=1 USE_TLS=1)
if(${BUILD_COMM_TCP_TLS})
    target_compile_definitions(concord_storage PUBLIC USE_COMM_TLS_TCP)
endif()
