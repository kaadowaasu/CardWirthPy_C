#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/cwl.hpp"
#include "cwxeditor_src/core/cwx/perf.hpp"
#include "cwxeditor_src/core/cwx/imagesize.hpp"
#include "cwxeditor_src/core/cwx/msgutils.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <exception>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace cwxeditor::cwx::summary {
namespace {

std::atomic<std::uint64_t> next_summary_id{1};
std::atomic<std::uint64_t> next_backup_id{1};

class SummaryPathProxy final : public path::CWXPath {
public:
    SummaryPathProxy(Summary* summary,
                     std::weak_ptr<path::OwnerLifetimeToken> lifetime) noexcept
        : summary_(summary), lifetime_(std::move(lifetime)) {}

    [[nodiscard]] lib::Owner<path::CWXPath> retainForCallback() override {
        const auto guard = liveSummary();
        return guard ? summary_->retainForCallback() : lib::Owner<path::CWXPath>{};
    }
    [[nodiscard]] String cwxPath(const bool id) override {
        const auto guard = liveSummary();
        return guard ? summary_->cwxPath(id) : String{};
    }
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(StringView value) override {
        const auto guard = liveSummary();
        return guard ? summary_->findCWXPath(value) : lib::Owner<path::CWXPath>{};
    }
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds() override {
        const auto guard = liveSummary();
        return guard ? summary_->cwxChilds()
                     : lib::Array<lib::Observer<path::CWXPath>>{};
    }
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override { return {}; }
    void changed() override {
        const auto guard = liveSummary();
        if (guard) summary_->changed();
    }

private:
    [[nodiscard]] std::shared_ptr<path::OwnerLifetimeToken> liveSummary() const noexcept {
        auto lifetime = lifetime_.lock();
        return lifetime && lifetime->alive.load(std::memory_order_acquire)
            ? lifetime : std::shared_ptr<path::OwnerLifetimeToken>{};
    }

    Summary* summary_{};
    std::weak_ptr<path::OwnerLifetimeToken> lifetime_;
};

template <typename OwnerType>
lib::Owner<OwnerType> sharedSummaryOwner(Summary& summary) {
    auto self = summary.weak_from_this().lock();
    if (!self) return {};
    return lib::Owner<OwnerType>(std::move(self), static_cast<OwnerType*>(&summary));
}

template <typename Pointer>
auto& requiredScenarioObject(const Pointer& value) {
    if (!value) throw std::invalid_argument("Null scenario child");
    return *value;
}

String fileLoadErrorMessage(const std::exception_ptr& error) {
    if (!error) throw std::invalid_argument("Null file loading exception");
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& value) {
        return value.what();
    }
}

std::exception_ptr retainedFileLoadError(const std::exception& error) {
    const auto retained = std::current_exception();
    if (retained) {
        try {
            std::rethrow_exception(retained);
        } catch (const std::exception& value) {
            if (&value == &error) return retained;
        } catch (...) {
        }
    }
    // DのGCが管理するクラス参照と異なり、任意のネイティブ例外参照はオブジェク
    // トや動的な型を保持できない。処理中の例外ハンドラーの外では、例外を黙って
    // 基底型へ切り詰めず、所有するexception_ptr入力を必須とする。
    throw std::invalid_argument("Use exception_ptr to retain an uncaught file loading exception");
}

template <typename Collection, typename Visit>
void visitScenarioSlice(const Collection& values, Visit visit) {
    // モデルのコールバックをまたいで、Dのforeach記述子に含まれるオブジェクトを保持する。
    const auto retained = values;
    for (const auto& value : retained) visit(requiredScenarioObject(value));
}

std::vector<lib::Owner<path::CWXPath>> retainedChildPaths(path::CWXPath& value) {
    const auto children = value.cwxChilds();
    std::vector<lib::Owner<path::CWXPath>> retained;
    retained.reserve(children.size());
    // ネイティブの境界は監視参照を返す。Dが新しいクラス参照配列を作
    // るのと同様に、最初の再帰呼び出しの前にすべての兄弟を保持する。
    for (const auto& child : children) retained.push_back(child.lock());
    return retained;
}

[[nodiscard]] Path canonicalWithMissingTail(const Path& value) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(value, error).lexically_normal();
    if (error) throw std::filesystem::filesystem_error("absolute", value, error);

    auto probe = absolute;
    Path tail;
    for (;;) {
        error.clear();
        const auto status = std::filesystem::symlink_status(probe, error);
        if (!error && std::filesystem::exists(status)) {
            const auto base = std::filesystem::canonical(probe, error);
            if (error)
                throw std::filesystem::filesystem_error("canonical", probe, error);
            return tail.empty() ? base : (base / tail).lexically_normal();
        }
        if (probe == probe.root_path() || probe.empty()) {
            if (error)
                throw std::filesystem::filesystem_error("symlink_status", probe, error);
            throw SummaryException("Unable to resolve path");
        }
        const auto name = probe.filename();
        if (!name.empty()) tail = tail.empty() ? name : name / tail;
        probe = probe.parent_path();
    }
}

template <typename T>
[[nodiscard]] auto resourceCounterId(const std::uint64_t id) {
    if constexpr (std::is_same_v<T, Area>) return usecounter::toAreaId(id);
    else if constexpr (std::is_same_v<T, Battle>) return usecounter::toBattleId(id);
    else if constexpr (std::is_same_v<T, Package>) return usecounter::toPackageId(id);
    else if constexpr (std::is_same_v<T, CastCard>) return usecounter::toCastId(id);
    else if constexpr (std::is_same_v<T, SkillCard>) return usecounter::toSkillId(id);
    else if constexpr (std::is_same_v<T, ItemCard>) return usecounter::toItemId(id);
    else if constexpr (std::is_same_v<T, BeastCard>) return usecounter::toBeastId(id);
    else if constexpr (std::is_same_v<T, InfoCard>) return usecounter::toInfoId(id);
}

template <typename T>
void changeResourceReference(Summary& owner, const std::uint64_t oldId,
                             const std::uint64_t newId) {
    if (oldId == newId) return;
    if (const auto counter = owner.useCounter()) {
        counter->change(resourceCounterId<T>(oldId), resourceCounterId<T>(newId));
    }
}

[[nodiscard]] Path fsPath(StringView value) {
    const auto utf8 = std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size());
    return Path(utf8);
}

[[nodiscard]] String utf8Path(const Path& value) {
    const auto encoded = value.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

// シナリオの絶対パスをStorageが必要とする論理パスへ変
// 換する。相対パスはすでに論理パスとし、絶対パスは設定され
// たマウント内に収まる場合だけ受け付ける。変換をここに置き
// 、Summaryの従来のネイティブファイル用代替経路が、
// ホストのStorageルート外へ誤って出ないようにする。
[[nodiscard]] std::optional<Path> storageRelativePath(const Path& value,
                                                      const Storage& storage) {
    if (!value.has_root_name() && !value.has_root_directory()) {
        const auto normalized = value.lexically_normal();
        for (const auto& component : normalized) {
            if (component == "..") return std::nullopt;
        }
        return normalized.empty() || normalized == "."
            ? std::optional<Path>{Path{}}
            : std::optional<Path>{normalized};
    }
    const auto root = storage.root();
    if (root.empty()) return std::nullopt;
    std::error_code error;
    const auto absoluteRoot = std::filesystem::absolute(root, error).lexically_normal();
    if (error) return std::nullopt;
    error.clear();
    const auto absoluteValue = std::filesystem::absolute(value, error).lexically_normal();
    if (error) return std::nullopt;
    const auto relative = absoluteValue.lexically_relative(absoluteRoot).lexically_normal();
    if (relative.empty() || relative == "." || relative.has_root_name() ||
        relative.has_root_directory()) return std::nullopt;
    for (const auto& component : relative) {
        if (component == "..") return std::nullopt;
    }
    return relative;
}

void makeDirectory(const Path& path, Storage* storage) {
    if (storage) {
        if (const auto relative = storageRelativePath(path, *storage)) {
            if (!storage->mkdir(*relative, true)) {
                auto diagnostic = storage->last_error();
                if (diagnostic.empty()) diagnostic = "Storage::mkdir failed";
                throw SummaryException("Unable to create directory: " +
                                       utf8Path(path) + ": " + diagnostic);
            }
            return;
        }
    }
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) throw SummaryException("Unable to create directory: " +
                                      utf8Path(path) + ": " + error.message());
}

[[nodiscard]] bool storageExists(const Path& path, Storage* storage) {
    if (storage) {
        if (const auto relative = storageRelativePath(path, *storage))
            return storage->exists(*relative);
    }
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

void removeEmptyDirectory(const Path& path, Storage* storage) {
    const auto relative = storage ? storageRelativePath(path, *storage) : std::optional<Path>{};
    bool empty;
    if (relative) {
        const auto stat = storage->stat(*relative);
        if (!stat || !stat->is_directory()) return;
        const auto entries = storage->list(*relative);
        const auto diagnostic = storage->last_error();
        if (!diagnostic.empty()) throw SummaryException("Unable to list directory: " + utf8Path(path) + ": " + diagnostic);
        empty = entries.empty();
    } else {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) return;
        empty = std::filesystem::is_empty(path, error);
        if (error) throw SummaryException("Unable to list directory: " + utf8Path(path) + ": " + error.message());
    }
    if (!empty) return;
    if (relative) {
        if (!storage->remove(*relative, false))
            throw SummaryException("Unable to remove empty category: " + utf8Path(path) + ": " + storage->last_error());
    } else {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) throw SummaryException("Unable to remove empty category: " + utf8Path(path) + ": " + error.message());
    }
}

void removeTree(const Path& path, Storage* storage) {
    if (storage) {
        if (const auto relative = storageRelativePath(path, *storage)) {
            if (!storage->remove(*relative, true)) {
                auto diagnostic = storage->last_error();
                if (diagnostic.empty()) diagnostic = "Storage::remove failed";
                throw SummaryException("Unable to remove temporary data: " +
                                       utf8Path(path) + ": " + diagnostic);
            }
            return;
        }
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) throw SummaryException("Unable to remove temporary data: " +
                                      utf8Path(path) + ": " + error.message());
}

void copyFile(const Path& source, const Path& destination, Storage* storage) {
    if (storage) {
        const auto sourceRelative = storageRelativePath(source, *storage);
        const auto destinationRelative = storageRelativePath(destination, *storage);
        if (sourceRelative && destinationRelative) {
            if (!storage->copy(*sourceRelative, *destinationRelative, true)) {
                auto diagnostic = storage->last_error();
                if (diagnostic.empty()) diagnostic = "Storage::copy failed";
                throw SummaryException("Unable to copy file: " +
                                       utf8Path(source) + ": " + diagnostic);
            }
            return;
        }
    }
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) throw SummaryException("Unable to create copy directory: " +
                                      utf8Path(destination.parent_path()) + ": " +
                                      error.message());
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) throw SummaryException("Unable to copy file: " + utf8Path(source) +
                                      ": " + error.message());
}

