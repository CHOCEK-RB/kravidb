set_project("kravidb")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")

target("kravidb")
    set_kind("binary")
    set_languages("c++23")
    set_warnings("all", "error")

    add_files("src/**.cpp")
    add_files("src/**.cppm")

    add_includedirs("src")

    if is_mode("debug") then
        set_symbols("debug")
        set_optimize("none")
    elseif is_mode("release") then
        set_symbols("hidden")
        set_optimize("fastest")
    end
