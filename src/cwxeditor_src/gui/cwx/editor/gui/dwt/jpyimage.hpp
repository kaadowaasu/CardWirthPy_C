#pragma once

// cwx/editor/gui/dwt/jpyimage.dの移植可能なC++20実装。

#include "cwxeditor_src/core/cwx/graphics.hpp"
#include "cwxeditor_src/core/cwx/jpy.hpp"
#include "cwxeditor_src/core/cwx/skin.hpp"
#include "cwxeditor_src/core/cwx/summary.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/areaviewutils.hpp"
#include "cwxeditor_src/gui/cwx/editor/gui/dwt/dprops.hpp"
#include "cwxeditor_src/lib/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cwxeditor::cwx::editor::gui::dwt::jpyimage {

namespace areaviewutils = ::cwxeditor::cwx::editor::gui::dwt::areaviewutils;
namespace dprops = ::cwxeditor::cwx::editor::gui::dwt::dprops;
namespace jpy = ::cwxeditor::cwx::jpy;
namespace lib = ::cwxeditor::lib;
namespace skin = ::cwxeditor::cwx::skin;
namespace summary = ::cwxeditor::cwx::summary;

using Props = dprops::Props;
using Skin = skin::Skin;
using Summary = summary::Summary;
using ImageData = lib::ImageData;
using ImageDataWithScale = areaviewutils::ImageDataWithScale;
using String = lib::String;
using StringView = lib::StringView;

[[nodiscard]] ImageDataWithScale loadJPYImageWithScale(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    std::uint32_t target_scale, std::vector<String> stratum,
    bool& resizable);

[[nodiscard]] ImageDataWithScale loadJPYImageWithScale(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    std::uint32_t target_scale, std::vector<String> stratum,
    std::uint32_t& width, std::uint32_t& height, bool& resizable);

[[nodiscard]] ImageDataWithScale loadJPYImageImpl(
    Props& prop, const Skin* skin, const Summary* summ, String path,
    std::uint32_t target_scale, std::vector<String> stratum);

[[nodiscard]] ImageDataWithScale loadJPTXImage(
    Props& prop, String path, std::uint32_t target_scale);

[[nodiscard]] ImageData warningImage(
    Props& prop, const Summary* summ, const jpy::EffectBoosterError& error);

[[nodiscard]] ImageDataWithScale loadJPDCImage(
    Props& prop, String scenario_path, String path, std::uint32_t target_scale);

}  // namespace cwxeditor::cwx::editor::gui::dwt::jpyimage
