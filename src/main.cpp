import std;
import storage.engine;

int main() {
  try {
    std::print("Booting: ");
    init_storage();
    return 0;
  } catch (const std::exception& e) {
    std::println("Fatal error: {}", e.what());
    return 1;
  } catch (...) {
    std::println("Unknown fatal error occurred.");
    return 1;
  }
}
