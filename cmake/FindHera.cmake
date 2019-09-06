find_path(EVM_INCLUDE_DIR evm.h
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../hera)

if(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(SEND_ERROR "Hera not found")
else(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(STATUS "Hera found at " ${EVM_INCLUDE_DIR})
   set(Hera_FOUND 1)

   set(Hera_INCLUDE_DIRS ${EVM_INCLUDE_DIR} ${EVM_INCLUDE_DIR}/src)
   link_directories(${EVM_INCLUDE_DIR}/build/src
                    ${EVM_INCLUDE_DIR}/build/deps/lib
                    ${EVM_INCLUDE_DIR}/build/deps/src/binaryen-build/lib/)

   set(Hera_LIBRARIES hera wasm passes asmjs cfg ir support)
endif(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
