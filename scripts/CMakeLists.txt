# Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

find_program(
  CLANG_FORMAT_EXE
  clang-format
  DOC "Path to clang-format"
)
find_program(
  FORMAT_CODE_CONCORD_SCRIPT
  format-code-concord.sh
  PATH ${CMAKE_SOURCE_DIR}/scripts/
  NO_DEFAULT_PATH
  DOC "Path to format-code-concord.sh"
)
if(CLANG_FORMAT_EXE)
  if(FORMAT_CODE_CONCORD_SCRIPT)
    add_custom_target(format-check-concord ALL
      COMMENT "Check code format (concord) via ${FORMAT_CODE_CONCORD_SCRIPT}"
      COMMAND ${FORMAT_CODE_CONCORD_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR}/.. --is-required)
    add_custom_target(format-concord
      COMMENT "Change code format (concord) via ${FORMAT_CODE_CONCORD_SCRIPT}"
      COMMAND ${FORMAT_CODE_CONCORD_SCRIPT} ${CMAKE_CURRENT_SOURCE_DIR}/..)
  else()
    message("Couldn't find format-code-concord.sh")
  endif()
else()
  message("Couldn't find clang-format")
endif()
