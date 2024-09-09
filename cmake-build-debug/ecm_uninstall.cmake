if(NOT EXISTS "/home/maheesha/Documents/fcitx5-sayura/cmake-build-debug/install_manifest.txt")
    message(FATAL_ERROR "Cannot find install manifest: /home/maheesha/Documents/fcitx5-sayura/cmake-build-debug/install_manifest.txt")
endif()

file(READ "/home/maheesha/Documents/fcitx5-sayura/cmake-build-debug/install_manifest.txt" files)
string(REGEX REPLACE "\n" ";" files "${files}")
foreach(file ${files})
    message(STATUS "Uninstalling $ENV{DESTDIR}${file}")
    if(IS_SYMLINK "$ENV{DESTDIR}${file}" OR EXISTS "$ENV{DESTDIR}${file}")
        execute_process(
            COMMAND "/home/maheesha/.local/share/JetBrains/Toolbox/apps/clion/bin/cmake/linux/x64/bin/cmake" -E remove "$ENV{DESTDIR}${file}"
            RESULT_VARIABLE rm_retval
            )
        if(NOT "${rm_retval}" STREQUAL 0)
            message(FATAL_ERROR "Problem when removing $ENV{DESTDIR}${file}")
        endif()
    else()
        message(STATUS "File $ENV{DESTDIR}${file} does not exist.")
    endif()
endforeach()
