// cw/sprite/transition.py の移植。テクスチャーの合成は SDL に委ねる。
#include "cw/sprite/transition.hpp"
#include "cw/cw.hpp"
#include "cw/dice.hpp"
#include "cw/setting.hpp"
#include "cw/sprite/background.hpp"
#include "cw/thread.hpp"
#include "cw/lib/simd_image.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

namespace {
void convert_capture_alpha(const std::shared_ptr<::cw::lib::Surface>& image)
{
    // Py の Blinds/PixelDissolve と同様、convert_alpha()
    // を使う。 SRCALPHA でない取得画像の全体不透明度は変換時に一度だけピクセルアルファへ
    // 反映し、その後の穴はピクセルアルファだけで表す。 新しい取得画像はこの画面効果が所有する。
    if (image->surface_alpha != 255)
        ::cw::lib::simd_image::force_alpha(image->rgba.data(), image->rgba.size() / 4U,
                                          image->surface_alpha);
    image->surface_alpha = 255;
    image->known_opaque = false;
}

void clear_rect(std::shared_ptr<::cw::lib::Surface> surface, const ::cw::lib::Rect& rect,
                const ::cw::lib::Color& color)
{
    if (!surface || surface->size.width <= 0 || surface->size.height <= 0) return;
    const auto pixels = static_cast<std::size_t>(surface->size.width) *
                        static_cast<std::size_t>(surface->size.height) * 4u;
    if (surface->rgba.size() < pixels) surface->rgba.resize(pixels);
    const auto y_start = std::max<::cw::lib::Integer>(0, rect.y);
    const auto y_end = std::min(rect.y + rect.height, surface->size.height);
    const auto x_start = std::max<::cw::lib::Integer>(0, rect.x);
    const auto x_end = std::min(rect.x + rect.width, surface->size.width);
    for (auto y = y_start; y < y_end; ++y) for (auto x = x_start; x < x_end; ++x) {
        const auto index = static_cast<std::size_t>((y * surface->size.width + x) * 4);
        surface->rgba[index] = color.r;
        surface->rgba[index + 1] = color.g;
        surface->rgba[index + 2] = color.b;
        surface->rgba[index + 3] = color.a;
    }
}

void alpha_blit(const std::shared_ptr<::cw::lib::Surface>& target,
                const std::shared_ptr<::cw::lib::Surface>& source,
                ::cw::lib::Integer left, ::cw::lib::Integer top)
{
    using I = ::cw::lib::Integer;
    if (!target || !source || target->size.width <= 0 || target->size.height <= 0 ||
        source->size.width <= 0 || source->size.height <= 0) return;
    if (!source->surface_alpha) return;
    const auto source_bytes = static_cast<std::size_t>(source->size.width * source->size.height * 4);
    const auto target_bytes = static_cast<std::size_t>(target->size.width * target->size.height * 4);
    if (source->rgba.size() < source_bytes) return;
    if (target->rgba.size() < target_bytes) target->rgba.resize(target_bytes);
    const I sx_begin = std::max<I>(0, -left);
    const I dx_begin = std::max<I>(0, left);
    const I width = std::min(source->size.width - sx_begin, target->size.width - dx_begin);
    if (width <= 0) return;
    for (I sy = 0; sy < source->size.height; ++sy) {
        const I dy = top + sy;
        if (dy < 0 || dy >= target->size.height) continue;
        const auto si = static_cast<std::size_t>((sy * source->size.width + sx_begin) * 4);
        const auto di = static_cast<std::size_t>((dy * target->size.width + dx_begin) * 4);
        ::cw::lib::simd_image::alpha_blend_rgba(source->rgba.data() + si, source->size.width * 4U,
            target->rgba.data() + di, target->size.width * 4U, static_cast<std::size_t>(width), 1,
            source->surface_alpha);
    }
}
}

