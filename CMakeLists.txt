cmake_minimum_required(VERSION 3.10)
project(frontend)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# 修改为生成到 include 和 src
BISON_TARGET(SysYParser 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysy_yacc.y
    ${CMAKE_CURRENT_SOURCE_DIR}/src/y.tab.c
    DEFINES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/include/y.tab.h
)

FLEX_TARGET(SysYLexer 
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sysy_flex.l
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lex.yy.c
)

ADD_FLEX_BISON_DEPENDENCY(SysYLexer SysYParser)

# include 目录现在也会找到 y.tab.h
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 这样就像普通文件一样
set(FRONTEND_SOURCES
    src/AST.c
    src/symbol_table.c
    src/y.tab.c
    src/lex.yy.c
)

add_library(frontend STATIC ${FRONTEND_SOURCES})

install(TARGETS frontend
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES 
    ${CMAKE_CURRENT_SOURCE_DIR}/include/AST.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/symbol.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/symbol_table.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/y.tab.h
    DESTINATION include/frontend
)

enable_testing()
add_subdirectory(tests)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/frontendConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/frontendConfig.cmake
    @ONLY
)

export(TARGETS frontend FILE frontendConfig.cmake)
