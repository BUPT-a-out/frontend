target("parser_test")
    set_kind("binary")
    set_languages("c11", "c++17")
    
    add_files("parser_test.cpp")
    
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
        
        local main_project_dir = os.scriptdir()
        local is_subproject = false
        
        while main_project_dir and main_project_dir ~= "/" do
            if os.isfile(path.join(main_project_dir, "xmake.lua")) and 
               os.isdir(path.join(main_project_dir, "modules", "frontend")) then
                is_subproject = true
                break
            end
            main_project_dir = path.directory(main_project_dir)
        end
        
        if not is_subproject then
            main_project_dir = path.directory(os.scriptdir())
            if not os.isfile(path.join(main_project_dir, "xmake.lua")) then
                print("Error: Could not find xmake.lua in project directory")
                return
            end
        end
        
        local current_dir = os.curdir()
        os.cd(main_project_dir)
        
        if is_subproject then
            print("Building parser_test from main project directory...")
        else
            print("Building parser_test from frontend project directory...")
        end
        task.run("build", {target="parser_test"})
        
        if not path.is_absolute(sy_file) then
            sy_file = path.absolute(sy_file, current_dir)
        end
        
        print("Running test with file: " .. sy_file)
        os.exec("xmake run parser_test \"" .. sy_file .. "\"")
    end)