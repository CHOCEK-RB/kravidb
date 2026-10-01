set_project("kravidb")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")
add_rules("plugin.compile_commands.autoupdate", { outputdir = "." })

if is_plat("windows") then
	set_toolchains("llvm")
else
	set_toolchains("clang")
end

add_requires("gtest", { system = false, configs = { main = true } })
add_requires("benchmark", { system = false })

target("kravidb")
set_kind("binary")
set_languages("c++23")
set_warnings("all", "error")

add_defines("_LIBCPP_DISABLE_DEPRECATION_WARNINGS")
add_cxxflags("-Wno-deprecated-declarations")

add_files("src/**.cpp")
remove_files("src/benchmark.cpp")
add_files("src/**.cppm")

add_includedirs("src")

if is_mode("debug") then
	set_symbols("debug")
	set_optimize("none")
	add_cxflags("-fno-omit-frame-pointer")
	set_policy("build.sanitizer.address", true)
	set_policy("build.sanitizer.undefined", true)
elseif is_mode("release") then
	set_symbols("hidden")
	set_optimize("fastest")
end

target("kravidb_tests")
set_kind("binary")
set_languages("c++23")
set_warnings("all", "error")

add_defines("_LIBCPP_DISABLE_DEPRECATION_WARNINGS")
add_cxxflags("-Wno-deprecated-declarations")

add_files("tests/**.cpp")
add_files("src/**.cppm")
add_files("src/index/**.cpp", "src/storage/**.cpp")

add_includedirs("src")
add_packages("gtest")

if is_mode("debug") then
	set_symbols("debug")
	set_optimize("none")
	add_cxflags("-fno-omit-frame-pointer")
	set_policy("build.sanitizer.address", true)
	set_policy("build.sanitizer.undefined", true)
elseif is_mode("release") then
	set_symbols("hidden")
	set_optimize("fastest")
end

add_tests("default")

target("benchmarks")
set_kind("binary")
set_languages("c++23")
set_warnings("all", "error")

add_defines("_LIBCPP_DISABLE_DEPRECATION_WARNINGS")
add_cxxflags("-Wno-deprecated-declarations")

add_files("src/benchmark.cpp")
add_files("src/**.cppm")
add_files("src/index/**.cpp", "src/storage/**.cpp")

add_includedirs("src")
add_packages("benchmark")

-- Los benchmarks no llevan sanitizers: ASan/UBSan distorsionan la latencia.
if is_mode("debug") then
	set_symbols("debug")
	set_optimize("none")
	add_cxflags("-fno-omit-frame-pointer")
elseif is_mode("release") then
	set_symbols("hidden")
	set_optimize("fastest")
end

task("lint")
set_category("plugin")
on_run(function()
	local function lint_files()
		local override = os.getenv("KRAVIDB_LINT_FILES")
		if override and override ~= "" then
			local list = {}
			for _, file in ipairs(override:split("%s+")) do
				if file ~= "" then
					table.insert(list, file)
				end
			end
			return list
		end
		return table.join(
			os.files("src/**.cpp"),
			os.files("src/**.cppm"),
			os.files("tests/**.cpp"),
			os.files("tests/**.cppm")
		)
	end

	local files = lint_files()
	if #files == 0 then
		print("[lint] No hay archivos fuente que revisar.")
		return
	end

	print("[lint] Checking formatting with clang-format...")
	os.execv("clang-format", table.join({ "--dry-run", "--Werror" }, files))

	print("[lint] Checking build with xmake...")
	os.exec("xmake -q")

	print("[lint] Updating compile_commands.json...")
	os.exec("xmake project -k compile_commands")

	print("[lint] Running clang-tidy checks...")
	os.execv("clang-tidy", table.join({ "-p", "." }, files))

	print("[lint] All checks passed.")
end)
set_menu({
	usage = "xmake lint",
	description = "Ejecuta formato, build y clang-tidy (igual que el hook y CI)",
})
task_end()