[[nodiscard]] String lower(String value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

[[nodiscard]] bool equalName(StringView first, StringView second) {
    return lower(String(first)) == lower(String(second));
}

[[nodiscard]] bool equalPath(StringView first, StringView second) {
    // Summaryが相対scenarioPathを保持していても、archive::zip
    // は絶対パスで訪問する。両方の表記を同じ絶対基準から比較する。
    return lower(utils::nabs(first)) == lower(utils::nabs(second));
}

[[nodiscard]] bool hasExtension(StringView value, StringView extension) {
    const auto p = fsPath(value);
    return equalName(utf8Path(p.extension()), extension);
}

[[nodiscard]] bool isXmlFile(const Path& value) {
    return equalName(utf8Path(value.extension()), ".xml");
}

[[nodiscard]] bool isTemporaryName(StringView value) {
    // TEMP_REGは大文字小文字変換も複数行モードも使わない。ファイル名の規則
    // とは独立に元のバイト列を照合し、末尾部分が入力終端に達することを要求する。
    const auto name = value;
    constexpr StringView marker = ".cwxeditor_temp";
    const auto pos = name.rfind(marker);
    if (pos == std::string::npos) return false;
    if (pos + marker.size() == name.size()) return true;
    if (name[pos + marker.size()] != '(' || name.back() != ')') return false;
    if (pos + marker.size() + 2 >= name.size()) return false;
    for (std::size_t i = pos + marker.size() + 1; i + 1 < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
    }
    return true;
}

constexpr std::array<StringView, 8> managedXmlCategories{
    PATH_CAST, PATH_SKILL, PATH_ITEM, PATH_BEAST,
    PATH_INFO, PATH_AREA, PATH_BATTLE, PATH_PACKAGE};

class XmlSaveRollback final {
public:
    explicit XmlSaveRollback(Path output)
        : output_(std::move(output)) {
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto sequence = next_backup_id.fetch_add(1, std::memory_order_relaxed);
        auto name = utf8Path(output_.filename());
        if (name.empty()) name = "scenario";
        snapshot_ = output_.parent_path() /
            fsPath(".cwxeditor_rollback_" + name + "_" +
                   std::to_string(timestamp) + "_" + std::to_string(sequence));
    }

    XmlSaveRollback(const XmlSaveRollback&) = delete;
    XmlSaveRollback& operator=(const XmlSaveRollback&) = delete;

    ~XmlSaveRollback() {
        if (!preserve_snapshot_) cleanup();
    }

    void capture() {
        if (captured_) return;
        std::error_code error;
        std::filesystem::create_directories(snapshot_, error);
        if (error) fail("create rollback directory", snapshot_, error);
        try {
            copyIfPresent(output_ / "Summary.xml", snapshot_ / "Summary.xml");
            for (const auto category : managedXmlCategories) {
                const auto source = output_ / String(category);
                error.clear();
                if (!std::filesystem::exists(source, error)) {
                    if (error) fail("inspect XML directory", source, error);
                    continue;
                }
                if (!std::filesystem::is_directory(source, error)) {
                    if (error) fail("inspect XML directory", source, error);
                    continue;
                }
                for (std::filesystem::directory_iterator it(source, error), end;
                     it != end && !error; it.increment(error)) {
                    if (it->is_regular_file(error) && isXmlFile(it->path())) {
                        copyIfPresent(it->path(), snapshot_ / String(category) /
                                                   it->path().filename());
                    }
                }
                if (error) fail("enumerate XML directory", source, error);
            }
            captured_ = true;
        } catch (...) {
            cleanup();
            throw;
        }
    }

    void commit() noexcept {
        captured_ = false;
        cleanup();
    }

    void restore(FileSync& sync) {
        if (!captured_) return;
        try {
            removeManagedXml();
            std::error_code error;
            for (std::filesystem::recursive_directory_iterator it(snapshot_, error), end;
                 it != end && !error; it.increment(error)) {
                if (!it->is_regular_file(error)) continue;
                const auto relative = std::filesystem::relative(it->path(), snapshot_, error);
                if (error) fail("resolve rollback file", it->path(), error);
                const auto data = utils::readBinary(it->path());
                filesync::writeFile(
                    output_ / relative,
                    lib::ByteView(data.data(), data.size()), {}, {}, sync.storage());
            }
            if (error) fail("enumerate rollback files", snapshot_, error);
            captured_ = false;
            cleanup();
        } catch (...) {
            // 自動復旧自体が失敗した場合は、保存した状態を保持する。
            // 最後の整合したXML一式の完全なコピーなので、失敗した
            // Storageバックエンドに依存せず手動復元できる。
            preserve_snapshot_ = true;
            throw;
        }
    }

    [[nodiscard]] String snapshotPath() const { return utf8Path(snapshot_); }

private:
    [[noreturn]] static void fail(StringView operation, const Path& path,
                                  const std::error_code& error) {
        throw SummaryException(String(operation) + ": " + utf8Path(path) +
                               ": " + error.message());
    }

    void copyIfPresent(const Path& source, const Path& destination) {
        std::error_code error;
        if (!std::filesystem::exists(source, error)) {
            if (error) fail("inspect rollback source", source, error);
            return;
        }
        if (!std::filesystem::is_regular_file(source, error)) {
            if (error) fail("inspect rollback source", source, error);
            return;
        }
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) fail("create rollback parent", destination.parent_path(), error);
        std::filesystem::copy_file(source, destination,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        if (error) fail("copy rollback source", source, error);
    }

    void removeManagedXml() {
        std::error_code error;
        std::filesystem::remove(output_ / "Summary.xml", error);
        if (error) fail("remove failed Summary.xml", output_ / "Summary.xml", error);
        for (const auto category : managedXmlCategories) {
            const auto directory = output_ / String(category);
            error.clear();
            if (!std::filesystem::exists(directory, error)) {
                if (error) fail("inspect failed XML directory", directory, error);
                continue;
            }
            if (!std::filesystem::is_directory(directory, error)) {
                if (error) fail("inspect failed XML directory", directory, error);
                continue;
            }
            for (std::filesystem::directory_iterator it(directory, error), end;
                 it != end && !error;) {
                const auto file = it->path();
                it.increment(error);
                if (error) break;
                std::error_code typeError;
                if (!std::filesystem::is_regular_file(file, typeError) ||
                    typeError || !isXmlFile(file)) continue;
                std::filesystem::remove(file, typeError);
                if (typeError) fail("remove failed XML", file, typeError);
            }
            if (error) fail("enumerate failed XML directory", directory, error);
        }
    }

    void cleanup() noexcept {
        std::error_code ignored;
        std::filesystem::remove_all(snapshot_, ignored);
    }

    Path output_;
    Path snapshot_;
    bool captured_{};
    bool preserve_snapshot_{};
};

[[nodiscard]] lib::Owner<FileSync> syncOwner(FileSync& sync) {
    return lib::Owner<FileSync>(&sync, [](FileSync*) noexcept {});
}

// DのtoNewDirectoryはClassicの直列化前に実素材をコ
// ピーする。Classicの符号化処理から独立させ、通常の素材書き込みと
// 同じStorageの振り分けを使う。Dの欠落ファイルがあっても継続する
// 動作とは異なり、既存のC++の公開前に失敗させる安全規則を維持する。
void copyClassicMaterials(const Summary& summary, const Path& destination,
                          Storage* storage, const SaveOption& option,
                          const lib::Callback<void(String)>& showWarn) {
    const auto source = fsPath(summary.scenarioPath());
    const auto sourceCanonical = canonicalWithMissingTail(source);
    const auto targetCanonical = canonicalWithMissingTail(destination);
    if (sourceCanonical == targetCanonical) return;
    const auto relative = targetCanonical.lexically_relative(sourceCanonical);
    if (!relative.empty() && !relative.has_root_name() &&
        !relative.has_root_directory() && *relative.begin() != "..")
        throw SummaryException("The save destination must not be inside the source scenario: " +
                               utf8Path(destination));
    struct Entry { Path path; bool directory; bool regular; };
    const auto entries = [storage](const Path& from) {
        std::vector<Entry> result;
        if (storage) {
            if (const auto backendPath = storageRelativePath(from, *storage)) {
                const auto listed = storage->list(*backendPath);
                const auto error = storage->last_error();
                if (!error.empty())
                    throw SummaryException("Unable to enumerate scenario material: " +
                                           utf8Path(from) + ": " + error);
                for (const auto& entry : listed) {
                    // Storageのリンクは巡回の境界とし、
                    // シナリオ外のパスをたどってコピーしない。
                    if (entry.stat.directory_link ||
                        entry.stat.type == ::cardwirth::platform::StorageEntryType::Symlink)
                        continue;
                    result.push_back({from / fsPath(entry.name),
                                      entry.stat.is_directory(), entry.stat.is_file()});
                }
                return result;
            }
        }
        for (const auto& entry : std::filesystem::directory_iterator(from)) {
            const auto status = entry.symlink_status();
            if (std::filesystem::is_symlink(status)) continue;
            result.push_back({entry.path(), std::filesystem::is_directory(status),
                              std::filesystem::is_regular_file(status)});
        }
        return result;
    };
    try {
        std::vector<Entry> roots;
        option.runIo([&] { roots = entries(source); });
        // Dが除外するのはシナリオのルートだけであり、通常のサブディレクト
        // リ内の同名素材は除外しない。モデルの分類は呼び出し側スレッドで行
        // い、ワーカーにはパスとファイルシステムのメタデータだけを渡す。
        std::erase_if(roots, [&](const Entry& entry) {
            return summary.isSystemFile(utf8Path(entry.path), entry.directory) ||
                   equalName(utf8Path(entry.path.filename()), "cwxeditor.lock");
        });
        option.runIo([&] {
            std::function<void(const Entry&, const Path&)> copy;
            copy = [&](const Entry& entry, const Path& output) {
                if (entry.directory) {
                    makeDirectory(output, storage);
                    for (const auto& child : entries(entry.path))
                        copy(child, output / child.path.filename());
                } else if (entry.regular) copyFile(entry.path, output, storage);
            };
            makeDirectory(destination, storage);
            for (const auto& entry : roots)
                copy(entry, destination / entry.path.filename());
        });
    } catch (const std::exception& error) {
        // 診断はUIのコールバックなので、I/Oワーカー上で実行してはならない。
        if (showWarn) showWarn(error.what());
        throw;
    }
}

struct ClassicTemporaryOutput final {
    Path path;
    Storage* storage{};
    FileSync* sync{};
    bool transferred{};
    ~ClassicTemporaryOutput() {
        if (!transferred) {
            try { if (sync) sync->sync(); } catch (...) {}
            try { removeTree(path, storage); } catch (...) {}
        }
    }
};

// Classic出力の直列化は呼び出し側で行い、完成したディレクトリだけをアーカ
// イブのワーカーへ渡す。入力と公開出力の両方について、待機中の処理を完了させる。
void archiveSavedDirectory(String directory, Path output,
                           const SaveOption& option, FileSync& sync) {
    option.runIo([&sync] { sync.sync(); });
    const auto extension = lower(utf8Path(output.extension()));
    if (extension == ".cab") {
        option.runArchive([directory, output] {
            if (!cab::cab(directory, utf8Path(output), [](String value) {
                return !equalName(utf8Path(fsPath(value).filename()), "cwxeditor.lock");
            })) throw SummaryException("Unable to create CAB: " + utf8Path(output));
        });
    } else {
        lib::Bytes bytes;
        option.runArchive([directory, &bytes] {
            lib::Array<lib::Bytes> data;
            // Classic出力では最上位フォルダーを含め、ファイル名はuseSysEnc=trueで格納する
            // 。ネイティブ実装ではWindowsでShift-JISへ変換し、他環境では元のバイト列を使う。
            auto file = archive::zip(fsPath(directory), true,
                [](String value) {
                    return equalName(utf8Path(fsPath(value).filename()), "cwxeditor.lock");
                }, true, data);
            bytes = file->build();
        });
        filesync::writeFile(output, bytes, syncOwner(sync));
    }
    option.runIo([&sync] { sync.sync(); });
}

[[nodiscard]] Summary::Owner summaryOwner(Summary& value) {
    try {
        return value.shared_from_this();
    } catch (const std::bad_weak_ptr&) {
        return Summary::Owner(&value, [](Summary*) noexcept {});
    }
}

[[nodiscard]] std::uint64_t cardId(const lib::Owner<card::Card>& value) {
    return value ? value->id() : 0U;
}

[[nodiscard]] String cardName(const lib::Owner<card::Card>& value) {
    return value ? value->name() : String{};
}

template <typename Collection>
[[nodiscard]] typename Collection::value_type findCard(const Collection& values,
                                                       std::uint64_t id) {
    using Owner = typename Collection::value_type;
    const auto index = utils::qsearch(values, id,
        [](const Owner& value, std::uint64_t key) {
            // Dは選択した中点の要素を参照する。null項目を飛ばし
            // たり、線形の代替検索で別の重複要素を選んだりしない。
            if (!value) throw std::invalid_argument("null card search element");
            return utils::dcmp(value->id(), key);
        });
    return index == -1 ? Owner{} : values[static_cast<std::size_t>(index)];
}

template <typename Collection>
[[nodiscard]] std::uint64_t nextCardId(const Collection& values) {
    std::uint64_t result = 1;
    for (const auto& value : values) {
        if (value) result = std::max(result, value->id() + 1U);
    }
    return result;
}

// コンテナーの基本操作は、下記の資源変更規則を共有する。
// vectorは読み込み側が完全なカード集合を準備する間
// だけ使う。正規のSharedSlice集合は、以前の記
// 述子が保持する項目を書き換えずに構造変更を公開する。
template <typename Collection>
void appendCollection(Collection& values, typename Collection::value_type value) {
    if constexpr (requires { values.push_back(std::move(value)); })
        values.push_back(std::move(value));
    else
        lib::appendToSlice(values, std::move(value));
}

template <typename Collection>
void eraseCollectionAt(Collection& values, std::size_t index) {
    if (index >= values.size()) throw std::out_of_range("Summary collection erase index");
    if constexpr (requires { values.erase(values.begin()); })
        values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
    else
        lib::eraseFromSlice(values, index);
}

template <typename T>
[[nodiscard]] const path::CWXPath* pathPointer(const lib::Owner<T>& value) {
    if (!value) return nullptr;
    if constexpr (std::is_base_of_v<area::AbstractArea, T>) {
        return static_cast<const path::CWXPath*>(
            static_cast<const area::AbstractArea*>(value.get()));
    } else if constexpr (std::is_base_of_v<card::Card, T>) {
        return static_cast<const path::CWXPath*>(
            static_cast<const card::Card*>(value.get()));
    } else {
        return static_cast<const path::CWXPath*>(value.get());
    }
}

template <typename T>
[[nodiscard]] lib::Owner<path::CWXPath> pathOwner(const lib::Owner<T>& value) {
    if (!value) return {};
    if constexpr (std::is_base_of_v<area::AbstractArea, T>) {
        return lib::Owner<path::CWXPath>(
            value,
            static_cast<path::CWXPath*>(
                static_cast<area::AbstractArea*>(value.get())));
    } else if constexpr (std::is_base_of_v<card::Card, T>) {
        return lib::Owner<path::CWXPath>(
            value,
            static_cast<path::CWXPath*>(static_cast<card::Card*>(value.get())));
    } else {
        return std::static_pointer_cast<path::CWXPath>(value);
    }
}

template <typename Collection>
void sortCards(Collection& values) {
    std::stable_sort(values.begin(), values.end(), [](const auto& first,
                                                       const auto& second) {
        if (!first) return static_cast<bool>(second);
        if (!second) return false;
        return first->id() < second->id();
    });
}

template <typename Collection>
void sortAreas(Collection& values) {
    if (values.size() < 2U) return;
    std::stable_sort(values.begin(), values.end(), [](const auto& first,
                                                       const auto& second) {
        if (!first) return static_cast<bool>(second);
        if (!second) return false;
        return first->id() < second->id();
    });
}

[[nodiscard]] bool ignoredPath(const lib::Array<String>& ignorePaths,
                               StringView value) {
    const auto name = utf8Path(fsPath(value).filename());
    // Dはフォルダーも含め、訪問した各basenameへファイル名のグロブを適用
    // する。素材・CABの呼び出し側と共有するEditorの判定を再利用する。
    return ::cwxeditor::cwx::utils::containsPath(ignorePaths, name);
}

void replaceAll(String& value, StringView from, StringView to) {
    if (from.empty()) return;
    std::size_t position = 0;
    while ((position = value.find(from, position)) != String::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

[[nodiscard]] String encodeNewlines(String value) {
    replaceAll(value, "\r\n", "\n");
    replaceAll(value, "\r", "\n");
    return value;
}

[[nodiscard]] String joinLines(const lib::Array<String>& values) {
    String result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result.push_back('\n');
        result += values[i];
    }
    return result;
}

[[nodiscard]] String nameForXml(std::uint64_t id, StringView name,
                                const SaveOption& option) {
    auto number = std::to_string(id);
    if (number.size() < 2) number.insert(number.begin(), '0');
    if (option.xmlFileNameIsIDOnly) {
        return utils::cleanFileName(number) + ".xml";
    }
    return utils::cleanFileName(number + "_" + String(name)) + ".xml";
}

[[nodiscard]] bool isDirectory(const Path& value) {
    std::error_code error;
    return std::filesystem::is_directory(value, error) && !error;
}

[[nodiscard]] bool isRegularFile(const Path& value) {
    std::error_code error;
    return std::filesystem::is_regular_file(value, error) && !error;
}

[[nodiscard]] String relativeKey(const Path& root, const Path& value) {
    std::error_code error;
    auto relative = std::filesystem::relative(value, root, error);
    if (error) relative = value.lexically_relative(root);
    // 実際の表記を保持する。小文字化すると、大文字小文字を区別するプ
    // ロバイダーで別ファイルを同一視し、後で誤った素材を更新し得る。
    return utf8Path(relative.lexically_normal());
}

[[nodiscard]] bool retainedXmlEntry(const Path& root, StringView value,
                                    const XmlFileTree& xml) {
    if (xml.empty()) return false;
    const auto relative = fsPath(relativeKey(root, fsPath(value)));
    const auto directory = xml.find(utf8Path(relative.parent_path()));
    return directory != xml.end() &&
           directory->second.contains(utf8Path(relative.filename()));
}

[[nodiscard]] bool ignoredZipEntry(const Path& root,
                                   const lib::Array<String>& ignorePaths,
                                   const XmlFileTree& xml, StringView value) {
    const auto lock = utf8Path(root / "cwxeditor.lock");
    return equalPath(value, lock) || ignoredPath(ignorePaths, value) ||
           retainedXmlEntry(root, value, xml);
}

[[nodiscard]] String retainedXmlMemberName(const Path& root, bool isWsn,
                                           StringView directory, StringView name) {
    String member = (directory.empty() ? String{} : String(directory) + "/") + String(name);
    // Dは通常のZIPで、メモリ内XMLの先頭にシナリオフォルダーを付け
    // る。共通の実ファイル用アーカイブ書き込みと同じ正規化ルートを使い、
    // メモリと実ファイルのメンバーを一つのシナリオディレクトリに収める。
    if (!isWsn) {
        const auto prefix = std::filesystem::absolute(root).lexically_normal().filename();
        if (!prefix.empty()) member = utf8Path(prefix) + "/" + member;
    }
    return member;
}

[[nodiscard]] Path findSummaryFile(const Path& root, bool& legacy, const LoadOption& option) {
    legacy = false;
    if (isRegularFile(root)) {
        const auto base = utf8Path(root.filename());
        if (equalName(base, "Summary.xml")) return root;
        if (equalName(base, "Summary.wsm")) {
            legacy = true;
            return root;
        }
    }
    if (!isDirectory(root)) return {};
    std::error_code error;
    Path selected;
    int selectedDepth = std::numeric_limits<int>::max();
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         it != end && !error; it.increment(error)) {
        // キャンセルと進捗表示に対応する。選択順はD版と同じに保つ。
        option.reportProgress("概要ファイルの探索", utf8Path(it->path()));
        if (!it->is_regular_file(error)) continue;
        const auto base = utf8Path(it->path().filename());
        const bool xml = equalName(base, "Summary.xml");
        const bool classic = equalName(base, "Summary.wsm");
        if (!xml && !classic) continue;
        // Dは浅い階層のシナリオを選ぶ。同じ深さの場合だけXMLを優先する。
        if (it.depth() < selectedDepth || (it.depth() == selectedDepth && xml && legacy)) {
            selected = it->path();
            selectedDepth = it.depth();
            legacy = classic;
        }
    }
    return selected;
}

[[nodiscard]] String readText(const Path& path) {
    // シナリオXMLの文字コードは仕様でUTF-8。DはSummary.xml
    // と全Area/Battle/Card XMLをstd.file.readText
    // で読み、UTF-8を検証してShift-JISへの代替変換は行
    // わない。readTextFileはShift-JISを先に調べ
    // るため、ここでは動作が異なる。有効なUTF-8の日本語（特に既
    // 定の戦闘名「バトルエリア」）がCP932としても有効な場合があ
    // り、CP932で復号すると保存・再読み込み後に文字化けする。
    const auto bytes = utils::readBinary(path);
    String value;
    if (!bytes.empty())
        value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    utils::validate(value);
    return value;
}

void writeText(const Path& path, StringView value, FileSync& sync) {
    if (const auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    utils::writeTextFile(path, value, false, syncOwner(sync));
}

void copyTree(const Path& source, const Path& destination,
              lib::Array<String>& failures) {
    std::error_code error;
    const bool exists = std::filesystem::exists(source, error);
    if (error || !exists) {
        failures.push_back(utf8Path(source));
        return;
    }
    try {
        utils::copyAll(source, destination, true);
    } catch (const std::exception&) {
        failures.push_back(utf8Path(source));
    }
}

template <typename T>
[[nodiscard]] String resourceXml(const lib::Owner<T>& value,
                                  const card::XMLOption& option) {
    if (!value) return {};
    return value->toXML(option);
}

}  // namespace

void Summary::Lock::close() noexcept {
    if (!stream) return;
    stream->flush();
    stream->close();
    stream.reset();
}

Summary::SaveOperationScope::SaveOperationScope(Summary& summary)
    : owner(summary) {
    const std::lock_guard lock(owner.save_state_mutex_);
    ++owner.save_state_depth_;
}

Summary::SaveOperationScope::~SaveOperationScope() {
    bool cleanup = false;
    {
        const std::lock_guard lock(owner.save_state_mutex_);
        if (owner.save_state_depth_ > 0U) --owner.save_state_depth_;
        if (owner.save_state_depth_ == 0U) {
            cleanup = owner.deferred_temp_cleanup_;
            owner.deferred_temp_cleanup_ = false;
        }
    }
    if (cleanup) {
        // アーカイブの公開とモデルの最終更新も含む、公開保存操作全体
        // の完了後にdelTempを遅延実行するDの順序に合わせる。
        try { owner.delTemp(false); } catch (...) {}
    }
}

Summary::XmlSaveScope::XmlSaveScope(Summary& summary)
    : owner(summary) {
    const std::lock_guard lock(owner.save_state_mutex_);
    if (owner.in_xml_save_) throw SummaryException("Scenario XML save is already in progress");
    owner.in_xml_save_ = true;
}

Summary::XmlSaveScope::~XmlSaveScope() {
    release();
}

void Summary::XmlSaveScope::release() {
    if (!active) return;
    const std::lock_guard lock(owner.save_state_mutex_);
    owner.in_xml_save_ = false;
    active = false;
}

struct Summary::TempLifetime final {
    String path;
    Storage* storage{};
    Lock lock;
    bool retained{};
    bool cleaned{};

    TempLifetime(String value, Storage* backend)
        : path(std::move(value)), storage(backend) {}

    ~TempLifetime() {
        if (cleaned || retained || path.empty()) return;
        lock.close();
        try { removeTree(fsPath(path), storage); } catch (...) {}
    }

    void cleanup() {
        if (cleaned || retained || path.empty()) return;
        lock.close();
        removeTree(fsPath(path), storage);
        cleaned = true;
    }
};

void Summary::activateTempLifetime(bool createLock) {
    if (!use_temp_ || temp_path_.empty()) {
        temp_lifetime_.reset();
        return;
    }
    if (temp_lifetime_ && temp_lifetime_->path == temp_path_ &&
        !temp_lifetime_->cleaned) {
        temp_lifetime_->storage = storage_;
        return;
    }
    temp_lifetime_ = std::make_shared<TempLifetime>(temp_path_, storage_);
    if (createLock) {
        createLockFile(temp_path_, storage_);
        temp_lifetime_->lock.stream = std::make_unique<std::ofstream>(
            fsPath(temp_path_) / "cwxeditor.lock",
            std::ios::binary | std::ios::out | std::ios::app);
    }
}

void Summary::setStorage(Storage* storage) noexcept {
    storage_ = storage;
    if (temp_lifetime_) temp_lifetime_->storage = storage;
}

Summary::Summary(String sPath)
    : id_("summary-" + std::to_string(next_summary_id.fetch_add(1))),
      scenario_path_(std::move(sPath)) {
    owner_proxy_ = std::make_shared<SummaryPathProxy>(this, owner_lifetime_);
    use_counter_ = std::make_shared<UseCounter>(owner_proxy_);
    start_area_user_ = std::make_shared<usecounter::AreaUser>(owner_proxy_);
    start_area_user_->setUseCounter(use_counter_);
    flag_root_ = std::make_shared<FlagDir>(owner_proxy_, "");
    flag_root_->changeHandler(guardedChangeHandler());
    flag_root_->useCounter(use_counter_);
}

Summary::Summary(String sname, String type, String skinName, String sPath,
                 bool temp, bool legacy, Storage* storage)
    : Summary(std::move(sPath)) {
    storage_ = storage;
    scenario_name_ = std::move(sname);
    type_ = std::move(type);
    skin_name_ = std::move(skinName);
    use_temp_ = temp;
    legacy_ = legacy;
    load_scaled_image_ = !legacy_;
    if (use_temp_) {
        temp_path_ = scenario_path_;
        activateTempLifetime();
    }
}

Summary::~Summary() {
    invalidateBeastOwnerLifetime();
    if (owner_lifetime_)
        owner_lifetime_->alive.store(false, std::memory_order_release);
    // DはSummaryオブジェクトごとではなく、ドキュメントの置換・終了時
    // に展開ツリーを破棄する。共有の寿命管理で、再読み込みの補助処理や結果が
    // 使用中のツリーを削除するのを防ぎつつ、拒否した新規読み込みは片付ける。
    temp_lifetime_.reset();
}

String Summary::createTempDirFromName(StringView tempPath, StringView name) {
    const auto base = fsPath(tempPath) / fsPath(name);
    const auto result = utils::createNewFileName(base, true);
    std::filesystem::create_directories(result);
    return utf8Path(result);
}

String Summary::createTempDir(StringView tempPath, StringView name,
                              bool createLock, Storage* storage) {
    const auto clean = utils::cleanFileName(String(name));
    const auto base = fsPath(tempPath) / fsPath(clean);
    Path result = base;
    std::uint64_t sequence = 2;
    while (storageExists(result, storage)) {
        if (sequence == 2 && result == base)
            result = fsPath(utf8Path(base) + "(" + std::to_string(sequence) + ")");
        else {
            ++sequence;
            result = fsPath(utf8Path(base) + "(" + std::to_string(sequence) + ")");
        }
    }
    makeDirectory(result, storage);
    if (createLock) createLockFile(utf8Path(result), storage);
    return utf8Path(result);
}

void Summary::createLockFile(StringView temp, Storage* storage) {
    const auto directory = fsPath(temp);
    // Dは空ファイルを書き、存在しない親は作らない。サービス経由のI/O
    // でも、create_parent=falseで同じ保存処理を使う。
    if (storage) {
        if (const auto relative = storageRelativePath(directory / "cwxeditor.lock", *storage)) {
            const std::span<const std::byte> empty;
            if (!storage->write(*relative, empty, false)) {
                auto diagnostic = storage->last_error();
                if (diagnostic.empty()) diagnostic = "Storage::write failed";
                throw SummaryException("Unable to create lock file: " + diagnostic);
            }
            return;
        }
    }
    std::ofstream file(directory / "cwxeditor.lock", std::ios::binary | std::ios::trunc);
    if (!file) throw std::filesystem::filesystem_error(
        "createLockFile", directory / "cwxeditor.lock", std::make_error_code(std::errc::io_error));
}

Summary::Owner Summary::createScenario(
    const CProps& prop, StringView tempPath, StringView name, const Skin& skin,
    bool createStart, StringView newAreaName,
    const lib::Array<BgImageS>& bgImagesDefault, bool saveSkinName,
    FileSync& sync) {
    // Dはシナリオ作成を一つの公開操作として追跡する。素材やXML
    // の個別書き込みは追跡しない。失敗時には、呼び出し境界を
    // 隠すファイル単位のスタックではなく、その境界が必要。
    perf::TraceScope trace(__FILE__, __LINE__);
    const auto storage = sync.storage();
    // 先にディレクトリを作り、巻き戻しの保護を有効にしてからSummary
    // がロックを取得する。ロックの書き込み失敗でツリーを放置しない。
    const auto directory = createTempDir(tempPath, name, false, storage);
    // Dとの意図的な違い。初期化が例外で終われば、この未公開の
    // 一時ツリーを破棄する。Dは作成途中のディレクトリを残す。
    struct NewScenarioDirectory final {
        String path;
        Storage* storage{};
        bool transferred{};
        ~NewScenarioDirectory() {
            if (transferred || path.empty()) return;
            try { removeTree(fsPath(path), storage); } catch (...) {}
        }
    } directoryOwner{directory, storage};
    // DはSkin.materialPath()が返すディレクトリだけを作る。Classic
    // のEngineから資源を取得する旧形式スキンでは、そのパスは空。
    const auto materialPath = skin.materialPath();
    if (!materialPath.empty())
        makeDirectory(fsPath(directory) / fsPath(materialPath), storage);
    auto result = std::make_shared<Summary>(String(name), skin.type(), skin.name(),
                                             directory, true, false, storage);
    // ここからはSummaryのデストラクターが後処理を担当す
    // る。初期XML、素材、開始エリアの構築中の例外も含む。
    directoryOwner.transferred = true;
    // 新規シナリオのnumericKeycodesは無効とする。既存シ
    // ナリオのSettings/numericKeycodesが欠落し
    // た場合と同じ既定値。Dは新形式の新規シナリオを、最初の明示的な
    // 「名前を付けて保存」までメモリ内に保つ。開始エリアと外側の呼び
    // 出し側で設定する作者の代入前に、途中のXML一式を公開しない。
    static_cast<void>(saveSkinName);
    result->refCheckPaths();
    result->updateJpy1List(prop);
    createStartArea(*result, prop, createStart, newAreaName, bgImagesDefault, &skin);
    return result;
}

void Summary::createStartArea(
    Summary& summ, const CProps& prop, bool createStart, StringView newAreaName,
    const lib::Array<BgImageS>& bgImagesDefault, const Skin* skinValue,
    StringView enginePath, StringView classicEngineRegex,
    StringView classicDataDirRegex, StringView classicMatchKey,
    const lib::Array<ClassicEngine>& classicEngines, StringView defaultSkin) {
    // 作成が無効でも、Dの公開された初期化の境界を維
    // 持する。呼び出し側は意図的に空のシナリオと、
    // 開始エリアの初期化失敗を区別するために使う。
    perf::TraceScope trace(__FILE__, __LINE__);
    if (!createStart) return;
    auto value = std::make_shared<Area>(summ.newAreaId(), String(newAreaName));
    Skin::Ptr legacySkin;
    if (summ.legacy()) {
        legacySkin = Skin::findLegacySkin(
            prop, enginePath, summ.scenarioPath(), classicEngineRegex,
            classicDataDirRegex, classicMatchKey, classicEngines, defaultSkin);
        skinValue = legacySkin.get();
    }
    if (skinValue) {
        // スキンを使う構築では、論理的なテーブル名をプラットフォ
        // ームの素材パスへ解決し、セル固有の全項目も保持する。
        // 下記の代替処理は、直列化済みのBgImageS値だけ
        // を持ち、スキンを読み込んでいない呼び出し側に使える。
        for (auto& image : skin::createBgImages(*skinValue, bgImagesDefault)) {
            if (image) value->append(std::move(image));
        }
    } else for (const auto& setting : bgImagesDefault) {
        lib::Owner<background::BgImage> image;
        if (setting.type == "image") {
            auto cell = std::make_shared<background::ImageCell>(
                setting.name, String{}, setting.x, setting.y,
                static_cast<std::int32_t>(setting.width),
                static_cast<std::int32_t>(setting.height), setting.mask);
            cell->smoothing(setting.smoothing);
            image = std::move(cell);
        } else if (setting.type == "text") {
            auto cell = std::make_shared<background::TextCell>(
                setting.text, setting.fontName, setting.size, setting.color,
                setting.bold, setting.italic, setting.underline, setting.strike,
                setting.vertical, setting.antialias, setting.borderingType,
                setting.borderingColor, setting.borderingWidth, setting.updateType,
                String{}, setting.x, setting.y,
                static_cast<std::int32_t>(setting.width),
                static_cast<std::int32_t>(setting.height), setting.mask);
            image = std::move(cell);
        } else if (setting.type == "color") {
            image = std::make_shared<background::ColorCell>(
                setting.blendMode, setting.gradientDir, setting.color1,
                setting.color2, String{}, setting.x, setting.y,
                static_cast<std::int32_t>(setting.width),
                static_cast<std::int32_t>(setting.height), setting.mask);
        } else if (setting.type == "pc") {
            auto cell = std::make_shared<background::PCCell>(
                setting.pcNumber, setting.expand, String{}, setting.x, setting.y,
                static_cast<std::int32_t>(setting.width),
                static_cast<std::int32_t>(setting.height), setting.mask);
            cell->smoothing(setting.smoothing);
            image = std::move(cell);
        }
        if (image) {
            image->cellName(setting.cellName);
            image->layer(setting.layer);
            value->append(std::move(image));
        }
    }
    auto enter = std::make_shared<event::EventTree>(String(prop.requiredMsgs().enterTree));
    enter->enter(true);
    value->add(enter);
    summ.add(std::move(value), false);
}

Summary::Owner Summary::loadScenarioFromFile(
    const CProps& prop, const LoadOption& option, lib::Array<String>& errorFiles,
    StringView fname, StringView tempPath, const Skin* defSkin,
    ClassicDirCallback classicDir, const Owner& old, ProgressCallback setMax,
    ProgressCallback worked, StringView newName, Storage* storage) {
    // XML、Classic、アーカイブ入力を、一つの公開操作の追跡
    // フレームで扱う。個別のメンバーやパスは追跡しない。Dの診断スタ
    // ックは呼び出し境界を記録し、ファイルごとのフレームはアーカイ
    // ブ失敗の情報を冗長にし、通常読み込みにも不要な処理を増やす。
    perf::TraceScope trace(__FILE__, __LINE__);
    errorFiles.clear();
    if (fname.empty()) return {};
    option.checkCancelled();
    // oldは読み込みの状態を引き継ぐ呼び出し側向けのD APIの一部
    // だが、別のシナリオの選択は再読み込みではない。Dはここでは意図的
    // に無視し、reloadXMLs()はMainWindow::reload()
    // だけが呼ぶ。oldを再利用すると、二回目以降の「開く」で選
    // んだファイルではなく表示中のシナリオを読み込んでしまう。
    static_cast<void>(old);

    const auto source = fsPath(fname);
    std::error_code error;
    if (!std::filesystem::exists(source, error)) {
        throw SummaryException("Scenario does not exist: " + String(fname));
    }

    Path root = source;
    bool temporary = false;
    struct TemporaryLoad final {
        Path& root;
        bool& temporary;
        bool committed{};
        ~TemporaryLoad() {
            if (temporary && !committed) {
                try { utils::delAll(root); } catch (...) {}
            }
        }
    } temporary_load{root, temporary};
    bool legacy = false;

    const auto extension = lower(utf8Path(source.extension()));
    const auto selectArchiveKind = [&](StringView xmlPath, StringView classicPath) {
        const auto depth = [](StringView path) {
            const auto normalized = fsPath(path).lexically_normal();
            return std::distance(normalized.begin(), normalized.end());
        };
        legacy = xmlPath.empty() || (!classicPath.empty() && depth(classicPath) < depth(xmlPath));
    };
    const auto expandName = [&](String path, bool directory) -> String {
        option.reportProgress("アーカイブの解凍", path);
        // DのFDIコールバックに合わせる。条件付きのexpandName分岐は無効なため、CAB
        // メンバーはここではtextOnlyとsummaryOnlyの両方の絞り込みを通さない。
        if (extension == ".cab") return path;
        const auto member = fsPath(path);
        const auto name = utf8Path(member.filename());
        if (option.summaryOnly)
            return utils::cfnmatch(name, legacy ? "Summary.wsm" : "Summary.xml") ? path : String{};
        if (!option.textOnly) return path;
        const auto ext = utf8Path(member.extension());
        if (utils::cfnmatch(ext, ".jptx")) return path;
        if (legacy) {
            if (utils::cfnmatch(ext, ".wsm") || utils::cfnmatch(ext, ".wid") ||
                utils::cfnmatch(ext, ".wex")) return path;
        } else {
            if (utils::cfnmatch(name, "Summary.xml")) return path;
            if (utils::cfnmatch(ext, ".xml") &&
                isScenarioSystemDir(utf8Path(member.parent_path().filename())))
                return utf8Path(member.parent_path().filename() / member.filename());
        }
        if (!directory) {
            const auto dummy = (root / member).lexically_normal();
            if (member.is_absolute() || !utils::hasPath(root, dummy))
                throw SummaryException("Archive path is outside the extraction directory: " + path);
            std::filesystem::create_directories(dummy.parent_path());
            filesync::writeFile(dummy, lib::ByteView{}, {});
        }
        return {};
    };
    if (isDirectory(source)) {
        root = source;
    } else if (extension == ".zip" || extension == ".wsn") {
        const auto directory = createTempDir(tempPath, utf8Path(source.stem()),
                                             true, storage);
        root = fsPath(directory);
        temporary = true;
        option.reportProgress("ZIPの中央ディレクトリ読み込み", String(fname));
        archive::ZipArchive archiveFile(source);
        String xmlSummary, classicSummary;
        for (const auto& member : archiveFile.directory) if (member) {
            const auto name = utf8Path(fsPath(member->name).filename());
            if (xmlSummary.empty() && utils::cfnmatch(name, "Summary.xml")) xmlSummary = member->name;
            if (classicSummary.empty() && utils::cfnmatch(name, "Summary.wsm")) classicSummary = member->name;
        }
        selectArchiveKind(xmlSummary, classicSummary);
        archive::unzip(fsPath(directory), archiveFile,
                       expandName,
                       [&](std::uint32_t value) { if (setMax) setMax(value); },
                       [&](std::uint32_t value) { if (worked) worked(value); });
        root = fsPath(directory);
        temporary = true;
    } else if (extension == ".cab") {
        const auto directory = createTempDir(tempPath, utf8Path(source.stem()),
                                             true, storage);
        root = fsPath(directory);
        temporary = true;
        option.reportProgress("CABの解凍", String(fname));
        selectArchiveKind(cab::cabHasFile(fname, "Summary.xml"),
                          cab::cabHasFile(fname, "Summary.wsm"));
        if (!cab::uncab(String(fname), directory, [&](String path) { return expandName(std::move(path), false); })) {
            throw SummaryException("Unable to extract CAB: " + String(fname));
        }
        root = fsPath(directory);
        temporary = true;
    } else if (extension == ".lha" || extension == ".lzh") {
        const auto directory = createTempDir(tempPath, utf8Path(source.stem()),
                                             true, storage);
        root = fsPath(directory);
        temporary = true;
        option.reportProgress("LHAの読み込み", String(fname));
        archive::LhaFile lha(source);
        String xmlSummary, classicSummary;
        for (const auto& path : lha.nameList) {
            const auto name = utf8Path(fsPath(path).filename());
            if (xmlSummary.empty() && utils::cfnmatch(name, "Summary.xml")) xmlSummary = path;
            if (classicSummary.empty() && utils::cfnmatch(name, "Summary.wsm")) classicSummary = path;
        }
        selectArchiveKind(xmlSummary, classicSummary);
        archive::unlha(directory, lha, expandName,
                       [&](std::uint32_t value) { if (setMax) setMax(value); },
                       [&](std::uint32_t value) { if (worked) worked(value); });
        root = fsPath(directory);
        temporary = true;
    }

    option.checkCancelled();
    option.reportProgress("概要ファイルの探索", utf8Path(root));
    const auto summaryFile = findSummaryFile(root, legacy, option);
    option.reportProgress("概要ファイルの読み込み", utf8Path(summaryFile));
    if (summaryFile.empty()) {
        throw SummaryException("Scenario summary was not found: " + String(fname));
    }
    if (legacy) {
        std::int32_t classicVersion = 0;
        const String skinType = defSkin ? defSkin->type() : String{};
        const String skinName = defSkin ? defSkin->name() : String{};
        auto result = cwl::loadLScenario(utf8Path(summaryFile.parent_path()), skinType, skinName,
                                         prop, option, errorFiles,
                                         classicVersion, newName);
        option.checkCancelled();
        if (!result) {
            throw SummaryException("Unable to load classic scenario: " + String(fname));
        }
        result->legacy_ = true;
        result->expand_xmls_ = false;
        result->use_temp_ = temporary;
        result->temp_path_ = temporary ? utf8Path(root) : String{};
        result->zip_name_ = temporary && extension != ".lha" && extension != ".lzh" ? String(fname) : String{};
        result->orig_zip_name_ = temporary ? String(fname) : String{};
        result->scenario_path_ = utf8Path(summaryFile.parent_path());
        result->setStorage(storage);
        result->repairId0();
        result->refCheckPaths();
        if (classicDir) {
            const auto destinationName = classicDir();
            if (destinationName.empty()) return {};
            option.checkCancelled();
            const auto destination = fsPath(destinationName);
            const auto sourceDirectory = fsPath(result->scenario_path_);
            // テンプレートを既存のシナリオやテンプレート自身へ結合しない。
            const auto relative = canonicalWithMissingTail(destination)
                .lexically_relative(canonicalWithMissingTail(sourceDirectory));
            if (relative == "." ||
                (!relative.empty() && *relative.begin() != "..") ||
                (std::filesystem::exists(destination) && !std::filesystem::is_empty(destination)))
                throw SummaryException("作成先にはテンプレート外の空のフォルダを指定してください。");
            std::filesystem::create_directories(destination);
            // 汎用のcopyAllはファイルシステムのエラーを無視する。テ
            // ンプレート作成では、コピー途中の出力先を公開してはならない。
            std::filesystem::copy(sourceDirectory, destination,
                                  std::filesystem::copy_options::recursive);
            option.checkCancelled();
            result->scenario_path_ = utf8Path(destination);
            result->use_temp_ = false;
            result->temp_path_ = utf8Path(destination);
            result->zip_name_.clear();
            result->orig_zip_name_.clear();
            result->read_only_path_.clear();
            result->to_x_ = false;
            result->refCheckPaths();
            result->updateJpy1List(prop, &option);
            // TemporaryLoadが所有するのは展開したテンプレートだけで、出力先ではない。
            return result;
        }
        result->updateJpy1List(prop, &option);
        if (classicVersion >= 7) {
            const auto converted = createTempDir(tempPath,
                utf8Path(summaryFile.parent_path().filename()), true, storage);
            std::optional<Skin> fallback;
            if (!defSkin) fallback.emplace(prop, "");
            const Skin& conversionSkin = defSkin ? *defSkin : *fallback;
            lib::Array<String> copyFailures;
            const auto sourceIdentity = temporary ? String(fname) : result->scenario_path_;
            (void)result->classicToX(prop, converted, tempPath, conversionSkin, copyFailures);
            if (!copyFailures.empty())
                throw SummaryException("Unable to copy scenario material: " + copyFailures.front());
            result->read_only_path_ = sourceIdentity;
            result->zip_name_.clear();
            result->resetChanged();
            // TemporaryLoadは元の展開先だけを破棄する。新し
            // い変換ツリーは、自身の保持されたモデル寿命とロックを持つ。
        } else {
            if (temporary) result->activateTempLifetime();
            temporary_load.committed = true;
        }
        return result;
    }

    auto result = fromXMLs(prop, utf8Path(summaryFile), option, errorFiles);
    if (temporary && !option.expandXMLs && !classicDir) {
        // Dは管理対象のアーカイブ内XMLをメモリに保持する。共通の
        // C++アーカイブ処理は、パス・衝突検査のため先に展開する
        // 。解析成功後、公開前にシナリオXMLだけをメモリへ移す。
        const auto directory = summaryFile.parent_path();
        const auto bytes = utils::readBinary(summaryFile);
        result->old_xmls_[""]["Summary.xml"] = String(bytes.begin(), bytes.end());
        for (const auto category : managedXmlCategories) {
            const auto xmlDirectory = directory / fsPath(category);
            if (!isDirectory(xmlDirectory)) continue;
            for (const auto& entry : std::filesystem::directory_iterator(xmlDirectory)) {
                option.checkCancelled();
                if (!entry.is_regular_file() || !isXmlFile(entry.path())) continue;
                const auto data = utils::readBinary(entry.path());
                result->old_xmls_[String(category)][utf8Path(entry.path().filename())] =
                    String(data.begin(), data.end());
            }
        }
        for (const auto& [category, files] : result->old_xmls_)
            for (const auto& [name, text] : files)
                removeTree(directory / fsPath(category) / fsPath(name), storage);
        // Summaryの名前は読み込み時に大文字小文字を区別しない。
        // 正規のXMLキーと表記が違う場合は、実際の入力元も削除する。
        if (storageExists(summaryFile, storage)) removeTree(summaryFile, storage);
    }
    // DはSummary.xmlのあるディレクトリを、ユーザー
    // のアーカイブ展開設定に関係なく展開済みシナリオとして扱う
    // 。その設定は、アーカイブ所有の一時ツリーにだけ適用する。
    result->expand_xmls_ = temporary && !classicDir ? option.expandXMLs : true;
    result->use_temp_ = temporary;
    result->temp_path_ = temporary ? utf8Path(root) : String{};
    result->zip_name_ = temporary && extension != ".lha" && extension != ".lzh" ? String(fname) : String{};
    result->orig_zip_name_ = temporary ? String(fname) : String{};
    result->legacy_ = false;
    result->scenario_path_ = utf8Path(summaryFile.parent_path());
    result->setStorage(storage);
    // DがnewNameを渡すのはClassicの読み込みだけ。XMLシナリ
    // オはProperty/Nameを正とし、フォルダー名で置き換えない。
    result->repairId0();
    result->refCheckPaths();
    result->updateJpy1List(prop, &option);
    if (classicDir) {
        // Classicの出力先ディレクトリのコールバックは、テンプレート読み込みの
        // 印にもなる。新形式のテンプレートは、無効なClassic出力先の入力欄を
        // 参照せず、最初の「名前を付けて保存」まで未保存の一時ドキュメントに保つ。
        const auto sourceDirectory = fsPath(result->scenario_path_);
        const auto destination = fsPath(createTempDir(
            tempPath, newName.empty() ? utf8Path(sourceDirectory.filename()) : newName,
            true, storage));
        try {
            const auto relative = canonicalWithMissingTail(destination)
                .lexically_relative(canonicalWithMissingTail(sourceDirectory));
            if (relative == "." ||
                (!relative.empty() && !relative.has_root_name() &&
                 !relative.has_root_directory() && *relative.begin() != ".."))
                throw SummaryException("The temporary copy must not be inside the source template");
            std::filesystem::copy(sourceDirectory, destination,
                                  std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
            option.checkCancelled();
            // 意図的な安全性の拡張。Dは元の入力パスを保持するが、
            // ここでは有効な全パスを所有するコピーへ向ける。編集
            // や後処理で元のテンプレートを対象にしてはならない。
            result->scenario_path_ = utf8Path(destination);
            result->use_temp_ = true;
            result->temp_path_ = utf8Path(destination);
            result->zip_name_.clear();
            result->orig_zip_name_.clear();
            result->read_only_path_.clear();
            result->to_x_ = false;
            result->expand_xmls_ = true;
            result->activateTempLifetime();
            result->refCheckPaths();
            result->updateJpy1List(prop, &option);
        } catch (...) {
            try { removeTree(destination, storage); } catch (...) {}
            throw;
        }
        // TemporaryLoadはアーカイブのテンプレート展開先だけを破棄す
        // る。独立して所有する新形式のコピーは、新しいモデルとともに生存させる。
        return result;
    }
    option.checkCancelled();
    if (temporary) result->activateTempLifetime();
    temporary_load.committed = true;
    return result;
}

void Summary::delTemp(bool fileOnly) {
    std::unique_lock saveStateLock(save_state_mutex_);
    // Dは先にuseTempを検査する。恒久的な入力元への要求によって、実行中の
    // 「名前を付けて保存」が後から作るアーカイブ用ツリーを削除してはならない。
    if (!use_temp_) return;
    if (save_state_depth_ != 0U) {
        // Dは保存中にバックグラウンド処理で待つ。キュー内の直列化がこのツ
        // リーを再作成しないよう、実行中の保存の終了まで後処理を遅らせる。
        deferred_temp_cleanup_ = true;
        return;
    }
    // Dは展開ディレクトリの記録がなければscenarioPathを使う。公開
    // Summary(sPath)コンストラクターでは、この状態を直接作れる。
    const auto cleanupPath = temp_path_.empty() ? scenario_path_ : temp_path_;
    if (cleanupPath.empty()) return;
    try {
        if (temp_lifetime_ && temp_lifetime_->path == cleanupPath)
            temp_lifetime_->cleanup();
        else
            removeTree(fsPath(cleanupPath), storage_);
    } catch (...) {
        createLockFile(cleanupPath, storage_);
        return;
    }
    temp_lifetime_.reset();
    if (!fileOnly) {
        use_temp_ = false;
        zip_name_.clear();
        orig_zip_name_.clear();
        temp_path_.clear();
    }
}

String Summary::retainTempForRecovery() noexcept {
    if (!use_temp_ || temp_path_.empty()) return {};
    if (!temp_lifetime_) {
        try { activateTempLifetime(false); } catch (...) { return {}; }
    }
    temp_lifetime_->retained = true;
    temp_lifetime_->lock.close();
    return temp_path_;
}

Summary::ChangeSubscription Summary::observeChanges(ChangeHandler callback) {
    std::erase_if(change_observers_, [](const auto& entry) { return entry.subscription.expired(); });
    auto subscription = std::make_shared<const int>(0);
    change_observers_.push_back({subscription,
        std::make_shared<ChangeHandler>(std::move(callback))});
    return subscription;
}

void Summary::changeHandler() {
    // 利用者のコールバックが最後の外部Summary所有元を解放する場合があ
    // る。通知と監視先の後処理が終わるまで、実際の共有モデルを生存させる。
    [[maybe_unused]] const auto keepAlive = weak_from_this().lock();
    if (!changed_) {
        changed_ = true;
        for (const auto& callback : changedEvent) if (callback) callback();
    }
    for (const auto& callback : changedEventForce) if (callback) callback();
    // std::functionではなくハンドルをコピーする。可変のコール
    // バック状態は元の安定したノードに属する。取得済みの通知先へ配送中で
    // も、再帰的なホストの再接続が監視先の登録表を置換できるようにする。
    const auto observers = change_observers_;
    for (const auto& observer : observers)
        if (!observer.subscription.expired() && observer.callback && *observer.callback)
            (*observer.callback)();
    std::erase_if(change_observers_, [](const auto& entry) { return entry.subscription.expired(); });
}

String Summary::cwxPath(bool) { return {}; }

lib::Owner<path::CWXPath> Summary::findCWXPath(StringView value) {
    if (path::cpempty(value)) {
        if (auto self = retainForCallback()) return self;
        return owner_proxy_;
    }
    const auto category = lower(path::cpcategory(value));
    const auto index = path::cpindex(value);
    const bool idPath = category.ends_with(":id");
    const auto tail = path::cpbottom(value);
    auto child = [&tail](const auto& owner) -> lib::Owner<path::CWXPath> {
        if (!owner) return {};
        if (tail.empty()) return pathOwner(owner);
        return owner->findCWXPath(tail);
    };
    if (category == "area" || category == "area:id") return idPath ? child(area(index)) : (index < areas_.size() ? child(areas_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "battle" || category == "battle:id") return idPath ? child(battle(index)) : (index < battles_.size() ? child(battles_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "package" || category == "package:id") return idPath ? child(cwPackage(index)) : (index < packages_.size() ? child(packages_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "castcard" || category == "castcard:id") return idPath ? child(cwCast(index)) : (index < casts_.size() ? child(casts_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "skillcard" || category == "skillcard:id") return idPath ? child(skill(index)) : (index < skills_.size() ? child(skills_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "itemcard" || category == "itemcard:id") return idPath ? child(item(index)) : (index < items_.size() ? child(items_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "beastcard" || category == "beastcard:id") return idPath ? child(beast(index)) : (index < beasts_.size() ? child(beasts_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "infocard" || category == "infocard:id") return idPath ? child(info(index)) : (index < infos_.size() ? child(infos_[index]) : lib::Owner<path::CWXPath>{});
    if (category == "variable" && flag_root_) return tail.empty()
        ? std::static_pointer_cast<path::CWXPath>(flag_root_) : flag_root_->findCWXPath(tail);
    return {};
}

lib::Array<lib::Observer<path::CWXPath>> Summary::cwxChilds() {
    lib::Array<lib::Observer<path::CWXPath>> result;
    for (const auto& value : cardChildren()) result.emplace_back(value);
    result.emplace_back(flagDirRoot());
    return result;
}

lib::Observer<path::CWXPath> Summary::cwxParent() { return {}; }

std::vector<lib::Owner<path::CWXPath>> Summary::cardChildren() const {
    std::vector<lib::Owner<path::CWXPath>> result;
    for (const auto& value : areas_) result.emplace_back(pathOwner(value));
    for (const auto& value : battles_) result.emplace_back(pathOwner(value));
    for (const auto& value : packages_) result.emplace_back(pathOwner(value));
    for (const auto& value : casts_) result.emplace_back(pathOwner(value));
    for (const auto& value : skills_) result.emplace_back(pathOwner(value));
    for (const auto& value : items_) result.emplace_back(pathOwner(value));
    for (const auto& value : beasts_) result.emplace_back(pathOwner(value));
    for (const auto& value : infos_) result.emplace_back(pathOwner(value));
    return result;
}

void Summary::resetChanged() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    if (!changed_) return;
    refCheckPaths();
    changed_ = false;
    std::function<void(lib::Owner<path::CWXPath>)> reset =
        [&](lib::Owner<path::CWXPath> current) {
            auto& model = requiredScenarioObject(current);
            if (const auto area = std::dynamic_pointer_cast<area::AbstractArea>(current)) area->resetChanged();
            else if (const auto card = std::dynamic_pointer_cast<card::Card>(current)) card->resetChanged();
            for (const auto& child : retainedChildPaths(model)) reset(child);
        };
    reset(owner_proxy_);
}

void Summary::changed() { changeHandler(); }

void Summary::changedAll() {
    [[maybe_unused]] const auto self_owner = weak_from_this().lock();
    const auto notify = [](auto& value) { value.changed(); };
    visitScenarioSlice(areas_, notify);
    visitScenarioSlice(battles_, notify);
    visitScenarioSlice(packages_, notify);
    visitScenarioSlice(casts_, notify);
    visitScenarioSlice(skills_, notify);
    visitScenarioSlice(items_, notify);
    visitScenarioSlice(beasts_, notify);
    visitScenarioSlice(infos_, notify);
    changed();
}

ChangedResourceSet Summary::changedResources() const {
    ChangedResourceSet result;
    const auto topResource = [this](lib::Owner<path::CWXPath> value)
        -> lib::Owner<path::CWXPath> {
        while (value) {
            if (value.get() == owner_proxy_.get()) return owner_proxy_;
            if (value.get() == static_cast<const path::CWXPath*>(
                    static_cast<const card::CastOwner*>(this))) return value;
            const auto parent = value->cwxParent().lock();
            if (!parent || parent.get() == owner_proxy_.get() ||
                parent.get() == static_cast<const path::CWXPath*>(
                    static_cast<const card::CastOwner*>(this))) return value;
            value = parent;
        }
        return {};
    };
    const auto addReferenceOwners = [this, &result, &topResource](
        const auto& resource, const auto& id) {
        if (!resource || !resource->isChanged() || isTargetVersion("1") ||
            !use_counter_) return;
        for (const auto& user : use_counter_->values(id)) {
            if (!user) continue;
            if (const auto owner = topResource(user->owner())) {
                result.insert(owner.get());
            }
        }
    };
    if (changed_) {
        result.insert(static_cast<const path::CWXPath*>(
            static_cast<const card::CastOwner*>(this)));
    }
    for (const auto& value : casts_) if (value && value->isChanged()) result.insert(pathPointer(value));
    for (const auto& value : skills_) {
        if (value && value->isChanged()) result.insert(pathPointer(value));
        if (value) addReferenceOwners(value, usecounter::toSkillId(value->id()));
    }
    for (const auto& value : items_) {
        if (value && value->isChanged()) result.insert(pathPointer(value));
        if (value) addReferenceOwners(value, usecounter::toItemId(value->id()));
    }
    for (const auto& value : beasts_) {
        if (value && value->isChanged()) result.insert(pathPointer(value));
        if (value) addReferenceOwners(value, usecounter::toBeastId(value->id()));
    }
    for (const auto& value : infos_) if (value && value->isChanged()) result.insert(pathPointer(value));
    for (const auto& value : areas_) if (value && value->isChanged()) result.insert(pathPointer(value));
    for (const auto& value : battles_) if (value && value->isChanged()) result.insert(pathPointer(value));
    for (const auto& value : packages_) if (value && value->isChanged()) result.insert(pathPointer(value));
    if (legacy_ && use_counter_) {
        const auto normalized = [](const auto& paths) {
            std::decay_t<decltype(paths)> result;
            for (const auto& [path, time] : paths) {
                auto key = utils::encodePath(path);
                if (utils::fncmp("A", "a") == 0) key = utils::caseFold(key);
                result[key] = time;
            }
            return result;
        };
        const auto current = normalized(allPaths());
        const auto previous = normalized(no_save_check_paths_);
        for (const auto& id : use_counter_->keys<usecounter::PathId>()) {
            if (id.isBinData()) continue;
            std::vector<String> spellings{String(id)};
            // ネイティブのPathIdは、単純な小文字化で一致する別名をまとめ
            // 、Dのハッシュと等価判定の不整合を修正する。Dは完全な小文字化
            // のキーを別々に保持できる（ドット付きIとiなど）。代表キーが別
            // の実ファイルを隠さないよう、ここでは登録した各表記を保持する。
            for (const auto& user : use_counter_->values(id)) {
                if (!user) continue;
                const auto path = user->path();
                if (usecounter::toPathId(path) == id) spellings.push_back(path);
            }
            std::unordered_set<String> seen;
            for (auto path : spellings) {
                if (path.empty()) continue;
                path = utils::encodePath(path);
                if (utils::fncmp("A", "a") == 0) path = utils::caseFold(path);
                if (!seen.insert(path).second) continue;
                if (!std::filesystem::exists(fsPath(scenario_path_) / fsPath(path))) continue;
                const auto now = current.find(path), before = previous.find(path);
                if (now == current.end() || before == previous.end() || now->second != before->second) {
                    if (const auto owner = topResource(isIncludedImpl(path))) result.insert(owner.get());
                }
            }
        }
    }
    return result;
}

lib::Owner<path::CWXPath> Summary::isIncludedImpl(StringView value) const {
    auto wanted = utils::encodePath(String(value));
    const bool insensitive = utils::fncmp("A", "a") == 0;
    if (insensitive) wanted = utils::caseFold(wanted);
    auto matches = [&wanted, insensitive](const lib::Owner<CardImage>& image) {
        if (!image) return false;
        auto path = utils::encodePath(image->path());
        if (insensitive) path = utils::caseFold(path);
        // 最初の所有元を選ぶ際、DはPathIdの広い単純小文字化によ
        // る等価性ではなく、完全に小文字化した画像の表記を比較する。
        return path == wanted;
    };
    if (!use_counter_) return {};
    for (const auto& user : use_counter_->values(usecounter::toPathId(value))) {
        if (!user) continue;
        auto owner = user->owner();
        if (!owner) continue;
        if (owner.get() == owner_proxy_.get()) {
            for (const auto& image : image_paths_) {
                if (matches(image)) return owner_proxy_;
            }
            continue;
        }
        if (owner.get() == static_cast<const path::CWXPath*>(
                static_cast<const card::CastOwner*>(this))) {
            for (const auto& image : image_paths_) {
                if (matches(image)) return owner;
            }
            continue;
        }
        if (const auto card = std::dynamic_pointer_cast<card::Card>(owner)) {
            for (const auto& image : card->paths()) {
                if (matches(image)) return owner;
            }
        }
    }
    return {};
}

bool Summary::isIncluded(StringView value) const {
    if (!legacy_ || value.empty() || utils::isBinImg(value)) return false;
    const auto full = fsPath(scenario_path_) / fsPath(value);
    if (!isRegularFile(full)) return false;
    return static_cast<bool>(isIncludedImpl(value));
}

bool Summary::isSystemFile(StringView value) const {
    if (isTemporaryName(value)) return true;
    const auto p = fsPath(value);
    std::error_code error;
    const bool directory = std::filesystem::is_directory(p, error) && !error;
    return isSystemFile(value, directory);
}

bool Summary::isSystemFile(StringView value, bool directory) const {
    if (isTemporaryName(value)) return true;
    const auto name = utils::pathBaseName(value);
    if (!directory && use_temp_ && utils::cfnmatch(name, "cwxeditor.lock")) return true;
    if (legacy_) {
        if (directory) return false;
        const auto extension = utils::pathExtension(value);
        return utils::cfnmatch(extension, ".wid") || utils::cfnmatch(extension, ".wex") || utils::cfnmatch(extension, ".wsm");
    }
    return directory ? isScenarioSystemDir(name) : utils::cfnmatch(name, "Summary.xml");
}

lib::OrderedDictionary<String, std::filesystem::file_time_type> Summary::allPaths() const {
    lib::OrderedDictionary<String, std::filesystem::file_time_type> result;
    const auto root = fsPath(scenario_path_);
    if (!isDirectory(root)) return result;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(root, error), end;
         it != end && !error; it.increment(error)) {
        const auto current = it->path();
        if (isSystemFile(utf8Path(current))) {
            if (it->is_directory(error)) it.disable_recursion_pending();
            continue;
        }
        const auto key = relativeKey(root, current);
        if (it->is_directory(error)) result[key] = std::filesystem::file_time_type{};
        else if (it->is_regular_file(error)) result[key] = it->last_write_time(error);
    }
    return result;
}

void Summary::checkPathsIsChanged(bool) {
    if (!needCheckPaths()) return;
    const auto previous = check_paths_;
    check_paths_ = allPaths();
    if (previous == check_paths_) return;
    if (use_temp_) {
        changed();
        return;
    }
    for (const auto& [key, time] : check_paths_) {
        const auto old = previous.find(key);
        if (old != previous.end() && old->second == time) continue;
        if (isIncluded(key)) { changed(); return; }
    }
    for (const auto& [key, time] : previous) {
        const auto now = check_paths_.find(key);
        if (now == check_paths_.end() || now->second != time) {
            if (isIncluded(key)) { changed(); return; }
        }
    }
}

void Summary::refCheckPaths() {
    if (needCheckPaths()) {
        check_paths_ = allPaths();
        no_save_check_paths_ = check_paths_;
    }
}

void Summary::repairId0() {
    // DのrepairID0は全カテゴリをadd(resource)へ委ねる
    // 。同じ配送処理を再利用し、末端要素の戻り値、カードのホールド・リンク解
    // 除、集合の順序、所有元・使用カウンターの通知が異ならないようにする。
    const auto area0 = area(0);
    const auto battle0 = battle(0);
    const auto package0 = cwPackage(0);
    const auto cast0 = cwCast(0);
    const auto skill0 = skill(0);
    const auto item0 = item(0);
    const auto beast0 = beast(0);
    const auto info0 = info(0);

    if (!(area0 || battle0 || package0 || cast0 || skill0 || item0 || beast0 || info0))
        return;

    if (area0) (void)add(area0);
    if (battle0) (void)add(battle0);
    if (package0) (void)add(package0);
    if (cast0) (void)add(cast0);
    if (skill0) (void)add(skill0);
    if (item0) (void)add(item0);
    if (beast0) (void)add(beast0);
    if (info0) (void)add(info0);

    // add()は登録済みの利用者を再対応付けする。ツリーにある
    // が現在未登録の参照には、Dの再帰的な代替処理を維持する。
    const auto areaId = area0 ? area0->id() : 0U;
    const auto battleId = battle0 ? battle0->id() : 0U;
    const auto packageId = package0 ? package0->id() : 0U;
    const auto castId = cast0 ? cast0->id() : 0U;
    const auto skillId = skill0 ? skill0->id() : 0U;
    const auto itemId = item0 ? item0->id() : 0U;
    const auto beastId = beast0 ? beast0->id() : 0U;
    const auto infoId = info0 ? info0->id() : 0U;

    // Dのrecurse(this)はStartAreaIdも修復する。AreaUserは
    // ID 0を登録しないため、0以外への置換後にSummaryルートを明示的に更新する。
    if (areaId != 0U && startArea() == 0U) startArea(areaId);

    std::unordered_set<const path::CWXPath*> visited;
    std::function<void(const lib::Owner<path::CWXPath>&)> repairReferences =
        [&](const lib::Owner<path::CWXPath>& current) {
            if (!current || !visited.insert(current.get()).second) return;
            if (auto content = std::dynamic_pointer_cast<event::Content>(current)) {
                const auto detail = content->detail();
                if (areaId && detail.use(event::CArg::Area) && content->area() == 0) content->area(areaId);
                if (battleId && detail.use(event::CArg::Battle) && content->battle() == 0) content->battle(battleId);
                if (packageId && detail.use(event::CArg::Package) && content->packages() == 0) content->packages(packageId);
                if (castId && detail.use(event::CArg::Cast) && content->casts() == 0) content->casts(castId);
                if (skillId && detail.use(event::CArg::Skill) && content->skill() == 0) content->skill(skillId);
                if (itemId && detail.use(event::CArg::Item) && content->item() == 0) content->item(itemId);
                if (beastId && detail.use(event::CArg::Beast) && content->beast() == 0) content->beast(beastId);
                if (infoId && detail.use(event::CArg::Info) && content->info() == 0) content->info(infoId);
            }
            if (auto enemy = std::dynamic_pointer_cast<area::EnemyCard>(current)) {
                if (castId && enemy->id() == 0) enemy->id(castId);
            }
            for (const auto& child : current->cwxChilds()) {
                if (auto next = child.lock()) repairReferences(next);
            }
        };
    for (const auto& value : casts_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : skills_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : items_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : beasts_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : infos_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : areas_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : battles_) if (value) repairReferences(pathOwner(value));
    for (const auto& value : packages_) if (value) repairReferences(pathOwner(value));
}

void Summary::toArchive(String zipName, String scenarioPath, bool expandXMLs) {
    expand_xmls_ = expandXMLs;
    use_temp_ = true;
    zip_name_ = std::move(zipName);
    orig_zip_name_ = zip_name_;
    legacy_ = false;
    // DのscenarioPathプロパティは、読み込み済みのJPY1/JPDCのパスを新しいルート基準に変える。
    this->scenarioPath(std::move(scenarioPath));
    temp_path_ = scenario_path_;
    activateTempLifetime();
}

void Summary::dataVersion(String value) {
    if (data_version_ == value) return;
    const auto retainedSelf = retainForCallback();
    changed();
    const auto notify = [](auto& resource) { resource.changed(); };
    visitScenarioSlice(areas_, notify);
    visitScenarioSlice(battles_, notify);
    visitScenarioSlice(packages_, notify);
    visitScenarioSlice(casts_, notify);
    visitScenarioSlice(skills_, notify);
    visitScenarioSlice(items_, notify);
    visitScenarioSlice(beasts_, notify);
    visitScenarioSlice(infos_, notify);
    data_version_ = std::move(value);
}

void Summary::loadScaledImage(bool value) {
    if (load_scaled_image_ == value) return;
    const auto retainedSelf = retainForCallback();
    changed();
    const auto notify = [](auto& card) { card.changed(); };
    visitScenarioSlice(casts_, notify);
    visitScenarioSlice(skills_, notify);
    visitScenarioSlice(items_, notify);
    visitScenarioSlice(beasts_, notify);
    visitScenarioSlice(infos_, notify);
    load_scaled_image_ = value;
}

bool Summary::isTargetVersion(StringView value) const {
    return isTargetVersionWith(legacy_, data_version_, value);
}

bool Summary::isTargetVersionWith(bool legacy, StringView dataVersion,
                                  StringView value) {
    return !legacy && versioninfo::isTargetVersion(dataVersion, value);
}

void Summary::setBaseParams(String newScenarioName, String newAuthor) {
    if (scenario_name_ == newScenarioName && author_ == newAuthor) return;
    changed();
    const auto oldName = scenario_name_;
    const auto oldAuthor = author_;
    // Dは効果系カードの属性によって子の効果内容への巡回を制限す
    // るが、イベントツリーは独立に訪問する。効果内容も含む汎用cwxChilds()
    // の接続一覧で、その制限を迂回しない。
    std::unordered_set<const path::CWXPath*> walked;
    std::function<void(const lib::Owner<path::CWXPath>&)> walk =
        [&](const lib::Owner<path::CWXPath>& current) {
            if (!current || !walked.insert(current.get()).second) return;
            if (const auto effect = std::dynamic_pointer_cast<card::EffectCard>(current)) {
                if (effect->scenario() == oldName && effect->author() == oldAuthor) {
                    effect->scenario(newScenarioName);
                    effect->author(newAuthor);
                    for (const auto& motion : effect->motions())
                        if (motion) walk(std::static_pointer_cast<path::CWXPath>(motion));
                }
                for (const auto& tree : effect->trees())
                    if (tree) walk(std::static_pointer_cast<path::CWXPath>(tree));
                return;
            }
            for (const auto& child : current->cwxChilds()) {
                if (auto next = child.lock()) walk(next);
            }
        };
    for (const auto& value : casts_) if (value) walk(pathOwner(value));
    for (const auto& value : skills_) if (value) walk(pathOwner(value));
    for (const auto& value : items_) if (value) walk(pathOwner(value));
    for (const auto& value : beasts_) if (value) walk(pathOwner(value));
    for (const auto& value : infos_) if (value) walk(pathOwner(value));
    for (const auto& value : areas_) if (value) walk(pathOwner(value));
    for (const auto& value : battles_) if (value) walk(pathOwner(value));
    for (const auto& value : packages_) if (value) walk(pathOwner(value));
    scenario_name_ = std::move(newScenarioName);
    author_ = std::move(newAuthor);
}

void Summary::author(String value) { setBaseParams(scenario_name_, std::move(value)); }

void Summary::type(String value) {
    if (type_ != value && !legacy_) changed();
    type_ = std::move(value);
}

void Summary::skinName(String value) {
    if (skin_name_ != value && !legacy_) changed();
    skin_name_ = std::move(value);
}

lib::Array<lib::Owner<CardImage>> Summary::imagePaths() const {
    lib::Array<lib::Owner<CardImage>> result;
    result.reserve(image_paths_.size());
    for (const auto& image : image_paths_) {
        if (image) result.push_back(std::make_shared<CardImage>(nullptr, *image));
        else result.push_back({});
    }
    return result;
}

void Summary::imagePaths(const lib::Array<lib::Owner<CardImage>>& paths) {
    bool equal = paths.size() == image_paths_.size();
    if (equal) for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!paths[i] || !image_paths_[i] || !paths[i]->opEquals(*image_paths_[i])) { equal = false; break; }
    }
    if (equal) return;
    changed();
    for (const auto& image : image_paths_) if (image) image->removeUseCounter();
    image_paths_.clear();
    for (const auto& image : paths) {
        if (!image) continue;
        auto copy = std::make_shared<CardImage>(
            static_cast<path::CWXPath*>(static_cast<card::CastOwner*>(this)),
            *image);
        copy->setUseCounter(use_counter_);
        image_paths_.push_back(std::move(copy));
    }
}

void Summary::desc(String value) { if (description_ != value) changed(); description_ = std::move(value); }
void Summary::levelMin(std::uint32_t value) { if (level_min_ != value) changed(); level_min_ = value; }
void Summary::levelMax(std::uint32_t value) { if (level_max_ != value) changed(); level_max_ = value; }
void Summary::rCouponNum(std::uint32_t value) { if (required_coupon_num_ != value) changed(); required_coupon_num_ = value; }
void Summary::rCoupons(lib::SharedSlice<String> values) {
    if (rCoupons() == values) return;
    [[maybe_unused]] const auto keepAlive = weak_from_this().lock();
    changed();
    for (const auto& user : required_coupons_) user->removeUseCounter();
    // Dはcoupon(name)が通知する前に、サイズ変更した項目へ空の利用者
    // を設定する。コールバックによる再代入をまたいで、入力記述子を生存させる。
    required_coupons_.resize(values.size());
    for (std::size_t index = 0; index < values.size(); ++index) {
        auto user = std::make_shared<usecounter::CouponUser>(owner_proxy_);
        required_coupons_[index] = user;
        user->coupon(values[index]);
        user->setUseCounter(use_counter_, static_cast<path::CWXPath*>(nullptr));
    }
}
lib::Array<String> Summary::rCoupons() const {
    lib::Array<String> result;
    // Dは各クラス項目を参照する。拡張中にコールバックが未初期化
    // の項目を読む場合は、共通のネイティブnull検査を使う。
    for (const auto& user : required_coupons_) result.push_back(requiredScenarioObject(user).coupon());
    return result;
}
void Summary::startArea(std::uint64_t value) {
    if (startArea() != value) changed();
    start_area_id_ = value;
    if (start_area_user_) {
        start_area_user_->area(value);
        // 数値が変わらなくても、使用カウンターの不変条件を維持する。D
        // の一つのAreaUserを使うプロパティに合わせ、カウンタ
        // ーの整合後の再読み込み・保存で状態が修復されるようにする。
        start_area_user_->setUseCounter(use_counter_);
    }
}

std::uint64_t Summary::startArea() const noexcept {
    // DはこのプロパティをAreaUserに保持する。UseCounter
    // が参照先のエリアIDを付け替える場合があるため、監視参照を
    // 正とする。単一値は構築・読み込み時の代替用だけに保持する。
    return start_area_user_ ? start_area_user_->area() : start_area_id_;
}

lib::Owner<CastCard> Summary::cwCast(std::uint64_t id) {
    return findCard(casts_, id);
}
lib::Owner<SkillCard> Summary::skill(std::uint64_t id) {
    return findCard(skills_, id);
}
lib::Owner<ItemCard> Summary::item(std::uint64_t id) {
    return findCard(items_, id);
}
lib::Owner<BeastCard> Summary::beast(std::uint64_t id) {
    return findCard(beasts_, id);
}
lib::Owner<InfoCard> Summary::info(std::uint64_t id) {
    return findCard(infos_, id);
}
lib::Owner<const CastCard> Summary::cwCast(std::uint64_t id) const { return findCard(casts_, id); }
lib::Owner<const SkillCard> Summary::skill(std::uint64_t id) const { return findCard(skills_, id); }
lib::Owner<const ItemCard> Summary::item(std::uint64_t id) const { return findCard(items_, id); }
lib::Owner<const BeastCard> Summary::beast(std::uint64_t id) const { return findCard(beasts_, id); }
lib::Owner<const InfoCard> Summary::info(std::uint64_t id) const { return findCard(infos_, id); }

lib::Owner<Area> Summary::area(std::uint64_t id) const {
    return findCard(areas_, id);
}
lib::Owner<Battle> Summary::battle(std::uint64_t id) const {
    return findCard(battles_, id);
}
lib::Owner<Package> Summary::cwPackage(std::uint64_t id) const {
    return findCard(packages_, id);
}
bool Summary::hasAreaId(std::uint64_t id) const { return static_cast<bool>(area(id)); }
bool Summary::hasBattleId(std::uint64_t id) const { return static_cast<bool>(battle(id)); }
bool Summary::hasPackageId(std::uint64_t id) const { return static_cast<bool>(cwPackage(id)); }

lib::Owner<BeastCard> Summary::findSameBeast(const lib::Owner<BeastCard>& value) const {
    if (!value) return {};
    return findSameBeast(*value);
}

lib::Owner<BeastCard> Summary::findSameBeast(const BeastCard& value) const {
    if (value.linkId() != 0) return const_cast<Summary*>(this)->beast(value.linkId());
    for (const auto& candidate : beasts_) if (candidate && value.equalsExcludeId(*candidate)) return candidate;
    return {};
}

std::uint64_t Summary::newAreaId() const {
    std::uint64_t result = 1;
    for (const auto& value : areas_) if (value) result = std::max(result, value->id() + 1U);
    return result;
}
std::uint64_t Summary::newBattleId() const {
    std::uint64_t result = 1;
    for (const auto& value : battles_) if (value) result = std::max(result, value->id() + 1U);
    return result;
}
std::uint64_t Summary::newPackageId() const {
    std::uint64_t result = 1;
    for (const auto& value : packages_) if (value) result = std::max(result, value->id() + 1U);
    return result;
}

std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<Area>& value) {
    if (!value) return 0;
    if (index < 0 || static_cast<std::size_t>(index) > areas_.size()) throw std::out_of_range("Summary::insert area");
    if (index == static_cast<std::ptrdiff_t>(areas_.size())) return add(value, true);
    const auto old = value->id();
    const auto existing = std::find_if(areas_.begin(), areas_.end(),
        [&](const auto& item) { return item.get() == value.get(); });
    bool moved = false;
    std::uint64_t temporaryId = 0;
    if (existing != areas_.end()) {
        const auto existingIndex = static_cast<std::size_t>(existing - areas_.begin());
        if (existingIndex == static_cast<std::size_t>(index)) return old;
        moved = true;
        temporaryId = areas_.back()->id() + 2U;
        changeResourceReference<Area>(*this, old, temporaryId);
        lib::eraseFromSlice(areas_, existingIndex);
        if (existingIndex < static_cast<std::size_t>(index)) --index;
    }
    const auto previous = index == 0 ? 0U : areas_[static_cast<std::size_t>(index - 1)]->id();
    auto inserted = value->id();
    lib::Array<std::pair<std::uint64_t, std::uint64_t>> changes;
    if (inserted <= previous || (static_cast<std::size_t>(index) < areas_.size() &&
                                 areas_[static_cast<std::size_t>(index)]->id() <= inserted)) {
        inserted = previous + 1U;
        for (std::size_t i = static_cast<std::size_t>(index); i < areas_.size() &&
             areas_[i]->id() <= (i == static_cast<std::size_t>(index) ? inserted : areas_[i - 1]->id()); ++i) {
            const auto before = areas_[i]->id();
            areas_[i]->id(before + 1U);
            changes.emplace_back(before, before + 1U);
        }
    }
    value->id(inserted);
    lib::insertIntoSlice(areas_, static_cast<std::size_t>(index), value);
    attachArea(value);
    value->changed();
    // 追加される入れ子の利用元は、衝突する参照の再割り当て前に参加させる。
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        changeResourceReference<Area>(*this, it->first, it->second);
    if (moved) changeResourceReference<Area>(*this, temporaryId, inserted);
    changed();
    return old;
}
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<Battle>& value) {
    if (!value) return 0;
    if (index < 0 || static_cast<std::size_t>(index) > battles_.size()) throw std::out_of_range("Summary::insert battle");
    if (index == static_cast<std::ptrdiff_t>(battles_.size())) return add(value, true);
    const auto old = value->id();
    const auto existing = std::find_if(battles_.begin(), battles_.end(),
        [&](const auto& item) { return item.get() == value.get(); });
    bool moved = false;
    std::uint64_t temporaryId = 0;
    if (existing != battles_.end()) {
        const auto existingIndex = static_cast<std::size_t>(existing - battles_.begin());
        if (existingIndex == static_cast<std::size_t>(index)) return old;
        moved = true;
        temporaryId = battles_.back()->id() + 2U;
        changeResourceReference<Battle>(*this, old, temporaryId);
        lib::eraseFromSlice(battles_, existingIndex);
        if (existingIndex < static_cast<std::size_t>(index)) --index;
    }
    const auto previous = index == 0 ? 0U : battles_[static_cast<std::size_t>(index - 1)]->id();
    auto inserted = value->id();
    lib::Array<std::pair<std::uint64_t, std::uint64_t>> changes;
    if (inserted <= previous || (static_cast<std::size_t>(index) < battles_.size() &&
                                 battles_[static_cast<std::size_t>(index)]->id() <= inserted)) {
        inserted = previous + 1U;
        for (std::size_t i = static_cast<std::size_t>(index); i < battles_.size() &&
             battles_[i]->id() <= (i == static_cast<std::size_t>(index) ? inserted : battles_[i - 1]->id()); ++i) {
            const auto before = battles_[i]->id();
            battles_[i]->id(before + 1U);
            changes.emplace_back(before, before + 1U);
        }
    }
    value->id(inserted);
    lib::insertIntoSlice(battles_, static_cast<std::size_t>(index), value);
    attachBattle(value);
    value->changed();
    // 追加される入れ子の利用元は、衝突する参照の再割り当て前に参加させる。
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        changeResourceReference<Battle>(*this, it->first, it->second);
    if (moved) changeResourceReference<Battle>(*this, temporaryId, inserted);
    changed();
    return old;
}
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<Package>& value) {
    if (!value) return 0;
    if (index < 0 || static_cast<std::size_t>(index) > packages_.size()) throw std::out_of_range("Summary::insert package");
    if (index == static_cast<std::ptrdiff_t>(packages_.size())) return add(value, true);
    const auto old = value->id();
    const auto existing = std::find_if(packages_.begin(), packages_.end(),
        [&](const auto& item) { return item.get() == value.get(); });
    bool moved = false;
    std::uint64_t temporaryId = 0;
    if (existing != packages_.end()) {
        const auto existingIndex = static_cast<std::size_t>(existing - packages_.begin());
        if (existingIndex == static_cast<std::size_t>(index)) return old;
        moved = true;
        temporaryId = packages_.back()->id() + 2U;
        changeResourceReference<Package>(*this, old, temporaryId);
        lib::eraseFromSlice(packages_, existingIndex);
        if (existingIndex < static_cast<std::size_t>(index)) --index;
    }
    const auto previous = index == 0 ? 0U : packages_[static_cast<std::size_t>(index - 1)]->id();
    auto inserted = value->id();
    lib::Array<std::pair<std::uint64_t, std::uint64_t>> changes;
    if (inserted <= previous || (static_cast<std::size_t>(index) < packages_.size() &&
                                 packages_[static_cast<std::size_t>(index)]->id() <= inserted)) {
        inserted = previous + 1U;
        for (std::size_t i = static_cast<std::size_t>(index); i < packages_.size() &&
             packages_[i]->id() <= (i == static_cast<std::size_t>(index) ? inserted : packages_[i - 1]->id()); ++i) {
            const auto before = packages_[i]->id();
            packages_[i]->id(before + 1U);
            changes.emplace_back(before, before + 1U);
        }
    }
    value->id(inserted);
    lib::insertIntoSlice(packages_, static_cast<std::size_t>(index), value);
    attachPackage(value);
    value->changed();
    // 追加される入れ子の利用元は、衝突する参照の再割り当て前に参加させる。
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        changeResourceReference<Package>(*this, it->first, it->second);
    if (moved) changeResourceReference<Package>(*this, temporaryId, inserted);
    changed();
    return old;
}

template <typename T>
void resetInsertedCard(const lib::Owner<T>& value) {
    if (!value) return;
    if constexpr (requires(T& card) { card.hold(false); }) value->hold(false);
    if constexpr (requires(T& card) { card.linkId(std::uint64_t{}); }) value->linkId(0);
}

template <typename Collection, typename T>
bool removeCard(Collection& values, const lib::Owner<T>& input,
                Summary& owner);

template <typename T>
std::uint64_t insertCard(lib::SharedSlice<lib::Owner<T>>& values,
                         std::ptrdiff_t index,
                         const lib::Owner<T>& input, Summary& owner) {
    // 移動のために切り離す間に、Dのコールバックが呼び出し側の集合記
    // 述子を置換したり、最後の外部所有元を解放したりする場合がある。
    const auto value = input;
    [[maybe_unused]] const auto keepAlive = owner.weak_from_this().lock();
    // C++はDのnull参照を再現せず、公開境界でnull
    // カードを拒否する。集合とカウンターは変更しない。
    if (!value) throw std::invalid_argument("Summary::insert card: null card");
    if (index == static_cast<std::ptrdiff_t>(values.size())) return owner.add(value, true);
    const auto existing = std::find_if(values.begin(), values.end(),
        [&](const auto& item) { return item.get() == value.get(); });
    bool moved = false;
    std::uint64_t temporaryId = 0;
    if (existing != values.end()) {
        const auto existingIndex = static_cast<std::size_t>(existing - values.begin());
        if (index >= 0 && existingIndex == static_cast<std::size_t>(index)) return value->id();
        moved = true;
        temporaryId = (values.empty() || !values.back()) ? value->id() + 2U
                                                         : values.back()->id() + 2U;
        changeResourceReference<T>(owner, value->id(), temporaryId);
        // Dは再対応付けの後にIDで削除する。削除コールバックか
        // らは、移動カードが完全に切り離された短い列が見える。
        removeCard(values, value, owner);
        if (index >= 0 && existingIndex <= static_cast<std::size_t>(index)) --index;
    }
    // Dの切り離し後の最初のインデックス読み取りに合わせる。不正な移動イン
    // デックスでも、一時的な再対応付けと削除通知の後でだけ例外を送出する。
    if (index < 0 || static_cast<std::size_t>(index) > values.size())
        throw std::out_of_range("Summary::insert card");
    const auto old = value->id();
    const auto previous = index == 0 ? 0U : values[static_cast<std::size_t>(index - 1)]->id();
    auto inserted = old;
    // Dは旧IDごとに最終的な対応付けを一つ記録し、整列したキーを逆順にたどる。
    std::map<std::uint64_t, std::uint64_t> changes;
    if ((index > 0 && inserted <= previous) || (static_cast<std::size_t>(index) < values.size() &&
                                 values[static_cast<std::size_t>(index)]->id() <= inserted)) {
        inserted = previous + 1U;
    }
    value->id(inserted);
    lib::insertIntoSlice(values, static_cast<std::size_t>(index), value);
    for (std::size_t i = static_cast<std::size_t>(index) + 1U; i < values.size(); ++i) {
        const auto previousId = values[i - 1U]->id();
        const auto before = values[i]->id();
        if (previousId != before) continue;
        values[i]->id(before + 1U);
        changes[before] = before + 1U;
    }
    resetInsertedCard(value);
    owner.attachCard(value);
    value->changed();
    // 追加される入れ子の利用元は、衝突する参照の再割り当て前に参加させる。
    for (auto it = changes.rbegin(); it != changes.rend(); ++it)
        changeResourceReference<T>(owner, it->first, it->second);
    if (moved) changeResourceReference<T>(owner, temporaryId, inserted);
    return old;
}

std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<CastCard>& value) { return insertCard(casts_, index, value, *this); }
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<SkillCard>& value) { return insertCard(skills_, index, value, *this); }
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<ItemCard>& value) { return insertCard(items_, index, value, *this); }
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<BeastCard>& value) { return insertCard(beasts_, index, value, *this); }
std::uint64_t Summary::insert(std::ptrdiff_t index, const lib::Owner<InfoCard>& value) { return insertCard(infos_, index, value, *this); }

