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
    
    add_files(
        "src/utils.c",
        "src/AST.c",
        "src/symbol_table.c",
        "src/runtime_lib_symbols.c",
        "src/ir_gen.cpp"
    )
    
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
    before_build(function (target)
        local hooks_dir = path.join(os.scriptdir(), ".git", "hooks")
        local pre_commit_path = path.join(hooks_dir, "pre-commit")
        if os.isdir(hooks_dir) then
            local expected_hook_content = [[#!/bin/sh
# Auto-generated pre-commit hook by xmake
# This hook runs tests and formatting checks before committing

echo "Running tests..."
if ! xmake test; then
    echo "Tests failed! Commit aborted."
    exit 1
fi

echo "Checking code formatting..."
if ! xmake format --check; then
    echo "Code formatting check failed! Commit aborted."
    echo "Please run 'xmake format' to fix formatting issues."
    exit 1
fi

echo "All checks passed!"
]]
            
            local should_write = false
            if not os.isfile(pre_commit_path) then
                cprint("${yellow}No git pre-commit hook found. Setting up automatically...")
                should_write = true
            else
                local current_content = io.readfile(pre_commit_path)
                if current_content ~= expected_hook_content then
                    cprint("${yellow}Existing pre-commit hook differs from expected. Updating...")
                    should_write = true
                end
            end
            
            if should_write then
                io.writefile(pre_commit_path, expected_hook_content)
                os.exec("chmod +x " .. pre_commit_path)
                cprint("${green}Git pre-commit hook has been set up automatically!")
            end
        end
    end)


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

task("test")
    set_menu {
        usage = "xmake test",
        description = "Run parser tests on all .sy files in tests/cases/"
    }
    
    on_run(function ()
        import("core.base.task")
        import("core.project.project")
        
        -- Build parser first
        task.run("build", {target="parser"})
        
        local script_dir = os.scriptdir()
        local cases_dir = path.join(script_dir, "tests", "cases")
        
        if not os.isdir(cases_dir) then
            print("Error: Test cases directory not found: " .. cases_dir)
            return
        end
        
        -- Get parser executable path
        local target = project.target("parser")
        local parser_exe = target:targetfile()
        
        if not os.isfile(parser_exe) then
            print("Error: Parser executable not found")
            return
        end
        
        -- Find all .sy files
        local test_files = {}
        for _, file in ipairs(os.files(path.join(cases_dir, "*.sy"))) do
            table.insert(test_files, file)
        end
        
        if #test_files == 0 then
            print("No test files found in " .. cases_dir)
            return
        end
        
        print("Running tests...")
        print("=" .. string.rep("=", 50))
        
        local failed_tests = {}
        local passed_count = 0
        
        for _, sy_file in ipairs(test_files) do
            local basename = path.basename(sy_file)
            local out_file = path.join(cases_dir, path.basename(sy_file) .. ".out")
            
            io.write(string.format("Testing %-30s ... ", basename))
            io.flush()
            
            -- Run parser and capture output
            local outdata, errdata = os.iorunv(parser_exe, {sy_file})
            
            -- Check if .out file exists
            if os.isfile(out_file) then
                -- Read expected output
                local expected = io.readfile(out_file)
                
                -- Compare outputs
                if outdata == expected then
                    cprint("${green}PASS")
                    passed_count = passed_count + 1
                else
                    cprint("${red}FAIL")
                    table.insert(failed_tests, basename)
                    
                    -- Save actual output for debugging
                    local actual_file = path.join(cases_dir, basename .. ".actual")
                    io.writefile(actual_file, outdata)
                end
            else
                -- No .out file, just run to check for crashes
                if errdata and #errdata > 0 then
                    cprint("${red}ERROR")
                    table.insert(failed_tests, basename)
                else
                    cprint("${yellow}NO EXPECTED OUTPUT")
                    -- Create .out file with current output for future use
                    io.writefile(out_file, outdata)
                end
            end
        end
        
        print("=" .. string.rep("=", 50))
        print(string.format("Tests: %d total, %d passed, %d failed", 
                           #test_files, passed_count, #failed_tests))
        
        if #failed_tests > 0 then
            cprint("${red}Failed tests:")
            for _, test in ipairs(failed_tests) do
                print("  - " .. test)
            end
            os.exit(1)
        else
            cprint("${green}All tests passed!")
        end
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