namespace cw::sprite::transition {
namespace {
TransitionRuntimeContext* g_runtime{};

::cw::lib::String configured_name()
{
    if (g_runtime && g_runtime->default_name) return g_runtime->default_name();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_transition;
    return "Fade";
}

::cw::lib::Integer configured_speed()
{
    if (g_runtime && g_runtime->default_speed) return g_runtime->default_speed();
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) return ::cw::g_cwpy->m_setting->m_transitionspeed;
    return 5;
}

// Python の cw.s() と同様、int(n * UP_SCR) でゼロ方向へ切り捨てる。
::cw::lib::Integer scale_value(::cw::lib::Integer value)
{
    if (g_runtime && g_runtime->scale) return g_runtime->scale(value);
    const auto factor = std::isfinite(::cw::g_UP_SCR) && ::cw::g_UP_SCR > 0.0 ? ::cw::g_UP_SCR : 1.0;
    return static_cast<::cw::lib::Integer>(static_cast<::cw::lib::Real>(value) * factor);
}

void shuffle_points(std::vector<::cw::lib::Point>& points)
{
    if (g_runtime && g_runtime->shuffle) {
        g_runtime->shuffle(points);
        return;
    }
    if (::cw::g_cwpy && ::cw::g_cwpy->m_dice && !points.empty()) {
        std::vector<::cw::lib::Value> indexes;
        indexes.reserve(points.size());
        for (::cw::lib::Integer i = 0; i < static_cast<::cw::lib::Integer>(points.size()); ++i)
            indexes.emplace_back(i);
        const auto shuffled = ::cw::g_cwpy->m_dice->shuffle(std::move(indexes));
        std::vector<::cw::lib::Point> result;
        result.reserve(shuffled.size());
        for (const auto& value : shuffled) {
            if (const auto* index = std::get_if<::cw::lib::Integer>(&value.storage())) {
                if (*index >= 0 && static_cast<std::size_t>(*index) < points.size())
                    result.push_back(points[static_cast<std::size_t>(*index)]);
            }
        }
        if (result.size() == points.size()) {
            points = std::move(result);
            return;
        }
    }
    std::mt19937 generator{std::random_device{}()};
    std::shuffle(points.begin(), points.end(), generator);
}

std::shared_ptr<::cw::lib::Object> group_object(const ::cw::lib::Value& value,
                                                const std::shared_ptr<::cw::lib::Object>& fallback)
{
    if (const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage()); object && *object)
        return *object;
    return fallback;
}

void invalidate(Transition& transition, const ::cw::lib::Rect& rect)
{
    transition.m_dirty = std::max<::cw::lib::Integer>(1, transition.m_dirty);
    if (transition.m_ui_backend) transition.m_ui_backend->invalidate(transition, rect);
}

std::shared_ptr<::cw::lib::Surface> capture_current_screen()
{
    auto image = std::make_shared<::cw::lib::Surface>();
    image->size = {scale_value(std::get<0>(::cw::g_SIZE_AREA)),
                   scale_value(std::get<1>(::cw::g_SIZE_AREA))};
    const auto pixels = static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, image->size.width)) *
                        static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, image->size.height));
    image->rgba.resize(pixels * 4u, 0);
    for (std::size_t index = 3; index < image->rgba.size(); index += 4) image->rgba[index] = 255;
    // Python と同様、新背景の読み込み前に cwpy.background.image
    // を取得し、現在の前景・カードを合成する。 不
    // 透明なゼロ画像から始めると、すべての画面効果
    // が前のフレームではなく黒から始まってしまう。
    if (::cw::g_cwpy && ::cw::g_cwpy->m_background) {
        const auto& background = static_cast<::cw::sprite::base::CWPySprite&>(
            *::cw::g_cwpy->m_background);
        if (background.m_image)
            alpha_blit(image, background.m_image, background.m_rect.x, background.m_rect.y);
    }
    const auto cardgrp = ::cw::g_cwpy
        ? group_object(::cw::g_cwpy->m_cardgrp, ::cw::thread::CWPy::s_cardgrp)
        : ::cw::thread::CWPy::s_cardgrp;
    ::cw::sprite::background::layered_draw_ex(cardgrp, image);
    const auto topgrp = ::cw::g_cwpy
        ? group_object(::cw::g_cwpy->m_topgrp, ::cw::thread::CWPy::s_topgrp)
        : ::cw::thread::CWPy::s_topgrp;
    for (const auto& sprite : ::cw::sprite::background::sprites_from_layer(topgrp, ::cw::g_LAYER_JPY_TEMPORAL))
        if (sprite && sprite->image)
            alpha_blit(image, sprite->image, sprite->rect.x, sprite->rect.y);
    return image;
}
}
void set_runtime_context(TransitionRuntimeContext* context){g_runtime=context;}
void Transition::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed)
{
    initialize(std::move(arg_bgscr), ::cw::lib::PythonInteger(arg_speed));
}