template <typename Collection, typename T>
bool removeCard(Collection& values, const lib::Owner<T>& input,
                Summary& owner) {
    // DのSummary.removeはID順の型付き集合をqsearch
    // で検索し、同じIDの別インスタンスを渡した場合も、その渡されたオブ
    // ジェクトに後処理を適用する。この二つの観測可能な契約を維持する。
    const auto value = input;
    if (!value) return false;
    const auto keepAlive = owner.weak_from_this().lock();

    // Summaryの検索と同じqsearchを再利用し、IDの
    // 中点による選択を維持する。重複IDがある場合の動作も含む。
    const auto index = utils::qsearch(values, value->id(),
        [](const lib::Owner<T>& candidate, std::uint64_t key) {
            return utils::dcmp(requiredScenarioObject(candidate).id(), key);
        });
    if (index == -1) return false;
    eraseCollectionAt(values, static_cast<std::size_t>(index));
    if constexpr (std::is_same_v<T, Area>) owner.detachArea(value);
    else if constexpr (std::is_same_v<T, Battle>) owner.detachBattle(value);
    else if constexpr (std::is_same_v<T, Package>) owner.detachPackage(value);
    else owner.detachCard(value);
    owner.changed();
    return true;
}

template <typename Collection, typename T>
std::uint64_t addCard(Collection& values, const lib::Owner<T>& input,
                      bool forceNewId, Summary& owner) {
    // 削除通知が呼び出し側の項目や記述子を置換する場合がある。Dの切り離
    // し・通知・再接続の間、入力の所有元とSummaryを生存させる。
    const auto value = input;
    const auto keepAlive = owner.weak_from_this().lock();
    if (!value) return 0;
    const auto old = value->id();
    const auto existing = std::find_if(values.begin(), values.end(),
        [&](const auto& item) { return item.get() == value.get(); });
    const bool moved = existing != values.end();
    if (moved && existing == values.end() - 1) return old;
    auto next = value->id();
    if (forceNewId || (!values.empty() && values.back() && values.back()->id() >= value->id())) {
        next = nextCardId(values);
    }
    if (moved) {
        // Dは同一性で移動対象を検出するが、削除はID検索で行う。
        (void)removeCard(values, value, owner);
        changeResourceReference<T>(owner, old, next);
    }
    value->id(next);
    resetInsertedCard(value);
    appendCollection(values, value);
    if constexpr (std::is_same_v<T, Area>) owner.attachArea(value);
    else if constexpr (std::is_same_v<T, Battle>) owner.attachBattle(value);
    else if constexpr (std::is_same_v<T, Package>) owner.attachPackage(value);
    else owner.attachCard(value);
    value->changed();
    return old;
}

