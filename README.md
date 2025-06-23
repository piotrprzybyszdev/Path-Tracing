# Path Tracing
Path Tracer written in Vulkan for an engineering thesis.

## Requirements
* [Vulkan SDK 1.2 or higher](https://www.lunarg.com/vulkan-sdk/)
* [CMake 3.25 or higher](https://cmake.org/)
* C++ 20 capable compiler (tested with MSVC)

## Build
In order to build the project execute the following commands:
```
git clone https://github.com/piotrprzybyszdev/Path-Tracing.git --recursive --shallow-submodules
cd Path-Tracing
cmake -S . -B build
```
Build files for the default build system of your platform should generate.