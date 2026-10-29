#include "cw/lib/sqlite.hpp"

#include <algorithm>
#include <limits>
#include <mutex>
#include <sstream>
#include <type_traits>
#include <utility>

// 外部SQLiteを優先し、Windowsでは明示指定時にWindows SDKのWinSQLiteへ
// フォールバックする。いずれも対応ライブラリのリンク設定と対で有効化する。
#if defined(__has_include)
#  if defined(CW_ENABLE_SQLITE_C_API) && CW_ENABLE_SQLITE_C_API && __has_include(<sqlite3.h>)
#    define CW_SQLITE_C_API 1
#    define CW_SQLITE_WINSQLITE3 0
#    include <sqlite3.h>
#  elif defined(_WIN32) && defined(CW_ENABLE_WINSQLITE3_API) && CW_ENABLE_WINSQLITE3_API && __has_include(<winsqlite/winsqlite3.h>)
#    define CW_SQLITE_C_API 1
#    define CW_SQLITE_WINSQLITE3 1
#    include <winsqlite/winsqlite3.h>
#  else
#    define CW_SQLITE_C_API 0
#    define CW_SQLITE_WINSQLITE3 0
#  endif
#else
#  define CW_SQLITE_C_API 0
#  define CW_SQLITE_WINSQLITE3 0
#endif

namespace cw::lib::sqlite {
namespace {

[[noreturn]] void throw_conversion(const char* expected)
{
    throw std::invalid_argument(std::string("SQLite column is not ") + expected);
}

[[noreturn]] void throw_backend_unavailable(const String& query)
{
    throw SqliteError(k_backend_unavailable, k_backend_unavailable,
                      "SQLite backend is not compiled; link SQLite3 or enable and link Windows SDK WinSQLite3",
                      query);
}

String quote_identifier(const String& value)
{
    if (value.empty() || value.find('\0') != String::npos)
        throw SqliteError(k_invalid_argument, k_invalid_argument, "Invalid SQLite identifier", value);
    String result{"\""};
    for (const char character:value) {
        result.push_back(character);
        if (character=='"') result.push_back('"');
    }
    result.push_back('"');
    return result;
}

template<typename T>
const T& require_column_ref(const Column::Storage& storage, const char* expected)
{
    if (const auto* value = std::get_if<T>(&storage)) return *value;
    throw_conversion(expected);
}

}  // namespace

namespace detail {
struct ConnectionState {
    mutable std::recursive_mutex mutex;
    String path{};
    bool closed{true};
#if CW_SQLITE_C_API
    sqlite3* database{};
    ~ConnectionState() noexcept { if (database) sqlite3_close_v2(database); }
#endif
};
}  // namespace detail

SqliteError::SqliteError(int code, int extended_code, String message, String query)
    : std::runtime_error(message), m_code(code), m_extended_code(extended_code),
      m_message(std::move(message)), m_query(std::move(query))
{
}

ColumnType Column::type() const noexcept
{
    if (std::holds_alternative<std::monostate>(m_value)) return ColumnType::Null;
    if (std::holds_alternative<Integer>(m_value)) return ColumnType::Integer;
    if (std::holds_alternative<Real>(m_value)) return ColumnType::Real;
    if (std::holds_alternative<String>(m_value)) return ColumnType::Text;
    return ColumnType::Blob;
}

Integer Column::as_integer() const
{
    if (const auto* value = std::get_if<Integer>(&m_value)) return *value;
    if (const auto* value = std::get_if<Real>(&m_value)) return static_cast<Integer>(*value);
    throw_conversion("an integer");
}

Real Column::as_real() const
{
    if (const auto* value = std::get_if<Real>(&m_value)) return *value;
    if (const auto* value = std::get_if<Integer>(&m_value)) return static_cast<Real>(*value);
    throw_conversion("a real number");
}

const String& Column::as_text() const
{
    return require_column_ref<String>(m_value, "text");
}

const Bytes& Column::as_blob() const
{
    return require_column_ref<Bytes>(m_value, "a blob");
}

Row::Row(std::vector<String> names, std::vector<Column> columns)
    : m_names(std::move(names)), m_columns(std::move(columns))
{
}

const Column& Row::at(std::size_t index) const
{
    if (index >= m_columns.size()) throw std::out_of_range("SQLite row column index out of range");
    return m_columns[index];
}

const Column& Row::at(const String& name) const
{
    const auto it = std::find(m_names.begin(), m_names.end(), name);
    if (it == m_names.end()) throw std::out_of_range("SQLite row column name not found: " + name);
    return m_columns[static_cast<std::size_t>(std::distance(m_names.begin(), it))];
}

QueryResult::QueryResult(std::vector<String> names, std::vector<Row> rows)
    : m_names(std::move(names)), m_rows(std::move(rows))
{
}

const Row& QueryResult::at(std::size_t index) const
{
    if (index >= m_rows.size()) throw std::out_of_range("SQLite result row index out of range");
    return m_rows[index];
}

Statement::Statement(std::shared_ptr<detail::ConnectionState> state, void* statement, String sql)
    : m_state(std::move(state)), m_statement(statement), m_sql(std::move(sql))
{
}

Statement::~Statement() noexcept
{
    finalize();
}

Statement::Statement(Statement&& other) noexcept
    : m_state(std::move(other.m_state)), m_statement(std::exchange(other.m_statement, nullptr)),
      m_sql(std::move(other.m_sql)), m_current_row(std::move(other.m_current_row)),
      m_has_current_row(other.m_has_current_row)
{
    other.m_has_current_row = false;
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this == &other) return *this;
    finalize();
    m_state = std::move(other.m_state);
    m_statement = std::exchange(other.m_statement, nullptr);
    m_sql = std::move(other.m_sql);
    m_current_row = std::move(other.m_current_row);
    m_has_current_row = other.m_has_current_row;
    other.m_has_current_row = false;
    return *this;
}

