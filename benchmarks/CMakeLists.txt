add_executable(nff_benchmarks
    BenchmarkMain.cpp
    BenchmarkSupport.cpp
)

target_link_libraries(nff_benchmarks PRIVATE nff_core)
target_include_directories(nff_benchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nff_benchmarks PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_benchmarks PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(nff_benchmarks PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

add_executable(nff_stress
    StressMain.cpp
    BenchmarkSupport.cpp
)

target_link_libraries(nff_stress PRIVATE nff_core)
target_include_directories(nff_stress PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(nff_stress PRIVATE cxx_std_23)

if(MSVC)
    target_compile_options(nff_stress PRIVATE /W4 /permissive- /EHsc /utf-8)
else()
    target_compile_options(nff_stress PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()

foreach(target IN ITEMS nff_hot_path_bench nff_allocation_failure_tests)
    if(target STREQUAL "nff_hot_path_bench")
        add_executable(${target} HotPathBench.cpp BenchmarkSupport.cpp)
    else()
        add_executable(${target} AllocationFailureTests.cpp BenchmarkSupport.cpp)
    endif()
    target_link_libraries(${target} PRIVATE nff_core)
    target_compile_features(${target} PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endforeach()
if(WIN32)
    target_link_libraries(nff_hot_path_bench PRIVATE Psapi)
endif()

if(NFF_BUILD_WX_GUI)
    add_executable(nff_wx_large_bench WxLargeEditBench.cpp BenchmarkSupport.cpp)
    target_link_libraries(nff_wx_large_bench PRIVATE nff_core ${wxWidgets_LIBRARIES})
    target_compile_features(nff_wx_large_bench PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(nff_wx_large_bench PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(nff_wx_large_bench PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
    if(WIN32)
        target_link_libraries(nff_wx_large_bench PRIVATE Psapi)
    endif()
endif()
