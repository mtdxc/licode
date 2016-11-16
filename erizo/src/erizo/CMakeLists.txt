cmake_minimum_required(VERSION 2.6)

project (ERIZO)

set(ERIZO_VERSION_MAJOR 0)
set(ERIZO_VERSION_MINOR 1)
IF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
set(CMAKE_CXX_FLAGS "-g -Wall -std=c++11 -DWEBRTC_WIN ${ERIZO_CMAKE_CXX_FLAGS}")
ELSE()
set(CMAKE_CXX_FLAGS "-g -Wall -std=c++11 -DWEBRTC_POSIX ${ERIZO_CMAKE_CXX_FLAGS}")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

include_directories("${ERIZO_SOURCE_DIR}" "${THIRD_PARTY_INCLUDE}" "${NICER_INCLUDE}")

file(GLOB_RECURSE ERIZO_SOURCES "${ERIZO_SOURCE_DIR}/*.h" "${ERIZO_SOURCE_DIR}/*.c" "${ERIZO_SOURCE_DIR}/*.cpp" "${ERIZO_SOURCE_DIR}/*.cc")

add_library(erizo SHARED ${ERIZO_SOURCES})

#message("Libs ${SRTP} ${NICE} ${GTHREAD} ${SSL} ${CRYPTO} ${LIBS} ${LOG}")

target_link_libraries(erizo ${GLIB_LIBRARIES} ${Boost_LIBRARIES} ${SRTP} ${NICE} ${GTHREAD} ${SSL} ${CRYPTO} ${LIBS} ${LOG} webrtc nicer nrappkit)