void Transition::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed)
{
    if (!arg_bgscr) throw std::invalid_argument("transition surface");
    CWPySprite::initialize({});
    m_image = std::move(arg_bgscr);
    m_rect = {0, 0, m_image->size.width, m_image->size.height};
    if (m_image->size.width > 0 && m_image->size.height > 0) {
        const auto pixels = static_cast<std::size_t>(m_image->size.width) *
                            static_cast<std::size_t>(m_image->size.height) * 4u;
        if (m_image->rgba.size() < pixels) m_image->rgba.resize(pixels);
    }
    m_status = "hidden";
    m_frame = 0;
    m_speed = arg_speed;
    m_global_alpha = 255;
    invalidate(*this, m_rect);
}

void Transition::update_scale()
{
    // Python と同様、このフックは意図的に空にする。 画面
    // 倍率の変更時は、背景の所有者が新しい取得画像を供給する。
}

void Transition::clear()
{
    const auto old_rect = m_rect;
    auto cx=m_rect.x+m_rect.width/2,cy=m_rect.y+m_rect.height/2;m_image=std::make_shared<::cw::lib::Surface>();m_rect={cx,cy,0,0};m_status="hidden";m_global_alpha=255;
    invalidate(*this, old_rect);
}

void Transition::update(const ::cw::lib::Arguments& arg_args, const ::cw::lib::KeywordArguments& arg_kwargs)
{
    (void)arg_args;
    (void)arg_kwargs;
    // Python と同様、update_<status> に振り分ける。 ハンドラーがある
    // のは transition だけで、hidden など他の状態では意図的に何もしない。
    if (m_status == "transition") update_transition();
}

void Transition::update_transition()
{
    clear();
}

void Blinds::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed)
{
    initialize(std::move(arg_bgscr), ::cw::lib::PythonInteger(arg_speed));
}

void Blinds::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed)
{
    Transition::initialize(std::move(arg_bgscr), arg_speed);
    convert_capture_alpha(m_image);
    m_variation = 11 - m_speed;
    m_num_split = 30;
    const auto width = std::get<0>(::cw::g_SIZE_GAME);
    const auto height = std::get<1>(::cw::g_SIZE_GAME);
    m_w_blinds = width / m_num_split;
    m_rect_blinds = {0, 0, m_w_blinds, height};
    m_poslist.clear();
    for (::cw::lib::Integer n = 0; n < m_num_split + 2; ++n)
        m_poslist.push_back({n * m_w_blinds, 0});
}