std::uint64_t Summary::add(const lib::Owner<CastCard>& value, bool forceNewId) { return addCard(casts_, value, forceNewId, *this); }
std::uint64_t Summary::add(const lib::Owner<SkillCard>& value, bool forceNewId) { return addCard(skills_, value, forceNewId, *this); }
std::uint64_t Summary::add(const lib::Owner<ItemCard>& value, bool forceNewId) { return addCard(items_, value, forceNewId, *this); }
std::uint64_t Summary::add(const lib::Owner<BeastCard>& value, bool forceNewId) { return addCard(beasts_, value, forceNewId, *this); }
std::uint64_t Summary::add(const lib::Owner<InfoCard>& value, bool forceNewId) { return addCard(infos_, value, forceNewId, *this); }
std::uint64_t Summary::add(const lib::Owner<Area>& input, bool forceNewId) {
    const auto keepAlive = weak_from_this().lock();
    const auto value = input;
    const auto old = addCard(areas_, value, forceNewId, *this);
    if (value && areas_.size() == 1) startArea(value->id());
    return old;
}
std::uint64_t Summary::add(const lib::Owner<Battle>& value, bool forceNewId) {
    const auto old = addCard(battles_, value, forceNewId, *this);
    return old;
}
std::uint64_t Summary::add(const lib::Owner<Package>& value, bool forceNewId) {
    const auto old = addCard(packages_, value, forceNewId, *this);
    return old;
}

