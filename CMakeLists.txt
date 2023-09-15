cmake_minimum_required(VERSION 3.5.1)

project(RecognizeClient C CXX)

if (BUILD_DEBUG)
  message("Debug Symbols are configured!")
  set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_STANDARD 14)

include(cmake/common.cmake)

set(Glog_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
find_package(Glog REQUIRED)
INCLUDE_DIRECTORIES(${GLOG_INCLUDE_DIR})

if(NOT TARGET absl::strings)
  find_package(absl CONFIG)
endif()
if(NOT TARGET utf8_range)
  find_package(utf8_range CONFIG)
endif()

# Proto file
get_filename_component(rg_proto "proto/RecognizeService.proto" ABSOLUTE)
get_filename_component(rg_proto_path "${rg_proto}" PATH)

# Generated sources
set(rg_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/RecognizeService.pb.cc")
set(rg_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/RecognizeService.pb.h")
set(rg_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/RecognizeService.grpc.pb.cc")
set(rg_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/RecognizeService.grpc.pb.h")
add_custom_command(
      OUTPUT "${rg_proto_srcs}" "${rg_proto_hdrs}" "${rg_grpc_srcs}" "${rg_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}"
        -I "${rg_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${rg_proto}"
      DEPENDS "${rg_proto}")

# Include generated *.pb.h files
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories (${GLOG_INCLUDE_DIRS})

# rg_grpc_proto
add_library(rg_grpc_proto
  ${rg_grpc_srcs}
  ${rg_grpc_hdrs}
  ${rg_proto_srcs}
  ${rg_proto_hdrs})
target_link_libraries(rg_grpc_proto
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})

foreach(_target
  recognize_client
  recognize_client_async
  )
  add_executable(${_target}
    "${_target}.cc"
    "audio_source.cc"
    "file_audio_source.cc")

  target_link_libraries(${_target}
    rg_grpc_proto
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})
  target_link_libraries (${_target} glog)
endforeach()

