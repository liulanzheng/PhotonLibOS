include_directories(${CMAKE_SOURCE_DIR}/include)
link_directories(${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
add_definitions(-w)

add_executable(test-tls test.cpp)
target_link_libraries(test-tls PRIVATE photon ${testing_libs})
add_test(NAME test-tls COMMAND $<TARGET_FILE:test-tls>)

add_executable(test-sasl test-sasl.cpp)
target_link_libraries(test-sasl PRIVATE photon ${testing_libs})
add_test(NAME test-sasl COMMAND $<TARGET_FILE:test-sasl>)
