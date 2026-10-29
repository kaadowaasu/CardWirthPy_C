#include "cw/debug/recording.hpp"
#include "cw/cw.hpp"
#include "cw/thread.hpp"
#include "cw/data.hpp"
#include "cw/advlog.hpp"
#include "cw/xmlcreater.hpp"
#include "cw/util.hpp"
#include "cw/lib/encoding.hpp"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <stdexcept>
#include <thread>
namespace cw::debug::recording {
namespace {

std::shared_ptr<::cw::data::ScenarioData> scenario_data(::cw::thread::CWPy& game)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&game.m_sdata.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<::cw::data::ScenarioData>(*object);
}

::cw::lib::Integer camp_area()
{
    const auto* value = std::get_if<::cw::lib::Integer>(&::cw::g_AREA_CAMP.storage());
    if (!value) throw std::logic_error("AREA_CAMP is not an integer");
    return *value;
}

void load_func_impl(::cw::thread::CWPy& game,
                    ::cw::lib::Integer areaid,
                    std::optional<std::shared_ptr<::cw::data::CWPyElement>> data)
{
    // cw.cwpy.change_area(areaid, False, bginhrt=True, data=data) の
    // Pythonデフォルト値を明示的に並べ、トランジション等を勝手に補わない。
    game.change_area(
        areaid, false, true, {"Default", ::cw::lib::Value("Default")},
        false, false, false, true, std::move(data),
        false, false, false, false, false);
}

::cw::lib::Callback area_change_callback(std::shared_ptr<::cw::thread::CWPy> game)
{
    return [game = std::move(game)] (const ::cw::lib::Arguments& args,
                                     const ::cw::lib::KeywordArguments&) -> ::cw::lib::Value {
        if (args.empty()) throw std::invalid_argument("recording.load: missing areaid");
        const auto* areaid = std::get_if<::cw::lib::Integer>(&args[0].storage());
        if (!areaid) throw std::invalid_argument("recording.load: areaid is not an integer");

        std::optional<std::shared_ptr<::cw::data::CWPyElement>> data;
        if (args.size() > 1 && !args[1].is_none()) {
            const auto* object = std::get_if<::cw::lib::ObjectPtr>(&args[1].storage());
            if (!object || !*object) {
                throw std::invalid_argument("recording.load: data is not a CWPyElement");
            }
            auto element = std::dynamic_pointer_cast<::cw::data::CWPyElement>(*object);
            if (!element) {
                throw std::invalid_argument("recording.load: data is not a CWPyElement");
            }
            data = std::move(element);
        }
        // コールバックを予約するのは、記録を読み込んだものと同じ CWPy イ
        // ンスタンスだけとする。 所有権を保持し、遅れて実行されるエリア変更
        // コールバックが破棄済みのゲームオブジェクトを参照しないようにする。
        load_func_impl(*game, *areaid, std::move(data));
        return {};
    };
}

bool game_thread_running(::cw::thread::CWPy& game)
{
    std::scoped_lock lock(game.m_game_exec_mutex);
    return game.m_game_thread_id != std::thread::id{};
}

bool on_game_thread(::cw::thread::CWPy& game)
{
    std::scoped_lock lock(game.m_game_exec_mutex);
    return game.m_game_thread_id == std::this_thread::get_id();
}

::cw::lib::String save_on_game_thread(::cw::thread::CWPy& game,
                                      ::cw::lib::String path)
{
    if (!game.is_playingscenario()) return {};

    // Python版の isinstance(assert) に相当。シナリオ実行中なのに別の
    // SystemData が入っている場合は、ログを捏造せず呼び出し元へ知らせる。
    const auto scenario = scenario_data(game);
    if (!scenario) throw std::logic_error("recording.save requires ScenarioData");
    if (!game.m_advlog) {
        throw std::logic_error("recording.save requires AdventurerLogger");
    }

    ::cw::xmlcreater::create_scenariolog(
        scenario, path, true, game.m_advlog->m_logfilepath);
    return path;
}

void load_on_game_thread(const std::shared_ptr<::cw::thread::CWPy>& game,
                         ::cw::lib::String path)
{
    if (!game || !game->is_playingscenario()) return;

    const auto scenario = scenario_data(*game);
    if (!scenario) throw std::logic_error("recording.load requires ScenarioData");

    // areaid は load_log() によってログ内 Property/AreaId へ更新される
    // ため、キャンプ判定とコールバック引数用の値を分けて保持する。
    const auto current_area = game->m_areaid;
    const bool in_camp = current_area == camp_area();
    auto resource_area = current_area;
    if (in_camp) {
        if (game->m_pre_areaids.empty()) {
            throw std::out_of_range("recording.load: pre_areaids is empty");
        }
        resource_area = std::get<0>(game->m_pre_areaids.back());
    }

    // Python版と同じく、対象エリアのリソースが無ければログを読まず終了。
    const auto data = scenario->get_areadata(resource_area, "", false, std::nullopt);
    if (!data || !*data) return;

    // ScenarioData が変数・背景・ログファイル・BGM状態を復元する。時刻や
    // ファイル形式の責務もこのAPIに委譲し、ここで固定値を補わない。
    const auto musicpaths = scenario->load_log(path, true);
    for (std::size_t i = 0; i < musicpaths.size() && i < ::cw::thread::CWPy::s_music.size(); ++i) {
        const auto& music = ::cw::thread::CWPy::s_music[i];
        // Py はこのチャンネルで play() を直接呼ぶ。load_log の
        // スナップショット取得後にチャンネルが消えた場合、 ネイティブ版では null
        // を参照せず論理エラーを通知し、成功したエリア遷移を予約しない。
        if (!music) throw std::logic_error("recording.load: missing music channel");
        const auto& [musicpath, subvolume, loopcount, inusecard, fullpath] = musicpaths[i];
        music->play(musicpath, true, false, inusecard, subvolume, loopcount, 0, fullpath);
    }

    const auto callback = area_change_callback(game);
    // Python版は load_log() 後の cw.cwpy.areaid を使う。ScenarioData の
    // C++実装も同じタイミングで m_areaid を更新するため、ここで再取得する。
    const auto restored_area = game->m_areaid;
    if (in_camp) {
        game->m_pre_areaids.back() = {restored_area, data};
        game->exec_func_2(callback, camp_area());
    } else {
        const ::cw::lib::Value data_value(
            std::static_pointer_cast<::cw::lib::Object>(*data));
        game->exec_func_3(callback, restored_area, data_value);
    }
}

} // namespace