void Summary::attachCard(const lib::Owner<card::Card>& value) {
    if (!value) return;
    value->setUseCounter(use_counter_, owner_proxy_);
    value->changeHandler(guardedChangeHandler());
    if (auto card = std::dynamic_pointer_cast<CastCard>(value)) {
        if (auto owner = sharedSummaryOwner<card::CastOwner>(*this)) card->owner(owner);
        else card->owner(static_cast<card::CastOwner*>(this), owner_lifetime_);
    } else if (auto card = std::dynamic_pointer_cast<SkillCard>(value)) {
        if (auto owner = sharedSummaryOwner<card::SkillOwner>(*this)) card->owner(owner);
        else card->owner(static_cast<card::SkillOwner*>(this), owner_lifetime_);
    } else if (auto card = std::dynamic_pointer_cast<ItemCard>(value)) {
        if (auto owner = sharedSummaryOwner<card::ItemOwner>(*this)) card->owner(owner);
        else card->owner(static_cast<card::ItemOwner*>(this), owner_lifetime_);
    } else if (auto card = std::dynamic_pointer_cast<BeastCard>(value)) {
        if (auto owner = sharedSummaryOwner<card::BeastOwner>(*this)) card->owner(owner);
        else card->owner(static_cast<card::BeastOwner*>(this), owner_lifetime_);
    } else if (auto card = std::dynamic_pointer_cast<InfoCard>(value)) {
        if (auto owner = sharedSummaryOwner<card::InfoOwner>(*this)) card->owner(owner);
        else card->owner(static_cast<card::InfoOwner*>(this), owner_lifetime_);
    }
}

// Dの通常のSummaryへの接続では、入力元の範囲情報としてnull
// を渡す。上下の資源の所有関係は、使用箇所への移動範囲とは独立させる。
void Summary::attachArea(const lib::Owner<Area>& value) {
    if (!value) return;
    if (auto owner = sharedSummaryOwner<area::AreaOwner>(*this)) value->owner(owner);
    else value->owner(static_cast<area::AreaOwner*>(this), owner_lifetime_);
    value->setUseCounter(use_counter_,
        static_cast<path::CWXPath*>(nullptr));
    value->changeHandler(guardedChangeHandler());
}

void Summary::attachBattle(const lib::Owner<Battle>& value) {
    if (!value) return;
    if (auto owner = sharedSummaryOwner<area::BattleOwner>(*this)) value->owner(owner);
    else value->owner(static_cast<area::BattleOwner*>(this), owner_lifetime_);
    value->setUseCounter(use_counter_,
        static_cast<path::CWXPath*>(nullptr));
    value->changeHandler(guardedChangeHandler());
}

void Summary::attachPackage(const lib::Owner<Package>& value) {
    if (!value) return;
    if (auto owner = sharedSummaryOwner<area::PackageOwner>(*this)) value->owner(owner);
    else value->owner(static_cast<area::PackageOwner*>(this), owner_lifetime_);
    value->setUseCounter(use_counter_,
        static_cast<path::CWXPath*>(nullptr));
    value->changeHandler(guardedChangeHandler());
}

Summary::ChangeHandler Summary::guardedChangeHandler() {
    const std::weak_ptr<path::OwnerLifetimeToken> lifetime = owner_lifetime_;
    return [this, lifetime] {
        const auto guard = lifetime.lock();
        if (!guard || !guard->alive.load(std::memory_order_acquire)) return;
        changeHandler();
    };
}

void Summary::detachArea(const lib::Owner<Area>& value) {
    if (!value) return;
    value->removeUseCounter();
    value->changeHandler({});
    value->owner(nullptr);
}

void Summary::detachBattle(const lib::Owner<Battle>& value) {
    if (!value) return;
    value->removeUseCounter();
    value->changeHandler({});
    value->owner(nullptr);
}

void Summary::detachPackage(const lib::Owner<Package>& value) {
    if (!value) return;
    value->removeUseCounter();
    value->changeHandler({});
    value->owner(nullptr);
}

void Summary::detachCard(const lib::Owner<card::Card>& value) {
    if (!value) return;
    value->removeUseCounter();
    value->changeHandler({});
    if (auto card = std::dynamic_pointer_cast<CastCard>(value)) card->owner(static_cast<card::CastOwner*>(nullptr));
    else if (auto card = std::dynamic_pointer_cast<SkillCard>(value)) card->owner(static_cast<card::SkillOwner*>(nullptr));
    else if (auto card = std::dynamic_pointer_cast<ItemCard>(value)) card->owner(static_cast<card::ItemOwner*>(nullptr));
    else if (auto card = std::dynamic_pointer_cast<BeastCard>(value)) card->owner(static_cast<card::BeastOwner*>(nullptr));
    else if (auto card = std::dynamic_pointer_cast<InfoCard>(value)) card->owner(static_cast<card::InfoOwner*>(nullptr));
}

void Summary::remove(const lib::Owner<CastCard>& value) { removeCard(casts_, value, *this); }
void Summary::remove(const lib::Owner<SkillCard>& value) { removeCard(skills_, value, *this); }
void Summary::remove(const lib::Owner<ItemCard>& value) { removeCard(items_, value, *this); }
void Summary::remove(const lib::Owner<BeastCard>& value) { removeCard(beasts_, value, *this); }
void Summary::remove(const lib::Owner<InfoCard>& value) { removeCard(infos_, value, *this); }
void Summary::remove(const lib::Owner<Area>& input) {
    const auto keepAlive = weak_from_this().lock();
    const auto value = input;
    if (!value) return;
    (void)removeCard(areas_, value, *this);
    if (startArea() == value->id()) startArea(areas_.empty() ? 0 : areas_.front()->id());
}
void Summary::remove(const lib::Owner<Battle>& input) {
    const auto value = input;
    (void)removeCard(battles_, value, *this);
}
void Summary::remove(const lib::Owner<Package>& input) {
    const auto value = input;
    (void)removeCard(packages_, value, *this);
}
void Summary::remove(const lib::Owner<AbstractArea>& value) {
    if (!value) return;
    // Dと同様に型で振り分ける。共通の削除処理は、選んだ各型
    // 付き集合をIDで検索し、Dのqsearchに合わせる。
    if (auto area = std::dynamic_pointer_cast<Area>(value)) { remove(area); return; }
    if (auto battle = std::dynamic_pointer_cast<Battle>(value)) { remove(battle); return; }
    if (auto package = std::dynamic_pointer_cast<Package>(value)) { remove(package); return; }
    throw SummaryException("Unsupported abstract area type");
}

void Summary::eventTemplates(lib::SharedSlice<EvTemplate> value) {
    const auto equal = [](const EvTemplate& first, const EvTemplate& second) {
        return first.name == second.name && first.script == second.script &&
               first.mnemonic == second.mnemonic && first.hotkey == second.hotkey;
    };
    bool same = event_templates_.size() == value.size();
    for (std::size_t index = 0; same && index < event_templates_.size(); ++index) {
        same = equal(event_templates_[index], value[index]);
    }
    if (same) return;
    changed();
    event_templates_ = value;
}
void Summary::useKeyInAction(bool value) { if (use_key_in_action_ != value) changed(); use_key_in_action_ = value; }
void Summary::numericKeycodes(bool value) {
    if (numeric_keycodes_ != value) changed();
    numeric_keycodes_ = value;
}

void Summary::noConsumeUnmatchedMenuCard(bool value) {
    if (no_consume_unmatched_menu_card_ != value) changed();
    no_consume_unmatched_menu_card_ = value;
}

String Summary::summaryToXML(const card::XMLOption& option) const {
    auto root = XNode::create("Summary");
    if (!data_version_.empty()) root.newAttr("dataVersion", data_version_);
    if (load_scaled_image_) root.newAttr("scaledimage", true);
    auto property = root.newElement("Property");
    property.newElement("Name", scenario_name_);
    (void)card::CardImage::toNode(property, image_paths_);
    property.newElement("Author", author_);
    property.newElement("Description", utils::encodeLf(description_));
    auto level = property.newElement("Level");
    level.newAttr("min", level_min_); level.newAttr("max", level_max_);
    auto coupons = property.newElement("RequiredCoupons", utils::encodeLf(rCoupons()));
    coupons.newAttr("number", required_coupon_num_);
    property.newElement("StartAreaId", startArea());
    property.newElement("Tags");
    auto type = property.newElement("Type", type_);
    if (option.saveSkinName) type.newAttr("skinname", skin_name_);
    if (use_key_in_action_ || no_consume_unmatched_menu_card_ || numeric_keycodes_) {
        auto settings = property.newElement("Settings");
        if (use_key_in_action_) settings.newAttr("useKeyInAction", true);
        if (no_consume_unmatched_menu_card_) settings.newAttr("noConsumeUnmatchedMenuCard", true);
        if (numeric_keycodes_) settings.newAttr("numericKeycodes", true);
    }
    if (flag_root_) flag_root_->toNodeAll(root, option.logicalSort);
    root.newElement("Labels");
    auto templates = root.newElement("EventTemplates");
    for (const auto& value : event_templates_) value.toNode(templates);
    return root.text();
}

XmlFileTree Summary::toXMLs(const System& sys, const SaveOption& saveOpt) const {
    card::XMLOption option(sys, data_version_);
    option.loadScaledImage = load_scaled_image_;
    option.includeCard = !isTargetVersion("1");
    option.skill = [this](std::uint64_t id) { return skill(id); };
    option.item = [this](std::uint64_t id) { return item(id); };
    option.beast = [this](std::uint64_t id) { return beast(id); };
    option.saveSkinName = saveOpt.saveSkinName;
    option.logicalSort = saveOpt.logicalSort;
    XmlFileTree result;
    result[""]["Summary.xml"] = summaryToXML(option);
    auto putCards = [&](StringView directory, const auto& values) {
        for (const auto& value : values) if (value) result[String(directory)][nameForXml(value->id(), value->name(), saveOpt)] = value->toXML(option);
    };
    putCards(PATH_CAST, casts_); putCards(PATH_SKILL, skills_); putCards(PATH_ITEM, items_); putCards(PATH_BEAST, beasts_); putCards(PATH_INFO, infos_);
    auto putAreas = [&](StringView directory, const auto& values) {
        for (const auto& value : values) if (value) {
            result[String(directory)][nameForXml(value->id(), value->name(), saveOpt)] = value->toXML(option);
        }
    };
    putAreas(PATH_AREA, areas_); putAreas(PATH_BATTLE, battles_); putAreas(PATH_PACKAGE, packages_);
    return result;
}

String Summary::fileNameFor(std::uint64_t id, StringView name, const SaveOption& option) { return nameForXml(id, name, option); }

void Summary::saveXMLSet(const Path& directory, StringView category,
                         const SaveOption& option, const card::XMLOption& xopt,
                         const ChangedResourceSet* changed, FileSync& sync,
                         lib::Array<Path>& staleFiles) {
    // Dは保存対象がある場合だけカテゴリのディレクトリを作る。
    std::unordered_set<String> wrote;
    auto save = [&](const auto& values) {
        if (!values.empty()) makeDirectory(directory, storage_);
        for (const auto& value : values) {
            if (!value) continue;
            const auto file = fileNameFor(value->id(), value->name(), option);
            wrote.insert(file);
            // fileはUTF-8。Windowsでpath /
            // std::stringを使うと現在のコードページで解
            // 釈され、日本語を含むArea/Battle名が壊れる
            // か拒否される。全シナリオルートのパスと同じUTF-8
            // アダプターで、ネイティブのファイルシステムへ渡す。
            const auto full = directory / fsPath(file);
            const bool changedValue = !changed || changed->contains(pathPointer(value));
            if (!option.saveChangedOnly || !isRegularFile(full) || changedValue) writeText(full, value->toXML(xopt), sync);
        }
    };
    if (category == PATH_CAST) save(casts_);
    else if (category == PATH_SKILL) save(skills_);
    else if (category == PATH_ITEM) save(items_);
    else if (category == PATH_BEAST) save(beasts_);
    else if (category == PATH_INFO) save(infos_);
    else if (category == PATH_AREA) save(areas_);
    else if (category == PATH_BATTLE) save(battles_);
    else if (category == PATH_PACKAGE) save(packages_);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file(error) || !isXmlFile(entry.path())) continue;
        if (!wrote.contains(utf8Path(entry.path().filename())) &&
            !isTemporaryName(utf8Path(entry.path().filename()))) {
            staleFiles.push_back(entry.path());
        }
    }
}

void Summary::saveXMLs(StringView path, const System& sys, const SaveOption& option,
                       FileSync& sync) {
    SaveOperationScope saving(*this);
    saveXMLsImpl(path, sys, option, sync);
}

