#pragma once
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dutils.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/utils.hpp"
#include "cwxeditor_src/lib/image_import.hpp"
#include "cwxeditor_src/lib/window_host.hpp"
#include <algorithm>
#include <exception>
#include <new>
#include <utility>
#include <functional>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::scenehost {
// Dと同じく、新しく開いたモードレス編集の対象だけを選択へ反映する
// 。 既存画面の有効化ではこの補助処理を通さず、現在の選択を保つ。
template<class View, class Live>
bool projectEditSelection(View* view, bool card, std::size_t index, Live live) {
    if (!live()) return false;
    if (lib::editor_window_host_options().all_mode) return true;
    if (card) {
        if (!view->selectedBackIndices().empty()) {
            view->backList().selection.clear();
            view->listSelectB();
            if (!live()) return false;
        }
        view->cardList().selection = {index};
        view->listSelectC();
    } else {
        if (!view->selectedCardIndices().empty()) {
            view->cardList().selection.clear();
            view->listSelectC();
            if (!live()) return false;
        }
        view->backList().selection = {index};
        view->listSelectB();
    }
    return live();
}

// 実際のMainWindowの通知と、ヘッドレスのコマンド検証で共用する。 Dは
// 選択された両種類を編集するため、カードを開いたことで背景の編集を省略しない。
template<class View, class OpenCard, class Previous, class Live>
lib::Action cardEditCallback(View* view, OpenCard open, Previous previous, Live live,
        std::function<void(const std::vector<std::size_t>&)> openBacks = {}) {
    return [view, open, previous, live, openBacks] {
        if (!live()) return;
        auto selection = view->selectedCardIndices();
        auto back_selection = view->selectedBackIndices();
        // DのImagePane::selectedIndicesと同じく、クリック順でなくモデルの格納順で列挙する。
        std::sort(selection.begin(), selection.end());
        std::sort(back_selection.begin(), back_selection.end());
        const auto cards = view->area().cards();
        bool opened = false;
        for (const auto index : selection) {
            if (!lib::editor_window_host_options().all_mode) {
                // Dと同じく、選択カードのダイアログを開くたびにstopOpenを確認する。
                // 再入でホストが終了しても無効にならないよう、コールバックをコピーする。
                const auto stop = view->context().stop_open;
                const bool stopping = stop && stop();
                if (!live()) return;
                if (stopping) break;
            }
            if (index >= cards.size()) continue;
            open(cards[index]);
            if (!live()) return;
            opened = true;
        }
        // カード専用・Battle用の代替動作は変えない。
        if (!lib::editor_window_host_options().all_mode && openBacks &&
            !back_selection.empty()) {
            // 新しいカード画面は選択を狭めるため、 選択を一時復
            // 元せず、元のスナップショットから背景を処理する。
            openBacks(back_selection);
        } else if (previous && (!opened || !view->selectedBacks().empty())) previous();
    };
}

