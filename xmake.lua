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

rule("yacc")
    set_extensions(".y", ".yacc")
    before_build_file(function (target, sourcefile, opt)
        import("lib.detect.find_tool")
        
        local bison = assert(find_tool("bison"), "bison not found!")
        local srcdir = path.join(target:scriptdir(), "src/sy_parser")
        local incdir = path.join(target:scriptdir(), "include/sy_parser")
        local target_c = path.join(srcdir, "y.tab.c")
        local target_h = path.join(incdir, "y.tab.h")
        local sourcefile_mtime = os.mtime(sourcefile) or 0
        local target_c_mtime = os.mtime(target_c) or 0
        local target_h_mtime = os.mtime(target_h) or 0
        if sourcefile_mtime > target_c_mtime or sourcefile_mtime > target_h_mtime or 
           target_c_mtime == 0 or target_h_mtime == 0 then
            os.mkdir(srcdir)
            os.mkdir(incdir)
            cprint("${color.build.object}generating.yacc %s", sourcefile)
            os.vrunv(bison.program, {"-d", "-o", target_c, sourcefile})
            local temp_header = path.join(srcdir, "y.tab.h")
            if os.isfile(temp_header) then
                os.mv(temp_header, target_h)
            end
        end
    end)
rule_end()

rule("lex")
    set_extensions(".l", ".lex")
    before_build_file(function (target, sourcefile, opt)
        import("lib.detect.find_tool")
        local flex = assert(find_tool("flex"), "flex not found!")
        local srcdir = path.join(target:scriptdir(), "src/sy_parser")
        local target_c = path.join(srcdir, "lex.yy.c")
        local sourcefile_mtime = os.mtime(sourcefile) or 0
        local target_c_mtime = os.mtime(target_c) or 0
        local yacc_header = path.join(target:scriptdir(), "include/sy_parser/y.tab.h")
        local yacc_header_mtime = os.mtime(yacc_header) or 0
        if sourcefile_mtime > target_c_mtime or yacc_header_mtime > target_c_mtime or 
           target_c_mtime == 0 then
            os.mkdir(srcdir)
            cprint("${color.build.object}generating.lex %s", sourcefile)
            os.vrunv(flex.program, {"-o", target_c, sourcefile})
        end
    end)
rule_end()

target("frontend")
    set_kind("static")
    set_languages("c11", "c++17")
    add_rules("yacc", "lex")
    
    add_files(
        "src/sy_parser/utils.c",
        "src/sy_parser/AST.c",
        "src/sy_parser/symbol_table.c",
        "src/runtime_lib_def.cpp",
        "src/ir_gen.cpp",
        "flex_yacc/sysy_yacc.y",
        "flex_yacc/sysy_flex.l",
        "src/sy_parser/y.tab.c",
        "src/sy_parser/lex.yy.c"
    )
    
    add_includedirs("include", {public = true})
    add_includedirs("include/sy_parser", {public = true})
    
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
            path.join(script_dir, "src/sy_parser/lex.yy.c"),
            path.join(script_dir, "src/sy_parser/y.tab.c"),
            path.join(script_dir, "include/sy_parser/y.tab.h")
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

task("std_parse")
    set_menu {
        usage = "xmake std_parse <prefix>",
        description = "用解析器解析functional/functional目录下以<前缀>开头的所有文件",
        options = {
            {nil, "prefix", "v", nil, "文件名前缀"},
        }
    }
    on_run(function ()
        import("core.base.option")
        import("core.base.task")
        import("core.project.project")
        local prefix = option.get("prefix")
        if not prefix then
            print("Error: 请指定文件名前缀")
            print("Usage: xmake std_parse <prefix>")
            return
        end
        local dir = "/home/thatyear/projects/contest/compiler_design/functional/functional"
        if not os.isdir(dir) then
            print("Error: 目标目录不存在: " .. dir)
            return
        end
        -- 查找所有以prefix开头且以.sy结尾的文件
        local files = {}
        for _, file in ipairs(os.files(path.join(dir, prefix .. "*.sy"))) do
            table.insert(files, file)
        end
        if #files == 0 then
            print("未找到以" .. prefix .. "开头的文件")
            return
        end
        -- 构建解析器
        task.run("build", {target="parser"})
        local target = project.target("parser")
        local parser_exe = target:targetfile()
        if not os.isfile(parser_exe) then
            print("Error: 解析器可执行文件未找到")
            return
        end
        for _, file in ipairs(files) do
            print("解析: " .. file)
            local outdata, errdata = os.iorunv(parser_exe, {file})
            if outdata then
                print(outdata)
            end
            if errdata and #errdata > 0 then
                print("[Error] " .. errdata)
            end
        end
    end)