target("parser")
    set_kind("binary")
    set_languages("c11", "c++17")
    
    add_files("parser.cpp")
    
    add_deps("frontend")
    
    set_warnings("all")
    add_cxflags("-Wall", "-Wextra")
    
    if is_mode("debug") then
        add_cflags("-g", "-O0")
        set_symbols("debug")
        set_optimize("none")
    elseif is_mode("release") then
        add_cflags("-O3", "-DNDEBUG")
        set_symbols("hidden")
        set_optimize("fastest")
    end
