add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

target("bison_gen")
    set_kind("phony")
    
    on_build(function (target)
        import("lib.detect.find_tool")
        
        local bison = find_tool("bison")
        if not bison then
            raise("bison not found, please install bison")
        end
        
        local srcdir = path.join(os.scriptdir(), "src")
        local incdir = path.join(os.scriptdir(), "include")
        
        local yacc_input = path.join(srcdir, "sysy_yacc.y")
        local yacc_output = path.join(srcdir, "y.tab.c")
        local yacc_header = path.join(incdir, "y.tab.h")
        
        if not os.isfile(yacc_output) or os.mtime(yacc_input) > os.mtime(yacc_output) then
            print("Generating parser with bison...")
            os.mkdir(incdir)
            os.execv(bison.program, {"-d", "-o", yacc_output, yacc_input})
            
            local temp_header = path.join(srcdir, "y.tab.h")
            if os.isfile(temp_header) then
                os.mv(temp_header, yacc_header)
            end
        end
    end)

target("flex_gen")
    set_kind("phony")
    add_deps("bison_gen")
    
    on_build(function (target)
        import("lib.detect.find_tool")
        
        local flex = find_tool("flex")
        if not flex then
            raise("flex not found, please install flex")
        end
        
        local srcdir = path.join(os.scriptdir(), "src")
        local flex_input = path.join(srcdir, "sysy_flex.l")
        local flex_output = path.join(srcdir, "lex.yy.c")
        
        if not os.isfile(flex_output) or os.mtime(flex_input) > os.mtime(flex_output) then
            print("Generating lexer with flex...")
            os.execv(flex.program, {"-o", flex_output, flex_input})
        end
    end)

target("frontend")
    set_kind("static")
    set_languages("c11")
    
    add_deps("flex_gen")
    
    add_files("src/AST.c", "src/symbol_table.c")
    
    -- Generate files in config phase if they don't exist
    on_config(function (target)
        local script_dir = os.scriptdir()
        local srcdir = path.join(script_dir, "src")
        local incdir = path.join(script_dir, "include")
        
        local yacc_output = path.join(srcdir, "y.tab.c")
        local flex_output = path.join(srcdir, "lex.yy.c")
        
        -- If files don't exist, generate them immediately
        if not os.isfile(yacc_output) or not os.isfile(flex_output) then
            print("Pre-generating parser files...")
            
            import("lib.detect.find_tool")
            
            -- Generate bison files
            if not os.isfile(yacc_output) then
                local bison = find_tool("bison")
                if bison then
                    local yacc_input = path.join(srcdir, "sysy_yacc.y")
                    local yacc_header = path.join(incdir, "y.tab.h")
                    
                    os.mkdir(incdir)
                    os.execv(bison.program, {"-d", "-o", yacc_output, yacc_input})
                    
                    local temp_header = path.join(srcdir, "y.tab.h")
                    if os.isfile(temp_header) then
                        os.mv(temp_header, yacc_header)
                    end
                    print("Generated parser files")
                end
            end
            
            -- Generate flex files
            if not os.isfile(flex_output) then
                local flex = find_tool("flex")
                if flex then
                    local flex_input = path.join(srcdir, "sysy_flex.l")
                    os.execv(flex.program, {"-o", flex_output, flex_input})
                    print("Generated lexer files")
                end
            end
            
            -- Add the generated files to the target
            target:add("files", yacc_output, flex_output)
        else
            -- Files exist, add them normally
            target:add("files", yacc_output, flex_output)
        end
    end)
    
    add_includedirs("include", {public = true})
    
    add_headerfiles("include/(**.h)")
    
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

if os.isdir(path.join(os.scriptdir(), "tests")) then
    includes("tests/xmake.lua")
end

task("clean")
    set_menu {
        usage = "xmake clean",
        description = "Clean generated files and build directory"
    }
    
    on_run(function ()
        import("core.base.task")
        
        local script_dir = os.scriptdir()
        
        -- Generated files to clean
        local generated_files = {
            path.join(script_dir, "src", "lex.yy.c"),
            path.join(script_dir, "src", "y.tab.c"),
            path.join(script_dir, "include", "y.tab.h")
        }
        
        -- Build directory
        local build_dir = path.join(script_dir, "build")
        
        print("Cleaning generated files...")
        
        for _, file in ipairs(generated_files) do
            if os.isfile(file) then
                print("Removing: " .. file)
                os.rm(file)
            end
        end
        
        if os.isdir(build_dir) then
            print("Removing build directory: " .. build_dir)
            os.rmdir(build_dir)
        end
        
        print("Clean completed!")
    end)