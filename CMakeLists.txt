cmake_minimum_required(VERSION 3.10)
project(frontend)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 添加编译选项
add_compile_options(-Wall -Wextra)

# Flex和Bison的查找
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# 生成词法分析器和语法分析器
BISON_TARGET(SysYParser 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysy_yacc.y
    ${CMAKE_CURRENT_BINARY_DIR}/y.tab.c
    DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/y.tab.h
)

FLEX_TARGET(SysYLexer 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysy_flex.l
    ${CMAKE_CURRENT_BINARY_DIR}/lex.yy.c
)

ADD_FLEX_BISON_DEPENDENCY(SysYLexer SysYParser)

# 设置头文件搜索路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 源文件列表
set(FRONTEND_SOURCES
    src/AST.c
    src/symbol_table.c
    ${BISON_SysYParser_OUTPUTS}
    ${FLEX_SysYLexer_OUTPUTS}
)

# 创建前端库
add_library(frontend STATIC ${FRONTEND_SOURCES})

# 安装规则
install(TARGETS frontend
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/include/AST.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/symbol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/symbol_table.h
    DESTINATION include/frontend
)

# 添加测试目录
add_subdirectory(tests)

# 配置文件
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/frontendConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/frontendConfig.cmake
    @ONLY
)

# 导出目标
export(TARGETS frontend FILE frontendConfig.cmake) 