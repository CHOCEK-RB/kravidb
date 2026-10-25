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
add_requires("cpp-httplib")

function kravidb_common()
	set_languages("c++23")
	set_warnings("all", "error")
	add_defines("_LIBCPP_DISABLE_DEPRECATION_WARNINGS")
	add_cxxflags("-Wno-deprecated-declarations")
	add_includedirs("src")
end

function kravidb_mode(enable_sanitizers)
	if is_mode("debug") then
		set_symbols("debug")
		set_optimize("none")
		add_cxflags("-fno-omit-frame-pointer")
		if enable_sanitizers then
			set_policy("build.sanitizer.address", true)
			set_policy("build.sanitizer.undefined", true)
		end
	elseif is_mode("release") then
		set_symbols("hidden")
		set_optimize("fastest")
	end
end

target("kravidb_lib")
set_kind("static")
kravidb_common()
kravidb_mode(true)

add_files("src/**.cppm", { public = true })
add_files("src/**.cpp")
add_packages("cpp-httplib")

target("kravidb")
set_kind("binary")
kravidb_common()
kravidb_mode(true)
set_rundir("$(projectdir)")

add_files("apps/cli/main.cpp")
add_deps("kravidb_lib")

target("kravidb_tests")
set_kind("binary")
kravidb_common()
kravidb_mode(true)

add_files("tests/**.cpp")
add_packages("gtest")
add_deps("kravidb_lib")
add_tests("default", { realtime_output = true })

target("benchmarks")
set_kind("binary")
kravidb_common()
kravidb_mode(false)
set_rundir("$(projectdir)")

add_files("benchmarks/**.cpp")
add_packages("benchmark")
add_deps("kravidb_lib")

target("kravidb_api")
set_kind("binary")
kravidb_common()
kravidb_mode(true)
set_rundir("$(projectdir)")

add_files("apps/api/main.cpp")
add_packages("cpp-httplib")
add_deps("kravidb_lib")

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
			os.files("apps/**.cpp"),
			os.files("apps/**.cppm"),
			os.files("tests/**.cpp"),
			os.files("tests/**.cppm"),
			os.files("benchmarks/**.cpp")
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
	local jobs = os.getenv("KRAVIDB_LINT_JOBS")
	if not jobs or jobs == "" then
		jobs = tostring(os.cpuinfo().ncpu or 4)
	end
	local quoted = {}
	for _, file in ipairs(files) do
		table.insert(quoted, string.format("'%s'", file))
	end
	local tidy = string.format("printf '%%s\\n' %s | xargs -P %s -n 1 clang-tidy -p .",
		table.concat(quoted, " "), jobs)
	os.execv("sh", { "-c", tidy })

	print("[lint] All checks passed.")
end)
set_menu({
	usage = "xmake lint",
	description = "Ejecuta formato, build y clang-tidy (igual que el hook y CI)",
})
task_end()

task("docs")
set_category("plugin")
on_run(function()
	print("[docs] Generando XML con Doxygen...")
	os.exec("doxygen Doxyfile")

	print("[docs] Convirtiendo XML a Markdown...")
	os.execv("python3", { "scripts/doxygen_to_md.py" })

	print("[docs] Documentacion escrita en docs/md.")
end)
set_menu({
	usage = "xmake docs",
	description = "Genera la documentacion Markdown desde los modulos (Doxygen XML -> Markdown)",
})
task_end()
