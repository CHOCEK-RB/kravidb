/// \file
/// \brief Implementacion paginada del almacen de registros (`PageManager`).

export module storage.page_manager;

import std;
export import storage.record_store;
export import storage.row_id;

namespace kravidb::storage::detail {

/// \brief Entrada de ranura en el directorio: desplazamiento y longitud dentro del buffer.
struct SlotEntry {
  /// \brief Desplazamiento desde el inicio de la pagina hacia el inicio del registro.
  std::uint16_t offset{0};

  /// \brief Longitud en bytes del registro almacenado.
  std::uint16_t length{0};
};

/// \brief Coste de contabilidad por ranura: tamano de una entrada `SlotEntry` (4 bytes).
inline constexpr std::size_t slot_overhead = sizeof(SlotEntry);

/// \brief Pagina fisica en memoria con layout slotted-page continuo.
///
/// La pagina aloja un unico buffer de bytes continuo (`data_`):
/// - El directorio de ranuras (`SlotEntry`, 4 bytes cada uno) crece desde el byte 0 hacia adelante.
/// - Los registros de tuplas se escriben al fondo del buffer y crecen hacia atras.
/// - El espacio libre es el hueco entre el final del directorio y el inicio del ultimo registro.
///
/// No se exporta; es un detalle de implementacion de `PageManager`.
class Page {
  public:
  /// \brief Crea una pagina con la capacidad indicada y reserva su buffer continuo.
  /// \param capacity Capacidad total de la pagina, en bytes.
  explicit Page(std::size_t capacity)
      : capacity_{capacity}, free_space_end_{capacity}, data_(capacity, std::byte{0}) {}

  /// \brief Comprueba si un registro de este tamano cabe en la pagina.
  /// \param record_size Tamano del registro, sin la contabilidad de la ranura.
  [[nodiscard]] auto can_fit(std::size_t record_size) const noexcept -> bool {
    return used_bytes_ + record_size + slot_overhead <= capacity_;
  }

  /// \brief Anade un registro al fondo de la pagina y registra su ranura en el directorio.
  /// \param record Bytes del registro.
  /// \return La ranura asignada al registro.
  [[nodiscard]] auto append(std::span<const std::byte> record) -> SlotID {
    const auto record_size = record.size();
    const auto target_offset = free_space_end_ - record_size;
    std::ranges::copy(record, data_.begin() + static_cast<std::ptrdiff_t>(target_offset));

    const SlotEntry entry{
        .offset = static_cast<std::uint16_t>(target_offset),
        .length = static_cast<std::uint16_t>(record_size),
    };
    const auto slot_byte_offset = slot_count_ * slot_overhead;
    const auto entry_bytes = std::bit_cast<std::array<std::byte, sizeof(SlotEntry)>>(entry);
    std::ranges::copy(entry_bytes, data_.begin() + static_cast<std::ptrdiff_t>(slot_byte_offset));

    free_space_end_ = target_offset;
    used_bytes_ += record_size + slot_overhead;
    const auto assigned_slot = static_cast<SlotID>(slot_count_);
    ++slot_count_;
    return assigned_slot;
  }

  /// \brief Devuelve los bytes de una ranura leyendo su descriptor en el directorio.
  /// \param slot_id Ranura consultada.
  /// \return Los bytes almacenados, o `std::nullopt` si la ranura no existe.
  [[nodiscard]] auto slot(SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> {
    if (slot_id >= slot_count_) {
      return std::nullopt;
    }
    const auto slot_byte_offset = static_cast<std::size_t>(slot_id) * slot_overhead;
    std::array<std::byte, sizeof(SlotEntry)> raw{};
    std::ranges::copy(
        std::span<const std::byte>{data_}.subspan(slot_byte_offset, sizeof(SlotEntry)),
        raw.begin());
    const auto entry = std::bit_cast<SlotEntry>(raw);
    return std::span<const std::byte>{data_}.subspan(entry.offset, entry.length);
  }

  /// \brief Numero de ranuras ocupadas en la pagina.
  [[nodiscard]] auto slot_count() const noexcept -> std::size_t { return slot_count_; }

  /// \brief Bytes consumidos por registros y contabilidad de ranuras.
  [[nodiscard]] auto used_bytes() const noexcept -> std::size_t { return used_bytes_; }

  /// \brief Acceso de solo lectura al buffer fisico continuo de la pagina.
  [[nodiscard]] auto raw_bytes() const noexcept -> std::span<const std::byte> { return data_; }

  private:
  /// \brief Capacidad total de la pagina, en bytes.
  std::size_t capacity_;

  /// \brief Bytes consumidos hasta el momento (ranuras + tuplas).
  std::size_t used_bytes_{0};

  /// \brief Numero de ranuras actualmente ocupadas.
  std::size_t slot_count_{0};

  /// \brief Limite inferior de la region de datos (desciende con cada insercion).
  std::size_t free_space_end_;

  /// \brief Buffer continuo de memoria de la pagina.
  std::vector<std::byte> data_;
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
