enable_testing()

# 添加测试可执行文件
add_executable(parser_test
    parser_test.c
)

# 链接前端库
target_link_libraries(parser_test
    PRIVATE
    frontend
)

# 使用相对路径，增强可移植性
set(PROJECT_ROOT_DIR ${CMAKE_SOURCE_DIR}/../../..)
set(TEST_FILE ${PROJECT_ROOT_DIR}/functional/functional/03_arr_defn2.sy)

# 添加测试用例
add_test(NAME parser
         COMMAND parser_test ${TEST_FILE})

# 添加更多测试... 