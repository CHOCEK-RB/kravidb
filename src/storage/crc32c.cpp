module storage.crc32c;

import std;

namespace kravidb::storage {

namespace {

constexpr std::uint32_t crc_polynomial = 0x82F63B78U;
constexpr std::uint32_t crc_initial = 0xFFFFFFFFU;
constexpr std::size_t crc_table_size = 256;
constexpr std::uint32_t bits_per_byte = 8;
constexpr std::uint32_t byte_mask = 0xFF;

consteval auto make_crc_table() -> std::array<std::uint32_t, crc_table_size> {
  std::array<std::uint32_t, crc_table_size> table{};
  for (std::size_t index = 0; index < table.size(); ++index) {
    auto crc = static_cast<std::uint32_t>(index);
    for (std::uint32_t bit = 0; bit < bits_per_byte; ++bit) {
      crc = (crc & 1U) != 0U ? (crc >> 1U) ^ crc_polynomial : crc >> 1U;
    }
    table.at(index) = crc;
  }
  return table;
}

inline constexpr auto crc_table = make_crc_table();

}  // namespace

auto crc32c_update(std::uint32_t crc, std::span<const std::byte> bytes) -> std::uint32_t {
  for (const std::byte item : bytes) {
    const auto index = (crc ^ std::to_integer<std::uint32_t>(item)) & byte_mask;
    crc = crc_table.at(index) ^ (crc >> bits_per_byte);
  }
  return crc;
}

auto crc32c(std::span<const std::byte> bytes) -> std::uint32_t {
  return ~crc32c_update(crc_initial, bytes);
}

}  // namespace kravidb::storage
