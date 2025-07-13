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

# 添加测试用例
add_test(NAME parser_basic_test
         COMMAND parser_test ${CMAKE_CURRENT_SOURCE_DIR}/test_cases/basic.sy)

# 添加更多测试... 