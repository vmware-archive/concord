find_path(yaml-cpp_INCLUDE_DIR yaml.h
          HINTS /usr/include/yaml-cpp
                /usr/local/include/yaml-cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/../yaml-cpp)

if(yaml-cpp_INCLUDE_DIR EQUAL yaml-cpp_INCLUDE_DIR-NOTFOUND)
   message(SEND_ERROR "yaml-cpp not found")
else()
   message(STATUS "yaml-cpp found at " ${yaml-cpp_INCLUDE_DIR})
   set(yaml-cpp_INCLUDE_DIRS ${yaml-cpp_INCLUDE_DIR})

   find_path(yaml-cpp_LIB_DIR libyaml-cpp.a libyaml.a
             HINTS /usr/lib/x86_64-linux-gnu/
                   /usr/local/lib/
             ${yaml-cpp_INCLUDE_DIR}/build/)
   if(NOT yaml-cpp_LIB_DIR)
      message(SEND_ERROR "yaml-cpp not found")
   else(NOT yaml-cpp_LIB_DIR)
      link_directories(${yaml-cpp_LIB_DIR})
      set(yaml-cpp_LIBRARIES yaml-cpp)
      set(yaml-cpp_FOUND 1)
   endif(NOT yaml-cpp_LIB_DIR)
endif(yaml-cpp_INCLUDE_DIR EQUAL yaml-cpp_INCLUDE_DIR-NOTFOUND)