void Statement::finalize() noexcept
{
#if CW_SQLITE_C_API
    if (m_statement && m_state) {
        std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
        sqlite3_finalize(static_cast<sqlite3_stmt*>(m_statement));
    }
#endif
    m_statement = nullptr;
    m_has_current_row = false;
}

void Statement::ensure_valid() const
{
    if (!m_state || !m_statement) throw SqliteError(k_closed_connection, k_closed_connection, "SQLite statement is closed", m_sql);
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    if (m_state->closed) throw SqliteError(k_closed_connection, k_closed_connection, "SQLite statement is closed", m_sql);
}

void Statement::check_bind_index(int index) const
{
    ensure_valid();
    if (index <= 0 || index > parameter_count()) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite bind index out of range", m_sql);
}

int Statement::parameter_count() const
{
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    return sqlite3_bind_parameter_count(static_cast<sqlite3_stmt*>(m_statement));
#else
    throw_backend_unavailable(m_sql);
#endif
}

int Statement::column_count() const
{
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    return sqlite3_column_count(static_cast<sqlite3_stmt*>(m_statement));
#else
    throw_backend_unavailable(m_sql);
#endif
}

String Statement::column_name(int index) const
{
    (void)index;
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    if (index < 0 || index >= sqlite3_column_count(static_cast<sqlite3_stmt*>(m_statement))) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite column index out of range", m_sql);
    const auto* name = sqlite3_column_name(static_cast<sqlite3_stmt*>(m_statement), index);
    return name ? String(name) : String{};
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::bind(int index, const BindValue& value)
{
    check_bind_index(index);
    std::visit([&](const auto& item) {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::monostate>) bind_null(index);
        else if constexpr (std::is_same_v<T, Integer>) bind_int64(index, item);
        else if constexpr (std::is_same_v<T, Real>) bind_double(index, item);
        else if constexpr (std::is_same_v<T, String>) bind_text(index, item);
        else if constexpr (std::is_same_v<T, Bytes>) bind_blob(index, item);
}, value);
}

void Statement::bind(int index, std::nullptr_t)
{
    bind_null(index);
}

