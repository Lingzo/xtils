# Installation Guide for xtils

This guide explains how to install and use the xtils library in your own projects.

## Building and Installing xtils

### Prerequisites
- CMake 4.0 or higher
- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)

### Build and Install Steps

1. Clone or download the xtils source code
2. Create a build directory and configure:
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
```

3. Build the library:
```bash
make -j$(nproc)
```

4. Install the library (may require sudo):
```bash
make install
```

This will install:
- Library files to `/usr/local/lib/`
- Header files to `/usr/local/include/`
- CMake config files to `/usr/local/lib/cmake/xtils/`

### Custom Installation Prefix

To install to a custom location:
```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/your/install/dir
make install
```

## Using xtils in Your Project

### Method 1: Using find_package (Recommended)

In your project's `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.12)
project(MyProject)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find xtils
find_package(xtils REQUIRED)

# Create your executable
add_executable(my_app main.cpp)

# Link against xtils
target_link_libraries(my_app PRIVATE xtils::xtils)
```

### Method 2: Using directly

```bash
g++ -std=c++17 main.cpp -I/path/xtils/include -L/path/xitls/lib/ -lxtils -o my_app
```

### Example Usage in Code

```cpp
#include <xtils/app/service.h>
#include <iostream>

int main(int argc, char** argv) {
    // Example usage of xtils functionality
    // (Add specific examples based on your library's API)

    std::cout << "Using xtils library successfully!" << std::endl;
    // setup_srv(s);
    run_srv(argc,argv);
    return 0;
}
```

or

```cpp
#include <xtils/app/service.h>

// no need main, auto call by framework
void app_main(int argc, char** argv) {
    // setup_srv(s);
}
```

## Troubleshooting

### CMake can't find xtils

If `find_package(xtils)` fails, you may need to help CMake find the installation:

```bash
# Option 1: Set CMAKE_PREFIX_PATH
cmake .. -DCMAKE_PREFIX_PATH=/path/to/xtils/install

# Option 2: Set xtils_DIR directly
cmake .. -Dxtils_DIR=/path/to/xtils/install/lib/cmake/xtils
```

### Custom Installation Location

If you installed xtils to a custom location, make sure to:

1. Add the installation path to your `CMAKE_PREFIX_PATH` environment variable
2. Or set `xtils_DIR` to point to the cmake config directory
3. For runtime, you may need to add the lib directory to `LD_LIBRARY_PATH` (Linux) or `PATH` (Windows)

### Compiler Compatibility

Ensure your compiler supports C++17:
- GCC 7.0 or later
- Clang 5.0 or later
- MSVC 2017 (Visual Studio 15.0) or later

## Integration Examples

### Basic CMake Project Structure

```
my_project/
├── CMakeLists.txt
├── src/
│   └── main.cpp
└── include/
    └── my_header.h
```

### Sample CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.12)
project(MyProject VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find dependencies
find_package(xtils REQUIRED)

# Add executable
add_executable(${PROJECT_NAME} src/main.cpp)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE include)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE xtils::xtils)

# Optional: Set additional compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
```

## Support

If you encounter issues:
1. Check that xtils was built with the same compiler and C++ standard as your project
2. Verify installation paths and permissions
3. Check CMake version compatibility
4. Ensure all dependencies are properly installed
