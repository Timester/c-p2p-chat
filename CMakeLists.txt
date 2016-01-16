cmake_minimum_required(VERSION 3.3)

project(p2pchat C)

FIND_PACKAGE ( Threads REQUIRED )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c99")

set(PEER_SOURCE_FILES peer.c protocol.h)
set(TRACKER_SOURCE_FILES tracker.c protocol.h uthash.h)

add_executable(tracker ${TRACKER_SOURCE_FILES})
add_executable(peer ${PEER_SOURCE_FILES})

TARGET_LINK_LIBRARIES ( tracker ${CMAKE_THREAD_LIBS_INIT} )
TARGET_LINK_LIBRARIES ( peer ${CMAKE_THREAD_LIBS_INIT} )
