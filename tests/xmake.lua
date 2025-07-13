target("parser_test")
    set_kind("binary")
    set_languages("c11")
    
    add_files("parser_test.c")
    
    add_deps("frontend")
    
    set_warnings("all")
    add_cflags("-Wall", "-Wextra")
    
    if is_mode("debug") then
        add_cflags("-g", "-O0")
        set_symbols("debug")
        set_optimize("none")
    elseif is_mode("release") then
        add_cflags("-O3", "-DNDEBUG")
        set_symbols("hidden")
        set_optimize("fastest")
    end

task("test")
    set_menu {
        usage = "xmake test <file.sy>",
        description = "Run parser test with SysY file",
        options = {
            {'f', "file", "v", nil, "测试文件"},
        }
    }
    
    on_run(function ()
        import("core.base.option")
        import("core.base.task")
        
        local sy_file = option.get("file")
        if not sy_file then
            print("Error: Please specify a SysY file to test")
            print("Usage: xmake test <file.sy>")
            return
        end
        
        if not os.isfile(sy_file) then
            print("Error: File not found: " .. sy_file)
            return
        end
        
        task.run("build", {target = "parser_test"})
        
        if not path.is_absolute(sy_file) then
            sy_file = path.absolute(sy_file)
        end
        
        print("Running test with file: " .. sy_file)
        os.exec("xmake run parser_test " .. sy_file)
    end)