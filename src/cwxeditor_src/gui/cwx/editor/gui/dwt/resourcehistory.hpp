#pragma once

#include "cwxeditor_src/core/cwx/area.hpp"
#include "cwxeditor_src/core/cwx/card.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include <functional>
#include <map>
#include <memory>

namespace cwxeditor::cwx::editor::gui::dwt::resourcehistory {

// 切り離されたメンバーだけを登録する。 別の呼び出し元が資源
// を保持していても、履歴が終了したら子Userの登録を外す。
class References {
public:
    explicit References(const usecounter::CounterOwner& main)
        : counter_(main ? main->sub() : nullptr), owner_(main ? main->owner() : nullptr) {}
    ~References() { clear(); }
    References(const References&) = delete;
    References& operator=(const References&) = delete;

    void hold(const std::shared_ptr<card::Card>& value) {
        if (!value || !counter_) return;
        release(value.get());
        value->setUseCounter(counter_, owner_);
        held_[value.get()] = [value] { value->removeUseCounter(); };
    }
    void hold(const area::AbstractAreaPtr& value) {
        if (!value || !counter_) return;
        release(value.get());
        if (auto typed = std::dynamic_pointer_cast<area::Area>(value)) holdArea(typed);
        else if (auto typed = std::dynamic_pointer_cast<area::Battle>(value)) holdArea(typed);
        else if (auto typed = std::dynamic_pointer_cast<area::Package>(value)) holdArea(typed);
    }
    void release(const void* key) {
        const auto it = held_.find(key);
        if (it == held_.end()) return;
        it->second();
        held_.erase(it);
    }
    void clear() {
        for (const auto& [key, detach] : held_) detach();
        held_.clear();
    }
private:
    template<class T> void holdArea(const std::shared_ptr<T>& value) {
        value->setUseCounter(counter_, owner_.get());
        held_[value.get()] = [value] { value->removeUseCounter(); };
    }
    usecounter::CounterOwner counter_;
    std::shared_ptr<path::CWXPath> owner_;
    std::map<const void*, std::function<void()>> held_;
};
} // namespace cwxeditor::cwx::editor::gui::dwt::resourcehistory
