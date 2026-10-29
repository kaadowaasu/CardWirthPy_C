#include "ui_value_dialogs.hpp"
#include "ui_metrics.hpp"
#include "ui_actions.hpp"

#include <array>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <system_error>

namespace cwxeditor::lib {
namespace {
std::filesystem::path canonical_with_missing_tail(const std::filesystem::path& value) {
    namespace fs = std::filesystem;
    std::error_code error;
    auto absolute = fs::absolute(value, error).lexically_normal();
    if (error) throw fs::filesystem_error("absolute", value, error);

    auto probe = absolute;
    fs::path tail;
    for (;;) {
        error.clear();
        const auto status = fs::symlink_status(probe, error);
        if (!error && fs::exists(status)) {
            const auto base = fs::canonical(probe, error);
            if (error) throw fs::filesystem_error("canonical", probe, error);
            return tail.empty() ? base : (base / tail).lexically_normal();
        }
        if (probe == probe.root_path() || probe.empty()) {
            if (error) throw fs::filesystem_error("symlink_status", probe, error);
            throw std::runtime_error("保存先の親フォルダーを解決できません");
        }
        const auto name = probe.filename();
        if (!name.empty()) tail = tail.empty() ? name : name / tail;
        probe = probe.parent_path();
    }
}

std::shared_ptr<Dialog> make_dialog(const std::string& title,
                                    const std::shared_ptr<Window>& owner) {
    auto dialog=std::make_shared<Dialog>();
    dialog->title=title; dialog->modal=true; dialog->owner=owner;
    dialog->center_on_owner=true; dialog->minimizable=false;
    if (owner) {
        dialog->initially_hidden=owner->initially_hidden;
        dialog->activate_on_show=owner->activate_on_show;
    }
    const std::weak_ptr<Dialog> weak=dialog;
    dialog->on_close_requested=[weak] {
        if (const auto current=weak.lock()) current->result=DialogResult::Cancel;
    };
    return dialog;
}
void add_buttons(const std::shared_ptr<Dialog>& dialog, std::size_t row, std::size_t columns) {
    auto buttons=std::make_shared<Panel>();
    auto layout=std::make_shared<LinearLayout>(); layout->orientation=Orientation::Horizontal;
    layout->spacing=8; buttons->set_layout(layout);
    buttons->set_layout_item({row,0,1,columns,Align::End,Align::Center});
    const std::weak_ptr<Dialog> weak=dialog;
    auto ok=std::make_shared<Button>("OK"); ok->default_button=true;
    ok->set_layout_item({0,0,1,1,Align::Stretch,Align::Center,{90,0}});
    ok->on_click=[weak] { if(const auto current=weak.lock()) current->result=DialogResult::OK; };
    auto cancel=std::make_shared<Button>("キャンセル"); cancel->cancel_button=true;
    cancel->set_layout_item({0,0,1,1,Align::Stretch,Align::Center,{90,0}});
    cancel->on_click=[weak] { if(const auto current=weak.lock()) current->result=DialogResult::Cancel; };
    buttons->add(ok); buttons->add(cancel); dialog->add(buttons);
}
void size_to_content(const std::shared_ptr<Dialog>& dialog) {
    const auto size=preferred_ui_size(dialog);
    // 共通の計測値はUI倍率を含むため、ウィンドウモデルには指定時の
    // 単位で保存する。 二倍で実体化するときに倍率を二重適用しない。
    const auto scale=editor_ui_text_scale();
    dialog->set_bounds({0,0,size.width/scale,size.height/scale});
    dialog->minimum_size=Size{size.width/scale,size.height/scale};
}
} // namespace

namespace {
ResourceId g_stored_file_dialog_folder_icon{};
}

void set_stored_file_dialog_folder_icon(const ResourceId icon) noexcept {
    g_stored_file_dialog_folder_icon = icon;
}

ValueDialogPresentation<std::vector<std::filesystem::path>> make_stored_file_dialog(
    const std::filesystem::path& root, const FilePickerOperation operation,
    const FileDialogRequest& request, const std::shared_ptr<Window>& owner,
    std::shared_ptr<std::optional<std::size_t>> selected_filter) {
    namespace fs = std::filesystem;
    const bool save = operation == FilePickerOperation::SaveFile;
    const bool folders = operation == FilePickerOperation::SelectDirectory;
    const auto plan = plan_native_file_dialog(operation, request);
    struct State {
        fs::path root, directory;
        std::vector<fs::path> rows, selected;
        std::optional<fs::path> overwrite;
        fs::path checked(const fs::path& value) const {
            // MSVCの weakly_canonical は、末尾の要素が未存在だと
            // access_denied を返す場合がある。 最も深い既存の祖先を解決し
            // 、リンクによる範囲外への移動を拒否しつつ、新しい保存名は選べるようにする。
            const auto path = canonical_with_missing_tail(
                value.is_absolute() ? value : directory / value);
            auto a = root.begin(), b = path.begin();
            for (; a != root.end(); ++a, ++b)
                if (b == path.end() || *a != *b)
                    throw std::runtime_error("保存領域の外は選択できません");
            return path;
        }
    };
    auto state = std::make_shared<State>();
    if (root.empty() || !root.is_absolute()) throw std::runtime_error("保存領域が未設定です");
    state->root = fs::canonical(root);
    if (!fs::is_directory(state->root)) throw std::runtime_error("保存領域がありません");
    state->directory = state->root;
    try {
        auto initial = state->checked(request.initial_directory);
        while (initial != state->root && !fs::is_directory(initial)) initial = initial.parent_path();
        if (fs::is_directory(initial)) state->directory = initial;
    } catch (...) { /* 古いデスクトップのパスで、外部の選択画面を開かない。 */ }
    const auto utf8 = [](const fs::path& path) {
        const auto value = path.u8string(); return std::string(value.begin(), value.end());
    };
    auto dialog = make_dialog(request.title.empty() ? "保存領域から選択" : request.title, owner);
    auto layout = std::make_shared<GridLayout>(); layout->columns = 1;
    layout->padding = {12,12,12,12}; layout->spacing = 8; dialog->set_layout(layout);
    // 現在のフォルダーパスは長くなり得る。折り返すLabelでは、最初
    // の短いパスで決めたサイズを後で超え、 開く・キャンセルのボタン
    // を外へ押し出すため、三行分を確保し、それ以上はスクロールする。
    auto location = std::make_shared<TextEdit>();
    location->accessibility_name = "現在のフォルダー";
    location->multiline = true;
    location->wrap = true;
    location->read_only = true;
    location->vertical_scroll = true;
    location->minimum_visible_lines = 3U;
    auto up = std::make_shared<Button>("上のフォルダー");
    auto list = std::make_shared<ListView>(); list->multi_select = !folders && !save && plan.allow_multiple;
    list->accessibility_name = "保存領域のファイルとフォルダー";
    list->set_layout_item({2,0,1,1,Align::Stretch,Align::Stretch,{340,240},{},{},1});
    auto filename = std::make_shared<TextEdit>(request.initial_name);
    filename->accessibility_name = "ファイル名"; filename->set_visible(!folders);
    // 保存時のファイルの種類は、ネイティブの保存画面と同じ選択肢にする。 例: 展開した
    // WSNフォルダー、圧縮した .wsn、Classicの Summary.wsm。
    auto format = std::make_shared<ComboBox>();
    format->accessibility_name = "ファイルの種類";
    for (std::size_t index = 0; index < request.filters.size(); ++index)
        format->items.push_back({request.filters[index].label, std::to_string(index), 0U, true});
    const std::size_t initial_filter = request.selected_filter &&
        *request.selected_filter < request.filters.size() ? *request.selected_filter : 0U;
    if (!format->items.empty()) format->selected_index = initial_filter;
    format->set_visible(save && request.filters.size() > 1U);
    if (selected_filter) {
        *selected_filter = request.filters.empty()
            ? request.selected_filter : std::optional<std::size_t>(initial_filter);
        format->on_selection_changed = [selected_filter](const std::optional<std::size_t> index) {
            if (index) *selected_filter = *index;
        };
    }

    // 一行に固定し、内容に合わせて決めたダイアログの
    // サイズを、エラー文の折り返しで後から変えない。
    auto error = std::make_shared<Label>();
    auto ok = std::make_shared<Button>(save ? "保存" : folders ? "このフォルダーを選択" : "開く");
    ok->default_button = true;
    auto cancel = std::make_shared<Button>("キャンセル"); cancel->cancel_button = true;
    const std::weak_ptr<Dialog> weak_dialog = dialog;
    const std::weak_ptr<ListView> weak_list = list;
    const std::weak_ptr<TextEdit> weak_location = location;
    const std::weak_ptr<Label> weak_error = error;
    const std::weak_ptr<Button> weak_up = up, weak_ok = ok;
    const std::weak_ptr<TextEdit> weak_name = filename;
    auto refresh = std::make_shared<std::function<void()>>();
    *refresh = [=] {
        const auto view = weak_list.lock(); if (!view) return;
        std::vector<std::pair<bool, fs::path>> rows;
        for (const auto& entry : fs::directory_iterator(state->directory)) {
            try {
                const auto path = state->checked(entry.path());
                const bool directory = fs::is_directory(path);
                if (!directory) {
                    if (folders || !fs::is_regular_file(path)) continue;
                    if (!plan.combined_extensions.unrestricted) {
                        auto name = utf8(path.filename());
                        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                        if (std::none_of(plan.combined_extensions.extensions.begin(), plan.combined_extensions.extensions.end(),
                            [&](const auto& ext) { return name.ends_with("." + ext); })) continue;
                    }
                }
                rows.emplace_back(directory, path);
            } catch (...) { /* マウント先の外へ出るリンクはたどらない。 */ }
        }
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.first != b.first ? a.first > b.first : a.second.filename() < b.second.filename();
        });
        state->rows.clear(); view->items.clear(); view->selection.clear();
        view->top_index = 0; state->overwrite.reset();
        const auto folder_icon = g_stored_file_dialog_folder_icon;
        for (const auto& [directory,path] : rows) {
            state->rows.push_back(path);
            // エディタが提供していれば、フォルダー行にフォルダーのアイコンを使う。
            ListItem item{(directory && !folder_icon ? "[フォルダー] " : "") +
                              utf8(path.filename()), utf8(path)};
            if (directory) item.icon = folder_icon;
            view->items.push_back(std::move(item));
        }
        ++view->item_update_revision; ++view->item_order_revision;
        if (const auto label = weak_location.lock()) label->text = utf8(state->directory);
        if (const auto control = weak_up.lock()) control->set_enabled(state->directory != state->root);
        if (const auto label = weak_error.lock()) label->text.clear();
        if (const auto control = weak_ok.lock()) control->text = save ? "保存" : folders ? "このフォルダーを選択" : "開く";
    };
    const auto accept = [=] {
        const auto view = weak_list.lock(); const auto name = weak_name.lock();
        if (!view || !name) return;
        try {
            std::vector<fs::path> paths;
            for (const auto index : view->selection)
                if (index < state->rows.size()) paths.push_back(state->checked(state->rows[index]));
            if (paths.size() == 1 && fs::is_directory(paths.front()) && !folders) {
                state->directory = paths.front(); (*refresh)(); return;
            }
            if (folders) {
                paths = {paths.empty() ? state->directory : paths.front()};
                if (!fs::is_directory(paths.front())) throw std::runtime_error("フォルダーを選択してください");
            } else if (save || paths.empty()) {
                const auto file = fs::u8path(name->text);
                if (name->text.empty() || file.has_parent_path() || file == "." || file == ".." ||
                    name->text.find_first_of("\\/\r\n") != std::string::npos || name->text.find('\0') != std::string::npos)
                    throw std::runtime_error("有効なファイル名を入力してください");
                paths = {state->checked(state->directory / file)};
            }
            for (const auto& path : paths) {
                if (!folders && fs::is_directory(path)) throw std::runtime_error("同名のフォルダーがあります");
                if (!save && !folders && !fs::is_regular_file(path)) throw std::runtime_error("ファイルがありません");
            }
            if (save && fs::exists(paths.front()) && request.confirm_overwrite && state->overwrite != paths.front()) {
                state->overwrite = paths.front();
                if (const auto label = weak_error.lock()) label->text = "同名のファイルがあります。上書きする場合は再度押してください。";
                if (const auto control = weak_ok.lock()) control->text = "上書き保存";
                return;
            }
            state->selected = std::move(paths);
            if (const auto current = weak_dialog.lock()) current->result = DialogResult::OK;
        } catch (const std::exception& failure) {
            if (const auto label = weak_error.lock()) label->text = failure.what();
        }
    };
    up->on_click = [=] {
        if (state->directory == state->root) return;
        state->directory = state->checked(state->directory.parent_path()); (*refresh)();
    };
    list->on_selection_changed = [=](const std::vector<std::size_t>& selection) {
        state->overwrite.reset();
        if (selection.size() == 1 && selection.front() < state->rows.size() &&
            fs::is_regular_file(state->rows[selection.front()]))
            if (const auto name = weak_name.lock()) name->text = utf8(state->rows[selection.front()].filename());
    };
    list->on_activate = [=](std::size_t index) {
        if (index >= state->rows.size()) return;
        if (fs::is_directory(state->rows[index])) { state->directory = state->checked(state->rows[index]); (*refresh)(); }
        else accept();
    };
    filename->on_changed = [=](std::string_view) {
        state->overwrite.reset(); if (const auto view = weak_list.lock()) view->selection.clear();
    };
    filename->on_enter = accept; ok->on_click = accept;
    cancel->on_click = [=] { if (const auto current = weak_dialog.lock()) current->result = DialogResult::Cancel; };
    // Android・Webでは、シナリオ・外部シナリオを保
    // 存済みファイルのダイアログで開く。 ネイティブのエディ
    // タと同様、操作ボタンは一つの横並びの行に置き、 一列
    // グリッドの連続行でOK・キャンセルを縦積みにしない。
    auto buttons = std::make_shared<Panel>();
    auto buttons_layout = std::make_shared<LinearLayout>(Orientation::Horizontal);
    buttons_layout->spacing = 8.0F;
    buttons_layout->cross_axis = Align::Center;
    buttons->set_layout(buttons_layout);
    buttons->add(ok);
    buttons->add(cancel);
    const std::array<WidgetPtr,7> controls{location,up,list,filename,format,error,buttons};
    for (std::size_t i=0;i<controls.size();++i) {
        if (i != 2) controls[i]->set_layout_item({i,0,1,1,Align::Stretch,Align::Center});
        dialog->add(controls[i]);
    }
    (*refresh)(); size_to_content(dialog);
    return {dialog,[=]() -> std::optional<std::vector<fs::path>> {
        const auto current = weak_dialog.lock();
        return current && current->result == DialogResult::OK ? std::optional(state->selected) : std::nullopt;
    }};
}

