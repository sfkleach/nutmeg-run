cmake_minimum_required(VERSION 3.15)
project(nutmeg-run LANGUAGES CXX)

# Export compile commands for IDE support.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Conan integration (Conan 2)
find_package(fmt REQUIRED)
find_package(Catch2 REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json REQUIRED)

# Collect sources
file(GLOB SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt SQLite::SQLite3 nlohmann_json::nlohmann_json)

# Enable testing
enable_testing()
file(GLOB TEST_SOURCES "${CMAKE_SOURCE_DIR}/tests/*.cpp")
# Include source files except main.cpp for testing (defensive check to avoid linking main() twice).
file(GLOB TEST_LIB_SOURCES "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER TEST_LIB_SOURCES EXCLUDE REGEX ".*main\\.cpp$")
add_executable(tests ${TEST_SOURCES} ${TEST_LIB_SOURCES})
target_compile_features(tests PRIVATE cxx_std_20)
target_link_libraries(tests PRIVATE fmt::fmt SQLite::SQLite3 nlohmann_json::nlohmann_json Catch2::Catch2WithMain)
add_test(NAME AllTests COMMAND tests)
