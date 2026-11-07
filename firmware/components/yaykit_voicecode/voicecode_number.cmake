# An app's 应用码 number into its image (BOSS-2, 11-01: from app.json at build time, the app never passes it): the
# project's app.json voiceCode.number, or -D YK_VC_APP_NUMBER=<n> on the build's command line (a firmware without
# app.json, or a test number 65280..65535 tried on one's own round screen). Plain CMake: CMakeLists.txt uses it in the
# build, test/run.sh runs it alone (cmake -P voicecode_number.cmake <app.json> [override]).

# yk_vc_number(<app.json> <override> <out>): <out> gets the number, or "" when there is none (no app.json, or one
# without voiceCode). A broken app.json, a voiceCode without a number, or a number that is not 1..65535 stops the
# build: an image must never quietly carry no number or another one.
function(yk_vc_number json override out)
  set(n "")
  if(NOT "${override}" STREQUAL "")
    set(n "${override}")
    set(from "YK_VC_APP_NUMBER")
  elseif(EXISTS "${json}")
    file(READ "${json}" text)
    string(JSON type ERROR_VARIABLE err TYPE "${text}")
    if(err OR NOT type STREQUAL "OBJECT")
      message(FATAL_ERROR "${json} is not a JSON object: ${err}")
    endif()
    string(JSON code_type ERROR_VARIABLE err TYPE "${text}" voiceCode)
    if(NOT err)
      if(NOT code_type STREQUAL "OBJECT")
        message(FATAL_ERROR "${json}: voiceCode is an object with the app's number (devkit/docs/app-json.md)")
      endif()
      string(JSON number_type ERROR_VARIABLE err TYPE "${text}" voiceCode number)
      if(err OR NOT number_type STREQUAL "NUMBER")
        message(FATAL_ERROR "${json}: voiceCode has no number (devkit/docs/app-json.md)")
      endif()
      string(JSON n GET "${text}" voiceCode number)
    endif()
    set(from "${json} voiceCode.number")
  endif()
  if(NOT "${n}" STREQUAL "")
    if(NOT n MATCHES "^[1-9][0-9]*$" OR n GREATER 65535)
      message(FATAL_ERROR "${from} is ${n}: an app number is a whole number 1..65535")
    endif()
  endif()
  set(${out} "${n}" PARENT_SCOPE)
endfunction()

# Run alone: cmake -P voicecode_number.cmake <app.json> [override] prints "number=<n>" (empty for none).
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  set(override "")
  if(CMAKE_ARGC GREATER 4)
    set(override "${CMAKE_ARGV4}")
  endif()
  yk_vc_number("${CMAKE_ARGV3}" "${override}" n)
  message("number=${n}")
endif()
