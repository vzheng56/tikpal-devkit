# The shared folders an app asks for (Yay 0.1.19 开发文档 2.4, 4.6): the project's app.json `permissions.folders`, read
# at build time, as voicecode_number.cmake reads voiceCode. A kind app.json does not write is one the app never reads.
# Plain CMake: CMakeLists.txt uses it in the build, test/run.sh runs it alone (cmake -P folders.cmake <app.json>).

# yk_folder_declared(<app.json> <override> <out>): <out> gets the kinds as bits (music 1, stories 2, pictures 4,
# documents 8), 0 for none (no app.json, no permissions.folders). A broken app.json, or a kind that is not one of the
# four (Recordings is none: 开发文档 2.1), stops the build: an image must never read what its app.json does not say, nor
# quietly less. <override>: -D YK_FOLDER_KINDS=<0..15> on a test build's command line (the bench, shared-test's
# `st folder`), in place of app.json; never in a release, whose kinds the review holds to its app.json.
function(yk_folder_declared json override out)
  set(mask 0)
  if(NOT "${override}" STREQUAL "")
    if(NOT override MATCHES "^[0-9]+$" OR override GREATER 15)
      message(FATAL_ERROR "YK_FOLDER_KINDS is ${override}: the four kinds' bits, 0..15")
    endif()
    set(mask "${override}")
  elseif(EXISTS "${json}")
    file(READ "${json}" text)
    string(JSON type ERROR_VARIABLE err TYPE "${text}")
    if(err OR NOT type STREQUAL "OBJECT")
      message(FATAL_ERROR "${json} is not a JSON object: ${err}")
    endif()
    string(JSON folders_type ERROR_VARIABLE err TYPE "${text}" permissions folders)
    if(NOT err)
      if(NOT folders_type STREQUAL "OBJECT")
        message(FATAL_ERROR "${json}: permissions.folders is an object, a kind and what it is for (app-json.md)")
      endif()
      string(JSON n LENGTH "${text}" permissions folders)
      if(n GREATER 0)
        math(EXPR last "${n} - 1")
        foreach(i RANGE ${last})
          string(JSON kind MEMBER "${text}" permissions folders ${i})
          if(kind STREQUAL "music")
            math(EXPR mask "${mask} | 1")
          elseif(kind STREQUAL "stories")
            math(EXPR mask "${mask} | 2")
          elseif(kind STREQUAL "pictures")
            math(EXPR mask "${mask} | 4")
          elseif(kind STREQUAL "documents")
            math(EXPR mask "${mask} | 8")
          else()
            message(FATAL_ERROR "${json}: permissions.folders has ${kind}: the kinds are music, stories, pictures, documents")
          endif()
        endforeach()
      endif()
    endif()
  endif()
  set(${out} "${mask}" PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
  yk_folder_declared("${CMAKE_ARGV3}" "${CMAKE_ARGV4}" mask)
  message("${mask}")
endif()