void Blinds::update_transition()
{
    const auto current_frame = get_frame();
    m_frame = current_frame;
    // Py の整数乗算・切り下げ除算を、ネイティブ Rect の
    // 境界まで正確に行う。 表現できない形は例外とし、速度自体
    // は切り捨て・折り返し・設定の既定値への置き換えをしない。
    const auto exact_width = (current_frame * m_variation).floor_divide(5);
    if (exact_width == m_rect_blinds.width) return;
    // pygame の Rect.size は 64 ビット環境でも符号付
    // き 32 ビットの寸法を受け付ける。 共通 Rect はより広いが、
    // 代入前に元の API の境界を確認し、失敗時は前の矩形を維持する。
    if (exact_width < std::numeric_limits<std::int32_t>::min() ||
        exact_width > std::numeric_limits<std::int32_t>::max())
        throw std::out_of_range("Blinds: invalid pygame rectangle dimension");
    const auto width = exact_width.to_integer();
    m_rect_blinds.width = width;
    for (const auto& position : m_poslist) {
        const auto exact_x = ::cw::lib::PythonInteger(position.x) - exact_width;
        // Py と同様、塗る前に保持中の Rect の topleft を
        // 設定する。 pygame の符号付き 32 ビット範囲外の座標は
        // 幅の代入後に失敗し、前の位置とそれまで成功した塗りを維持する。
        if (exact_x < std::numeric_limits<std::int32_t>::min() ||
            exact_x > std::numeric_limits<std::int32_t>::max() ||
            position.y < std::numeric_limits<std::int32_t>::min() ||
            position.y > std::numeric_limits<std::int32_t>::max())
            throw std::out_of_range("Blinds: invalid pygame rectangle position");
        m_rect_blinds.x = exact_x.to_integer();
        m_rect_blinds.y = position.y;
        auto rect = m_rect_blinds;
        rect.x = scale_value(rect.x);
        rect.y = scale_value(rect.y);
        rect.width = scale_value(rect.width);
        rect.height = scale_value(rect.height);
        clear_rect(m_image, rect, m_changecolor);
    }
    invalidate(*this, m_rect);
    if (width >= m_w_blinds) {
        m_frame = 0;
        m_status = "hidden";
    }
}

void Fade::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed)
{
    initialize(std::move(arg_bgscr), ::cw::lib::PythonInteger(arg_speed));
}

void Fade::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed)
{
    Transition::initialize(std::move(arg_bgscr),arg_speed);
    // Py の Fade は SRCALPHA でない取得画像を維持して set_alpha()
    // を変更する。 Blinds/Dissolve はピクセルアルファを
    // 変えるため、共通 Transition 初期化で不透明と指定しない。
    m_image->known_opaque = true;
    m_variation = (11 - m_speed) * 2;
    // Py と同様、速度を UI の 0～10 に制限しない。 負の変化量も有効
    // で、ここではゼロだけがゼロ除算となる。Python の切り下げ除算を使う。
    if (m_variation == 0) throw std::domain_error("Fade variation is zero");
    m_m_frame = ::cw::lib::PythonInteger(255).floor_divide(m_variation).to_integer();
    m_global_alpha = 255;
    m_image->surface_alpha = 255;
}

void Fade::update_transition()
{
    const auto current_frame = get_frame_integer();
    // Py と同様、初期化時の間隔ゼロは許し、最初のフレーム
    // も含め、 更新時にその間隔で除算する時点で例外にする。
    if (m_m_frame == 0) throw std::domain_error("Fade frame interval is zero");
    const auto raw_alpha = ::cw::lib::PythonInteger(255) -
        ::cw::lib::PythonInteger::from_truncated_real(
            static_cast<double>(m_frame) / static_cast<double>(m_m_frame) * 255.0);
    // Py と同様、int(float) の後に範囲を制限し、その後 pygame
    // の符号付き整数入力を確認する。 pygame 1.9.6 は過大な
    // 正数の変換失敗でも、例外の前に画像アルファをゼロへ変える。 その部
    // 分変更は維持するが、フレームを進めたり更新完了とみなしたりしない。
    if (raw_alpha > std::numeric_limits<::cw::lib::Integer>::max()) {
        if (m_image) m_image->surface_alpha = 0;
        throw std::out_of_range("Fade: pygame surface alpha input overflow");
    }
    const auto alpha = raw_alpha <= 0 ? ::cw::lib::Integer{0} : raw_alpha.to_integer();
    m_global_alpha = alpha;
    // pygame の Surface.set_alpha と同様、入力を範囲内に収める。 有効な
    // 負の間隔でアルファが 255 を超える場合も、uint8_t へ折り返して画像を変えない。
    if (m_image) m_image->surface_alpha = static_cast<std::uint8_t>(
        std::clamp<::cw::lib::Integer>(alpha, 0, 255));
    invalidate(*this, m_rect);
    if (alpha <= 0) {
        m_frame = 0;
        m_start_animation = 0;
        m_status = "hidden";
    } else {
        // Py の順序どおり、set_alpha の後にだけ保存する。 Fade
        // が完了した場合は中間フレームが Integer を超えても直接ゼロへ
        // 戻し、 継続するネイティブスプライトでは固定幅の保存カウンターを使う。
        m_frame = current_frame.to_integer();
    }
}

