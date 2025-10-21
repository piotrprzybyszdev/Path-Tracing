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

## Configurations
The project can be copmiled in 4 different configurations, each of them enabling different suboptions. Each suboption can also be enabled individually.

To specify a configuration add the following flag to the cmake command from the build section: `-DCMAKE_BUILD_TYPE=<CONFIG>`.  
The default configuration is `Release`.

|                                |  Release   |    Profile    | Debug  |   Trace  |
|:------------------------------:|:----------:|:-------------:|:------:|:--------:|
| OPTIMIZE_SHADERS               | ✅         | ✅           | ❌     | ❌      |
| OPTIMIZE_SCENE                 | ✅         | ✅           | ❌     | ❌      |
| SHADER_DEBUG_INFO              | ❌         | ✅           | ✅     | ✅      |
| VALIDATION_LAYERS              | ❌         | ❌           | ✅     | ✅      |
| ASSERTS                        | ❌         | ❌           | ✅     | ✅      |
| LOG_TO_FILE                    | ❌         | ❌           | ❌     | ✅      |
| LOG_LEVEL_*                    | INFO       | INFO          | DEBUG  | TRACE    |
| MAX_TEXTURE_LOADER_THREADS     | ❌         | ❌           | 2      | 2        |
| MAX_BUFFERS_PER_LOADER_THREAD  | ❌         | ❌           | 1      | 1        |
| MAX_SHADER_COMPILATION_THREADS | ❌         | ❌           | 2      | 2        |

Other configuration options:
* MAX_SHADER_INCLUDE_DEPTH
* MAX_SHADER_INCLUDE_CACHE_SIZE
* DISABLE_SHADER_PRECOMPILATION
* MAX_PIPELINE_VARIANT_CACHE_SIZE

For example to compile in `Profile` mode but with logging to a file at `trace` log level and max shader include depth set to `1` you should append to the cmake command: `-DCMAKE_BUILD_TYPE=Profile -DPATH_TRACING_CONFIG="CONFIG_LOG_TO_FILE;CONFIG_LOG_LEVEL_TRACE;CONFIG_MAX_SHADER_INCLUDE_DEPTH=1"`.

To revert the configuration changes to the default run cmake again with `-DPATH_TRACING_CONFIG=""`.

## Installing Scenes
To install additional scenes you should append to the cmake command: `-DASSETS="<ASSET_NAME1>;<ASSET_NAME2>;..."`.  
Some scenes are composed of separate components, in that case you can also install components individually.

### Intel Sponza
* Full scene: **INTEL_SPONZA_FULL** (*all of the below will be installed*)
* Intel Sponza Main Component - **INTEL_SPONZA_MAIN** (*3.9 GB zip*)
* Intel Sponza Curtains Component - **INTEL_SPONZA_CURTAINS** (*0.8 GB zip*)