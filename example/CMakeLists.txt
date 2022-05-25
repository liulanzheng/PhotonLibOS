include_directories(${CMAKE_SOURCE_DIR}/include)
link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

add_executable(simple-example simple-example.cpp)
target_link_libraries(simple-example PRIVATE photon)

add_executable(rpc-example-client rpc-example/client.cpp rpc-example/client_main.cpp)
target_link_libraries(rpc-example-client PRIVATE photon ${GFLAGS_LIBRARIES})

add_executable(rpc-example-server rpc-example/server.cpp rpc-example/server_main.cpp)
target_link_libraries(rpc-example-server PRIVATE photon ${GFLAGS_LIBRARIES})