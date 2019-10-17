find_path(EVM_INCLUDE_DIR evmc/evmc.h
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../evmone/evmc/include ${CMAKE_CURRENT_SOURCE_DIR}/../../evmone/evmc/include)

if(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(SEND_ERROR "Evmone not found")
else(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(STATUS "Evmone found at " ${EVM_INCLUDE_DIR})
   set(Evmone_FOUND 1)

   set(Evmonne_INCLUDE_DIRS ${EVM_INCLUDE_DIR} ${EVM_INCLUDE_DIR} ${EVM_INCLUDE_DIR}/../evmc/include)
   link_directories(${EVM_INCLUDE_DIR}/../../build/evmc/lib/loader)
   set(Evmone_LIBRARIES evmc-loader ${CMAKE_DL_LIBS})
endif(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
