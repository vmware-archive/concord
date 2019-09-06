# Before monorepo, evmjit was located at ../evmjit. After monorepo,
# it's located at ../../evmjit ... except in Docker building, where
# it's still at ../evmjit. (TODO)
find_path(Evmjit_INCLUDE_DIR evmjit.h
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../evmjit/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../evmjit/include)

if(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(SEND_ERROR "Evmjit not found")
else(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
   message(STATUS "Evmjit found at " ${Evmjit_INCLUDE_DIR})
   set(Evmjit_FOUND 1)

   set(Evmjit_INCLUDE_DIRS ${Evmjit_INCLUDE_DIR})
   link_directories(${Evmjit_INCLUDE_DIR}/../build/libevmjit/
     ${Evmjit_INCLUDE_DIR}/../deps/lib/)
if(APPLE)
   set(Evmjit_LIBRARIES evmjit-standalone dl)
else()
   set(Evmjit_LIBRARIES evmjit-standalone-thin dl)
endif(APPLE)
endif(DEFINED EVM_INCLUDE_DIR_NOTFOUND)
