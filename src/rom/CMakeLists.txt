cmake_minimum_required(VERSION 3.16)

#PMBIOS object Library
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pmbios.o
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    COMMAND ${CMAKE_LINKER} -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/pmbios.o pmbios.bin
    #places in data
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/pmbios.bin
)

add_custom_target(pmbiosobj ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pmbios.o)
add_library(pmbios OBJECT IMPORTED GLOBAL)
add_dependencies(pmbios pmbiosobj)
set_target_properties(pmbios PROPERTIES IMPORTED_OBJECTS "${CMAKE_CURRENT_BINARY_DIR}/pmbios.o")

# Experimental descriptor occupies verified FF padding; default builds use stock ROM.
if(PICOMEM_EXPERIMENTAL_LFN)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(LFN_CREATE_ARG "")
    if(PICOMEM_EXPERIMENTAL_LFN_CREATE)
        set(LFN_CREATE_ARG "--create")
    endif()
    set(LFN_RENAME_ARG "")
    if(PICOMEM_EXPERIMENTAL_LFN_RENAME)
        set(LFN_RENAME_ARG "--rename")
    endif()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.bin
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/../../tools/stamp-lfn-rom.py
            ${CMAKE_CURRENT_LIST_DIR}/pm2bios.bin ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.bin ${LFN_CREATE_ARG} ${LFN_RENAME_ARG}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/pm2bios.bin
            ${CMAKE_CURRENT_LIST_DIR}/../../tools/stamp-lfn-rom.py)
else()
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.bin
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_LIST_DIR}/pm2bios.bin
            ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.bin
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/pm2bios.bin)
endif()

#PM2BIOS object Library
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.o
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMAND ${CMAKE_LINKER} -r -b binary -o ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.o pm2bios.bin
    #places in data
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.bin
)

add_custom_target(pm2biosobj ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/pm2bios.o)
add_library(pm2bios OBJECT IMPORTED GLOBAL)
add_dependencies(pm2bios pm2biosobj)
set_target_properties(pm2bios PROPERTIES IMPORTED_OBJECTS "${CMAKE_CURRENT_BINARY_DIR}/pm2bios.o")