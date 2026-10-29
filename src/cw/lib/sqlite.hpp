#pragma once

// 型付き SQLite C API RAII 境界。
//
// CMakeでSQLite3が見つかった場合はC APIを有効化する。Windowsでは
// CW_ENABLE_WINSQLITE3_APIによりWindows SDKのWinSQLiteも利用できる。
// どちらも無い構成では、成功を偽装せずSqliteErrorを送出する。
#include "cw/lib/compat.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cw::lib::sqlite {

inline constexpr int k_backend_unavailable = 0x7fff0001;
inline constexpr int k_invalid_argument = 0x7fff0002;
inline constexpr int k_closed_connection = 0x7fff0003;

class SqliteError : public std::runtime_error {
public:
    SqliteError(int code, int extended_code, String message, String query = {});
    [[nodiscard]] int code() const noexcept { return m_code; }
    [[nodiscard]] int extended_code() const noexcept { return m_extended_code; }
    [[nodiscard]] const String& message() const noexcept { return m_message; }
    [[nodiscard]] const String& query() const noexcept { return m_query; }

private:
    int m_code{};
    int m_extended_code{};
    String m_message{};
    String m_query{};
};

enum class ColumnType { Null, Integer, Real, Text, Blob };

class Column {
public:
    using Storage = std::variant<std::monostate, Integer, Real, String, Bytes>;

    Column() = default;
    explicit Column(Storage value) : m_value(std::move(value)) {}
    [[nodiscard]] ColumnType type() const noexcept;
    [[nodiscard]] bool is_null() const noexcept { return std::holds_alternative<std::monostate>(m_value); }
    [[nodiscard]] Integer as_integer() const;
    [[nodiscard]] Real as_real() const;
    [[nodiscard]] const String& as_text() const;
    [[nodiscard]] const Bytes& as_blob() const;
    [[nodiscard]] const Storage& value() const noexcept { return m_value; }

private:
    Storage m_value{};
};

class Row {
public:
    Row() = default;
    Row(std::vector<String> names, std::vector<Column> columns);
    [[nodiscard]] std::size_t size() const noexcept { return m_columns.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_columns.empty(); }
    [[nodiscard]] const Column& at(std::size_t index) const;
    [[nodiscard]] const Column& at(const String& name) const;
    [[nodiscard]] const std::vector<String>& column_names() const noexcept { return m_names; }
    [[nodiscard]] const std::vector<Column>& columns() const noexcept { return m_columns; }

private:
    std::vector<String> m_names{};
    std::vector<Column> m_columns{};
};

struct TableInfo {
    String name{};
    String type{};
    String sql{};
};

struct ColumnInfo {
    Integer cid{};
    String name{};
    String declared_type{};
    bool not_null{};
    std::optional<String> default_value{};
    bool primary_key{};
};

class QueryResult {
public:
    QueryResult() = default;
    QueryResult(std::vector<String> names, std::vector<Row> rows);
    [[nodiscard]] std::size_t size() const noexcept { return m_rows.size(); }
    [[nodiscard]] bool empty() const noexcept { return m_rows.empty(); }
    [[nodiscard]] const Row& at(std::size_t index) const;
    [[nodiscard]] const std::vector<String>& column_names() const noexcept { return m_names; }
    [[nodiscard]] const std::vector<Row>& rows() const noexcept { return m_rows; }
    [[nodiscard]] std::vector<Row>::const_iterator begin() const noexcept { return m_rows.begin(); }
    [[nodiscard]] std::vector<Row>::const_iterator end() const noexcept { return m_rows.end(); }

private:
    std::vector<String> m_names{};
    std::vector<Row> m_rows{};
};

using BindValue = std::variant<std::monostate, Integer, Real, String, Bytes>;
using BindParameters = std::vector<BindValue>;

enum class StepResult { Row, Done };

namespace detail {
struct ConnectionState;
}

class Statement {
public:
    Statement() = default;
    ~Statement() noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept { return m_statement != nullptr; }
    [[nodiscard]] const String& sql() const noexcept { return m_sql; }
    [[nodiscard]] int parameter_count() const;
    [[nodiscard]] int column_count() const;
    [[nodiscard]] String column_name(int index) const;

    void bind(int index, const BindValue& value);
    void bind(int index, std::nullptr_t);
    void bind_null(int index);
    void bind_int64(int index, Integer value);
    void bind_double(int index, Real value);
    void bind_text(int index, const String& value);
    void bind_blob(int index, const Bytes& value);
    void clear_bindings();
    void reset();
    [[nodiscard]] StepResult step();
    [[nodiscard]] const Row& current_row() const;

private:
    friend class Connection;
    Statement(std::shared_ptr<detail::ConnectionState> state, void* statement, String sql);
    void finalize() noexcept;
    void ensure_valid() const;
    void check_bind_index(int index) const;

    std::shared_ptr<detail::ConnectionState> m_state{};
    void* m_statement{};
    String m_sql{};
    Row m_current_row{};
    bool m_has_current_row{};
};

class Connection {
public:
    Connection() = default;
    ~Connection() noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    static Connection open(const String& path, int busy_timeout_ms = 30000);
    static Connection open_read_only(const String& path, int busy_timeout_ms = 30000);
    [[nodiscard]] static bool compiled_with_c_api() noexcept;
    [[nodiscard]] static String backend_description();

    [[nodiscard]] bool is_open() const noexcept;
    void close();
    void set_busy_timeout(int milliseconds);
    [[nodiscard]] int busy_timeout() const noexcept { return m_busy_timeout_ms; }

    [[nodiscard]] Statement prepare(const String& sql) const;
    [[nodiscard]] QueryResult execute(const String& sql, const BindParameters& parameters = {}) const;
    [[nodiscard]] std::size_t executemany(const String& sql, const std::vector<BindParameters>& parameter_sets) const;

    [[nodiscard]] Integer last_insert_rowid() const;
    [[nodiscard]] Integer changes() const;

    void begin(const String& mode = "DEFERRED");
    void commit();
    void rollback();
    void savepoint(const String& name);
    void release_savepoint(const String& name);
    void rollback_to_savepoint(const String& name);
    void vacuum(const std::optional<String>& schema = std::nullopt);

    [[nodiscard]] std::vector<TableInfo> schema(bool include_internal = false) const;
    [[nodiscard]] std::vector<ColumnInfo> table_columns(const String& table) const;

private:
    explicit Connection(std::shared_ptr<detail::ConnectionState> state, int busy_timeout_ms);
    void ensure_open(const String& query = {}) const;
    void execute_control(const String& sql);

    std::shared_ptr<detail::ConnectionState> m_state{};
    int m_busy_timeout_ms{};
};

}  // namespace cw::lib::sqlite
