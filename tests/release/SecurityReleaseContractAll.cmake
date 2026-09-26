cmake_minimum_required(VERSION 3.24)

set(SECURITY_CASES
    portable_automation
    windows_dll_load
    portable_loadconfig_dumpbin_format
    portable_reproducible_link
    release_tree_hygiene
    release_tool_safety
    crash_privacy
    sidebar_boundary
    windows_acl_preserve
    portable_wx_config_compat
    session_allocation_bounds
    settings_resource_caps
    atomic_save_exclusive_temp
    posix_state_privacy
    posix_durable_atomic_save
    footprint_cleanup
    legal_distribution
)

foreach(security_case IN LISTS SECURITY_CASES)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
                "-DSECURITY_CASE=${security_case}"
                -P "${CMAKE_CURRENT_LIST_DIR}/SecurityReleaseContract.cmake"
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Security release contract failed: ${security_case}")
    endif()
endforeach()
