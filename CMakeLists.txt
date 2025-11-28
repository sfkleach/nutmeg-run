cmake_minimum_required(VERSION 3.15)
project(MyApp LANGUAGES CXX)

# Conan integration (Conan 2)
find_package(fmt REQUIRED)
find_package(Catch2 REQUIRED)

# Collect sources
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

# Enable testing
enable_testing()
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
add_executable(tests ${TEST_SOURCES})
target_compile_features(tests PRIVATE cxx_std_17)
target_link_libraries(tests PRIVATE fmt::fmt Catch2::Catch2WithMain)
add_test(NAME AllTests COMMAND tests)