ValueDialogPresentation<MessageResult> make_message_dialog(
    const MessageDialogRequest& request, const std::shared_ptr<Window>& owner) {
    auto dialog=make_dialog(request.title,owner);
    auto layout=std::make_shared<GridLayout>(); layout->columns=1;
    layout->padding={12,12,12,12}; layout->spacing=10; dialog->set_layout(layout);
    const auto severity=request.severity==MessageSeverity::Error ? "エラー" :
        request.severity==MessageSeverity::Warning ? "警告" :
        request.severity==MessageSeverity::Question ? "確認" : "情報";
    // 演算結果などの情報・確認画面には、本文の上に「情報」「確認
    // 」の見出しを重ねない。 タイトルですでに示しているため。
    const bool caption=request.severity!=MessageSeverity::Information &&
        request.severity!=MessageSeverity::Question;
    if(caption) dialog->add(std::make_shared<Label>(severity));
    const std::size_t body_row=caption ? 1U : 0U;
    auto body=std::make_shared<Label>(request.details.empty()
        ? request.message : request.message+"\n\n"+request.details);
    // DWTの MessageBox と同様、本文は受動的な文字と
    // して表示する。 読み取り専用TextEditでも枠・カーソル
    // ・フォーカスがあり入力欄に見えるため、確認画面には使わない。
    body->wrap=true;
    body->accessibility_name=severity;
    const auto lines=1+std::count(body->text.begin(),body->text.end(),'\n');
    const float height=std::clamp(static_cast<float>(lines)*18.0F,72.0F,300.0F);
    body->set_layout_item({body_row,0,1,1,Align::Stretch,Align::Stretch,{240,72},{420,height},{},1});
    dialog->add(body);
    auto buttons=std::make_shared<Panel>();
    auto row=std::make_shared<LinearLayout>(); row->orientation=Orientation::Horizontal;
    row->spacing=8; buttons->set_layout(row);
    buttons->set_layout_item({body_row+1U,0,1,1,Align::End,Align::Center});
    dialog->add(buttons);
    const auto selected=std::make_shared<std::optional<MessageResult>>();
    const std::weak_ptr<Dialog> weak=dialog;
    const auto add=[&](MessageResult result,const char* label,bool primary=false,bool cancel=false) {
        auto button=std::make_shared<Button>(label);
        button->default_button=primary; button->cancel_button=cancel;
        button->set_layout_item({0,0,1,1,Align::Stretch,Align::Center,{90,0}});
        button->on_click=[weak,selected,result] {
            if(const auto current=weak.lock()) { *selected=result; current->result=DialogResult::OK; }
        };
        if(cancel) dialog->on_close_requested=button->on_click;
        buttons->add(button);
    };
    switch(request.buttons) {
    case MessageButtons::OK: add(MessageResult::OK,"OK",true,true); break;
    case MessageButtons::OKCancel:
        add(MessageResult::OK,"OK",true); add(MessageResult::Cancel,"キャンセル",false,true); break;
    case MessageButtons::YesNo:
        add(MessageResult::Yes,"はい",true); add(MessageResult::No,"いいえ",false,true); break;
    case MessageButtons::YesNoCancel:
        add(MessageResult::Yes,"はい",true); add(MessageResult::No,"いいえ");
        add(MessageResult::Cancel,"キャンセル",false,true); break;
    case MessageButtons::RetryCancel:
        add(MessageResult::Retry,"再試行",true); add(MessageResult::Cancel,"キャンセル",false,true); break;
    case MessageButtons::AbortRetryIgnore:
        add(MessageResult::Abort,"中止",false,true); add(MessageResult::Retry,"再試行",true);
        add(MessageResult::Ignore,"無視"); break;
    }
    size_to_content(dialog);
    return {dialog,[weak,selected]() -> std::optional<MessageResult> {
        const auto current=weak.lock();
        return current && current->result==DialogResult::OK ? *selected : std::nullopt;
    }};
}

