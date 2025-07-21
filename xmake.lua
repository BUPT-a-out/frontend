add_rules("plugin.compile_commands.autoupdate", {outputdir = "."})

package("midend")
    set_description("Compiler middle-end")
    set_urls("https://github.com/BUPT-a-out/midend.git")

    on_install("linux", "macosx", "windows", function (package)
        import("package.tools.xmake").install(package)
    end)
package_end()

if os.projectdir() ~= os.scriptdir() then
    add_deps("midend")
else
    add_requires("midend main", {
        system = false,
        verify = false
    })
    add_packages("midend")
end

target("bison_gen")
    set_kind("phony")
    
    on_build(function (target)
        import("lib.detect.find_tool")
        
        local bison = find_tool("bison")
        if not bison then
            raise("bison not found, please install bison")
        end

        local script_dir = os.scriptdir()
        local lydir = path.join(script_dir, "flex_yacc")
        local srcdir = path.join(script_dir, "src")
        local incdir = path.join(script_dir, "include")
        
        local yacc_input = path.join(lydir, "sysy_yacc.y")
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

        local script_dir = os.scriptdir()
        local lydir = path.join(script_dir, "flex_yacc")
        local srcdir = path.join(script_dir, "src")

        local flex_input = path.join(lydir, "sysy_flex.l")
        local flex_output = path.join(srcdir, "lex.yy.c")
        
        if not os.isfile(flex_output) or os.mtime(flex_input) > os.mtime(flex_output) then
            print("Generating lexer with flex...")
            os.execv(flex.program, {"-o", flex_output, flex_input})
        end
    end)

target("frontend")
    set_kind("static")
    set_languages("c11", "c++17")
    
    add_deps("flex_gen")
    
    add_files("src/utils.c", "src/AST.c", "src/symbol_table.c", "src/ir_gen.cpp")
    
    on_config(function (target)
        local script_dir = os.scriptdir()
        local lydir = path.join(script_dir, "flex_yacc")
        local srcdir = path.join(script_dir, "src")
        local incdir = path.join(script_dir, "include")
        
        local yacc_output = path.join(srcdir, "y.tab.c")
        local flex_output = path.join(srcdir, "lex.yy.c")
        
        if not os.isfile(yacc_output) or not os.isfile(flex_output) then
            print("Pre-generating parser files...")
            
            import("lib.detect.find_tool")
            
            if not os.isfile(yacc_output) then
                local bison = find_tool("bison")
                if bison then
                    local yacc_input = path.join(lydir, "sysy_yacc.y")
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
            
            if not os.isfile(flex_output) then
                local flex = find_tool("flex")
                if flex then
                    local flex_input = path.join(lydir, "sysy_flex.l")
                    os.execv(flex.program, {"-o", flex_output, flex_input})
                    print("Generated lexer files")
                end
            end
            
            target:add("files", yacc_output, flex_output)
        else
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

task("parse")
    set_menu {
        usage = "xmake parse <file.sy>",
        description = "Run parser test with SysY file",
        options = {
            {nil, "sy_file", "v", nil, "测试文件"},
        }
    }
    
    on_run(function ()
        import("core.base.option")
        import("core.base.task")
        import("core.project.project")
        
        local file = option.get("sy_file")
        if not file then
            print("Error: Please specify a SysY file to test")
            print("Usage: xmake parse <file.sy>")
            return
        end

        local sy_file = path.absolute(file, os.workingdir())
        
        if not os.isfile(sy_file) then
            print("Error: File not found: " .. sy_file)
            return
        end
        
        task.run("build", {target="parser"})
        
        print("Running test with file: " .. sy_file)
        local target = project.target("parser")
        os.execv(target:targetfile(), {sy_file})
    end)

task("format")
    set_menu {
        usage = "xmake format",
        description = "Check code formatting with clang-format",
        options = {
            {'c', "check", "k", false, "Run clang-format in dry-run mode to check formatting without making changes."},
        }
    }
    on_run(function ()
        import("lib.detect.find_tool")
        import("core.base.option")
        local clang_format = find_tool("clang-format-15") or find_tool("clang-format")
        if not clang_format then
            raise("clang-format-15 or clang-format is required for formatting")
        end
        
        local cmd = "find . -name '*.cpp' -o -name '*.h' | grep -v build | grep -v googletest | grep -v _deps | xargs " .. clang_format.program
        if option.get("check") then
            cmd = cmd .. " --dry-run --Werror"
        else
            cmd = cmd .. " -i"
        end
        local ok, outdata, errdata = os.iorunv("sh", {"-c", cmd})
        
        if not ok then
            cprint("${red}Code formatting check failed:")
            if errdata and #errdata > 0 then
                print(errdata)
            end
            os.exit(1)
        else
            cprint("${green}All files are properly formatted!")
        end
    end)