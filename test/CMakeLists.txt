set(SOURCES
    main.cpp

    address_test.cpp
    array_test.cpp
    bitset_test.cpp
    btree_test.cpp
    default_allocator_test.cpp
    extent_test.cpp
    fixed_string_test.cpp
    free_list_test.cpp
    hash_table_test.cpp
    heap_test.cpp
    id_generator.cpp
    inlined_any_test.cpp
    io_test.cpp
    iter_tools_test.cpp
    list_test.cpp
    math_test.cpp
    node_allocator_test.cpp
    serialization_test.cpp
    stack_test.cpp
    test_file.hpp
    transaction_engine_test.cpp
)

if (UNIX)
    list(APPEND SOURCES
        mmap_engine_test.cpp
    )
endif()

add_executable(unit-tests ${SOURCES})
target_link_libraries(unit-tests prequel)
target_include_directories(unit-tests PRIVATE "${PROJECT_SOURCE_DIR}/src")