void Statement::bind_null(int index)
{
    check_bind_index(index);
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_bind_null(static_cast<sqlite3_stmt*>(m_statement), index);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::bind_int64(int index, Integer value)
{
    (void)value;
    check_bind_index(index);
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_bind_int64(static_cast<sqlite3_stmt*>(m_statement), index, static_cast<sqlite3_int64>(value));
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::bind_double(int index, Real value)
{
    (void)value;
    check_bind_index(index);
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_bind_double(static_cast<sqlite3_stmt*>(m_statement), index, value);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::bind_text(int index, const String& value)
{
    (void)value;
    check_bind_index(index);
#if CW_SQLITE_C_API
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite text bind is too large", m_sql);
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_bind_text(static_cast<sqlite3_stmt*>(m_statement), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::bind_blob(int index, const Bytes& value)
{
    (void)value;
    check_bind_index(index);
#if CW_SQLITE_C_API
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite blob bind is too large", m_sql);
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = value.empty()
        ? sqlite3_bind_zeroblob(static_cast<sqlite3_stmt*>(m_statement), index, 0)
        : sqlite3_bind_blob(static_cast<sqlite3_stmt*>(m_statement), index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::clear_bindings()
{
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_clear_bindings(static_cast<sqlite3_stmt*>(m_statement));
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

void Statement::reset()
{
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    const int rc = sqlite3_reset(static_cast<sqlite3_stmt*>(m_statement));
    m_has_current_row = false;
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
#else
    throw_backend_unavailable(m_sql);
#endif
}

StepResult Statement::step()
{
    ensure_valid();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    auto* statement = static_cast<sqlite3_stmt*>(m_statement);
    const int rc = sqlite3_step(statement);
    if (rc == SQLITE_DONE) { m_has_current_row = false; return StepResult::Done; }
    if (rc != SQLITE_ROW) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_sql);
    const int count = sqlite3_column_count(statement); std::vector<String> names; std::vector<Column> columns;
    names.reserve(count); columns.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto* name = sqlite3_column_name(statement, i); names.emplace_back(name ? name : "");
        switch (sqlite3_column_type(statement, i)) {
        case SQLITE_INTEGER: columns.emplace_back(Column::Storage(static_cast<Integer>(sqlite3_column_int64(statement, i)))); break;
        case SQLITE_FLOAT: columns.emplace_back(Column::Storage(sqlite3_column_double(statement, i))); break;
        case SQLITE_TEXT: {
            const auto* text = sqlite3_column_text(statement, i); const int size = sqlite3_column_bytes(statement, i);
            columns.emplace_back(Column::Storage(String(reinterpret_cast<const char*>(text ? text : reinterpret_cast<const unsigned char*>("")), static_cast<std::size_t>(std::max(size, 0))))); break;
        }
        case SQLITE_BLOB: {
            const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, i)); const int size = sqlite3_column_bytes(statement, i);
            Bytes blob; if (data && size > 0) blob.assign(data, data + size); columns.emplace_back(Column::Storage(std::move(blob))); break;
        }
        default: columns.emplace_back(Column::Storage(std::monostate{})); break;
        }
    }
    m_current_row = Row(std::move(names), std::move(columns)); m_has_current_row = true; return StepResult::Row;
#else
    throw_backend_unavailable(m_sql);
#endif
}

const Row& Statement::current_row() const
{
    ensure_valid();
    if (!m_has_current_row) throw std::logic_error("SQLite statement has no current row");
    return m_current_row;
}

Connection::Connection(std::shared_ptr<detail::ConnectionState> state, int busy_timeout_ms)
    : m_state(std::move(state)), m_busy_timeout_ms(busy_timeout_ms)
{
}

Connection::~Connection() noexcept
{
    try { close(); } catch (...) {}
}

Connection::Connection(Connection&& other) noexcept
    : m_state(std::move(other.m_state)),
      m_busy_timeout_ms(std::exchange(other.m_busy_timeout_ms,0))
{
}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this==&other) return *this;
    try { close(); } catch (...) {
        // 例外を出さない移動代入では、旧接続の解放を ConnectionState の RAII に委ねる。
    }
    m_state=std::move(other.m_state);
    m_busy_timeout_ms=std::exchange(other.m_busy_timeout_ms,0);
    return *this;
}

Connection Connection::open(const String& path, int busy_timeout_ms)
{
    if (busy_timeout_ms < 0) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite busy timeout must not be negative");
#if CW_SQLITE_C_API
    auto state = std::make_shared<detail::ConnectionState>(); state->path = path;
    sqlite3* database = nullptr; const int rc = sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        const String message = database ? sqlite3_errmsg(database) : "sqlite3_open_v2 failed"; const int extended = database ? sqlite3_extended_errcode(database) : rc;
        if (database) sqlite3_close_v2(database); throw SqliteError(rc, extended, message, path);
    }
    sqlite3_extended_result_codes(database,1);
    state->database = database; state->closed = false; Connection result(std::move(state), busy_timeout_ms); result.set_busy_timeout(busy_timeout_ms); return result;
#else
    throw_backend_unavailable(path);
#endif
}

Connection Connection::open_read_only(const String& path, int busy_timeout_ms)
{
    if (busy_timeout_ms < 0) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite busy timeout must not be negative");
#if CW_SQLITE_C_API
    auto state = std::make_shared<detail::ConnectionState>(); state->path = path; sqlite3* database = nullptr;
    const int rc = sqlite3_open_v2(path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK) { const String message = database ? sqlite3_errmsg(database) : "sqlite3_open_v2 failed"; const int extended = database ? sqlite3_extended_errcode(database) : rc; if (database) sqlite3_close_v2(database); throw SqliteError(rc, extended, message, path); }
    sqlite3_extended_result_codes(database,1);
    state->database = database; state->closed = false; Connection result(std::move(state), busy_timeout_ms); result.set_busy_timeout(busy_timeout_ms); return result;
#else
    throw_backend_unavailable(path);
#endif
}

bool Connection::compiled_with_c_api() noexcept
{
#if CW_SQLITE_C_API
    return true;
#else
    return false;
#endif
}

String Connection::backend_description()
{
#if CW_SQLITE_WINSQLITE3
    return "Windows SDK WinSQLite3 C API";
#elif CW_SQLITE_C_API
    return "SQLite C API";
#else
    return "SQLite未導入（SQLite3またはWindows SDK WinSQLite3のリンクが必要）";
#endif
}

bool Connection::is_open() const noexcept
{
    if (!m_state) return false;
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    return !m_state->closed;
}

void Connection::ensure_open(const String& query) const
{
    if (!m_state) throw SqliteError(k_closed_connection, k_closed_connection, "SQLite connection is closed", query);
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    if (m_state->closed) throw SqliteError(k_closed_connection, k_closed_connection, "SQLite connection is closed", query);
}

void Connection::close()
{
    if (!m_state) return;
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex);
    if (m_state->closed) return;
    // Statement は ConnectionState を共有する。sqlite3_close_v2()
    // は Statement が残っている間ハンドルを使用不能な状態に
    // し、 close() 直前に ensure_valid() を通っ
    // たスレッドがそのハンドルを使う可能性がある。 そのため、ここでの
    // close は論理的な受付停止だけとし、全 Statement
    // と実行中操作が状態を解放した後、最後の所有者が物理的に閉じる。
    m_state->closed = true;
}