// Areaとイベント背景キャンバスで、コピー・リンク・キャンセルのホスト処理を共
// 用する。 再入可能な画面をまたいでも、シナリオ・スキン・サービスを保持する。
inline auto imageImporter(dprops::Props* prop, lib::Services services,
          summary::Summary::Owner source_summary,
          skin::Skin::Ptr scene_skin) {
    return [prop, services, source_summary, scene_skin](
        const std::filesystem::path& source, const lib::ImageImportKind kind) {
        lib::ImageImportResult unavailable;
        if (!prop || !source_summary || !scene_skin || !services.image_codecs ||
            source_summary->scenarioPath().empty()) {
            unavailable.error = "scene image import is unavailable";
            return unavailable;
        }
        bool predecoded_background = false;
        auto codecs = services.image_codecs;
        if (kind == lib::ImageImportKind::Background &&
            !lib::editor_window_host_options().all_mode &&
            ::cwxeditor::cwx::utils::iendsWith(dutils::pathString(source.extension()), ".jpy1")) {
            // DはloadBgImageで背景寸法を得る。JPY1
            // はラスタ専用の変換器でなく、専用描画へ送る。 転送
            // 画面の通知でホストが終了し得るため、その前に行う。
            lib::ImageData decoded;
            try {
                decoded = dutils::loadImageWithScale(prop, scene_skin.get(), source_summary.get(),
                    dutils::pathString(source), 1U, false, false, 0, 0, {}, services).baseData();
            } catch (const std::exception& error) {
                unavailable.error = error.what();
                return unavailable;
            }
            if (decoded.frames.empty()) {
                unavailable.error = "JPY1 background decoder returned no frames";
                return unavailable;
            }
            // 共通のStorage・パスの処理を保つ。 コピー・リ
            // ンク後の唯一の復号要求には、この検証済み画像を使う。
            class DecodedSource final : public lib::ImageCodecBackend {
                lib::ImageData image_;
            public:
                explicit DecodedSource(lib::ImageData image) : image_(std::move(image)) {}
                lib::ImageData decode(const lib::DecodeImageRequest&, std::string& error) override {
                    error.clear();
                    return image_;
                }
                bool encode(const lib::ImageData&, const lib::EncodeImageRequest&, std::string&) override {
                    return false;
                }
                lib::Size probe(std::span<const std::byte>, lib::ImageFormat) override {
                    return image_.frames.empty() ? lib::Size{} : image_.frames.front().logical_size;
                }
            };
            codecs = std::make_shared<DecodedSource>(std::move(decoded));
            predecoded_background = true;
        }
        if (!lib::editor_window_host_options().all_mode && !predecoded_background) {
            // Dの通常の画像読み込みは、復号失敗時に透明な1x1の代
            // 替画像を返す。 コピー・リンクの中止は優先するが、対
            // 応形式で内容が不正な画像もモデルとしては取り込める。
            class PlaceholderCodec final : public lib::ImageCodecBackend {
                std::shared_ptr<lib::ImageCodecBackend> backend_;
            public:
                explicit PlaceholderCodec(std::shared_ptr<lib::ImageCodecBackend> backend)
                    : backend_(std::move(backend)) {}
                lib::ImageData decode(const lib::DecodeImageRequest& request,
                                      std::string& error) override {
                    try {
                        auto image = backend_->decode(request, error);
                        if (!image.frames.empty()) { error.clear(); return image; }
                    } catch (const std::bad_alloc&) {
                        throw;
                    } catch (const std::exception&) {
                    }
                    error.clear();
                    return dutils::blankImage();
                }
                bool encode(const lib::ImageData& image, const lib::EncodeImageRequest& request,
                            std::string& error) override {
                    return backend_->encode(image, request, error);
                }
                lib::Size probe(std::span<const std::byte> bytes, lib::ImageFormat format) override {
                    return backend_->probe(bytes, format);
                }
            };
            codecs = std::make_shared<PlaceholderCodec>(std::move(codecs));
        }
        lib::ImageImportRequest request{
            source, {}, kind,
            lib::paths::ScenarioRoot(dutils::filesystemPath(source_summary->scenarioPath())),
            dutils::filesystemPath(scene_skin->materialPath()), 1.0F, !predecoded_background, {}};
        request.transfer = [prop, services](
            const lib::ImageImportTransferRequest& transfer) {
            lib::ImageImportTransferResult result;
            const lib::paths::ScenarioRoot scenario(transfer.scenario_root);
            if (scenario.contains(transfer.source_path)) {
                result.action = lib::ImageImportAction::Link;
                result.material_path = transfer.source_path;
                return result;
            }
            if (!services.dialogs || !prop) {
                result.error = "scene image import requires user confirmation";
                return result;
            }
            lib::MessageDialogRequest prompt;
            const bool card = transfer.kind == lib::ImageImportKind::MenuCard;
            prompt.title = card ? prop->requiredMsgs().dlgTitDropCard.value
                                : prop->requiredMsgs().dlgTitDropBack.value;
            prompt.message = ::cwxeditor::cwx::utils::tryFormat(
                card ? prop->requiredMsgs().dlgMsgDropCard.value
                     : prop->requiredMsgs().dlgMsgDropBack.value,
                dutils::pathString(transfer.source_path));
            prompt.severity = lib::MessageSeverity::Question;
            prompt.buttons = lib::MessageButtons::YesNoCancel;
            const auto answer = services.dialogs->message(prompt);
            if (answer == lib::MessageResult::No) {
                result.action = lib::ImageImportAction::Link;
                result.material_path = transfer.source_path;
                return result;
            }
            if (answer != lib::MessageResult::Yes) return result;
            const auto copied = ::cwxeditor::cwx::utils::copyTo(
                transfer.scenario_root, transfer.source_path,
                transfer.material_directory, false);
            if (copied.empty()) {
                result.error = "scene image copy failed";
                return result;
            }
            result.action = lib::ImageImportAction::Copy;
            result.material_path = transfer.scenario_root / copied;
            return result;
        };
        return lib::ImageImportService(codecs).import(request);
    };
}
} // namespace cwxeditor::cwx::editor::gui::dwt::scenehost
