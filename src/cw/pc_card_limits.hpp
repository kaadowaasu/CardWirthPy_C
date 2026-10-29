#pragma once
#include "cw/data.hpp"
#include <algorithm>

namespace cw::pc_card_limits {
void reconcile_player(const std::shared_ptr<::cw::character::Player>& player,
                      const std::shared_ptr<::cw::data::Party>& party);
void reconcile_inn(const std::shared_ptr<::cw::data::YadoData>& inn);
// PCの所持上限は宿が管理し、シナリオのSummaryやグローバル設定からは読
// まない。 設定がなければレベル基準の動作を保ち、既存のカードは削除しない。
struct Limits {
    bool enabled = false;
    ::cw::lib::Integer skill = 50;
    ::cw::lib::Integer personal = 50;
    // 召喚獣カードの上限。実際の容量はレベル基準の値を下
    // 回らない。 既定値はスキル・個別上限と同じ50。
    ::cw::lib::Integer beast = 50;
    bool operator==(const Limits&) const = default;
};
inline Limits read(const std::shared_ptr<::cw::data::YadoData>& inn) {
    if (!inn || !inn->m_environment) return {};
    const auto& env = inn->m_environment;
    return {env->getbool("Property/PcCardLimits", "enabled", false),
        std::clamp<::cw::lib::Integer>(env->getint("Property/PcCardLimits", "skill", 50), 1, 50),
        std::clamp<::cw::lib::Integer>(env->getint("Property/PcCardLimits", "personal", 50), 1, 50),
        std::clamp<::cw::lib::Integer>(env->getint("Property/PcCardLimits", "beast", 50), 1, 50)};
}
inline void write(const std::shared_ptr<::cw::data::YadoData>& inn, Limits limits) {
    if (!inn || !inn->m_environment) return;
    limits.skill = std::clamp<::cw::lib::Integer>(limits.skill, 1, 50);
    limits.personal = std::clamp<::cw::lib::Integer>(limits.personal, 1, 50);
    limits.beast = std::clamp<::cw::lib::Integer>(limits.beast, 1, 50);
    if (read(inn) == limits) return;
    const auto root = inn->m_environment->getroot();
    if (!root) return;
    auto property = root->find("Property").value_or(nullptr);
    if (!property) { property = ::cw::data::make_element("Property"); root->append(property); }
    auto node = property->find("PcCardLimits").value_or(nullptr);
    if (!node) { node = ::cw::data::make_element("PcCardLimits"); property->append(node); }
    node->set("enabled", limits.enabled ? "True" : "False");
    node->set("skill", std::to_string(limits.skill));
    node->set("personal", std::to_string(limits.personal));
    node->set("beast", std::to_string(limits.beast));
    inn->m_environment->m_is_edited = true;
    inn->changed(); // 部品を操作するたびにディスクへ書かず、通常の宿の保存で永続化する。
}
}
