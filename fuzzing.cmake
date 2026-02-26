if(ENABLE_FUZZING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer,address,undefined")
    
    function(add_fuzz_target name source)
        add_executable(${name} ${source})
        target_link_libraries(${name} PRIVATE cynamodb_core)
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fuzzers")
    endfunction()
else()
    function(add_fuzz_target name source)
        # No-op if fuzzing is disabled
    endfunction()
endif()