void Summary::saveXMLsImpl(StringView path, const System& sys,
                           const SaveOption& option, FileSync& sync) {
    XmlSaveScope xmlSaving(*this);
    // XML/WSNの永続化の境界。保存失敗時に簡潔な公開呼び
    // 出しスタックを残すため、入れ子の補助処理は追跡しない。
    perf::TraceScope trace(__FILE__, __LINE__);
    const auto output = fsPath(path);
    XmlSaveRollback rollback(output);
    try {
        option.runIo([output, storage = storage_, &rollback, &sync,
                      backup = option.backup, backupDir = option.backupDir] {
            sync.sync();
            makeDirectory(output, storage);
            rollback.capture();
            // WSN/XMLの保存先は新規ディレクトリの場合や、素材なしテンプ
            // レートから作った場合がある。createScenarioの一時ツ
            // リーだけでなく、実際の出力時にも空の素材フォルダーを維持する。
            makeDirectory(output / "Material", storage);
            const auto backupParent = fsPath(backupDir);
            const bool canBackup = [&] {
                if (!backup) return false;
                if (backupDir.empty()) throw SummaryException("Backup directory is empty");
                if (!storageExists(backupParent, storage)) return true;
                if (storage) if (const auto relative = storageRelativePath(backupParent, *storage)) {
                    const auto stat = storage->stat(*relative);
                    return stat && stat->is_directory();
                }
                return std::filesystem::is_directory(backupParent);
            }();
            if (canBackup) {
                // Dの一つのバックアップを上書きする動作への意図的な拡張。前の完全
                // な世代を保持し、後の保存で復旧用データが失われないようにする。
                const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                const auto sequence = next_backup_id.fetch_add(1, std::memory_order_relaxed);
                const auto scenarioName = output.filename().empty()
                    ? String("scenario") : utf8Path(output.filename());
                const auto backupRoot = backupParent /
                    ("cwxeditor_presave_" + scenarioName + "_" +
                     std::to_string(timestamp) + "_" + std::to_string(sequence));

                auto copyExistingXml = [&](const Path& source, const Path& destination) {
                    if (storage) {
                        if (const auto relative = storageRelativePath(source, *storage)) {
                            const auto stat = storage->stat(*relative);
                            if (!stat || !stat->is_file()) return;
                            copyFile(source, destination, storage);
                            return;
                        }
                    }
                    std::error_code error;
                    if (!std::filesystem::is_regular_file(source, error)) return;
                    if (error) {
                        throw SummaryException("Unable to inspect pre-save backup: " +
                                               utf8Path(source) + ": " + error.message());
                    }
                    copyFile(source, destination, nullptr);
                };
                copyExistingXml(output / "Summary.xml", backupRoot / "Summary.xml");
                for (const auto category : {PATH_CAST, PATH_SKILL, PATH_ITEM,
                                            PATH_BEAST, PATH_INFO, PATH_AREA,
                                            PATH_BATTLE, PATH_PACKAGE}) {
                    const auto sourceDirectory = output / String(category);
                    std::error_code error;
                    if (!std::filesystem::is_directory(sourceDirectory, error)) continue;
                    for (std::filesystem::directory_iterator it(sourceDirectory, error), end;
                         it != end && !error; it.increment(error)) {
                        if (it->is_regular_file(error) && isXmlFile(it->path())) {
                            copyExistingXml(it->path(),
                                            backupRoot / String(category) /
                                                it->path().filename());
                        }
                    }
                    if (error) {
                        throw SummaryException("Unable to enumerate pre-save backup files: " +
                                               error.message());
                    }
                }
            }
        });
        card::XMLOption xopt(sys, data_version_);
        xopt.loadScaledImage = load_scaled_image_;
        xopt.includeCard = !isTargetVersion("1");
        xopt.skill = [this](std::uint64_t id) { return skill(id); };
        xopt.item = [this](std::uint64_t id) { return item(id); };
        xopt.beast = [this](std::uint64_t id) { return beast(id); };
        xopt.saveSkinName = option.saveSkinName;
        xopt.logicalSort = option.logicalSort;
        writeText(output / "Summary.xml", summaryToXML(xopt), sync);
        ChangedResourceSet resources;
        const ChangedResourceSet* changed = nullptr;
        if (option.saveChangedOnly) { resources = changedResources(); changed = &resources; }
        lib::Array<Path> staleFiles;
        saveXMLSet(output / String(PATH_CAST), PATH_CAST, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_SKILL), PATH_SKILL, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_ITEM), PATH_ITEM, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_BEAST), PATH_BEAST, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_INFO), PATH_INFO, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_AREA), PATH_AREA, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_BATTLE), PATH_BATTLE, option, xopt, changed, sync, staleFiles);
        saveXMLSet(output / String(PATH_PACKAGE), PATH_PACKAGE, option, xopt, changed, sync, staleFiles);

        // すべての置換内容が永続化されるまで、以前の資源一式を削
        // 除しない。キューの書き込み失敗時も最後の完全なシナリオ
        // を維持し、Editorモデルは変更済みのままにする。
        option.runIo([&sync, &rollback, output, staleFiles = std::move(staleFiles), storage = storage_] {
            sync.sync();
            for (const auto& stale : staleFiles) {
                if (storage) {
                    if (const auto relative = storageRelativePath(stale, *storage)) {
                        if (!storage->remove(*relative, false)) {
                            auto diagnostic = storage->last_error();
                            if (diagnostic.empty()) diagnostic = "Storage::remove failed";
                            throw SummaryException("Unable to remove obsolete XML: " +
                                                   utf8Path(stale) + ": " + diagnostic);
                        }
                        continue;
                    }
                }
                std::error_code error;
                if (!std::filesystem::remove(stale, error) && error) {
                    throw SummaryException("Unable to remove obsolete XML: " +
                                           utf8Path(stale) + ": " + error.message());
                }
            }
            for (const auto category : {PATH_CAST, PATH_SKILL, PATH_ITEM, PATH_BEAST,
                                        PATH_INFO, PATH_AREA, PATH_BATTLE, PATH_PACKAGE}) {
                removeEmptyDirectory(output / String(category), storage);
            }
            rollback.commit();
        });
        // 元のsaveXMLsの保護範囲はコールバックより前に終了する。その再入の境界を維持
        // しつつ、SaveOperationScopeでは引き続きdelTempを遅らせる。
        xmlSaving.release();
        if (option.savedCallback) option.savedCallback();
    } catch (...) {
        const auto saveFailure = std::current_exception();
        // キュー内で失敗しても、ワーカーの残りの処理は完了さ
        // せる。遅れて成功した書き込みが復旧済みの文書一式を
        // 上書きしないよう、その完了を待ってから復元する。
        try {
            sync.sync();
        } catch (...) {
        }
        try {
            rollback.restore(sync);
        } catch (const std::exception& recoveryError) {
            throw SummaryException(
                "Scenario save failed and automatic XML recovery also failed. "
                "Recovery snapshot: " + rollback.snapshotPath() + ": " +
                recoveryError.what());
        } catch (...) {
            throw SummaryException(
                "Scenario save failed and automatic XML recovery also failed. "
                "Recovery snapshot: " + rollback.snapshotPath());
        }
        std::rethrow_exception(saveFailure);
    }
}

void Summary::saveXMLs(const System& sys, const SaveOption& option, FileSync& sync) {
    saveXMLs(scenario_path_, sys, option, sync);
}

Summary::Owner Summary::summaryFromXML(const System& sys, StringView sPath,
                                        StringView xml) {
    const auto root = XNode::parse(xml);
    if (root.name() != "Summary") throw SummaryException("File is not summary: " + String(sPath));
    auto result = std::make_shared<Summary>(String(sPath));
    // Dはバージョン指定以前のSummary.xmlをWsn.0（空のdataVersion
    // ）として扱う。新規作成は引き続きDEFAULT_VERSIONを
    // 使うが、通常読み込みで既存の欠落属性を最新形式と解釈し直さない。
    result->data_version_ = root.attr<String>(
        "dataVersion", false, String{});
    result->load_scaled_image_ = root.attr<bool>("scaledimage", false, false);
    const auto property = root.child("Property", false);
    if (property.valid()) {
        result->scenario_name_ = property.childText<String>("Name", false, "");
        const auto parsedImages = card::CardImage::createFromNode(const_cast<XNode&>(property));
        result->image_paths_.reserve(parsedImages.size());
        const auto rootPath = static_cast<path::CWXPath*>(
            static_cast<card::CastOwner*>(result.get()));
        for (const auto& image : parsedImages) {
            if (!image) continue;
            auto copy = std::make_shared<CardImage>(rootPath, *image);
            copy->setUseCounter(result->use_counter_);
            result->image_paths_.push_back(std::move(copy));
        }
        result->author_ = property.childText<String>("Author", false, "");
        result->description_ = utils::decodeLf2(property.childText<String>("Description", false, ""));
        const auto level = property.child("Level", false);
        if (level.valid()) {
            // DではLevelは省略可能だが、存在する場合は両属性を必須とする。
            result->level_min_ = level.attr<std::uint32_t>("min", true);
            result->level_max_ = level.attr<std::uint32_t>("max", true);
        }
        const auto coupons = property.child("RequiredCoupons", false);
        if (coupons.valid()) {
            // DはRequiredCoupons要素が存在する場合、numberを
            // 必須とする。テキストの復号では、その件数とは独立して空の名前を除く。
            result->required_coupon_num_ = coupons.attr<std::uint32_t>("number", true);
            result->rCoupons(utils::decodeLf(coupons.value()));
        }
        result->startArea(property.childText<std::uint64_t>("StartAreaId", false, 0));
        const auto type = property.child("Type", false);
        if (type.valid()) { result->type_ = type.value(); result->skin_name_ = type.attr<String>("skinname", false, ""); }
        const auto settings = property.child("Settings", false);
        if (settings.valid()) {
            result->use_key_in_action_ = settings.attr<bool>("useKeyInAction", false, false);
            result->no_consume_unmatched_menu_card_ = settings.attr<bool>("noConsumeUnmatchedMenuCard", false, false);
            result->numeric_keycodes_ = settings.attr<bool>("numericKeycodes", false, false);
        }
    }
    const auto templates = root.child("EventTemplates", false);
    if (templates.valid()) {
        lib::Array<EvTemplate> values;
        for (auto child : templates.children()) if (child.name() == "eventTemplate") {
            EvTemplate value;
            value.fromNode(child);
            values.push_back(std::move(value));
        }
        result->event_templates_ = lib::SharedSlice<EvTemplate>(std::move(values));
    }
    const auto version = XMLInfo(sys, result->data_version_);
    if (result->flag_root_) result->flag_root_->fromXmlNode(const_cast<XNode&>(root), version);
    result->resetChanged();
    return result;
}

void Summary::checkStartArea() {
    const auto current = startArea();
    if (hasAreaId(current)) {
        startArea(current);
        return;
    }
    startArea(areas_.empty() || !areas_.front() ? 0 : areas_.front()->id());
}

template <typename T>
[[nodiscard]] bool hasExpectedResourceRoot(const XNode& node) {
    // DのloadXMLCommonは全ファイルを解析し、有効なXML
    // でもルートが読み込み対象の資源種別と違えば、通知せずに飛ばす。
    return node.name() == T::XML_NAME;
}

template <typename T>
void loadCardDirectory(const CProps& prop, const Path& directory,
                       const XMLInfo& version, StringView category,
                       std::vector<lib::Owner<T>>& values,
                       lib::Array<String>& errors, Summary& owner,
                       const LoadOption& option) {
    std::error_code error;
    if (!isDirectory(directory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        // 取消はこのメンバーの解析だけでなく、読み込み全体を終了する。
        option.checkCancelled();
        if (error || !entry.is_regular_file(error) || !isXmlFile(entry.path())) continue;
        // 取消はメンバーのエラー捕捉の外で扱う。
        option.reportProgress("シナリオXMLの読み込み", utf8Path(entry.path()));
        try {
            if (option.processFunc) option.processFunc(
                owner.scenarioName(), utf8Path(entry.path().filename()));
            auto node = XNode::parse(readText(entry.path()));
            if (!hasExpectedResourceRoot<T>(node)) continue;
            lib::Owner<T> value;
            if constexpr (std::is_same_v<T, CastCard>) value = T::createFromNode(prop, node, version);
            else value = T::createFromNode(node, version);
            if (value) {
                // 読み込みはEditorでの挿入操作ではない。add()は直
                // 前の挿入IDより小さいカードIDを変更し得るが、ディレク
                // トリ列挙（特にWeb MEMFS）はID順とは限らない。
                // 戦闘・イベントの参照を保つため、保存済みIDを維持する。
                appendCollection(values, value);
                owner.attachCard(value);
            }
        } catch (const std::exception& exception) {
            const String member = String(category) + "/" +
                                  utf8Path(entry.path().filename());
            errors.push_back(member);
            if (option.issueFunc) option.issueFunc(member, exception.what());
        } catch (...) {
            const String member = String(category) + "/" +
                                  utf8Path(entry.path().filename());
            errors.push_back(member);
            if (option.issueFunc) option.issueFunc(member, "不明な読み込みエラー");
        }
    }
}

template <typename T>
void loadAreaDirectory(const Path& directory, const XMLInfo& version,
                       StringView category, lib::SharedSlice<lib::Owner<T>>& values,
                       lib::Array<String>& errors, Summary& owner,
                       const LoadOption& option) {
    std::error_code error;
    if (!isDirectory(directory)) return;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        option.checkCancelled();
        if (error || !entry.is_regular_file(error) || !isXmlFile(entry.path())) continue;
        // 取消はメンバーのエラー捕捉の外で扱う。
        option.reportProgress("シナリオXMLの読み込み", utf8Path(entry.path()));
        try {
            if (option.processFunc) option.processFunc(
                owner.scenarioName(), utf8Path(entry.path().filename()));
            auto node = XNode::parse(readText(entry.path()));
            if (!hasExpectedResourceRoot<T>(node)) continue;
            auto value = T::createFromNode(node, version);
            if (value) {
                // DのloadXMLCommonは、前のファイルとID
                // が重複する場合も、解析した全資源を追加する。DMD
                // 2.097の比較実行では両方が残り、エラーファイルに
                // もならない。読み込みは保存済みIDをそのまま復元する
                // 。公開add APIはEditorの挿入操作で、列
                // 挙順によってIDを変え得るため、ここでは使わない。
                appendCollection(values, value);
                if constexpr (std::same_as<T, Area>) owner.attachArea(value);
                else if constexpr (std::same_as<T, Battle>) owner.attachBattle(value);
                else if constexpr (std::same_as<T, Package>) owner.attachPackage(value);
            }
        } catch (const std::exception& exception) {
            const String member = String(category) + "/" +
                                  utf8Path(entry.path().filename());
            errors.push_back(member);
            if (option.issueFunc) option.issueFunc(member, exception.what());
        } catch (...) {
            const String member = String(category) + "/" +
                                  utf8Path(entry.path().filename());
            errors.push_back(member);
            if (option.issueFunc) option.issueFunc(member, "不明な読み込みエラー");
        }
    }
    (void)values;
}

Summary::Owner Summary::fromXMLs(const CProps& prop, StringView summaryPath,
                                 const LoadOption& option,
                                 lib::Array<String>& errorFiles) {
    const auto path = fsPath(summaryPath);
    auto result = summaryFromXML(prop.sys(), utf8Path(path.parent_path()), readText(path));
    if (option.summaryOnly) return result;
    const auto version = XMLInfo(prop.sys(), result->dataVersion());
    if (!option.cardOnly) {
        // 公開add(Area) APIは最初に追加したエリアを開始エリア
        // にする。DのXML読み込みはadd()を経由せずSummary.xml
        // の値を維持するため、読み込みの前後で値を保持・復元する。
        const auto parsedStartArea = result->startArea();
        loadAreaDirectory<Area>(path.parent_path() / String(PATH_AREA), version,
                                PATH_AREA, result->areas_, errorFiles, *result, option);
        loadAreaDirectory<Battle>(path.parent_path() / String(PATH_BATTLE), version,
                                  PATH_BATTLE, result->battles_, errorFiles, *result, option);
        loadAreaDirectory<Package>(path.parent_path() / String(PATH_PACKAGE), version,
                                   PATH_PACKAGE, result->packages_, errorFiles, *result, option);
        sortAreas(result->areas_);
        sortAreas(result->battles_);
        sortAreas(result->packages_);
        result->startArea(parsedStartArea);
        result->checkStartArea();
    }
    CastList casts;
    SkillList skills;
    ItemList items;
    BeastList beasts;
    InfoList infos;
    loadCardDirectory<CastCard>(prop, path.parent_path() / String(PATH_CAST), version, PATH_CAST, casts, errorFiles, *result, option);
    loadCardDirectory<SkillCard>(prop, path.parent_path() / String(PATH_SKILL), version, PATH_SKILL, skills, errorFiles, *result, option);
    loadCardDirectory<ItemCard>(prop, path.parent_path() / String(PATH_ITEM), version, PATH_ITEM, items, errorFiles, *result, option);
    loadCardDirectory<BeastCard>(prop, path.parent_path() / String(PATH_BEAST), version, PATH_BEAST, beasts, errorFiles, *result, option);
    loadCardDirectory<InfoCard>(prop, path.parent_path() / String(PATH_INFO), version, PATH_INFO, infos, errorFiles, *result, option);
    sortCards(casts); sortCards(skills); sortCards(items); sortCards(beasts); sortCards(infos);
    result->casts_ = lib::SharedSlice<lib::Owner<CastCard>>(std::move(casts));
    result->skills_ = lib::SharedSlice<lib::Owner<SkillCard>>(std::move(skills));
    result->items_ = lib::SharedSlice<lib::Owner<ItemCard>>(std::move(items));
    result->beasts_ = lib::SharedSlice<lib::Owner<BeastCard>>(std::move(beasts));
    result->infos_ = lib::SharedSlice<lib::Owner<InfoCard>>(std::move(infos));
    // DのXML補助処理は0も含めて保存済みIDを保持する。シナリオ読
    // み込みの入口が、この解析の後にrepairId0を明示的に呼ぶ。
    result->resetChanged();
    return result;
}

Summary::Owner Summary::fromXMLs(const CProps& prop, StringView sPath,
                                 const XmlFileTree& xmls, const LoadOption& option,
                                 lib::Array<String>& errorFiles) {
    const auto parent = xmls.find("");
    if (parent == xmls.end()) throw SummaryException("invalid XML tree");
    const auto summary = parent->second.find("Summary.xml");
    if (summary == parent->second.end()) throw SummaryException("Summary.xml is missing");
    auto result = summaryFromXML(prop.sys(), sPath, summary->second);
    if (option.summaryOnly) return result;
    const auto version = XMLInfo(prop.sys(), result->dataVersion());
    auto loadAreas = [&](StringView category, auto& values) {
        const auto directory = xmls.find(String(category));
        if (directory == xmls.end()) return;
        for (const auto& [file, text] : directory->second) {
            option.reportProgress("シナリオXMLの読み込み", String(sPath) + "/" + String(category) + "/" + file);
            try {
                if (option.processFunc) option.processFunc(result->scenarioName(), file);
                auto node = XNode::parse(text);
                using T = typename std::remove_reference_t<decltype(values)>::value_type::element_type;
                if (!hasExpectedResourceRoot<T>(node)) continue;
                auto value = T::createFromNode(node, version);
                if (value) {
                    // Dは重複IDも追加する（loadAreaDirectory参照）。
                    appendCollection(values, value);
                    if constexpr (std::same_as<T, Area>) result->attachArea(value);
                    else if constexpr (std::same_as<T, Battle>) result->attachBattle(value);
                    else if constexpr (std::same_as<T, Package>) result->attachPackage(value);
                }
            } catch (const std::exception& exception) {
                const String member = String(category) + "/" + file;
                errorFiles.push_back(member);
                if (option.issueFunc) option.issueFunc(member, exception.what());
            } catch (...) {
                const String member = String(category) + "/" + file;
                errorFiles.push_back(member);
                if (option.issueFunc) option.issueFunc(member, "不明な読み込みエラー");
            }
        }
    };
    if (!option.cardOnly) {
        const auto parsedStartArea = result->startArea();
        loadAreas(PATH_AREA, result->areas_);
        loadAreas(PATH_BATTLE, result->battles_);
        loadAreas(PATH_PACKAGE, result->packages_);
        sortAreas(result->areas_);
        sortAreas(result->battles_);
        sortAreas(result->packages_);
        result->startArea(parsedStartArea);
        result->checkStartArea();
    }
    CastList casts;
    SkillList skills;
    ItemList items;
    BeastList beasts;
    InfoList infos;
    auto load = [&](StringView category, auto& values) {
        const auto directory = xmls.find(String(category));
        if (directory == xmls.end()) return;
        for (const auto& [file, text] : directory->second) {
            option.reportProgress("シナリオXMLの読み込み", String(sPath) + "/" + String(category) + "/" + file);
            try {
                if (option.processFunc) option.processFunc(result->scenarioName(), file);
                auto node = XNode::parse(text);
                using T = typename std::remove_reference_t<decltype(values)>::value_type::element_type;
                if (!hasExpectedResourceRoot<T>(node)) continue;
                lib::Owner<T> value;
                if constexpr (std::is_same_v<T, CastCard>) value = T::createFromNode(prop, node, version);
                else value = T::createFromNode(node, version);
                if (value) {
                    // ディレクトリ読み込みと同様に、XMLメンバ
                    // ーの順序で既存カードの同一性を変えない。
                    appendCollection(values, value);
                    result->attachCard(value);
                }
            } catch (const std::exception& exception) {
                const String member = String(category) + "/" + file;
                errorFiles.push_back(member);
                if (option.issueFunc) option.issueFunc(member, exception.what());
            } catch (...) {
                const String member = String(category) + "/" + file;
                errorFiles.push_back(member);
                if (option.issueFunc) option.issueFunc(member, "不明な読み込みエラー");
            }
        }
    };
    load(PATH_CAST, casts); load(PATH_SKILL, skills); load(PATH_ITEM, items); load(PATH_BEAST, beasts); load(PATH_INFO, infos);
    sortCards(casts); sortCards(skills); sortCards(items); sortCards(beasts); sortCards(infos);
    result->casts_ = lib::SharedSlice<lib::Owner<CastCard>>(std::move(casts));
    result->skills_ = lib::SharedSlice<lib::Owner<SkillCard>>(std::move(skills));
    result->items_ = lib::SharedSlice<lib::Owner<ItemCard>>(std::move(items));
    result->beasts_ = lib::SharedSlice<lib::Owner<BeastCard>>(std::move(beasts));
    result->infos_ = lib::SharedSlice<lib::Owner<InfoCard>>(std::move(infos));
    // XMLツリーのオーバーロードは、Dと同じ解析だけの境界に保
    // つ。ID 0の修復は明示的なシナリオ読み込み操作で行う。
    result->resetChanged();
    return result;
}

void Summary::fromXMLs(const CProps& prop, const Owner& result,
                       const LoadOption& option, lib::Array<String>& errors) {
    if (!result || option.summaryOnly) return;
    const auto summaryPath = result->scenario_path_.empty()
        ? String("Summary.xml")
        : result->scenario_path_ + "/Summary.xml";
    auto loaded = fromXMLs(prop, StringView(summaryPath), option, errors);
    result->areas_ = std::move(loaded->areas_);
    result->battles_ = std::move(loaded->battles_);
    result->packages_ = std::move(loaded->packages_);
    result->casts_ = std::move(loaded->casts_); result->skills_ = std::move(loaded->skills_); result->items_ = std::move(loaded->items_); result->beasts_ = std::move(loaded->beasts_); result->infos_ = std::move(loaded->infos_);
    for (const auto& value : result->areas_) result->attachArea(value);
    for (const auto& value : result->battles_) result->attachBattle(value);
    for (const auto& value : result->packages_) result->attachPackage(value);
    for (const auto& value : result->casts_) result->attachCard(value);
    for (const auto& value : result->skills_) result->attachCard(value);
    for (const auto& value : result->items_) result->attachCard(value);
    for (const auto& value : result->beasts_) result->attachCard(value);
    for (const auto& value : result->infos_) result->attachCard(value);
    result->resetChanged();
}

Summary::ReloadOperation Summary::prepareReload() const {
    auto source = std::make_shared<Summary>(scenario_path_);
    source->scenario_name_ = scenario_name_;
    source->type_ = type_;
    source->skin_name_ = skin_name_;
    source->legacy_ = legacy_;
    source->use_temp_ = use_temp_;
    source->expand_xmls_ = expand_xmls_;
    source->zip_name_ = zip_name_;
    source->orig_zip_name_ = orig_zip_name_;
    source->temp_path_ = temp_path_;
    source->read_only_path_ = read_only_path_;
    source->storage_ = storage_;
    source->old_xmls_ = old_xmls_;
    source->temp_lifetime_ = temp_lifetime_;
    return [source](const CProps& prop, const LoadOption& option, lib::Array<String>& errors) {
        return source->reloadXMLs(prop, option, errors);
    };
}