ValueDialogPresentation<Color> make_color_selection_dialog(
    Color initial, const std::shared_ptr<Window>& owner) {
    auto dialog=make_dialog("色の選択",owner);
    auto layout=std::make_shared<GridLayout>(); layout->columns=3;
    layout->padding={12,12,12,12}; layout->spacing=8; dialog->set_layout(layout);
    auto draft=std::make_shared<Color>(initial);
    auto preview=std::make_shared<ColorWell>(); preview->value=initial;
    preview->set_enabled(false); // 色見本であり、再帰的に別の色選択を開くものではない。
    preview->accessibility_name="色のプレビュー";
    preview->set_layout_item({0,0,1,3,Align::Stretch,Align::Stretch,{0,48},{0,48}});
    dialog->add(preview);
    const std::array<const char*,4> labels{"赤","緑","青","透明度"};
    const std::array<std::uint8_t Color::*,4> members{&Color::red,&Color::green,&Color::blue,&Color::alpha};
    for(std::size_t i=0;i<members.size();++i) {
        const auto member=members[i];
        auto label=std::make_shared<Label>(labels[i]); label->set_layout_item({i+1,0,1,1,Align::Start,Align::Center});
        auto slider=std::make_shared<Slider>(); slider->minimum=0; slider->maximum=255;
        slider->step=1; slider->value=initial.*member; slider->accessibility_name=labels[i];
        slider->set_layout_item({i+1,1,1,1,Align::Stretch,Align::Center,{180,0},{},{},1});
        auto number=std::make_shared<NumberEdit>(); number->minimum=0; number->maximum=255;
        number->step=1; number->value=initial.*member; number->accessibility_name=labels[i];
        number->set_layout_item({i+1,2,1,1,Align::Stretch,Align::Center,{70,0}});
        const std::weak_ptr<Slider> weak_slider=slider;
        const std::weak_ptr<NumberEdit> weak_number=number;
        const auto change=[draft,preview,member,weak_slider,weak_number](double value) {
            if(!std::isfinite(value)) return;
            const auto channel=static_cast<std::uint8_t>(std::lround(std::clamp(value,0.0,255.0)));
            draft.get()->*member=channel; preview->value=*draft;
            if(const auto control=weak_slider.lock()) control->value=channel;
            if(const auto control=weak_number.lock()) control->value=channel;
        };
        slider->on_changed=change; number->on_changed=change;
        dialog->add(label); dialog->add(slider); dialog->add(number);
    }
    add_buttons(dialog,5,3); size_to_content(dialog);
    const std::weak_ptr<Dialog> weak=dialog;
    return {dialog,[weak,draft]() -> std::optional<Color> {
        const auto current=weak.lock();
        return current && current->result==DialogResult::OK ? std::optional<Color>(*draft) : std::nullopt;
    }};
}