void Connection::set_busy_timeout(int milliseconds)
{
    if (milliseconds < 0) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite busy timeout must not be negative");
    ensure_open(m_state ? m_state->path : String{});
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex); const int rc = sqlite3_busy_timeout(m_state->database, milliseconds);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), m_state->path);
#else
    throw_backend_unavailable(m_state ? m_state->path : String{});
#endif
#if CW_SQLITE_C_API
    m_busy_timeout_ms = milliseconds;
#endif
}

Statement Connection::prepare(const String& sql) const
{
    ensure_open(sql);
#if CW_SQLITE_C_API
    if (sql.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite SQL text is too large", sql);
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex); sqlite3_stmt* statement = nullptr; const char* tail=nullptr;
    const int rc = sqlite3_prepare_v2(m_state->database, sql.c_str(), static_cast<int>(sql.size()), &statement, &tail);
    if (rc != SQLITE_OK) throw SqliteError(rc, sqlite3_extended_errcode(m_state->database), sqlite3_errmsg(m_state->database), sql);
    if (!statement) throw SqliteError(k_invalid_argument,k_invalid_argument,"SQLite SQL statement is empty",sql);
    if (tail && *tail) {
        sqlite3_stmt* extra=nullptr;
        const char* unused=nullptr;
        const int tail_rc=sqlite3_prepare_v2(m_state->database,tail,-1,&extra,&unused);
        if (tail_rc!=SQLITE_OK || extra) {
            if (extra) sqlite3_finalize(extra);
            sqlite3_finalize(statement);
            if (tail_rc!=SQLITE_OK)
                throw SqliteError(tail_rc,sqlite3_extended_errcode(m_state->database),sqlite3_errmsg(m_state->database),sql);
            throw SqliteError(k_invalid_argument,k_invalid_argument,"SQLite prepare accepts exactly one statement",sql);
        }
    }
    return Statement(m_state, statement, sql);
#else
    throw_backend_unavailable(sql);
#endif
}

