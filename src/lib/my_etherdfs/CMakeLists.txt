add_library(pm_etherdfs INTERFACE)
if(PICOMEM_EXPERIMENTAL_DIAG)
 target_compile_definitions(pm_etherdfs INTERFACE PICOMEM_EXPERIMENTAL_DIAG=1)
 target_sources(pm_etherdfs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/diag/diag.c ${CMAKE_CURRENT_LIST_DIR}/diag/platform.c)
endif()

target_sources(pm_etherdfs INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/ethersrv-pico.c
            ${CMAKE_CURRENT_LIST_DIR}/fs.c
#            ${CMAKE_CURRENT_LIST_DIR}/lock.c
            )

target_include_directories(pm_etherdfs INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}
            ${CMAKE_CURRENT_LIST_DIR}/my_etherdfs
        )
if(PICOMEM_EXPERIMENTAL_LFN)
    target_compile_definitions(pm_etherdfs INTERFACE PICOMEM_EXPERIMENTAL_LFN=1)
    target_sources(pm_etherdfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/lfn/lfn.c
        ${CMAKE_CURRENT_LIST_DIR}/lfn/fatfs-adapter.c
        ${CMAKE_CURRENT_LIST_DIR}/lfn/dispatch.c)
endif()

if(PICOMEM_EXPERIMENTAL_LFN_CREATE)
    target_compile_definitions(pm_etherdfs INTERFACE PICOMEM_EXPERIMENTAL_LFN_CREATE=1)
    target_sources(pm_etherdfs INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/lfn/create.c
        ${CMAKE_CURRENT_LIST_DIR}/lfn/create-fatfs.c)
endif()

if(PICOMEM_EXPERIMENTAL_LFN_RENAME)
 target_compile_definitions(pm_etherdfs INTERFACE PICOMEM_EXPERIMENTAL_LFN_RENAME=1)
 target_sources(pm_etherdfs INTERFACE ${CMAKE_CURRENT_LIST_DIR}/lfn/rename.c ${CMAKE_CURRENT_LIST_DIR}/lfn/rename-fatfs.c)
endif()
