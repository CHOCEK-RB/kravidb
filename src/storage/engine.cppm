module;

#include <print>

export module storage.engine;

export void init_storage() {
  std::print("Starting ");
  std::println("kravidb storage engine v0.1.0");
}
