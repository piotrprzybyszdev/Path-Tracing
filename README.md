# Path Tracing
Path Tracer written in Vulkan for an engineering thesis.

## Requirements
* [Vulkan SDK 1.3 or higher](https://www.lunarg.com/vulkan-sdk/)
* [CMake 3.25 or higher](https://cmake.org/)
* C++ 20 capable compiler (tested with MSVC and gcc)

To run the Debug version of the application on Windows, you must install the "shader toolchain debug symbols" through the Vulkan SDK installer.

On Linux, you also must install the [dependencies of the GLFW library](https://www.glfw.org/docs/latest/compile.html#compile_deps).

## Build
In order to build the project execute the following commands:
```
git clone https://github.com/piotrprzybyszdev/Path-Tracing.git --recursive --shallow-submodules
cd Path-Tracing
cmake -S . -B build
```
Build files for the default build system of your platform should generate.