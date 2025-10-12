function (add_vulkan_component LIB_NAME INCLUDE_DIR)
    add_library("${LIB_NAME}" STATIC IMPORTED)

    if (WIN32)
        set_target_properties("${LIB_NAME}" PROPERTIES
            IMPORTED_CONFIGURATIONS "DEBUG;RELEASE"
            IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
            INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include/${INCLUDE_DIR}"
            IMPORTED_LOCATION "$ENV{VULKAN_SDK}/Lib/${LIB_NAME}.lib"
            IMPORTED_LOCATION_DEBUG "$ENV{VULKAN_SDK}/Lib/${LIB_NAME}d.lib"
        )
    else()
        set_target_properties("${LIB_NAME}" PROPERTIES
            IMPORTED_CONFIGURATIONS "RELEASE"
            IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
            INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include/${INCLUDE_DIR}"
            IMPORTED_LOCATION "$ENV{VULKAN_SDK}/lib/lib${LIB_NAME}.a"
        )
    endif()
endfunction()


if (NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "ERROR: Environment variable VULKAN_SDK not defined.")
endif()

add_library(vulkan STATIC IMPORTED)

if (WIN32)
    set_target_properties(vulkan PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include/vulkan"
        IMPORTED_LOCATION "$ENV{VULKAN_SDK}/Lib/vulkan-1.lib"
    )
else()
    set_target_properties(vulkan PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "CXX"
        INTERFACE_INCLUDE_DIRECTORIES "$ENV{VULKAN_SDK}/include/vulkan"
        IMPORTED_LOCATION "$ENV{VULKAN_SDK}/lib/libvulkan.so"
    )
endif()

add_vulkan_component(shaderc_combined shaderc)
add_vulkan_component(spirv-cross-core spirv_cross)

