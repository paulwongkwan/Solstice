# common target definitions
# this file will also load platform specific macros

add_executable(solstice ${SOLSTICE_TARGET_FILES})
foreach(dep ${SOLSTICE_TARGET_DEPENDENCIES})
    add_dependencies(solstice ${dep})  # compile these before solstice
endforeach()

# platform specific target definitions
include(${CMAKE_MODULE_PATH}/targets/windows.cmake)

target_link_libraries(solstice ${SOLSTICE_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(solstice PUBLIC ${SOLSTICE_DEFINITIONS})

target_compile_options(solstice PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${SOLSTICE_COMPILE_OPTIONS}>)
target_link_options(solstice PRIVATE ${SOLSTICE_LINK_OPTIONS})

set(NPM_SOURCE_ASSETS_DIR ${SOLSTICE_SOURCE_ASSETS_DIR})
set(NPM_ASSETS_DIR ${CMAKE_BINARY_DIR})

#WebUI build
find_program(NPM npm REQUIRED)

get_filename_component(NPM_DIRECTORY "${NPM}" DIRECTORY)
find_program(NPM_NODE_EXECUTABLE NAMES node node.exe HINTS "${NPM_DIRECTORY}" NO_DEFAULT_PATH NO_CACHE REQUIRED)
string(CONCAT NPM_NODE_GNU_BINDING_CHECK
        "process.arch === 'x64' && "
        "(process.config.variables.shlib_suffix === 'dll.a' || "
        "process.config.variables.node_target_type === 'shared_library')")
execute_process(
        COMMAND "${NPM_NODE_EXECUTABLE}" -p "${NPM_NODE_GNU_BINDING_CHECK}"
        OUTPUT_VARIABLE NPM_NODE_USES_GNU_BINDING
        OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NPM_NODE_USES_GNU_BINDING STREQUAL "true")
    find_program(NATIVE_NPM NAMES npm.cmd npm HINTS "$ENV{ProgramFiles}/nodejs" NO_DEFAULT_PATH NO_CACHE)
    if(NOT NATIVE_NPM)
        message(FATAL_ERROR
                "The MSYS2 Node.js package is incompatible with Rolldown. "
                "Install native Windows Node.js or set NPM to its npm.cmd path.")
    endif()

    set(NPM "${NATIVE_NPM}" CACHE FILEPATH "Path to the npm executable" FORCE)
    get_filename_component(NPM_DIRECTORY "${NPM}" DIRECTORY)
    message(STATUS "MSYS2 Node.js is incompatible with Rolldown; using native npm: ${NPM}")
endif()

# Ninja wraps custom commands in cmd.exe. `call` keeps a quoted npm.cmd path intact when it contains spaces.
set(NPM_COMMAND cmd /C call)
set(NPM_PATH "PATH=${NPM_DIRECTORY};$ENV{PATH}")

set(NPM_INSTALL_FLAGS "--ignore-scripts")
if (NPM_OFFLINE)
    set(NPM_INSTALL_FLAGS "${NPM_INSTALL_FLAGS} --offline")
endif()

add_custom_target(web-ui ALL
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Installing NPM Dependencies and Building the Web UI"
        COMMAND "${CMAKE_COMMAND}" -E env "${NPM_PATH}" ${NPM_COMMAND} "${NPM}" ci ${NPM_INSTALL_FLAGS}
        COMMAND "${CMAKE_COMMAND}" -E env "${NPM_PATH}" "SOLSTICE_SOURCE_ASSETS_DIR=${NPM_SOURCE_ASSETS_DIR}" "SOLSTICE_ASSETS_DIR=${NPM_ASSETS_DIR}" ${NPM_COMMAND} "${NPM}" run build  # cmake-lint: disable=C0301
        VERBATIM)

# docs
if(BUILD_DOCS)
    include(third-party/dockle/cmake/Dockle.cmake)
    dockle_add_docs(docs TARGETS docs)
endif()

# tests
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# custom compile flags, must be after adding tests

if (NOT BUILD_TESTS)
    set(TEST_DIR "")
else()
    set(TEST_DIR "${CMAKE_SOURCE_DIR}/tests")
endif()

# src/upnp
set_source_files_properties("${CMAKE_SOURCE_DIR}/src/upnp.cpp"
        DIRECTORY "${CMAKE_SOURCE_DIR}" "${TEST_DIR}"
        PROPERTIES COMPILE_FLAGS -Wno-pedantic)

# src/nvhttp
string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
    if (NOT BUILD_TESTS)
        set_source_files_properties("${CMAKE_SOURCE_DIR}/src/nvhttp.cpp"
                DIRECTORY "${CMAKE_SOURCE_DIR}"
                PROPERTIES COMPILE_FLAGS -O2)
    else()
        set_source_files_properties("${CMAKE_SOURCE_DIR}/src/nvhttp.cpp"
                DIRECTORY "${CMAKE_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/tests"
                PROPERTIES COMPILE_FLAGS -O2)
    endif()
else()
    add_definitions(-DNDEBUG)
endif()
