/// \file
/// \brief CRC32C checksum shared by the WAL and the control file.

export module storage.crc32c;

import std;

export namespace kravidb::storage {

/// \brief Updates a running CRC32C value with `bytes`.
[[nodiscard]] auto crc32c_update(std::uint32_t crc, std::span<const std::byte> bytes)
    -> std::uint32_t;

/// \brief Computes the CRC32C checksum of `bytes`.
[[nodiscard]] auto crc32c(std::span<const std::byte> bytes) -> std::uint32_t;

}  // namespace kravidb::storage