void PixelDissolve::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::Integer arg_speed)
{
    initialize(std::move(arg_bgscr), ::cw::lib::PythonInteger(arg_speed));
}

void PixelDissolve::initialize(std::shared_ptr<::cw::lib::Surface> arg_bgscr, ::cw::lib::PythonInteger arg_speed)
{
    Transition::initialize(std::move(arg_bgscr), arg_speed);
    convert_capture_alpha(m_image);
    m_variation = (11 - m_speed) * 15;
    m_sec_w_noscale = m_sec_h_noscale = 10;
    m_sec_w = scale_value(m_sec_w_noscale);
    m_sec_h = scale_value(m_sec_h_noscale);
    if (!m_sec_w || !m_sec_h) throw std::domain_error("section size zero");
    m_rect_sec = {0, 0, m_sec_w, m_sec_h};
    m_poslist.clear();
    const auto width = scale_value(std::get<0>(::cw::g_SIZE_GAME));
    const auto height = scale_value(std::get<1>(::cw::g_SIZE_GAME));
    for (::cw::lib::Integer x = 0; x < width / m_sec_w + 1; ++x)
        for (::cw::lib::Integer y = 0; y < height / m_sec_h + 1; ++y)
            m_poslist.push_back({x * m_sec_w, y * m_sec_h});
    shuffle_points(m_poslist);
}

void PixelDissolve::update_transition()
{
    const auto current_frame = get_frame();
    const auto count = m_variation * (current_frame - m_frame);
    // Py と同様、有限の位置一覧が空になったら打ち切る。 作業量
    // だけをその一覧長に制限し、変化量・件数の正確な値は維持する。
    const auto available = static_cast<::cw::lib::Integer>(m_poslist.size());
    const auto work = count <= 0 ? ::cw::lib::Integer{0}
        : count >= available ? available : count.to_integer();
    for (::cw::lib::Integer i = 0; i < work; ++i) {
        const auto position = m_poslist.back();
        m_poslist.pop_back();
        auto rect = m_rect_sec;
        rect.x = position.x;
        rect.y = position.y;
        clear_rect(m_image, rect, m_changecolor);
    }
    if (count > 0) invalidate(*this, m_rect);
    m_frame = current_frame;
    if (m_poslist.empty()) {
        m_frame = 0;
        m_start_animation = 0;
        m_status = "hidden";
    }
}

std::optional<std::shared_ptr<::cw::sprite::transition::Transition>> get_transition(std::tuple<::cw::lib::String, ::cw::lib::Value> arg_name_and_speed)
{
    auto [name, speed_value] = std::move(arg_name_and_speed);
    if (name == "Default") name = configured_name();

    ::cw::lib::PythonInteger speed = configured_speed();
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&speed_value.storage())) {
        speed = *integer;
    } else if (const auto* boolean = std::get_if<bool>(&speed_value.storage())) {
        // Python の bool は int の派生型なので、True/False も有効な速度として扱う。
        speed = *boolean ? 1 : 0;
    } else if (const auto integer = ::cw::lib::python_integer_object(speed_value)) {
        speed = *integer;
    }
    if (name == "None" || speed == 0) return std::nullopt;
    if (name != "Transition" && name != "Fade" && name != "PixelDissolve" && name != "Blinds") return std::nullopt;

    auto image = g_runtime && g_runtime->capture_background
        ? g_runtime->capture_background() : nullptr;
    if (!image) image = capture_current_screen();

    if (name == "Transition") {
        auto transition = std::make_shared<Transition>();
        transition->initialize(std::move(image), speed);
        return transition;
    }
    if (name == "Fade") {
        auto transition = std::make_shared<Fade>();
        transition->initialize(std::move(image), speed);
        return transition;
    }
    if (name == "PixelDissolve") {
        auto transition = std::make_shared<PixelDissolve>();
        transition->initialize(std::move(image), speed);
        return transition;
    }
    auto transition = std::make_shared<Blinds>();
    transition->initialize(std::move(image), speed);
    return transition;
}

void main()
{
}

}

