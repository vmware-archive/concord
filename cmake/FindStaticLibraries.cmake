# Currently, conc_genconfig (the Concord configuration generation utility) is
# statically linked so that it can be run while setting up and deploying Concord
# clusters from a machine that is not a Concord node without having to install
# any additional dependencies to that machine to run the conc_genconfig binary.
# As such, it needs to be linked with some static libraries, which we need to
# locate separately from the shared libraries since the static libraries are
# different files.

macro(find_library_and_check_found lib_var lib_file lib_desc)
   find_library(${lib_var} ${lib_file}
                HINTS ${CMAKE_INSTALL_PREFIX}
		      /usr/local/lib
		      /usr/lib/x86_64-linux-gnu)
   if(${${lib_var}} STREQUAL "${liv_bar}-NOTFOUND")
      message(SEND_ERROR "${lib_desc} not found")
   else()
      message(STATUS "${lib_desc} found at ${${lib_var}}")
   endif()
endmacro()

find_library_and_check_found(log4cplus_STATIC_LIBRARY liblog4cplus.a
                             "log4cplus static library")

find_library_and_check_found(boost_program_options_STATIC_LIBRARY
                             libboost_program_options.a
			     "Boost program options static library")

find_library_and_check_found(yaml-cpp_STATIC_LIBRARY libyaml-cpp.a
                             "yaml-cpp static library")

find_library_and_check_found(pthread_SHARED_LIBRARY libpthread.so
                             "pthread shared library")