Summary::Owner Summary::reloadXMLs(const CProps& prop, const LoadOption& option,
                                   lib::Array<String>& errors) const {
    // MainWindow::reloadはこの公開再読み込み境界へ直
    // 接到達する。Dと同様にXMLとClassicの分岐を一つの診断フ
    // レームで扱い、各文書・メンバーの列挙中にフレームを追加しない。
    perf::TraceScope trace(__FILE__, __LINE__);
    option.checkCancelled();
    errors.clear();
    if (legacy_) {
        std::int32_t classicVersion = 0;
        auto result = cwl::loadLScenario(scenario_path_, type_, skin_name_,
                                         prop, option, errors, classicVersion,
                                         scenario_name_);
        if (!result) return {};
        option.checkCancelled();
        result->legacy_ = true;
        result->use_temp_ = use_temp_;
        result->zip_name_ = zip_name_;
        result->orig_zip_name_ = orig_zip_name_;
        result->temp_path_ = temp_path_;
        result->read_only_path_ = read_only_path_;
        result->scenario_path_ = scenario_path_;
        result->temp_lifetime_ = temp_lifetime_;
        result->setStorage(storage_);
        result->refCheckPaths();
        result->updateJpy1List(prop);
        return result;
    }
    const auto summaryPath = scenario_path_.empty()
        ? String("Summary.xml")
        : scenario_path_ + "/Summary.xml";
    // Dは未展開のアーカイブでも、存在しないディスク上のXMLを再読
    // み込みする。公開再読み込みを使用可能に保つため、同じfromXMLs
    // パーサーとモデルの所有管理で、保持済みのバイト列を読む。
    auto result = use_temp_ && !expand_xmls_
        ? fromXMLs(prop, scenario_path_, old_xmls_, option, errors)
        : fromXMLs(prop, StringView(summaryPath), option, errors);
    option.checkCancelled();
    result->setStorage(storage_);
    result->expand_xmls_ = expand_xmls_; result->use_temp_ = use_temp_; result->zip_name_ = zip_name_; result->orig_zip_name_ = orig_zip_name_; result->temp_path_ = temp_path_; result->legacy_ = legacy_; result->temp_lifetime_ = temp_lifetime_;
    result->scenario_path_ = scenario_path_;
    result->read_only_path_ = read_only_path_;
    result->old_xmls_ = old_xmls_;
    result->refCheckPaths();
    result->updateJpy1List(prop);
    return result;
}

void Summary::scenarioPath(String value) {
    if (scenario_path_ == value) return;
    const auto retainedSelf = retainForCallback();
    scenario_path_ = value;
    for (auto& valueJpy : jpy_data_) {
        const auto relative = utils::abs2rel(StringView(valueJpy.jpy1Path), StringView(valueJpy.sPath));
        valueJpy.jpy1Path = utils::buildPath(value, relative);
        for (auto& section : valueJpy.sections) {
            auto& current = requiredScenarioObject(section);
            const auto rel = utils::abs2rel(StringView(current.fPath), StringView(current.sPath));
            current.fPath = utils::buildPath(value, rel);
            current.sPath = value;
        }
    }
    for (auto& valueJpdc : jpdc_data_) {
        auto& current = requiredScenarioObject(valueJpdc);
        const auto relative = utils::abs2rel(StringView(current.jpdcPath), StringView(current.sPath));
        current.jpdcPath = utils::buildPath(value, relative);
        current.sPath = value;
    }
}

void Summary::scenarioName(String value) { setBaseParams(std::move(value), author_); }

String Summary::classicToX(const CProps& prop, StringView temp, StringView,
                           const Skin& toSkin, lib::Array<String>& copyFail) {
    if (!legacy_) throw SummaryException("classicToX called for a WSN scenario");
    copyFail.clear();
    const auto source = fsPath(scenario_path_);
    const auto target = fsPath(temp);
    // Dは変換結果を入力元自身へ結合し得る。C++ではこの破壊
    // 的な同一先を、コピーや変更済み参照の公開前に拒否する。
    const auto relative = canonicalWithMissingTail(target)
        .lexically_relative(canonicalWithMissingTail(source));
    if (relative == "." || (!relative.empty() && *relative.begin() != ".."))
        throw SummaryException("The conversion destination must be outside the source scenario");
    makeDirectory(target, storage_);
    String materialRelative = toSkin.materialPath();
    if (!materialRelative.empty() && storageExists(source / fsPath(materialRelative), storage_))
        materialRelative.clear(); // Dは余分なMaterial/Materialの階層を作らない。
    const auto material = target / fsPath(materialRelative);
    makeDirectory(material, storage_);
    std::error_code error;
    for (std::filesystem::directory_iterator it(source, error), end; it != end && !error; it.increment(error)) {
        const auto file = utf8Path(it->path());
        if (equalName(utf8Path(it->path().filename()), "cwxeditor.lock")) continue;
        const bool directory = isDirectory(it->path());
        if (!directory && isSystemFile(file)) continue;
        const bool asset = !directory && (toSkin.isCardImage(file, true, false)
            || toSkin.isBgImage(file) || toSkin.isBGM(file) || toSkin.isSE(file));
        const auto parent = directory || (asset && !msgutils::isSPFontFile(utf8Path(it->path().filename())))
            ? material : target;
        copyTree(it->path(), parent / it->path().filename(), copyFail);
    }
    if (error) copyFail.push_back(scenario_path_);
    // 「名前を付けて保存」は途中までの素材コピーを拒否する。その呼び出
    // し側が失敗を報告するまで、現在のモデルと参照の同一性を維持する。
    if (!copyFail.empty()) return utf8Path(target);

    std::map<lib::Bytes, lib::Array<String>> existingImages;
    for (const auto& entry : std::filesystem::directory_iterator(material)) {
        if (!entry.is_regular_file() || !toSkin.isCardImage(utf8Path(entry.path()), true, false)) continue;
        existingImages[utils::readBinary(entry.path())].push_back(
            utf8Path(fsPath(materialRelative) / entry.path().filename()));
    }
    const auto counter = use_counter_;
    for (auto& [bytes, files] : existingImages) {
        // DのcardImgTableはこれらの優先順位を適用しようとするが、
        // 値のコピーを変更している。仕様にあるフォント以外・使用数による優先
        // を、C++が所有する値へ適用し、同順位なら安定したパス順で決める。
        std::sort(files.begin(), files.end(), [&](const String& a, const String& b) {
            const bool af = msgutils::isSPFontFile(utf8Path(fsPath(a).filename()));
            const bool bf = msgutils::isSPFontFile(utf8Path(fsPath(b).filename()));
            if (af != bf) return !af;
            // 実ファイルの候補は入力元ルートからコピー済みだが、
            // カウンターのIDは後の名前変更まで元の名前のまま。
            // 変換で新しいMaterialディレクトリを導入する
            // 場合も、上記の意図的な優先順位の改善を維持する。
            const auto ac = counter ? counter->get(usecounter::toPathId(
                utf8Path(fsPath(a).filename()))) : 0;
            const auto bc = counter ? counter->get(usecounter::toPathId(
                utf8Path(fsPath(b).filename()))) : 0;
            return ac != bc ? ac > bc : a < b;
        });
    }
    struct Replacement { lib::Owner<usecounter::PathUser> user; String path; };
    lib::Array<Replacement> embedded;
    FileSync exportSync(storage_);
    exportSync.start();
    if (counter) {
        auto keys = counter->keys<usecounter::PathId>();
        std::sort(keys.begin(), keys.end());
        for (const auto& key : keys) {
            if (!key.isBinData()) continue;
            auto users = counter->values(key);
            std::sort(users.begin(), users.end(), [](const auto& a, const auto& b) {
                return path::cpcmp(path::cpsplit(a->cwxPath(true)), path::cpsplit(b->cwxPath(true))) < 0;
            });
            for (const auto& user : users) {
                const auto payload = user->path();
                const bool image = utils::isBinImg(payload);
                if (!image && !utils::isBinSnd(payload)) continue;
                const auto bytes = image ? utils::strToBImg(payload) : utils::strToBSnd(payload);
                auto found = existingImages.find(bytes);
                String path;
                if (found != existingImages.end() && !found->second.empty()) path = found->second.front();
                else {
                    const auto extension = image ? imagesize::imageType(bytes) : utils::soundType(bytes);
                    const auto name = pathUserToExportedImageName(prop, scenario_name_, author_, *user);
                    const auto file = utils::createFileI(material, name, extension, "", false);
                    filesync::writeFile(file, bytes, syncOwner(exportSync));
                    // 次の名前の選択や参照の公開前に同期する。ネイティ
                    // ブとStorageの書き込みは同じ経路を使う。
                    exportSync.sync();
                    path = utf8Path(fsPath(materialRelative) / file.filename());
                    existingImages[bytes].push_back(path);
                }
                embedded.push_back({user, std::move(path)});
            }
        }
        // エフェクトブースター内部のパスは自身のファイルからの相対パス。シ
        // ナリオのパスとして名前変更せず、コピーしたツリーから再構築する。
        jpy_data_.clear(); jpdc_data_.clear();
        if (!materialRelative.empty()) {
            for (const auto& key : counter->keys<usecounter::PathId>()) {
                if (key.isBinData() || key.id.empty()) continue;
                const auto original = static_cast<String>(key);
                // Dが除外するのはルート相対のfont_?.bmpだけ。下位のフォントファイル
                // はディレクトリごとMaterialへコピーされているため、それも移動する。
                if (fsPath(original).parent_path().empty() &&
                    msgutils::isSPFontFile(original)) continue;
                if (!storageExists(source / fsPath(original), storage_)) continue;
                counter->change(key, usecounter::toPathId(utf8Path(fsPath(materialRelative) / fsPath(original))));
            }
        }
        for (const auto& replacement : embedded) replacement.user->path(replacement.path);
    }
    scenarioPath(utf8Path(target));
    temp_path_ = scenario_path_;
    use_temp_ = true;
    legacy_ = false;
    to_x_ = true;
    load_scaled_image_ = true;
    if (counter) for (const auto& key : counter->keys<usecounter::PathId>()) {
        if (!key.isBinData() && !key.id.empty() && !utils::scaledImageInfo(fsPath(static_cast<String>(key))).path.empty())
            load_scaled_image_ = false;
    }
    updateJpy1List(prop);
    activateTempLifetime();
    return utf8Path(target);
}

bool Summary::isSaved() const noexcept { return (!use_temp_ || !zip_name_.empty()) && read_only_path_.empty(); }

void Summary::saveOverwrite(const CProps& prop, const Skin& skin, const SaveOption& option,
                             FileSync& sync) {
    // 旧形式とXMLシナリオの両方の、公開上書き保存経路を扱う。
    perf::TraceScope trace(__FILE__, __LINE__);
    SaveOperationScope saving(*this);
    if (!isSaved()) throw SummaryException("Scenario has no save destination");
    SaveOption current = option;
    current.savedCallback = {};
    // Dのscope(exit)コールバックは一部の失敗時にも実行する
    // 。既存のC++のトランザクションのコールバックは公開成功を報告
    // する契約なので、それを維持し、保存全体の確定後に一度だけ呼ぶ。
    if (current.autoUpdateJpy1File) updateJpy1Files(prop, true, sync);
    if (legacy_) {
        cwl::saveLScenario(summaryOwner(*this), scenario_path_, skin, prop,
                           current, &sync);
        option.runIo([&sync] { sync.sync(); });
        if (!zip_name_.empty()) {
            if (hasExtension(zip_name_, ".cab")) createCab(zip_name_, {}, option);
            else createZip(zip_name_, {}, true, sync, option);
        }
        option.runIo([&sync] { sync.sync(); });
        // DのsaveProcがスキン情報を消すのは、シナリ
        // オをXMLへ戻す変換ではない旧形式保存の後だけ。
        type_.clear();
        skin_name_.clear();
        refCheckPaths();
        updateJpy1List(prop);
        if (!option.deferChangedReset) resetChanged();
        if (option.savedCallback) option.savedCallback();
        return;
    }
    const auto previousXmls = old_xmls_;
    const auto previousExpanded = expand_xmls_;
    try {
        if (use_temp_ && !expand_xmls_ && !hasExtension(zip_name_, ".cab")) {
            old_xmls_ = toXMLs(prop.sys(), current);
        } else {
            saveXMLsImpl(scenario_path_, prop.sys(), current, sync);
            if (hasExtension(zip_name_, ".cab")) { expand_xmls_ = true; old_xmls_.clear(); }
        }
    if (!zip_name_.empty()) {
        if (hasExtension(zip_name_, ".cab")) createCab(zip_name_, {}, option);
        else createZip(zip_name_, {}, false, sync, option);
    }
        option.runIo([&sync] { sync.sync(); });
    } catch (...) {
        old_xmls_ = previousXmls;
        expand_xmls_ = previousExpanded;
        throw;
    }
    refCheckPaths();
    updateJpy1List(prop);
    if (!option.deferChangedReset) resetChanged();
    if (option.savedCallback) option.savedCallback();
}

Summary::SaveAsCheckpoint Summary::checkpointSaveAs() const {
    SaveAsCheckpoint checkpoint{scenario_path_, temp_path_, zip_name_, orig_zip_name_, use_temp_,
            legacy_, expand_xmls_, to_x_,
            temp_lifetime_ && temp_lifetime_->lock.open(), temp_lifetime_,
            old_xmls_, read_only_path_, type_, skin_name_, data_version_,
            {}, jpy_data_, jpdc_data_};
    for (const auto& value : jpy_data_) {
        auto& states = checkpoint.jpySectionStates.emplace_back();
        for (const auto& section : value.sections)
            states.push_back(section
                ? SaveAsCheckpoint::JpySectionState{section->filename, section->needUpdate}
                : SaveAsCheckpoint::JpySectionState{});
    }
    return checkpoint;
}

void Summary::rollbackSaveAs(const SaveAsCheckpoint& checkpoint) {
    // saveWithNameは、ポータルやプロバイダーが候補を
    // 公開する前に、使用中のシナリオを候補ディレクトリへ切り替え
    // てロックを置換する場合がある。ここでは同一性と寿命の状態
    // だけを戻す。候補ファイルは診断・復旧用に保持し、未確定の
    // 「名前を付けて保存」で以前のシナリオツリーを削除しない。
    const auto previousLifetime =
        std::static_pointer_cast<TempLifetime>(checkpoint.tempLifetime);
    if (temp_lifetime_ && temp_lifetime_ != previousLifetime) {
        temp_lifetime_->retained = true;
        temp_lifetime_->lock.close();
    }
    scenario_path_ = checkpoint.scenarioPath;
    temp_path_ = checkpoint.tempPath;
    zip_name_ = checkpoint.zipName;
    orig_zip_name_ = checkpoint.originalZipName;
    use_temp_ = checkpoint.useTemp;
    legacy_ = checkpoint.legacy;
    expand_xmls_ = checkpoint.expandXmls;
    to_x_ = checkpoint.convertingToWsn;
    temp_lifetime_ = previousLifetime;
    old_xmls_ = checkpoint.retainedXmls;
    read_only_path_ = checkpoint.readOnlyPath;
    type_ = checkpoint.type;
    skin_name_ = checkpoint.skinName;
    data_version_ = checkpoint.dataVersion;
    // 実際の編集済みブースター所有元を保持する。復元したツリーを再
    // 解析すると保留中の参照編集が失われる。先に候補の登録を外す。
    for (auto& value : jpy_data_) value.removeUseCounter();
    for (auto& value : jpdc_data_) if (value) value->removeUseCounter();
    jpy_data_ = checkpoint.jpyData;
    jpdc_data_ = checkpoint.jpdcData;
    for (std::size_t i = 0; i < jpy_data_.size(); ++i) {
        auto& value = jpy_data_[i];
        // 自動更新が共有区画のフラグを消費しても、保存した復元用状態は古い
        // 値所有の行配列を保持している場合がある。編集された変更だけを再設
        // 定する。未変更の区画を書き直すと、元の引用符や空白が失われる。
        for (std::size_t j = 0; j < value.sections.size(); ++j) {
            auto& section = value.sections[j];
            if (!section || i >= checkpoint.jpySectionStates.size() ||
                j >= checkpoint.jpySectionStates[i].size()) continue;
            const auto& state = checkpoint.jpySectionStates[i][j];
            section->needUpdate = section->needUpdate || state.needUpdate ||
                                  section->filename != state.filename;
        }
        value.setUseCounter(use_counter_);
    }
    for (auto& value : jpdc_data_) if (value) value->setUseCounter(use_counter_);
    if (!temp_lifetime_ && checkpoint.lockOpen && !temp_path_.empty())
        activateTempLifetime();
}

void Summary::commitSaveAs(const SaveAsCheckpoint& checkpoint) noexcept {
    if (!checkpoint.useTemp || checkpoint.tempPath.empty()) return;
    const auto previous = fsPath(checkpoint.tempPath).lexically_normal();
    const auto current = fsPath(temp_path_).lexically_normal();
    const auto liveScenario = fsPath(scenario_path_).lexically_normal();
    const auto overlaps_previous = [&previous](const Path& value) {
        if (value.empty()) return false;
        std::error_code error;
        const auto old_root = std::filesystem::weakly_canonical(previous, error);
        if (error) return true;  // 後処理の対象が曖昧な場合は、破壊的な処理を行わない。
        const auto candidate = std::filesystem::weakly_canonical(value, error);
        if (error) return true;
        const auto relative = candidate.lexically_relative(old_root);
        return relative == "." ||
               (!relative.empty() && !relative.has_root_name() &&
                !relative.has_root_directory() && *relative.begin() != "..");
    };
    auto previousLifetime =
        std::static_pointer_cast<TempLifetime>(checkpoint.tempLifetime);
    if (overlaps_previous(current) || overlaps_previous(liveScenario)) {
        if (previousLifetime) previousLifetime->retained = true;
        return;
    }
    // 出力先はプラットフォームのプロバイダーが公開済みなので、
    // 以前の展開シナリオを破棄してよい。ただし後処理の失敗によ
    // って、確定済みの「名前を付けて保存」を失敗扱いにしない。
    try {
        if (previousLifetime) previousLifetime->cleanup();
        else removeTree(previous, storage_);
    } catch (...) {
    }
}

void Summary::saveWithName(const CProps& prop, const Skin& skin,
                           const SaveOption& option, StringView fname,
                           StringView tempPath, bool defExpandXMLs,
    const Skin* defSkin, lib::Callback<void(String)> showWarn,
    bool classic, FileSync& sync) {
    // 「名前を付けて保存」と、そのWSN/ZIP/CAB/Classic変換を一つの操作として扱う。
    perf::TraceScope trace(__FILE__, __LINE__);
    SaveOperationScope saving(*this);
    const auto output = fsPath(fname);
    const bool modernDirectoryOutput = !classic &&
        (isDirectory(output) || equalName(utf8Path(output.filename()), "Summary.xml"));
    bool convertedFromClassic{};
    const Skin& conversionSkin = defSkin ? *defSkin : skin;
    SaveOption operation = option;
    operation.savedCallback = {};
    // 上書きと同様に、途中処理の失敗時にも通知するDのscope(exit)
    // ではなく、C++の公開成功時のコールバック契約を維持する。
    if (operation.autoUpdateJpy1File) updateJpy1Files(prop, true, sync);
    // 変換や素材コピーの前に、入力元のブースターの書き込みを完了する。未
    // 完了だとアトミック置換と列挙が競合し、古いバイト列をコピーし得る。
    option.runIo([&sync] { sync.sync(); });
    const auto finish = [&] {
        read_only_path_.clear();
        to_x_ = false;
        refCheckPaths();
        updateJpy1List(prop);
        if (!option.deferChangedReset) resetChanged();
        if (option.savedCallback) option.savedCallback();
    };
    const auto clearLegacySkinMetadata = [this] {
        // DのsaveProcはlegacy && !legacyToXの場合だけこれを行
        // う。旧形式からXMLへの変換では、Summary.xmlで使うメタデータを残す。
        type_.clear();
        skin_name_.clear();
    };
    const auto releaseTemporaryForPermanentOutput = [&](const Path& outputPath) {
        // Dは最終的な「名前を付けて保存」の出力先に一時ツリーの所有元
        // を付けない。C++の変換は一時ツリーにRAIIを使うため、公
        // 開に成功した恒久的な出力先は、共有所有元を解放する前に管理を
        // 移す。保存したばかりのディレクトリを削除しないため。別の古い
        // 一時ツリーは、通常の終了・最後の所有元による後処理に従う。
        if (temp_lifetime_ &&
            canonicalWithMissingTail(fsPath(temp_lifetime_->path)) ==
                canonicalWithMissingTail(outputPath)) {
            temp_lifetime_->retained = true;
            temp_lifetime_->lock.close();
        }
        temp_lifetime_.reset();
    };
    const auto reportCopyFailure = [&](const lib::Array<String>& failures) {
        if (failures.empty()) return;
        for (const auto& failure : failures)
            if (showWarn) showWarn(utils::tryFormat(String(prop.requiredMsgs().fileCopyError), failure));
        // Dは素材欠落でも継続する。C++は不完全なコピーでユー
        // ザーのシナリオを置き換えないよう、既存の公開前に失敗さ
        // せる規則を維持する。先に個々の具体的な警告を保持する。
        throw SummaryException("Unable to copy scenario material: " + failures.front());
    };
    if (!classic && legacy_) {
        const auto converted = modernDirectoryOutput
            ? utf8Path(isDirectory(output) ? output : output.parent_path())
            : createTempDir(tempPath, utf8Path(output.stem()), true, storage_);
        lib::Array<String> copyFailures;
        if (type_.empty()) type_ = conversionSkin.type();
        if (skin_name_.empty()) skin_name_ = conversionSkin.name();
        (void)classicToX(prop, converted, tempPath, conversionSkin, copyFailures);
        reportCopyFailure(copyFailures);
        convertedFromClassic = true;
    }
    if (classic) {
        const auto extension = lower(utf8Path(output.extension()));
        const bool archiveOutput = !isDirectory(output) &&
            (extension == ".cab" || extension == ".zip" || extension == ".wsn");
        if (archiveOutput) {
            const auto directory = createTempDir(
                tempPath, utf8Path(output.stem()), true, storage_);
            ClassicTemporaryOutput temporaryOutput{fsPath(directory), storage_, &sync};
            copyClassicMaterials(*this, fsPath(directory), storage_, operation, showWarn);
            SaveOption classicOption = operation;
            classicOption.saveChangedOnly = false;
            if (option.beforeSerialization) option.beforeSerialization();
            cwl::saveLScenario(summaryOwner(*this), directory, skin, prop,
                               classicOption, &sync);
            archiveSavedDirectory(directory, output, option, sync);
            scenario_path_ = directory;
            temp_path_ = directory;
            use_temp_ = true;
            zip_name_ = String(fname);
            orig_zip_name_ = zip_name_;
            legacy_ = true;
            activateTempLifetime();
            temporaryOutput.transferred = true;
            option.runIo([&sync] { sync.sync(); });
            clearLegacySkinMetadata();
            finish();
            return;
        }
        const auto directory = isDirectory(output) ? output :
            ((equalName(utf8Path(output.extension()), ".wsm") ||
              equalName(utf8Path(output.extension()), ".wsn"))
                 ? output.parent_path() : output);
        copyClassicMaterials(*this, directory, storage_, operation, showWarn);
        if (option.beforeSerialization) option.beforeSerialization();
        cwl::saveLScenario(summaryOwner(*this), utf8Path(directory), skin,
                           prop, operation, &sync);
        option.runIo([&sync] { sync.sync(); });
        releaseTemporaryForPermanentOutput(directory);
        scenario_path_ = utf8Path(directory);
        use_temp_ = false;
        temp_path_.clear();
        zip_name_.clear();
        orig_zip_name_.clear();
        legacy_ = true;
        clearLegacySkinMetadata();
        finish();
        return;
    }
    SaveOption current = operation;
    current.saveChangedOnly = false;
    if (isDirectory(output) || equalName(utf8Path(output.filename()), "Summary.xml")) {
        const auto directory = isDirectory(output) ? output : output.parent_path();
        const auto source = fsPath(scenario_path_).lexically_normal();
        const auto target = directory.lexically_normal();
        const auto canonical_source = canonicalWithMissingTail(source);
        const auto canonical_target = canonicalWithMissingTail(target);
        const auto target_relative =
            canonical_target.lexically_relative(canonical_source);
        if (target_relative != "." && !target_relative.empty() &&
            !target_relative.has_root_name() &&
            !target_relative.has_root_directory() &&
            *target_relative.begin() != "..") {
            throw SummaryException(
                "The save destination must not be inside the source scenario: " +
                utf8Path(target));
        }
        if (canonical_source != canonical_target) {
            lib::Array<String> copyFailures;
            option.runIo([source, target, &copyFailures] {
                copyTree(source, target, copyFailures);
            });
            reportCopyFailure(copyFailures);
        }
        if (option.beforeSerialization) option.beforeSerialization();
        saveXMLsImpl(utf8Path(directory), prop.sys(), current, sync);
        releaseTemporaryForPermanentOutput(directory);
        scenario_path_ = utf8Path(directory);
        use_temp_ = false; temp_path_.clear(); zip_name_.clear(); orig_zip_name_.clear();
        expand_xmls_ = true;
        old_xmls_.clear();
    } else {
        const auto directory = convertedFromClassic
            ? scenario_path_
            : createTempDir(tempPath, utf8Path(output.stem()), true, storage_);
        if (!convertedFromClassic) {
            lib::Array<String> copyFailures;
            option.runIo([source = fsPath(scenario_path_), directory, &copyFailures] {
                copyTree(source, fsPath(directory), copyFailures);
            });
            reportCopyFailure(copyFailures);
        }
        scenario_path_ = directory;
        const bool cabinet = equalName(utf8Path(output.extension()), ".cab");
        // 既存のアーカイブ形式の設定を維持する。DのsaveProcと同
        // 様に、既定値はClassicデータからの変換時だけ適用する。
        expand_xmls_ = cabinet || (convertedFromClassic ? defExpandXMLs : expand_xmls_);
        use_temp_ = true;
        if (option.beforeSerialization) option.beforeSerialization();
        if (expand_xmls_) {
            old_xmls_.clear();
            saveXMLsImpl(scenario_path_, prop.sys(), current, sync);
        } else {
            old_xmls_ = toXMLs(prop.sys(), current);
        }
        if (cabinet) {
            createCab(fname, {}, option);
        } else {
            createZip(fname, {}, false, sync, option);
        }
        option.runIo([&sync] { sync.sync(); });
        // プラットフォームの保存プロバイダーが候補を公開するまで、以前の有
        // 効な一時シナリオを破棄しない。最終処理が失敗した場合は、MainWindow
        // のSaveAsの復元用状態から以前のツリーへ戻す。
        use_temp_ = true; zip_name_ = String(fname); orig_zip_name_ = zip_name_; temp_path_ = directory;
        activateTempLifetime();
    }
    finish();
}