::cw::lib::String save(::cw::lib::String path)
{
    // ネイティブ版の意図的な差異として、実行環境がない場合は記録中
    // のセッションなしと扱い、非プレイ時と同じ空の結果を返す。 Py
    // はここで cw.cwpy を参照して例外となるが、この任
    // 意の保存用 API では存在しない実行環境を参照しない。
    const auto game = ::cw::g_cwpy;
    if (!game) return {};
    if (!game_thread_running(*game) || on_game_thread(*game))
        return save_on_game_thread(*game, std::move(path));

    const auto result = game->sync_exec(
        [game, path = std::move(path)](const ::cw::lib::Arguments&,
                                       const ::cw::lib::KeywordArguments&) mutable {
            return ::cw::lib::Value(save_on_game_thread(*game, std::move(path)));
        });
    const auto* saved = std::get_if<::cw::lib::String>(&result.storage());
    return saved ? *saved : ::cw::lib::String{};
}

std::vector<std::uint8_t> serialize()
{
    namespace fs = std::filesystem;
    struct Cleanup {
        fs::path file, directory;
        ~Cleanup() {
            std::error_code ignored;
            if (!file.empty()) fs::remove(file, ignored);
            if (!directory.empty()) fs::remove(directory, ignored);
        }
    } cleanup;
    const auto base = fs::temp_directory_path();
    std::random_device random;
    for (int attempt = 0; attempt < 16; ++attempt) {
        cleanup.directory = base / ("cw-recording-" + std::to_string(random()) +
                                    "-" + std::to_string(random()));
        std::error_code error;
        if (fs::create_directory(cleanup.directory, error)) break;
        cleanup.directory.clear();
    }
    if (cleanup.directory.empty())
        throw std::runtime_error("状態保存の一時領域を作成できません");
    cleanup.file = cleanup.directory / "recording.wstx";
    const auto path = ::cw::lib::encoding::path_to_utf8(cleanup.file);
    if (save(path).empty()) throw std::runtime_error("状態を保存できません");
    // XML の書き込みは共通の FileSync サービスに予約される場合がある。
    // 読み戻しや、Cleanup が書き込み先を削除する処理より前に完了を待つ。
    if (::cw::g_fsync) ::cw::g_fsync->sync();
    std::ifstream input(cleanup.file, std::ios::binary);
    if (!input) throw std::runtime_error("保存した状態を読み込めません");
    input.seekg(0, std::ios::end);
    const auto end = input.tellg();
    if (end < 0) throw std::runtime_error("保存した状態のサイズを取得できません");
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()),
                                   static_cast<std::streamsize>(bytes.size()));
    if (!input) throw std::runtime_error("保存した状態を読み込めません");
    return bytes;
}

void load(::cw::lib::String path)
{
    const auto game = ::cw::g_cwpy;
    // 実行環境がないと Py は cw.cwpy.is_playingscenario()
    // で失敗する。 ネイティブ版も論理エラーを通知し、何もせず成功したことにはしない。
    if (!game) throw std::logic_error("recording.load: CWPy is unavailable");
    if (!game_thread_running(*game) || on_game_thread(*game)) {
        load_on_game_thread(game, std::move(path));
        return;
    }
    (void)game->sync_exec(
        [game, path = std::move(path)](const ::cw::lib::Arguments&,
                                       const ::cw::lib::KeywordArguments&) mutable {
            load_on_game_thread(game, std::move(path));
            return ::cw::lib::Value{};
        });
}

void load_func(::cw::lib::Integer areaid,
               std::optional<std::shared_ptr<::cw::data::CWPyElement>> data)
{
    if (!::cw::g_cwpy) throw std::logic_error("recording.load: CWPy is unavailable");
    load_func_impl(*::cw::g_cwpy, areaid, std::move(data));
}

void main()
{
    // Python版も pass。記録処理は save/load の呼び出し時に実行される。
}
}
