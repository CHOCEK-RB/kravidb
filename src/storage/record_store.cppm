/// \file
/// \brief Contrato `RecordStore` y estadisticas logicas de E/S de paginas.

export module storage.record_store;

import std;
export import storage.row_id;

export namespace kravidb::storage {

/// \brief Contadores logicos de E/S de paginas del almacen.
///
/// Solo se incrementan en las operaciones tratadas como E/S real (`insert` y
/// `read`). Las lecturas por `slot_at` no los modifican, de modo que un recorrido
/// secuencial completo puede no contabilizar lecturas.
struct PageIOStats {
  /// \brief Numero de lecturas de pagina acumuladas.
  std::size_t page_reads{0};

  /// \brief Numero de escrituras de pagina acumuladas.
  std::size_t page_writes{0};
};

/// \brief Contrato abstracto de un almacen de registros paginado.
///
/// Un registro es una secuencia opaca de bytes a la que el almacen asigna un
/// `RowID` estable. La misma coleccion de bytes se puede recuperar tanto por ese
/// identificador (`read`) como por su ubicacion explicita de pagina y ranura
/// (`slot_at`). La clase no es copiable ni movible: se consume por referencia a
/// traves de la interfaz.
class RecordStore {
  public:
  /// \brief Construye el contrato, sin estado propio.
  RecordStore() = default;

  /// \brief Copia deshabilitada: el contrato se usa por referencia.
  RecordStore(const RecordStore&) = delete;

  /// \brief Asignacion por copia deshabilitada.
  auto operator=(const RecordStore&) -> RecordStore& = delete;

  /// \brief Movimiento deshabilitado.
  RecordStore(RecordStore&&) = delete;

  /// \brief Asignacion por movimiento deshabilitada.
  auto operator=(RecordStore&&) -> RecordStore& = delete;

  /// \brief Destructor virtual.
  virtual ~RecordStore() = default;

  /// \brief Inserta un registro y devuelve su localizador.
  /// \param record Bytes del registro a almacenar.
  /// \return El `RowID` asignado al registro.
  [[nodiscard]] virtual auto insert(std::span<const std::byte> record) -> RowID = 0;

  /// \brief Lee un registro por su localizador, contabilizando la E/S.
  /// \param row_id Localizador devuelto previamente por `insert`.
  /// \return Los bytes del registro, o `std::nullopt` si el identificador no existe.
  [[nodiscard]] virtual auto read(RowID row_id) const
      -> std::optional<std::span<const std::byte>> = 0;

  /// \brief Lee un registro por su ubicacion explicita, sin contabilizar E/S.
  /// \param page_id Pagina objetivo.
  /// \param slot_id Ranura dentro de la pagina.
  /// \return Los bytes del registro, o `std::nullopt` si la ubicacion no existe.
  [[nodiscard]] virtual auto slot_at(PageID page_id, SlotID slot_id) const noexcept
      -> std::optional<std::span<const std::byte>> = 0;

  /// \brief Numero de paginas que componen el almacen.
  [[nodiscard]] virtual auto page_count() const noexcept -> std::size_t = 0;

  /// \brief Numero de ranuras ocupadas en una pagina.
  /// \param page_id Pagina consultada.
  /// \return La cantidad de ranuras, o cero si la pagina no existe.
  [[nodiscard]] virtual auto slot_count(PageID page_id) const noexcept -> std::size_t = 0;

  /// \brief Tamano configurado de pagina, en bytes.
  [[nodiscard]] virtual auto page_size() const noexcept -> std::size_t = 0;

  /// \brief Estadisticas acumuladas de E/S.
  [[nodiscard]] virtual auto stats() const noexcept -> const PageIOStats& = 0;

  /// \brief Pone a cero las estadisticas de E/S.
  virtual void reset_stats() noexcept = 0;
};

}  // namespace kravidb::storage