void Summary::exportClassicScenario(const CProps& prop, const Skin& skin,
                                     const SaveOption& option, StringView fname,
                                      StringView tempPath, lib::Callback<void(String)> showWarn,
                                      FileSync& sync) {
    // UI上で出力と「名前を付けて保存」は別操作なので、同じClassic
    // 直列化処理へ委ねても、公開の追跡フレームは別にする。
    perf::TraceScope trace(__FILE__, __LINE__);
    SaveOperationScope saving(*this);
    const auto output = fsPath(fname);
    const auto extension = lower(utf8Path(output.extension()));
    const bool archiveOutput = !isDirectory(output) &&
        (extension == ".cab" || extension == ".zip" || extension == ".wsn");
    SaveOption current = option;
    current.saveChangedOnly = false;
    if (archiveOutput) {
        const auto temp = Summary::createTempDir(
            tempPath, utf8Path(output.stem()), true, storage_);
        try {
            copyClassicMaterials(*this, fsPath(temp), storage_, current, showWarn);
            if (current.beforeSerialization) current.beforeSerialization();
            cwl::saveLScenario(summaryOwner(*this), temp, skin, prop,
                                current, &sync);
            archiveSavedDirectory(temp, output, option, sync);
            option.runIo([temp, storage = storage_] { removeTree(fsPath(temp), storage); });
        } catch (...) {
            // 直列化が失敗しても、このツリーへの書き込みがキューに残る場合がある
            // 。UIの待機自体が失敗しても、破棄前に残りの書き込みを完了する。
            try { sync.sync(); } catch (...) {}
            try {
                removeTree(fsPath(temp), storage_);
            } catch (...) {
            }
            throw;
        }
    } else {
        const auto directory = isDirectory(output) ? output :
            (equalName(utf8Path(output.extension()), ".wsm")
                ? output.parent_path() : output);
        copyClassicMaterials(*this, directory, storage_, current, showWarn);
        if (current.beforeSerialization) current.beforeSerialization();
        cwl::saveLScenario(summaryOwner(*this), utf8Path(directory), skin,
                           prop, current, &sync);
    }
    option.runIo([&sync] { sync.sync(); });
    if (showWarn) showWarn({});
    if (current.savedCallback) current.savedCallback();
}

archive::ZipArchive Summary::createZipData(const lib::Array<String>& ignorePaths,
                                           bool useSysEnc, bool isWsn,
                                           lib::Array<lib::Bytes>& data,
                                           FileSync& sync) {
    sync.sync();
    const auto root = fsPath(scenario_path_);
    const auto xml = use_temp_ && !expand_xmls_ ? old_xmls_ : XmlFileTree{};
    const auto ignored = [&](String value) {
        // Dのメモリ形式は保持済みXMLを一度だけ出力する。C++ではディレクトリ
        // からの「名前を付けて保存」でコピーした実XMLも残り得るため、保持したモ
        // デルのバイト列で正確に対応する項目だけを置き換え、他のXMLは維持する。
        return ignoredZipEntry(root, ignorePaths, xml, value);
    };
    auto result = archive::zip(root, !isWsn, ignored, useSysEnc, data);
    if (!xml.empty()) {
        for (const auto& [directory, files] : xml) for (const auto& [name, text] : files) {
            auto member = archive::archive(retainedXmlMemberName(root, isWsn, directory, name),
                                           lib::Bytes(text.begin(), text.end()), false, useSysEnc);
            result->addMember(std::move(member));
        }
    }
    return std::move(*result);
}

void Summary::createZip(StringView zipName, const lib::Array<String>& ignorePaths,
                        bool useSysEnc, FileSync& sync, const SaveOption& option) {
    // MainWindowから直接呼ぶ出力の入口でもある。保存
    // や「名前を付けて保存」がここへ委ねる場合もあるが、アーカ
    // イブ構築を単独で呼んだときにも有用な呼び出し境界を残す。
    perf::TraceScope trace(__FILE__, __LINE__);
    option.runIo([&sync] { sync.sync(); });
    lib::Bytes bytes;
    // ファイルの識別情報と未展開のXMLバイト列だけを取得する。表示側の描画中に
    // 、アーカイブのワーカーが使用中のSummaryへアクセスしないようにする。
    option.runArchive([root = fsPath(scenario_path_), ignorePaths, useSysEnc,
                       isWsn = hasExtension(zipName, ".wsn"), &bytes,
                       xml = use_temp_ && !expand_xmls_ ? old_xmls_ : XmlFileTree{}] {
        const auto ignored = [&](String value) {
            // createZipDataと、保持済み・実ファイル項目の扱いを共有する。
            // 通常のアーカイブ入力と管理対象外のXMLは実ファイル項目のままにする。
            return ignoredZipEntry(root, ignorePaths, xml, value);
        };
        lib::Array<lib::Bytes> data;
        auto archiveFile = archive::zip(root, !isWsn, ignored, useSysEnc, data);
        for (const auto& [directory, files] : xml) for (const auto& [name, text] : files) {
            archiveFile->addMember(archive::archive(
                retainedXmlMemberName(root, isWsn, directory, name),
                lib::Bytes(text.begin(), text.end()), false, useSysEnc));
        }
        bytes = archiveFile->build();
    });
    filesync::writeFile(fsPath(zipName), bytes, syncOwner(sync));
    option.runIo([&sync] { sync.sync(); });
}

void Summary::createCab(StringView cabName, const lib::Array<String>& ignorePaths,
                        const SaveOption& option) {
    // createZip()と同様に、CAB出力の公開境
    // 界を一つ設け、ファイルシステムの巡回は追跡しない。
    perf::TraceScope trace(__FILE__, __LINE__);
    option.runArchive([root = scenario_path_, name = String(cabName), ignorePaths,
                       xml = use_temp_ && !expand_xmls_ ? old_xmls_ : XmlFileTree{}] {
        lib::Array<cab::MemoryFile> retained;
        for (const auto& [directory, files] : xml) for (const auto& [file, text] : files) {
            retained.push_back({utf8Path(fsPath(directory) / fsPath(file)),
                                lib::Bytes(text.begin(), text.end())});
        }
        // 意図的な安全性の拡張。Dはシナリオツリー内に一時XMLを書
        // いて削除するが、ここでは保持したバイト列を重ね合わせ、ツ
        // リーを変更しない。アーカイブの構築・公開の失敗時も同様。
        if (!cab::cab(root, name, [&](String value) {
            return !equalName(utf8Path(fsPath(value).filename()), "cwxeditor.lock") &&
                   !ignoredPath(ignorePaths, value);
        }, retained)) throw SummaryException("Unable to create CAB: " + name);
    });
}

lib::Array<String> Summary::notUsedFiles(const Skin& skin,
                                          const lib::Array<String>& ignorePaths,
                                          bool logicalSort) const {
    lib::Array<String> result;
    const auto root = fsPath(scenario_path_);
    std::error_code error;
    std::function<int(const Path&)> visit = [&](const Path& current) -> int {
        const auto name = utf8Path(current);
        const bool directory = isDirectory(current);
        if (isSystemFile(name, directory) || ignoredPath(ignorePaths, name)) return 1;
        if (directory) {
            int used = 0;
            std::vector<Path> children;
            for (const auto& entry : std::filesystem::directory_iterator(current, error)) {
                if (!error) children.push_back(entry.path());
            }
            std::sort(children.begin(), children.end(), [&](const Path& a, const Path& b) {
                const auto left = utf8Path(a.filename());
                const auto right = utf8Path(b.filename());
                return (logicalSort ? utils::fnncmp(left, right)
                                    : utils::fncmp(left, right)) < 0;
            });
            for (const auto& child : children) used += visit(child);
            const auto relative = relativeKey(root, current);
            if (relative.empty() || equalName(relative, skin.materialPath())) ++used;
            if (used == 0 && !relative.empty()) result.push_back(relative);
            return used;
        }
        const auto relative = relativeKey(root, current);
        if (!skin.isMaterial(utf8Path(current))) return 1;
        auto pathId = usecounter::toPathId(relative);
        if (!legacy_ && load_scaled_image_) {
            const auto unscaled = utils::noScaledPath(fsPath(relative));
            if (!unscaled.empty()) pathId = usecounter::toPathId(utf8Path(unscaled));
        }
        if (!use_counter_ || use_counter_->get(pathId) == 0) {
            result.push_back(relative);
            return 0;
        }
        return 1;
    };
    if (isDirectory(root)) (void)visit(root);
    // Dは並べ替えた深さ優先の巡回結果を返し、未使用ディレクトリはその
    // 子の後に置く。最後に全体を並べ替えると、その公開順序が変わる。
    return result;
}

bool Summary::hasMaterial(StringView value, const lib::Array<String>& ignorePaths) const {
    if (value.empty() || ignoredPath(ignorePaths, value)) return false;
    const auto path = fsPath(scenario_path_) / fsPath(value);
    return !isSystemFile(utf8Path(path)) && isRegularFile(path);
}

lib::Array<String> Summary::allMaterials(const Skin& skin,
                                         const lib::Array<String>& ignorePaths,
                                         bool logicalSort, bool scenarioOnly) const {
    lib::Array<String> result;
    const auto root = utils::nabs(StringView(scenario_path_));
    std::unordered_set<usecounter::PathId> known;
    const auto find = [&](auto&& self, const String& path) -> void {
        if (!utils::pathExists(fsPath(path))) return;
        if (isSystemFile(path) || utils::containsPath(ignorePaths, utils::pathBaseName(path))) return;
        if (utils::pathIsDirectory(fsPath(path))) {
            auto names = utils::clistdir(fsPath(path));
            if (logicalSort) {
                std::sort(names.begin(), names.end(), [](const String& first, const String& second) {
                    return utils::fnncmp(first, second) < 0;
                });
            } else {
                std::sort(names.begin(), names.end(), [](const String& first, const String& second) {
                    return utils::fncmp(first, second) < 0;
                });
            }
            for (const auto& name : names) self(self, utils::buildPath(path, name));
        } else if (skin.isMaterial(path)) {
            const auto relative = utils::abs2rel(StringView(path), StringView(root));
            result.push_back(utils::encodePath(relative));
            known.insert(usecounter::toPathId(relative));
        }
    };
    find(find, root);
    // Dのカテゴリ順と重複を維持する。集合は、最後に追加する参照
    // 素材の部分から、すでに列挙済みの対象を除くためだけに使う。
    if (!scenarioOnly) {
        const auto appendSkin = [&](const auto& paths) {
            for (const auto& path : paths) {
                known.insert(usecounter::toPathId(path));
                result.push_back(utils::encodePath(path));
            }
        };
        appendSkin(skin.tables(logicalSort));
        appendSkin(skin.musics(logicalSort));
        appendSkin(skin.sounds(logicalSort));
        for (const auto& key : requiredScenarioObject(use_counter_).keys<usecounter::PathId>()) {
            const auto path = static_cast<String>(key);
            if (path.empty()) continue;
            if (!key.isBinData() && !known.contains(key)) result.push_back(utils::encodePath(path));
        }
    }
    return result;
}

void Summary::updateJpy1List(const CProps& prop, const LoadOption* option) {
    if (!utils::pathExists(fsPath(scenario_path_))) return;
    const auto retainedSelf = retainForCallback();
    for (auto& value : jpy_data_) value.removeUseCounter();
    jpy_data_.clear();
    for (auto& value : jpdc_data_) requiredScenarioObject(value).removeUseCounter();
    jpdc_data_.clear();
    const auto scenario = scenario_path_;
    for (utils::DepthDirectory entries{fsPath(scenario)}; !entries.empty(); entries.popFront()) {
        const auto file = entries.front().name();
        // シナリオ読み込みの走査で、中止と進捗通知に対応する。 読み込
        // み以外の既存の呼び出し元は、トークンなしで同じ処理を使う。
        if (option) option->reportProgress("特殊画像ファイルの探索", file);
        try {
            if (utils::cfnmatch(utils::pathExtension(file), ".jpy1")) {
                jpy_data_.push_back(Jpy1::load(prop, scenario, file));
                jpy_data_.back().setUseCounter(useCounter());
            } else if (utils::cfnmatch(utils::pathExtension(file), ".jpdc")) {
                jpdc_data_.push_back(Jpdc::load(prop, scenario, file));
                requiredScenarioObject(jpdc_data_.back()).setUseCounter(useCounter());
            }
        } catch (const jpy::EffectBoosterError& error) {
            utils::clearStackTrace();
#if !defined(NDEBUG)
            for (const auto& detail : error.errors())
                utils::cdebugln(utils::tryFormat(String(prop.requiredMsgs().jpyError), detail.msg, file, detail.line));
#endif
        } catch (const std::bad_alloc&) {
            // Dのメモリ確保のErrorはcatch(Exception)の対象外。
            throw;
        } catch (const std::exception& error) {
            (void)utils::printStackTrace();
            utils::debugln(file);
            utils::debugln(error.what());
        }
    }
}

bool Summary::updateJpy1Files(const CProps& prop, bool rewrite, FileSync& sync) {
    bool result = false;
    for (auto& value : jpy_data_) result = value.updateJpy1File(prop, rewrite, syncOwner(sync)) || result;
    for (auto& value : jpdc_data_) if (value) result = value->updateJpdcFile(prop, rewrite, syncOwner(sync)) || result;
    return result;
}

void Summary::renameFile(StringView from, StringView to) {
    for (auto& value : jpy_data_) (void)value.renameFile(from, to);
    for (auto& value : jpdc_data_) if (value) (void)value->renameFile(from, to);
}

FileLoadException::FileLoadException(String path, std::exception_ptr error)
    : std::runtime_error(fileLoadErrorMessage(error)), path_(std::move(path)), error_(std::move(error)) {}

FileLoadException::FileLoadException(String path, const std::exception& error)
    : FileLoadException(std::move(path), retainedFileLoadError(error)) {}

bool isScenarioSystemDir(StringView dir) {
    return utils::cfnmatch(dir, PATH_AREA) || utils::cfnmatch(dir, PATH_PACKAGE) ||
           utils::cfnmatch(dir, PATH_BATTLE) || utils::cfnmatch(dir, PATH_CAST) ||
           utils::cfnmatch(dir, PATH_SKILL) || utils::cfnmatch(dir, PATH_ITEM) ||
           utils::cfnmatch(dir, PATH_BEAST) || utils::cfnmatch(dir, PATH_INFO);
}

void decScenarioPath(String& scenarioPath, lib::Array<String>& openPaths,
                     bool eventPriority) {
    const auto path = fsPath(scenarioPath);
    const auto extension = lower(utf8Path(path.extension()));
    if (extension != ".wid" && extension != ".wex") return;
    if (extension == ".wid") {
        std::uint64_t id = 0;
        const auto type = cwl::getType(utf8Path(path), id);
        String category;
        switch (type) {
        case cwl::ResourceType::Area: category = "area"; break;
        case cwl::ResourceType::Battle: category = "battle"; break;
        case cwl::ResourceType::Package: category = "package"; break;
        case cwl::ResourceType::Cast: category = "castcard"; break;
        case cwl::ResourceType::Skill: category = "skillcard"; break;
        case cwl::ResourceType::Item: category = "itemcard"; break;
        case cwl::ResourceType::Beast: category = "beastcard"; break;
        case cwl::ResourceType::Info: category = "infocard"; break;
        default: break;
        }
        if (!category.empty()) {
            auto pathValue = category + ":id:" + std::to_string(id);
            if (eventPriority && (category == "area" || category == "battle"))
                pathValue = path::cpaddattr(pathValue, "eventview");
            openPaths.push_back(std::move(pathValue));
        }
    }
    scenarioPath = utf8Path(path.parent_path());
}

lib::Owner<path::CWXPath> cwxPlace(lib::Owner<path::CWXPath> value) {
    for (;;) {
        auto& current = requiredScenarioObject(value);
        const auto first = current.cwxParent().lock();
        if (!first) return value;
        // DはSummaryの前で停止する。ネイティブの寿命管理の代理も、同じパスの境界とする。
        const auto castCandidate = current.cwxParent().lock();
        if (std::dynamic_pointer_cast<Summary>(castCandidate) ||
            std::dynamic_pointer_cast<SummaryPathProxy>(castCandidate)) return value;
        value = current.cwxParent().lock();
    }
}

String toExportedImageNameWithoutCardName(const CProps& prop,
                                          StringView scenarioName,
                                          StringView author) {
    if (author.empty()) return utils::tryFormat(static_cast<String>(prop.requiredMsgs().exportedImageName), scenarioName);
    return utils::tryFormat(static_cast<String>(prop.requiredMsgs().exportedImageNameWithAuthor), scenarioName, author);
}

String toExportedImageNameWithCardName(const CProps& prop,
                                       StringView scenarioName, StringView author,
                                       StringView cardName) {
    return utils::tryFormat(static_cast<String>(prop.requiredMsgs().exportedImageNameWithCard),
                            toExportedImageNameWithoutCardName(prop, scenarioName, author), cardName);
}

String pathUserToExportedImageName(const CProps& prop, StringView scenarioName,
                                   StringView author,
                                   const usecounter::PathUser& value) {
    const auto owner = value.owner();
    if (std::dynamic_pointer_cast<const Summary>(owner)) return toExportedImageNameWithoutCardName(prop, scenarioName, author);
    if (auto menu = std::dynamic_pointer_cast<const area::MenuCard>(owner)) {
        return toExportedImageNameWithCardName(prop, scenarioName, author, menu->name());
    }
    if (auto card = std::dynamic_pointer_cast<const card::Card>(owner)) {
        if (auto effect = std::dynamic_pointer_cast<const card::EffectCard>(owner)) {
            return toExportedImageNameWithCardName(prop, effect->scenario(),
                                                   effect->author(), effect->name());
        }
        return toExportedImageNameWithCardName(prop, scenarioName, author,
                                               card->name());
    }
    if (std::dynamic_pointer_cast<const event::Content>(owner))
        return toExportedImageNameWithoutCardName(prop, scenarioName, author);
    if (auto background = std::dynamic_pointer_cast<background::BgImage>(owner)) {
        const auto parent = background->cwxParent().lock();
        if (auto area = std::dynamic_pointer_cast<const area::Area>(parent)) {
            return toExportedImageNameWithCardName(prop, scenarioName, author,
                                                   area->name());
        }
        if (std::dynamic_pointer_cast<const event::Content>(parent))
            return toExportedImageNameWithoutCardName(prop, scenarioName, author);
    }
    return "@simage";
}

XNode areasToNode(StringView parentPath, StringView cutPath,
                  lib::SharedSlice<const lib::Owner<AbstractArea>> areas,
                  const card::XMLOption& option, const Summary* summ) {
    const String parent(parentPath), cut(cutPath);
    [[maybe_unused]] const auto summary_owner = summ ? summ->weak_from_this().lock() : lib::Owner<const Summary>{};
    auto result = XNode::create("Table");
    if (summ) {
        result.newAttr("scenarioPath", utils::nabs(StringView(summ->scenarioPath())));
        result.newAttr("scenarioName", summ->scenarioName());
        result.newAttr("scenarioAuthor", summ->author());
    }
    // 記述子はDの入力領域と範囲を保持するが、各読み取りでは現在
    // の共有項目を見る。出力呼び出し中はその要素を生存させる。
    for (std::size_t index = 0; index < areas.size(); ++index) {
        const auto value = areas[index];
        requiredScenarioObject(value).toNode(result, option, parent, cut);
    }
    return result;
}

XNode areasToNode(StringView parentPath, StringView cutPath,
                  const lib::Array<lib::Owner<AbstractArea>>& areas,
                  const card::XMLOption& option, const Summary* summ) {
    const String parent(parentPath), cut(cutPath);
    // ネイティブvectorはDの配列記述子のように領域を保持できな
    // い。このアダプター入力をコピーし、共通の一つの出力実装を使う。
    auto retained = lib::SharedSlice<const lib::Owner<AbstractArea>>(areas);
    return areasToNode(parent, cut, std::move(retained), option, summ);
}

}  // namespace cwxeditor::cwx::summary

namespace cwxeditor::cwx::props::detail {

bool summaryIsTargetVersion(const summary::Summary& value,
                            const lib::StringView version) {
    return value.isTargetVersion(version);
}

bool summaryIsTargetVersionWith(const bool legacy,
                                const lib::StringView dataVersion,
                                const lib::StringView version) {
    return summary::Summary::isTargetVersionWith(legacy, dataVersion, version);
}

}  // namespace cwxeditor::cwx::props::detail
