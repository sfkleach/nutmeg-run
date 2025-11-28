# Variables
build-dir := "_build"
binary := build-dir + "/MyApp"

[private]
default:
    @just ---list

# Build the project (Debug)
build:
    @mkdir -p {{build-dir}}
    @cd {{build-dir}} && conan install .. --output-folder=. --build=missing -s build_type=Debug -v quiet
    # CMake warns about unused CMAKE_TOOLCHAIN_FILE on cached builds. Only pass it on first configure.
    @cd {{build-dir}} && [ -f CMakeCache.txt ] && cmake .. -DCMAKE_BUILD_TYPE=Debug -Wno-dev --log-level=ERROR || cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Debug -Wno-dev --log-level=ERROR
    @cd {{build-dir}} && cmake --build . --config Debug -- -j $(nproc) --quiet

rebuild: clean build

# Release build (optimized)
release:
    @mkdir -p {{build-dir}}
    @cd {{build-dir}} && conan install .. --output-folder=. --build=missing -s build_type=Release -v quiet
    # CMake warns about unused CMAKE_TOOLCHAIN_FILE on cached builds. Only pass it on first configure.
    @cd {{build-dir}} && [ -f CMakeCache.txt ] && cmake .. -DCMAKE_BUILD_TYPE=Release -Wno-dev --log-level=ERROR || cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -Wno-dev --log-level=ERROR
    @cd {{build-dir}} && cmake --build . --config Release -- -j $(nproc) --quiet

rerelease: clean release

# Run the built binary (Debug)
run:
    just build
    {{binary}}

# Run the built binary (Release)
run-release:
    just release
    {{binary}}

# Clear build artifacts
clean:
    rm -rf {{build-dir}}
    rm -f CMakeUserPresets.json
    rm -f conan*.cmake conan*.sh deactivate_*.sh *.cmake CMakePresets.json

# Run tests
test:
    just build
    cd {{build-dir}} && ctest --output-on-failure

# Add a package interactively
add package-name:
    @read -p "Enter package name: " pkg
    echo "$pkg" >> conanfile.txt

# Remove a package interactively
remove package-name:
    @read -p "Enter package name to remove: " pkg
    sed -i "/$pkg/d" conanfile.txt

# Jumpstart (install prerequisites for Debian)
jumpstart:
    sudo apt-get update
    sudo apt-get install -y cmake g++ pipx
    @if ! command -v conan &> /dev/null; then \
        echo "Installing conan..."; \
        pipx install conan; \
    else \
        echo "conan is already installed"; \
    fi
    @if [ ! -f "$$HOME/.conan2/profiles/default" ]; then \
        echo "Creating default conan profile..."; \
        conan profile detect --force; \
    else \
        echo "conan default profile already exists"; \
    fi
