#pragma once
// UIの実装は一つに統一し、
// 別経路の実装を設けない。
#include "cw/dialog/presentation.hpp"
#include "cw/dialog/textedit.hpp"

namespace cw::dialog::textedit {
// ダイアログモデルで共有する一行編集。クリップボードは既存の OS
// サービス、または隔離した非 GUI テストから提供する。
inline bool single_line_key(Buffer& edit,DialogPresentationKey key,
    DialogPresentationModifiers modifiers,std::size_t limit,
    const std::function<std::string()>& read_clipboard,
    const std::function<void(const std::string&)>& write_clipboard) {
    using Key=DialogPresentationKey;
    if(!edit.composition.empty() || (modifiers & DialogPresentationModifierAlt)) return false;
    const bool ctrl=(modifiers & DialogPresentationModifierControl)!=0;
    const bool shift=(modifiers & DialogPresentationModifierShift)!=0;
    // 標準編集コントロールのクリップボード用の別キー操作。すべての所有者でこの経路を使う。
    if(key==Key::Insert && (ctrl || shift)) {
        if(ctrl) {
            if(edit.caret!=edit.anchor) write_clipboard(edit.text.substr(std::min(edit.caret,edit.anchor),
                std::max(edit.caret,edit.anchor)-std::min(edit.caret,edit.anchor)));
        } else edit.insert(read_clipboard(),limit,false);
        return true;
    }
    if(key==Key::Delete && shift && !ctrl) {
        if(edit.caret!=edit.anchor) {
            write_clipboard(edit.text.substr(std::min(edit.caret,edit.anchor),
                std::max(edit.caret,edit.anchor)-std::min(edit.caret,edit.anchor)));
            edit.insert("",limit,false);
        } else edit.insert("\b",limit,false);
        return true;
    }
    if(ctrl && key==Key::A) {edit.anchor=0;edit.move(edit.text.size(),true);}
    else if(ctrl && (key==Key::Z || key==Key::Y)) edit.restore(key==Key::Y || shift);
    else if(ctrl && key==Key::V) edit.insert(read_clipboard(),limit,false);
    else if(ctrl && (key==Key::C || key==Key::X)) {
        if(edit.caret!=edit.anchor) {
            write_clipboard(edit.text.substr(std::min(edit.caret,edit.anchor),
                std::max(edit.caret,edit.anchor)-std::min(edit.caret,edit.anchor)));
            if(key==Key::X) edit.insert("",limit,false);
        }
    } else if(!ctrl && key==Key::Backspace) edit.insert("\b",limit,false);
    else if(!ctrl && key==Key::Delete) {
        if(edit.caret==edit.anchor) edit.anchor=next(edit.text,edit.caret);
        edit.insert("",limit,false);
    } else if(key==Key::Home || key==Key::End || (!ctrl && (key==Key::Left || key==Key::Right))) {
        const auto pos=key==Key::Home?0:key==Key::End?edit.text.size():key==Key::Left?
            (!shift && edit.caret!=edit.anchor?std::min(edit.caret,edit.anchor):previous(edit.text,edit.caret)):
            (!shift && edit.caret!=edit.anchor?std::max(edit.caret,edit.anchor):next(edit.text,edit.caret));
        edit.move(pos,shift);
    } else return false;
    return true;
}
} // namespace cw::dialog::textedit
