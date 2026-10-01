/// \file
/// \brief Implementacion paginada del almacen de registros (`PageManager`).

export module storage.page_manager;

import std;
export import storage.record_store;
export import storage.row_id;

namespace kravidb::storage::detail {

/// \brief Coste de contabilidad por ranura: la longitud `u32` que la precede.
inline constexpr std::size_t slot_overhead = sizeof(std::uint32_t);

/// \brief Pagina en memoria: ranuras contiguas con su contabilidad de uso.
///
/// No se exporta; es un detalle de implementacion de `PageManager`.
class Page {
  public:
  /// \brief Crea una pagina con la capacidad indicada.
  /// \param capacity Capacidad total de la pagina, en bytes.
  explicit Page(std::size_t capacity) : capacity_{capacity} {}

  /// \brief Comprueba si un registro de este tamano cabe en la pagina.
  /// \param record_size Tamano del registro, sin la contabilidad de la ranura.
  [[nodiscard]] auto can_fit(std::size_t record_size) const noexcept -> bool {
    return used_bytes_ + record_size + slot_overhead <= capacity_;
  }

  /// \brief Anade un registro al final de la pagina.
  /// \param record Bytes del registro.
  /// \return La ranura asignada al registro.
  [[nodiscard]] auto append(std::span<const std::byte> record) -> SlotID {
    slots_.emplace_back(record.begin(), record.end());
    used_bytes_ += record.size() + slot_overhead;
    return static_cast<SlotID>(slots_.size() - 1);
  }

  /// \brief Devuelve los bytes de una ranura.
  /// \param slot_id Ranura consultada.
  /// \return Los bytes almacenados, o `std::nullopt` si la ranura no existe.
  [[nodiscard]] auto slot(SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> {
    if (slot_id >= slots_.size()) {
      return std::nullopt;
    }
    return std::span<const std::byte>{slots_.at(slot_id)};
  }

  /// \brief Numero de ranuras ocupadas en la pagina.
  [[nodiscard]] auto slot_count() const noexcept -> std::size_t { return slots_.size(); }

  /// \brief Bytes consumidos por registros y contabilidad.
  [[nodiscard]] auto used_bytes() const noexcept -> std::size_t { return used_bytes_; }

  private:
  /// \brief Capacidad total de la pagina, en bytes.
  std::size_t capacity_;

  /// \brief Bytes consumidos hasta el momento.
  std::size_t used_bytes_{0};

  /// \brief Ranuras almacenadas, cada una con su copia de los bytes.
  std::vector<std::vector<std::byte>> slots_;
};

}  // namespace kravidb::storage::detail

export namespace kravidb::storage {

/// \brief Tamano de pagina por defecto, en bytes (4 KiB).
inline constexpr std::size_t default_page_size = 4096;

/// \brief Almacen de registros paginado, de crecimiento solo por el final.
///
/// Los registros se insertan en la ultima pagina o en una nueva; las paginas
/// nunca se reasignan, por lo que los bytes devueltos por `read` y `slot_at`
/// siguen siendo validos despues de inserciones posteriores.
class PageManager final : public RecordStore {
  public:
  /// \brief Crea el almacen con el tamano de pagina indicado.
  /// \param page_size Tamano de pagina, en bytes.
  explicit PageManager(std::size_t page_size = default_page_size) : page_size_{page_size} {}

  /// \brief Inserta un registro.
  /// \param record Bytes del registro a almacenar.
  /// \return El `RowID` asignado.
  /// \note Lanza `std::length_error` si el registro no cabe ni en una pagina vacia.
  [[nodiscard]] auto insert(std::span<const std::byte> record) -> RowID override;

  /// \brief Lee un registro por su localizador, contabilizando la E/S.
  /// \param row_id Localizador del registro.
  /// \return Los bytes del registro, o `std::nullopt` si no existe.
  [[nodiscard]] auto read(RowID row_id) const -> std::optional<std::span<const std::byte>> override;

  /// \brief Lee un registro por ubicacion explicita, sin contabilizar E/S.
  /// \param page_id Pagina objetivo.
  /// \param slot_id Ranura dentro de la pagina.
  /// \return Los bytes del registro, o `std::nullopt` si la ubicacion no existe.
  [[nodiscard]] auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> override;

  /// \brief Numero de paginas del almacen.
  [[nodiscard]] auto page_count() const noexcept -> std::size_t override { return pages_.size(); }

  /// \brief Numero de ranuras ocupadas en una pagina.
  /// \param page_id Pagina consultada.
  [[nodiscard]] auto slot_count(PageID page_id) const noexcept -> std::size_t override;

  /// \brief Tamano de pagina configurado, en bytes.
  [[nodiscard]] auto page_size() const noexcept -> std::size_t override { return page_size_; }

  /// \brief Estadisticas acumuladas de E/S.
  [[nodiscard]] auto stats() const noexcept -> const PageIOStats& override { return stats_; }

  /// \brief Pone a cero las estadisticas de E/S.
  void reset_stats() noexcept override { stats_ = PageIOStats{}; }

  private:
  /// \brief Tamano de pagina configurado, en bytes.
  std::size_t page_size_;

  /// \brief Paginas en orden de creacion.
  std::vector<detail::Page> pages_;

  /// \brief Contadores de E/S, mutables para poder actualizarlos desde `read` (const).
  mutable PageIOStats stats_{};
};

}  // namespace kravidb::storage