QueryResult Connection::execute(const String& sql, const BindParameters& parameters) const
{
    ensure_open(sql);
    auto statement = prepare(sql); const auto expected = statement.parameter_count();
    if (parameters.size() != static_cast<std::size_t>(expected)) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite parameter count does not match SQL", sql);
    for (std::size_t i = 0; i < parameters.size(); ++i) statement.bind(static_cast<int>(i + 1), parameters[i]);
    std::vector<String> names; const int count = statement.column_count(); names.reserve(count); for (int i = 0; i < count; ++i) names.push_back(statement.column_name(i));
    std::vector<Row> rows; while (statement.step() == StepResult::Row) rows.push_back(statement.current_row()); return QueryResult(std::move(names), std::move(rows));
}

std::size_t Connection::executemany(const String& sql, const std::vector<BindParameters>& parameter_sets) const
{
    ensure_open(sql);
    auto statement = prepare(sql); std::size_t executed = 0;
    const auto expected = statement.parameter_count();
    for (const auto& parameters : parameter_sets) { if (parameters.size() != static_cast<std::size_t>(expected)) throw SqliteError(k_invalid_argument, k_invalid_argument, "SQLite parameter count does not match SQL", sql); statement.clear_bindings(); statement.reset(); for (std::size_t i = 0; i < parameters.size(); ++i) statement.bind(static_cast<int>(i + 1), parameters[i]); while (statement.step() == StepResult::Row) {} ++executed; }
    return executed;
}

Integer Connection::last_insert_rowid() const
{
    ensure_open();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex); return static_cast<Integer>(sqlite3_last_insert_rowid(m_state->database));
#else
    throw_backend_unavailable({});
#endif
}

Integer Connection::changes() const
{
    ensure_open();
#if CW_SQLITE_C_API
    std::lock_guard<std::recursive_mutex> guard(m_state->mutex); return static_cast<Integer>(sqlite3_changes64(m_state->database));
#else
    throw_backend_unavailable({});
#endif
}

void Connection::execute_control(const String& sql)
{
    (void)execute(sql);
}

void Connection::begin(const String& mode)
{
    if (mode != "DEFERRED" && mode != "IMMEDIATE" && mode != "EXCLUSIVE") throw SqliteError(k_invalid_argument, k_invalid_argument, "Invalid SQLite transaction mode", mode);
    execute_control("BEGIN " + mode);
}

void Connection::commit() { execute_control("COMMIT"); }
void Connection::rollback() { execute_control("ROLLBACK"); }

void Connection::savepoint(const String& name) { execute_control("SAVEPOINT " + quote_identifier(name)); }
void Connection::release_savepoint(const String& name) { execute_control("RELEASE SAVEPOINT " + quote_identifier(name)); }
void Connection::rollback_to_savepoint(const String& name) { execute_control("ROLLBACK TO SAVEPOINT " + quote_identifier(name)); }

void Connection::vacuum(const std::optional<String>& schema_name)
{
    execute_control(schema_name ? "VACUUM " + quote_identifier(*schema_name) : "VACUUM");
}

std::vector<TableInfo> Connection::schema(bool include_internal) const
{
    const String sql = include_internal ? "SELECT name,type,sql FROM sqlite_master ORDER BY type,name" : "SELECT name,type,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name";
    const auto result = execute(sql); std::vector<TableInfo> tables; tables.reserve(result.size());
    for (const auto& row : result) { TableInfo table; table.name = row.at(0).as_text(); table.type = row.at(1).as_text(); if (!row.at(2).is_null()) table.sql = row.at(2).as_text(); tables.push_back(std::move(table)); }
    return tables;
}

std::vector<ColumnInfo> Connection::table_columns(const String& table) const
{
    const auto result = execute("PRAGMA table_info(" + quote_identifier(table) + ")"); std::vector<ColumnInfo> columns; columns.reserve(result.size());
    for (const auto& row : result) { ColumnInfo info; info.cid = row.at(0).as_integer(); info.name = row.at(1).as_text(); info.declared_type = row.at(2).as_text(); info.not_null = row.at(3).as_integer() != 0; if (!row.at(4).is_null()) info.default_value = row.at(4).as_text(); info.primary_key = row.at(5).as_integer() != 0; columns.push_back(std::move(info)); }
    return columns;
}

}  // namespace cw::lib::sqlite
