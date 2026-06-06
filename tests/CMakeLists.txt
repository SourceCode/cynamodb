add_executable(unit_tests test_main.cpp test_core.cpp test_schema.cpp test_api.cpp test_json.cpp test_engine.cpp test_memory_engine.cpp test_recovery.cpp test_expressions.cpp test_lsm_primitives.cpp test_lsm_engine.cpp test_api_handlers.cpp test_table_manager.cpp test_metrics.cpp test_transactions_http.cpp test_tables_http.cpp test_items_http.cpp test_streams_http.cpp test_http_security_standardization.cpp test_streams_manager.cpp test_backups_manager.cpp test_scheduler.cpp test_compaction.cpp test_read_opt.cpp test_expr_opt.cpp test_auth_opt.cpp test_capacity.cpp test_partiql.cpp test_resilience.cpp test_complex_types.cpp test_dynamo_features.cpp test_update_expression.cpp test_sigv4_crypto.cpp)
target_link_libraries(unit_tests PRIVATE cynamodb_core Catch2::Catch2WithMain)
add_test(NAME unit_tests COMMAND unit_tests)

# Live end-to-end CRUD + persistence test: starts the real server binary over
# HTTP, exercises Create/Read/Update/Delete across multiple process restarts, and
# validates data survives correctly. Registered only when python3 is available.
find_program(PYTHON3_EXECUTABLE NAMES python3 python)
if(PYTHON3_EXECUTABLE)
    add_test(
        NAME crud_live_integration
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/crud_live_test.py $<TARGET_FILE:cynamodb>)
    set_tests_properties(crud_live_integration PROPERTIES TIMEOUT 120)

    add_test(
        NAME crash_recovery_integration
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/crash_recovery_test.py $<TARGET_FILE:cynamodb>)
    set_tests_properties(crash_recovery_integration PROPERTIES TIMEOUT 120)

    add_test(
        NAME concurrent_load_integration
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/concurrent_load_test.py $<TARGET_FILE:cynamodb>)
    set_tests_properties(concurrent_load_integration PROPERTIES TIMEOUT 120)

    # End-to-end coverage for the DynamoDB features added per CYNAMODB_FINDINGS.md:
    # complex types surviving a flush, conditional writes, UpdateItem, query
    # expressions, batch/transactions, table lifecycle, error shapes, and auth.
    add_test(
        NAME features_live_integration
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/integration/features_live_test.py $<TARGET_FILE:cynamodb>)
    set_tests_properties(features_live_integration PROPERTIES TIMEOUT 120)
endif()
