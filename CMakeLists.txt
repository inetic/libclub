cmake_minimum_required (VERSION 3.2)
################################################################################
set(CMAKE_BUILD_TYPE Debug)
set(GLOB BOOST_VERSION 1.58)

################################################################################
project (club-lib)

find_package(Boost ${BOOST_VERSION} COMPONENTS REQUIRED)
find_package(Threads)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread -Wall")

include_directories(
  "${Boost_INCLUDE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src/club")

file(GLOB sources
  "${CMAKE_SOURCE_DIR}/src/club/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/club/net/*.cpp"
  "${CMAKE_SOURCE_DIR}/src/club/net/PL/*.cpp")

add_library(club ${sources})

################################################################################
project (club-tests)
find_package(Boost ${BOOST_VERSION} COMPONENTS system unit_test_framework REQUIRED)
find_package(Threads)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")

include_directories(
  "${Boost_INCLUDE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src/club")

file(GLOB sources "${CMAKE_SOURCE_DIR}/tests/*.cpp")

add_executable(club-tests ${sources})
target_link_libraries(club-tests ${Boost_LIBRARIES} club)

################################################################################
project (rendezvous-server)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost ${BOOST_VERSION} COMPONENTS system program_options REQUIRED)
find_package(Threads)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")

include_directories(
  "${Boost_INCLUDE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src/rendezvous")

file(GLOB sources "${CMAKE_SOURCE_DIR}/src/rendezvous/server.cpp")

add_executable(rendezvous-server ${sources})
target_link_libraries(rendezvous-server ${Boost_LIBRARIES})

################################################################################
project (rendezvous-client-server-tests)

set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost ${BOOST_VERSION} COMPONENTS system unit_test_framework REQUIRED)
find_package(Threads)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")

include_directories(
  "${Boost_INCLUDE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/src/rendezvous")

file(GLOB sources "${CMAKE_SOURCE_DIR}/src/rendezvous/tests/main.cpp")

add_executable(rendezvous-tests ${sources})
target_link_libraries(rendezvous-tests ${Boost_LIBRARIES})

################################################################################
project (club-chat)

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost ${BOOST_VERSION} COMPONENTS system program_options REQUIRED)
find_package(Threads)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -pthread")

include_directories(
  "${Boost_INCLUDE_DIR}"
  "${CMAKE_SOURCE_DIR}/include")

file(GLOB sources "${CMAKE_SOURCE_DIR}/demo/club-chat.cpp")

add_executable(club-chat ${sources})
target_link_libraries(club-chat ${Boost_LIBRARIES} club)

################################################################################
