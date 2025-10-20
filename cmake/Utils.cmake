function (create_directory_link SRC DST)
    get_filename_component(DST_PATH ${DST} ABSOLUTE)
    if (IS_DIRECTORY ${DST_PATH})
        return()
    endif()

    # Creating a symlink on Windows requires administrative permissions - create junction link instead
    if (WIN32)
        file(TO_NATIVE_PATH ${SRC} WIN_SRC_ASSET)
        file(TO_NATIVE_PATH ${DST} WIN_DST_ASSET)
        execute_process(COMMAND cmd.exe /c mklink /J ${WIN_DST_ASSET} ${WIN_SRC_ASSET})
    else()
        file(CREATE_LINK ${SRC} ${DST} SYMBOLIC)
    endif()
endfunction()

function(add_configuration NAME INHERITED IS_DEBUG)
    set(CMAKE_C_FLAGS_${NAME} ${CMAKE_C_FLAGS_${INHERITED}})
    set(CMAKE_CXX_FLAGS_${NAME} ${CMAKE_CXX_FLAGS_${INHERITED}})
    set(CMAKE_EXE_LINKER_FLAGS_${NAME} ${CMAKE_EXE_LINKER_FLAGS_${INHERITED}})
    set(CMAKE_STATIC_LINKER_FLAGS_${NAME} ${CMAKE_STATIC_LINKER_FLAGS_${INHERITED}})

    if (${IS_DEBUG})
         set(CMAKE_${NAME}_POSTFIX d)

        if (MSVC)
            add_compile_options("$<$<CONFIG:${NAME}>:/Zi>")
            add_link_options("$<$<CONFIG:${NAME}>:/DEBUG>")
        endif()

        set_property(GLOBAL APPEND PROPERTY DEBUG_CONFIGURATIONS ${NAME})
    endif()
endfunction()