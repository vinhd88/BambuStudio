# Agent bridge tests: auth, UI-thread dispatch, HTTP server and JSON conversion.
# The GUI handlers need a running Studio; see live-checks/README.md.
get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

add_executable(${_TEST_NAME}_tests
    agent_bridge_tests_main.cpp
    agent_bridge_request_tests.cpp
    agent_bridge_server_tests.cpp
    agent_bridge_json_tests.cpp
    agent_bridge_apply_tests.cpp
    agent_bridge_revert_and_handoff_tests.cpp
    agent_bridge_mcp_tests.cpp
    agent_bridge_official_data_import_tests.cpp
    agent_bridge_ai_app_connect_tests.cpp
)

target_link_libraries(${_TEST_NAME}_tests test_gui)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests "${CATCH_EXTRA_ARGS} exclude:[NotWorking]")