ValueDialogPresentation<FontSpec> make_font_selection_dialog(
    const FontSpec& initial, const std::shared_ptr<Window>& owner) {
    auto dialog=make_dialog("フォントの選択",owner);
    auto layout=std::make_shared<GridLayout>(); layout->columns=2;
    layout->padding={14,14,14,14}; layout->spacing=9; dialog->set_layout(layout);
    auto draft=std::make_shared<FontSpec>(initial);
    auto family_label=std::make_shared<Label>("フォント名");
    auto family=std::make_shared<TextEdit>(initial.family); family->accessibility_name="フォント名";
    family->set_layout_item({0,1,1,1,Align::Stretch,Align::Center,{220,0},{},{},1});
    family->on_changed=[draft](std::string_view value) { draft->family=value; };
    auto size_label=std::make_shared<Label>("サイズ"); size_label->set_layout_item({1,0,1,1,Align::Start,Align::Center});
    auto size=std::make_shared<NumberEdit>(); size->accessibility_name="サイズ";
    // 開いて無変更で確定しても、既存の小数のサイズ・太さを維持する。
    size->minimum=std::min(4.0,static_cast<double>(initial.points));
    size->maximum=std::max(144.0,static_cast<double>(initial.points));
    size->decimals=1; size->step=1; size->value=initial.points;
    size->set_layout_item({1,1,1,1,Align::Start,Align::Center,{110,0}});
    size->on_changed=[draft](double value) { draft->points=static_cast<float>(value); };
    dialog->add(family_label); dialog->add(family); dialog->add(size_label); dialog->add(size);
    auto bold=std::make_shared<CheckBox>("太字"); bold->checked=initial.weight>=600;
    bold->set_layout_item({2,1});
    bold->on_changed=[draft](bool checked) { draft->weight=checked ? 700 : 400; };
    dialog->add(bold);
    const std::array<const char*,3> labels{"斜体","下線","取り消し線"};
    const std::array<bool FontSpec::*,3> members{&FontSpec::italic,&FontSpec::underline,&FontSpec::strikeout};
    for(std::size_t i=0;i<members.size();++i) {
        auto check=std::make_shared<CheckBox>(labels[i]); const auto member=members[i];
        check->checked=initial.*member; check->set_layout_item({i+3,1});
        check->on_changed=[draft,member](bool value) { draft.get()->*member=value; };
        dialog->add(check);
    }
    add_buttons(dialog,6,2); size_to_content(dialog);
    const std::weak_ptr<Dialog> weak=dialog;
    return {dialog,[weak,draft]() -> std::optional<FontSpec> {
        const auto current=weak.lock();
        return current && current->result==DialogResult::OK ? std::optional<FontSpec>(*draft) : std::nullopt;
    }};
}
} // namespace cwxeditor::lib
