#pragma once

// FlagDirの非公開の解析動作を検証するテスト専用の入
// 口。 公開のFlagDirインターフェースとは分ける。

#include "cwxeditor_src/core/cwx/flag.hpp"

namespace cwxeditor::cwx::flag {

struct FlagDirTestAccess final {
    [[nodiscard]] static bool loadVariables(
        FlagDir& directory, ::cwxeditor::cwx::xml::XNode& node,
        std::unordered_map<String, FlagPtr>& flags,
        std::unordered_map<String, StepPtr>& steps,
        std::unordered_map<String, VariantPtr>& variants, bool copy,
        const ::cwxeditor::cwx::system::XMLInfo& version);
};

struct VariantTestAccess final {
    [[nodiscard]] static VariantVal toVariantVal(
        ::cwxeditor::cwx::xml::XNode& node);
    static void fromVariantVal(::cwxeditor::cwx::xml::XNode& node,
                               const VariantVal& value);
};

}  // namespace cwxeditor::cwx::flag
