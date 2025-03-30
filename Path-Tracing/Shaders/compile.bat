glslc.exe --target-env=vulkan1.4 -o closesthit.spv closesthit.rchit
glslc.exe --target-env=vulkan1.4 -o miss.spv miss.rmiss
glslc.exe --target-env=vulkan1.4 -o raygen.spv raygen.rgen

pause