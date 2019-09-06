# Before monorepo, googletest was located at ../googletest. After
# monorepo, it's located at ../../googletest ... except in Docker
# building, where it's still at ../googletest. (TODO)
find_path(GTEST_INCLUDE_DIR gtest/gtest.h
  HINTS ${CMAKE_CURRENT_SOURCE_DIR}/../googletest/googletest/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../googletest/googletest/include)

if(DEFINED GTEST_INCLUDE_DIR_NOTFOUND)
  message(SEND_ERROR "GTEST not found")
else(DEFINED GTEST_INCLUDE_DIR_FOUND)
  message(STATUS "GTEST found at " ${GTEST_INCLUDE_DIR})
  set(GTEST_FOUND 1)

  link_directories(
    ${GTEST_INCLUDE_DIR}/../../_build/googlemock/
    ${GTEST_INCLUDE_DIR}/../../_build/googlemock/gtest)

endif(DEFINED GTEST_INCLUDE_DIR_NOTFOUND)
