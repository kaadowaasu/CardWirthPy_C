#include "cw/lib/ui_surface.hpp"
#include "cw/lib/simd_image.hpp"

#include "cw/cw.hpp"
#include "cw/image.hpp"
#include "cw/imageretouch.hpp"
#include "cw/setting.hpp"
#include "cw/thread.hpp"
#include "cw/util.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <map>
#include <mutex>
#include <tuple>
#include <SDL3/SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cw::lib::ui {
namespace {

std::atomic<::cw::lib::Real> captured_system_ui_scale{0.0};

struct AnimationDependency {
    std::shared_ptr<::cw::lib::Bitmap> bitmap;
    ::cw::lib::Rect rect{};
    ::cw::lib::Integer frame_id{};
};

struct SurfaceAnimationState {
    std::weak_ptr<::cw::lib::Surface> destination;
    std::vector<AnimationDependency> dependencies;
};

std::mutex surface_animation_mutex;
std::map<const ::cw::lib::Surface*, SurfaceAnimationState> surface_animations;

void prune_expired_surface_animations_locked()
{
    std::erase_if(surface_animations, [](const auto& item) {
        return item.second.destination.expired();
    });
}

void forget_surface_animations(const std::shared_ptr<::cw::lib::Surface>& surface)
{
    if (!surface) return;
    const std::scoped_lock lock(surface_animation_mutex);
    surface_animations.erase(surface.get());
}

void transfer_surface_animations(const std::shared_ptr<::cw::lib::Surface>& source,
                                 const std::shared_ptr<::cw::lib::Surface>& destination)
{
    if (!source || !destination || source.get() == destination.get()) return;
    const std::scoped_lock lock(surface_animation_mutex);
    const auto found = surface_animations.find(source.get());
    if (found == surface_animations.end()) return;
    auto state = std::move(found->second);
    surface_animations.erase(found);
    state.destination = destination;
    surface_animations[destination.get()] = std::move(state);
}

std::vector<AnimationDependency> animation_dependencies(
    const std::shared_ptr<::cw::lib::Surface>& surface)
{
    if (!surface) return {};
    const std::scoped_lock lock(surface_animation_mutex);
    // ツールチップなど一時的な合成描画面には、RetainedSurface
    // のリセットフックがない。 アニメーション登録表への問い合
    // わせ時に、無効になった出力先をすべて整理し、 強参照の Bitmap
    // 依存がプロセス終了まで蓄積しないようにする。
    prune_expired_surface_animations_locked();
    const auto found = surface_animations.find(surface.get());
    if (found == surface_animations.end()) return {};
    if (found->second.destination.expired()) {
        surface_animations.erase(found);
        return {};
    }
    return found->second.dependencies;
}

::cw::lib::Rect mapped_dependency_rect(const ::cw::lib::Rect source_rect,
                                        const ::cw::lib::Size source_size,
                                        const ::cw::lib::Integer x,
                                        const ::cw::lib::Integer y,
                                        const ::cw::lib::Integer width,
                                        const ::cw::lib::Integer height)
{
    const auto output_width = width > 0 ? width : source_size.width;
    const auto output_height = height > 0 ? height : source_size.height;
    if (source_size.width <= 0 || source_size.height <= 0) return {};
    const auto sx = static_cast<::cw::lib::Real>(output_width) / source_size.width;
    const auto sy = static_cast<::cw::lib::Real>(output_height) / source_size.height;
    const auto left = x + static_cast<::cw::lib::Integer>(std::floor(source_rect.x * sx));
    const auto top = y + static_cast<::cw::lib::Integer>(std::floor(source_rect.y * sy));
    const auto right = x + static_cast<::cw::lib::Integer>(std::ceil((source_rect.x + source_rect.width) * sx));
    const auto bottom = y + static_cast<::cw::lib::Integer>(std::ceil((source_rect.y + source_rect.height) * sy));
    return {left, top, std::max<::cw::lib::Integer>(0, right - left),
            std::max<::cw::lib::Integer>(0, bottom - top)};
}

void register_animation_dependency(const std::shared_ptr<::cw::lib::Surface>& destination,
                                   AnimationDependency dependency)
{
    const auto& bitmap = dependency.bitmap;
    if (!destination || !bitmap || dependency.rect.width <= 0 || dependency.rect.height <= 0) return;
    const std::scoped_lock lock(surface_animation_mutex);
    prune_expired_surface_animations_locked();
    if (const auto found=surface_animations.find(destination.get()); found!=surface_animations.end()) {
        const auto registered=found->second.destination.lock();
        if(!registered || registered.get()!=destination.get()) surface_animations.erase(found);
    }
    auto& state = surface_animations[destination.get()];
    state.destination = destination;
    const auto duplicate = std::find_if(state.dependencies.begin(), state.dependencies.end(),
        [&](const AnimationDependency& item) {
            const auto& existing = item.bitmap;
            return existing && existing.get() == bitmap.get() &&
                item.rect.x == dependency.rect.x && item.rect.y == dependency.rect.y &&
                item.rect.width == dependency.rect.width && item.rect.height == dependency.rect.height;
        });
    if (duplicate == state.dependencies.end()) state.dependencies.push_back(std::move(dependency));
    else duplicate->frame_id = dependency.frame_id;
}

bool surface_animation_changed(const std::shared_ptr<::cw::lib::Surface>& surface,
                               const bool consume)
{
    const auto dependencies = animation_dependencies(surface);
    bool changed = false;
    std::vector<std::pair<const ::cw::lib::Bitmap*, ::cw::lib::Integer>> frames;
    for (const auto& dependency : dependencies) {
        const auto& bitmap = dependency.bitmap;
        if (!bitmap) continue;
        const auto frame = ::cw::util::animated_wxbmp_frame_id(bitmap);
        if (frame != dependency.frame_id) changed = true;
        frames.emplace_back(bitmap.get(), frame);
    }
    if (consume && changed && surface) {
        const std::scoped_lock lock(surface_animation_mutex);
        if (const auto found = surface_animations.find(surface.get()); found != surface_animations.end()) {
            for (auto& dependency : found->second.dependencies) {
                const auto& bitmap = dependency.bitmap;
                if (!bitmap) continue;
                if (const auto frame = std::find_if(frames.begin(), frames.end(),
                        [&](const auto& item) { return item.first == bitmap.get(); }); frame != frames.end())
                    dependency.frame_id = frame->second;
            }
            std::erase_if(found->second.dependencies,
                [](const AnimationDependency& item) { return !item.bitmap; });
        }
    }
    return changed;
}

template <typename T>
std::shared_ptr<T> object_value(const ::cw::lib::Value& value)
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&value.storage());
    if (!object || !*object) return {};
    return std::dynamic_pointer_cast<T>(*object);
}

} // namespace

SystemSelectionColours system_selection_colours()
{
#ifdef _WIN32
    const auto colour = [](const int index, const ::cw::lib::Color fallback) {
        const COLORREF value = GetSysColor(index);
        return value == CLR_INVALID ? fallback : ::cw::lib::Color{
            static_cast<std::uint8_t>(GetRValue(value)),
            static_cast<std::uint8_t>(GetGValue(value)),
            static_cast<std::uint8_t>(GetBValue(value)), 255};
    };
    return {colour(COLOR_HIGHLIGHT, {0,120,215,255}),
            colour(COLOR_HIGHLIGHTTEXT, {255,255,255,255})};
#else
    return {};
#endif
}

void RetainedSurface::reset()
{
    forget_surface_animations(surface);
    surface.reset(); base.reset(); dirty.clear(); damage.clear(); components.clear();
    component_signatures.clear(); component_hits.clear(); full = true; revision = 0;
}

void RetainedSurface::invalidate_all()
{
    dirty.clear(); full = true;
}

void RetainedSurface::invalidate(::cw::lib::Rect rect)
{
    if (rect.width <= 0 || rect.height <= 0) return;
    // 接する領域をまとめ、移動する強調表示や動画タイルの更新を
    // 一つの転送・再描画として扱う。 小さな矩形を蓄積しない。
    for (auto it = dirty.begin(); it != dirty.end();) {
        const auto separated = rect.x + rect.width < it->x || it->x + it->width < rect.x ||
                               rect.y + rect.height < it->y || it->y + it->height < rect.y;
        if (separated) { ++it; continue; }
        const auto left = std::min(rect.x, it->x);
        const auto top = std::min(rect.y, it->y);
        const auto right = std::max(rect.x + rect.width, it->x + it->width);
        const auto bottom = std::max(rect.y + rect.height, it->y + it->height);
        rect = {left, top, right - left, bottom - top};
        it = dirty.erase(it);
    }
    dirty.push_back(rect);
}

void RetainedSurface::set_component(::cw::lib::String key, ::cw::lib::Rect rect)
{
    if (key.empty()) return;
    if (rect.width <= 0 || rect.height <= 0) components.erase(key);
    else components[std::move(key)] = rect;
}

bool RetainedSurface::component_changed(::cw::lib::String key,
                                        ::cw::lib::Rect rect,
                                        const std::uint64_t signature)
{
    if (key.empty() || rect.width <= 0 || rect.height <= 0) return false;
    const auto component_key = key;
    const auto old_rect = components.find(key);
    const auto old_signature = component_signatures.find(key);
    const bool geometry_changed = old_rect == components.end() ||
        old_rect->second.x != rect.x || old_rect->second.y != rect.y ||
        old_rect->second.width != rect.width || old_rect->second.height != rect.height;
    const bool changed = full || geometry_changed ||
        old_signature == component_signatures.end() ||
        old_signature->second != signature;
    if (changed) {
        if (old_rect != components.end() && geometry_changed) invalidate(old_rect->second);
        invalidate(rect);
    }
    components[std::move(key)] = rect;
    component_signatures[component_key] = signature;
    return changed;
}

bool RetainedSurface::invalidate_component(const ::cw::lib::String& key)
{
    const auto found = components.find(key);
    if (found == components.end()) return false;
    invalidate(found->second);
    return true;
}

bool RetainedSurface::remove_component(const ::cw::lib::String& key)
{
    const auto found=components.find(key);
    if(found==components.end()) return false;
    invalidate(found->second);
    components.erase(found);
    component_signatures.erase(key);
    component_hits.erase(key);
    return true;
}

std::optional<::cw::lib::Rect> RetainedSurface::component(const ::cw::lib::String& key) const
{
    const auto found = components.find(key);
    return found == components.end() ? std::nullopt : std::optional<::cw::lib::Rect>{found->second};
}

void RetainedSurface::set_component_hits(
    ::cw::lib::String key, std::vector<ComponentHitRegion> regions)
{
    if (key.empty()) return;
    if (regions.empty()) component_hits.erase(key);
    else component_hits[std::move(key)] = std::move(regions);
}

std::optional<::cw::lib::Integer> RetainedSurface::component_hit(
    const ::cw::lib::String& key, const ::cw::lib::Real x,
    const ::cw::lib::Real y) const
{
    const auto found = component_hits.find(key);
    if (found == component_hits.end() || !std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;
    for (auto it = found->second.rbegin(); it != found->second.rend(); ++it) {
        const auto& rect = it->rect;
        if (it->enabled && x >= rect.x && y >= rect.y &&
            x < rect.x + rect.width && y < rect.y + rect.height)
            return it->value;
    }
    return std::nullopt;
}

bool RetainedSurface::needs_full(::cw::lib::Size size) const
{
    return full || !surface || !base || surface->size.width != size.width ||
           surface->size.height != size.height;
}

void RetainedSurface::allocate(::cw::lib::Size size, ::cw::lib::Color color)
{
    forget_surface_animations(surface);
    surface = std::make_shared<::cw::lib::Surface>();
    surface->size = {std::max<::cw::lib::Integer>(1, size.width),
                     std::max<::cw::lib::Integer>(1, size.height)};
    const auto count = static_cast<std::size_t>(surface->size.width) * surface->size.height;
    surface->rgba.resize(count * 4U);
    ::cw::lib::simd_image::fill_rgba(surface->rgba.data(), count,
                                     color.r, color.g, color.b, color.a);
    base.reset(); dirty.clear(); damage.clear(); components.clear();
    component_signatures.clear(); component_hits.clear(); full = true;
}

void RetainedSurface::capture_base()
{
    base = surface ? std::make_shared<::cw::lib::Surface>(*surface) : nullptr;
}

void RetainedSurface::restore_dirty()
{
    if (!surface || !base || surface->size.width != base->size.width ||
        surface->size.height != base->size.height) return;
    if (dirty.empty()) return;
    // 公開した表示は不変のスナップショットとする。通常はホ
    // ストが前フレームを転送・解放済みなのでコピーは不要
    // である。 テスト・入れ子のダイアログ・非同期メディ
    // アが保持している場合は、更新行へ触る前に分離する。
    if (surface.use_count() > 1) {
        const auto previous = surface;
        surface = std::make_shared<::cw::lib::Surface>(*surface);
        transfer_surface_animations(previous, surface);
    }
    for (const auto& rect : dirty) {
        const auto left = std::max<::cw::lib::Integer>(0, rect.x);
        const auto top = std::max<::cw::lib::Integer>(0, rect.y);
        const auto right = std::min(surface->size.width, rect.x + rect.width);
        const auto bottom = std::min(surface->size.height, rect.y + rect.height);
        if (right <= left || bottom <= top) continue;
        for (auto y = top; y < bottom; ++y) {
            const auto offset = static_cast<std::size_t>((y * surface->size.width + left) * 4);
            const auto bytes = static_cast<std::size_t>(std::max<::cw::lib::Integer>(0, right - left)) * 4U;
            if (offset + bytes <= surface->rgba.size() && offset + bytes <= base->rgba.size())
                std::copy_n(base->rgba.begin() + static_cast<std::ptrdiff_t>(offset), bytes,
                            surface->rgba.begin() + static_cast<std::ptrdiff_t>(offset));
        }
    }
}

void RetainedSurface::finish()
{
    damage = full && surface
        ? std::vector<::cw::lib::Rect>{{0, 0, surface->size.width, surface->size.height}}
        : dirty;
    if (!damage.empty()) ++revision;
    dirty.clear(); full = false;
}

bool RetainedSurface::animated_content_changed() const
{
    return surface_animation_changed(surface, false);
}

bool RetainedSurface::consume_animated_content_change()
{
    if (!surface_animation_changed(surface, true)) return false;
    // 動画フレームの変更は実際のモデル変更として扱い、合成を一度再
    // 構築してマスク・ラベル・重ね合わせを元の順序で適用する。
    // 以前の固定タイマーと異なり、静的なリソースでは何もしない。
    invalidate_all();
    return true;
}

ScaledRetainedCanvas::ScaledRetainedCanvas(
    RetainedSurface& store, const ::cw::lib::Size logical,
    const ::cw::lib::Real output_scale, const ::cw::lib::Color background)
    : retained(store), scale(std::max<::cw::lib::Real>(1.0, output_scale)),
      logical_size(logical)
{
    const ::cw::lib::Size final_size{pixel(logical.width),pixel(logical.height)};
    full=retained.needs_full(final_size);
    if(full) retained.allocate(final_size,background);
    surface=retained.surface;
}

::cw::lib::Integer ScaledRetainedCanvas::pixel(const ::cw::lib::Integer value) const
{
    return static_cast<::cw::lib::Integer>(std::lround(value*scale));
}

::cw::lib::Rect ScaledRetainedCanvas::rect(const ::cw::lib::Rect logical) const
{
    const auto left=pixel(logical.x),top=pixel(logical.y);
    const auto right=pixel(logical.x+logical.width),bottom=pixel(logical.y+logical.height);
    return {left,top,right-left,bottom-top};
}

bool ScaledRetainedCanvas::component_changed(
    const ::cw::lib::String& key, const ::cw::lib::Rect logical,
    const std::uint64_t signature)
{
    return retained.component_changed(key,rect(logical),signature);
}

void ScaledRetainedCanvas::capture_base_and_restore()
{
    if(full) retained.capture_base();
    retained.restore_dirty();
    surface=retained.surface;
}

std::shared_ptr<::cw::lib::Surface> ScaledRetainedCanvas::layer(
    const ::cw::lib::Rect logical, const ::cw::lib::Color fill) const
{
    const auto target=rect(logical);
    auto result=std::make_shared<::cw::lib::Surface>();
    result->size={std::max<::cw::lib::Integer>(0,target.width),
                  std::max<::cw::lib::Integer>(0,target.height)};
    result->rgba.resize(static_cast<std::size_t>(result->size.width)*result->size.height*4U);
    for(std::size_t i=0;i<result->rgba.size();i+=4) {
        result->rgba[i]=fill.r;result->rgba[i+1]=fill.g;
        result->rgba[i+2]=fill.b;result->rgba[i+3]=fill.a;
    }
    result->draw_scale=scale;
    return result;
}

void ScaledRetainedCanvas::composite(
    const ::cw::lib::Rect logical,
    const std::shared_ptr<::cw::lib::Surface>& layer)
{
    if(!surface || !layer) return;
    const auto target=rect(logical);
    // 出力解像度の layer() はピクセルを一対一でコピー
    // し、 従来の論理レイヤーだけをここで再サンプリングする。
    if(std::abs(layer->draw_scale-scale)<1e-6 && layer->size.width==target.width &&
       layer->size.height==target.height) {
        blend_surface(surface,layer,target.x,target.y);
        return;
    }
    blend_surface(surface,layer,target.x,target.y,target.width,target.height);
}

void ScaledRetainedCanvas::finish()
{
    retained.finish();
    surface=retained.surface;
}

std::vector<WrappedLabelRow> wrap_label(const ::cw::lib::String& text,
    const FontStyle& font, ::cw::lib::Integer width, std::vector<InlineLabelImage> images)
{
    using I=::cw::lib::Integer;
    width=std::max<I>(1,width);
    // 外部からの不正な位置指定で、UTF-8 の文字を途中で分割しない。
    for (auto& image:images) {
        image.offset=std::min(image.offset,text.size());
        while (image.offset && image.offset<text.size() &&
            (static_cast<unsigned char>(text[image.offset])&0xc0U)==0x80U) --image.offset;
        image.size.width=std::max<I>(0,image.size.width);
    }
    std::stable_sort(images.begin(),images.end(),[](const auto& a,const auto& b){return a.offset<b.offset;});
    ::cw::imageretouch::Font metrics;
    metrics.initialize(font.face,-font.pixels,font.bold,font.italic);
    std::vector<WrappedLabelRow> rows;
    std::size_t pos=0,image_index=0;
    for (;;) {
        WrappedLabelRow row;
        row.offsets.push_back(pos); row.columns.push_back(0); row.positions.push_back(0);
        std::size_t break_column=0;
        I image_width=0;
        for (;;) {
            while (image_index<images.size() && images[image_index].offset<pos) ++image_index;
            // 画像は行内の描画区間として扱い、コピーする文字列に仮の文字を挿入しない。
            if (image_index<images.size() && images[image_index].offset==pos) {
                const auto& image=images[image_index];
                if (row.positions.back()+image.size.width>width && row.offsets.size()>1) break;
                row.images.push_back({image,row.positions.back()});
                row.positions.back()+=image.size.width;
                image_width+=image.size.width;
                ++image_index;
                continue;
            }
            if (pos==text.size() || text[pos]=='\r' || text[pos]=='\n') break;
            auto next=pos+1;
            while (next<text.size() && (static_cast<unsigned char>(text[next])&0xc0U)==0x80U) ++next;
            const bool tab=text[pos]=='\t';
            const auto candidate=row.text+(tab ? ::cw::lib::String(8-(row.offsets.size()-1)%8,' ') : text.substr(pos,next-pos));
            const I measured=std::get<0>(metrics.size(candidate))+image_width;
            if (measured>width && row.offsets.size()>1) {
                if (break_column) {
                    row.offsets.resize(break_column+1);
                    row.columns.resize(break_column+1);
                    row.positions.resize(break_column+1);
                    row.text.resize(row.columns.back());
                    pos=row.offsets.back();
                    const auto first_removed=std::find_if(row.images.begin(),row.images.end(),
                        [pos](const auto& image){return image.offset>=pos;});
                    for (auto i=first_removed;i!=row.images.end();++i)
                        if (i->offset==pos) row.positions.back()-=i->size.width;
                    row.images.erase(first_removed,row.images.end());
                    image_index=static_cast<std::size_t>(std::lower_bound(images.begin(),images.end(),pos,
                        [](const auto& image,std::size_t offset){return image.offset<offset;})-images.begin());
                }
                break;
            }
            row.text=candidate;
            row.offsets.push_back(next); row.columns.push_back(row.text.size()); row.positions.push_back(measured);
            if (tab || text[pos]==' ') break_column=row.offsets.size()-1;
            pos=next;
        }
        row.next=pos;
        if (pos<text.size() && (text[pos]=='\r' || text[pos]=='\n')) {
            const char newline=text[pos++];
            if (newline=='\r' && pos<text.size() && text[pos]=='\n') ++pos;
            row.next=pos;
            rows.push_back(std::move(row));
            continue;
        }
        rows.push_back(std::move(row));
        if (pos==text.size() && image_index==images.size()) break;
    }
    return rows;
}

::cw::lib::Integer win_i(const ::cw::lib::Integer value)
{
    const auto scaled = ::cw::wins(::cw::lib::Value(value));
    if (const auto* integer = std::get_if<::cw::lib::Integer>(&scaled.storage())) return *integer;
    return value;
}

::cw::lib::Integer SystemDialogMetrics::scaled(const ::cw::lib::Integer value) const
{
    const auto effective=std::isfinite(scale) && scale>0 ? scale : 1.0;
    return ::cw::_s_impl(::cw::lib::Value(value),effective).as_integer();
}

std::optional<::cw::lib::Integer> default_font_pixel_height()
{
#ifdef _WIN32
    // wxMSW と同様、12pt の既定フォント記述子を LOGPIXELSY から作る。 GetPixelSize()
    // は DEFAULT_GUI_FONT の文字寸法ではなく abs(lfHeight) を返す。
    const auto dc = GetDC(nullptr);
    if (!dc) return std::nullopt;
    const auto dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    if (dpi <= 0) return std::nullopt;
    return static_cast<::cw::lib::Integer>(std::lround(12.0 * dpi / 72.0));
#else
    // SDL の共通フォント処理には wx のフォント記述子に相当する基
    // 本操作がない。 欠けた計測をウィンドウ倍率や DEFAULT_GUI_FONT
    // の寸法で代用せず、対応するまで起動時の既定値を保つ。
    return std::nullopt;
#endif
}

SystemDialogMetrics system_dialog_metrics()
{
    SystemDialogMetrics result;
    result.scale=std::max<::cw::lib::Real>(1.0,::cw::g_dpi_level);
#ifdef _WIN32
    // SysMessage の DEFAULT_GUI_FONT について、ネイティ
    // ブのフォームを作らず、 メモリー DC で解決済みの書体と文字高を取得する。
    if (const auto dpi=GetDpiForSystem()) result.scale=dpi/96.0;
    result.font.pixels=std::max<::cw::lib::Integer>(1,result.scaled(12));
    result.line_height=std::max<::cw::lib::Integer>(1,result.scaled(14));
    if (const auto dc=CreateCompatibleDC(nullptr)) {
        const auto font=GetStockObject(DEFAULT_GUI_FONT);
        const auto previous=font?SelectObject(dc,font):nullptr;
        TEXTMETRICW metrics{};
        if (GetTextMetricsW(dc,&metrics)) {
            result.font.pixels=std::max<LONG>(1,metrics.tmHeight-metrics.tmInternalLeading);
            result.line_height=std::max<LONG>(1,metrics.tmHeight);
            result.font.bold=metrics.tmWeight>=FW_BOLD;
            result.font.italic=metrics.tmItalic!=0;
        }
        wchar_t face[128]{};
        if (GetTextFaceW(dc,128,face)>0) {
            const auto size=WideCharToMultiByte(CP_UTF8,0,face,-1,nullptr,0,nullptr,nullptr);
            if (size>1) {
                std::string text(static_cast<std::size_t>(size),'\0');
                if (WideCharToMultiByte(CP_UTF8,0,face,-1,text.data(),size,nullptr,nullptr)>0) {
                    text.pop_back(); result.font.face=std::move(text);
                }
            }
        }
        if (previous && previous!=HGDI_ERROR) SelectObject(dc,previous);
        DeleteDC(dc);
    }
#else
    // デスクトップ・モバイル・ブラウザーの密度は SDL が管理する。 フォ
    // ントの解決は既存の共通代替経路を使い、任意のフォントを必須にしない。
    if (const auto captured=captured_system_ui_scale.load(); captured>0) result.scale=captured;
    // SDL のディスプレイ問い合わせはメインスレッドで行う。 ALL の起動境
    // 界で Engine スレッド生成前に密度を取得し、ここではその値を読む。
    if (SDL_WasInit(SDL_INIT_VIDEO) && SDL_IsMainThread()) {
        const auto display=SDL_GetPrimaryDisplay();
        const auto scale=display?SDL_GetDisplayContentScale(display):0.0F;
        if (std::isfinite(scale) && scale>0) {
            result.scale=scale;captured_system_ui_scale.store(result.scale);
        }
    }
    result.font.pixels=std::max<::cw::lib::Integer>(1,result.scaled(12));
    ::cw::imageretouch::Font font;
    font.initialize(result.font.face,-result.font.pixels,false,false);
    result.line_height=std::max<::cw::lib::Integer>(1,font.get_height());
#endif
    return result;
}

void set_system_dialog_scale(const ::cw::lib::Real scale) noexcept
{
#ifndef _WIN32
    if (std::isfinite(scale) && scale>0) captured_system_ui_scale.store(scale);
#else
    (void)scale;
#endif
}

FontStyle configured_font(const ::cw::lib::String& type,
                          const ::cw::lib::Integer fallback_pixels,
                          const bool fallback_bold,
                          const bool fallback_italic,
                          const ::cw::lib::String& fallback_face)
{
    FontStyle result{fallback_face, std::max<::cw::lib::Integer>(1, fallback_pixels),
                     fallback_bold, fallback_italic};
    if (!::cw::g_cwpy || !::cw::g_cwpy->m_rsrc) return result;

    std::optional<::cw::setting::FontSettingValue> configured;
    if (::cw::g_cwpy->m_setting) {
        const auto types = ::cw::g_cwpy->m_setting->get_fonttypes();
        if (const auto found = types.find(type); found != types.end())
            configured = found->second;
    }
    const auto font = ::cw::g_cwpy->m_rsrc->get_fontfromtype(type, configured);
    if (!std::get<0>(font).empty()) result.face = std::get<0>(font);
    // CardWirthPy の get_wxfont() と同様、呼び出し
    // 側の明示的なピクセル寸法を使い、設定からは書体・装飾だけを読む。 設定
    // のピクセル値は Resource::create_fonts() が作
    // るゲームフォント用に限る。 ここにも適用すると、詳細設定でメッセージ・
    // カードの文字サイズを変えた時に固定のダイアログ配置まで変わってしまう。
    if (configured) {
        // Python の get_wxfont と同様、明示的な false も上書き指定として扱う。
        result.bold = (::cw::g_UP_SCR > 1.0 ? std::get<3>(font) : std::get<2>(font)).value_or(result.bold);
        result.italic = std::get<4>(font).value_or(result.italic);
    }
    return result;
}

FontStyle configured_font_scaled(const ::cw::lib::String& type,
                                 const ::cw::lib::Integer fallback_pixels,
                                 const bool fallback_bold,
                                 const bool fallback_italic,
                                 const ::cw::lib::String& fallback_face)
{
    // FontSettingValue の寸法とダイアログモデルの全代替
    // 値は、Python の get_wxfont() と同じ論理の等
    // 倍ピクセルである。 ここで代替値を UP_WIN で割ると、既定
    // サイズの時に下の win_i() が打ち消され、 ホスト内の Linux
    // ダイアログで二倍の配置に等倍の文字が表示されてしまう。
    auto result = configured_font(type, fallback_pixels, fallback_bold,
                                  fallback_italic, fallback_face);
    result.pixels = win_i(result.pixels);
    return result;
}

FontStyle configured_wx_font(const ::cw::lib::String& type,
                             const ::cw::lib::Integer logical_pixels,
                             const bool adjust_size,
                             const ::cw::lib::Integer pixel_adjustment,
                             const bool fallback_bold, const bool fallback_italic)
{
    auto result = configured_font_scaled(type, logical_pixels, fallback_bold, fallback_italic);
    result.pixels = std::max<::cw::lib::Integer>(1, result.pixels + pixel_adjustment - 1);
    if ((adjust_size || type == "tree" || type == "slider") &&
        result.pixels > 15 && result.pixels % 2 != 0) ++result.pixels;
    return result;
}

FontStyle configured_game_font(const ::cw::lib::String& type,
                               const ::cw::lib::Integer fallback_pixels,
                               const ::cw::lib::Real output_scale,
                               const bool fallback_bold, const bool fallback_italic,
                               const ::cw::lib::String& fallback_face)
{
    auto result = configured_font(type, fallback_pixels, fallback_bold, fallback_italic, fallback_face);
    if (::cw::g_cwpy && ::cw::g_cwpy->m_setting) {
        const auto types = ::cw::g_cwpy->m_setting->get_fonttypes();
        if (const auto found = types.find(type); found != types.end() && std::get<2>(found->second) > 0)
            result.pixels = std::get<2>(found->second);
    }
    const auto scale = std::isfinite(output_scale) && output_scale > 0.0 ? output_scale : 1.0;
    result.pixels = std::max<::cw::lib::Integer>(1,
        static_cast<::cw::lib::Integer>(std::lround(result.pixels * scale)));
    return result;
}

std::shared_ptr<::cw::lib::Surface> wins_surface(const std::shared_ptr<::cw::lib::Surface>& surface,
                                                 const ::cw::lib::Real loaded_scale)
{
    if (!surface) return {};
    const auto effective = ::cw::g_UP_WIN / std::max(loaded_scale, 1.0);
    if (effective <= 1.0 + 1e-9) return surface;
    const auto width = static_cast<::cw::lib::Integer>(std::lround(surface->size.width * effective));
    const auto height = static_cast<::cw::lib::Integer>(std::lround(surface->size.height * effective));
    auto result = ::cw::image::smoothscale_card(surface, {width, height}, true);
    if (result) {
        if (const auto source_bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(surface)) {
            auto bitmap = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*bitmap) = *result;
            ::cw::util::copy_animated_wxbmp_info(source_bitmap, bitmap);
            result = bitmap;
        } else {
            // 合成したダイアログ画像は、複数のアニメーションする子を含む場合が
            // ある。 ピクセルの拡縮に合わせて、その依存関係も拡縮・転送する。
            inherit_surface_animation(result, surface);
        }
    }
    return result;
}

namespace {
std::shared_ptr<::cw::lib::Surface> resource_surface_at_scale(
    const std::shared_ptr<::cw::lib::Surface>& surface,
    const ::cw::lib::Real target_scale)
{
    if (!surface) return {};
    const auto metadata = ::cw::imageretouch::get_surface_metadata(surface);
    const auto loaded_scale = std::isfinite(metadata.logical_scale) &&
            metadata.logical_scale > 0.0
        ? metadata.logical_scale : 1.0;
    const auto target = std::isfinite(target_scale) && target_scale > 0.0
        ? target_scale : 1.0;
    const auto effective = target / loaded_scale;
    if (std::abs(effective - 1.0) <= 1e-9) return surface;
    const auto width = std::max<::cw::lib::Integer>(1,
        static_cast<::cw::lib::Integer>(std::lround(surface->size.width * effective)));
    const auto height = std::max<::cw::lib::Integer>(1,
        static_cast<::cw::lib::Integer>(std::lround(surface->size.height * effective)));
    auto result = ::cw::image::smoothscale_card(
        surface, {width, height}, true);
    if (result) {
        if (const auto source_bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(surface)) {
            auto bitmap = std::make_shared<::cw::lib::Bitmap>();
            static_cast<::cw::lib::Surface&>(*bitmap) = *result;
            ::cw::util::copy_animated_wxbmp_info(source_bitmap, bitmap);
            result = bitmap;
        } else {
            inherit_surface_animation(result, surface);
        }
    }
    if (result) {
        auto result_metadata = metadata;
        result_metadata.logical_scale = target;
        ::cw::imageretouch::set_surface_metadata(result, result_metadata);
        result->scr_scale = target;
    }
    return result;
}
} // namespace

std::shared_ptr<::cw::lib::Surface> resource_surface_for_window(
    const std::shared_ptr<::cw::lib::Surface>& surface)
{
    return resource_surface_at_scale(surface, std::max<::cw::lib::Real>(::cw::g_UP_WIN, 1.0));
}

std::shared_ptr<::cw::lib::Surface> resource_surface_for_scale(
    const std::shared_ptr<::cw::lib::Surface>& surface, ::cw::lib::Real scale)
{
    return resource_surface_at_scale(surface, scale);
}

std::shared_ptr<::cw::lib::Surface> resource_surface_for_logical(
    const std::shared_ptr<::cw::lib::Surface>& surface)
{
    return resource_surface_at_scale(surface, 1.0);
}

std::shared_ptr<::cw::lib::Surface> resource_table_surface(const ::cw::lib::Value& table_value,
                                                           const ::cw::lib::String& key)
{
    const auto table = object_value<::cw::setting::ResourceTable>(table_value);
    // contains() も ResourceTable のキー欠落時
    // の遅延ローダーを呼ぶ。 実際にないキーで共有の既定値へ進むと、任
    // 意アイコンの枠に無関係なキャッシュ画像を描くため、それを避ける。
    if (!table || !table->contains(::cw::lib::Value(key))) return {};
    try {
        const auto item = table->get_item(::cw::lib::Value(key));
        if (const auto bitmap = object_value<::cw::lib::Bitmap>(item)) {
            auto copy = std::make_shared<::cw::lib::Bitmap>(*bitmap);
            return ::cw::util::copy_animated_wxbmp_info(bitmap, copy);
        }
        // 独自・注入されたリソースローダーは通常の Surface を返してもよい。 組み
        // 込みのダイアログ素材は、アニメーションの識別を保つため Bitmap を使う。
        if (const auto surface = object_value<::cw::lib::Surface>(item))
            return std::make_shared<::cw::lib::Surface>(*surface);
    } catch (...) {
    }
    return {};
}

std::shared_ptr<::cw::lib::Surface> resource_dialog_surface(const ::cw::lib::String& key)
{
    return ::cw::g_cwpy && ::cw::g_cwpy->m_rsrc
        ? resource_table_surface(::cw::g_cwpy->m_rsrc->m_dialogs, key)
        : std::shared_ptr<::cw::lib::Surface>{};
}

std::shared_ptr<::cw::lib::Surface> additional_controls_surface(const bool expanded)
{
    return resource_dialog_surface(expanded ? "HIDE_CONTROLS" : "SHOW_CONTROLS");
}

std::shared_ptr<::cw::lib::Surface> resource_dialog_background(const ::cw::lib::String& key)
{
    auto result = resource_dialog_surface(key);
    if (!result || result->size.width <= 1 || result->size.height <= 1) return {};
    ::cw::lib::simd_image::force_alpha(result->rgba.data(), result->rgba.size() / 4U, 255);
    result->has_colorkey = false;
    if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(result))
        ::cw::util::set_animated_wxbmp_opaque(bitmap);
    return result;
}

namespace {
// 出力先の描画倍率。Surface::draw_scale に従い、通常の描画面では 1 とする。
::cw::lib::Real destination_scale(const std::shared_ptr<::cw::lib::Surface>& surface) noexcept {
    if (!surface || !std::isfinite(surface->draw_scale) || surface->draw_scale <= 0.0) return 1.0;
    return surface->draw_scale;
}
bool scaled_destination(const std::shared_ptr<::cw::lib::Surface>& surface) noexcept {
    return std::abs(destination_scale(surface) - 1.0) > 1e-6;
}
::cw::lib::Integer to_pixel(const ::cw::lib::Integer value, const ::cw::lib::Real scale) noexcept {
    return static_cast<::cw::lib::Integer>(std::lround(value * scale));
}
::cw::lib::Rect intersect_surface_rect(const ::cw::lib::Rect rect,
                                     const ::cw::lib::Rect clip) noexcept {
    const auto left = std::max(rect.x, clip.x), top = std::max(rect.y, clip.y);
    const auto right = std::min(rect.x + rect.width, clip.x + clip.width);
    const auto bottom = std::min(rect.y + rect.height, clip.y + clip.height);
    return {left, top, std::max<::cw::lib::Integer>(0, right - left),
            std::max<::cw::lib::Integer>(0, bottom - top)};
}
} // namespace

void fill_rect(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::Rect rect,
               const ::cw::lib::Color color)
{
    if (!destination) return;
    if (scaled_destination(destination)) {
        // 各辺を独立して変換し、隣り合う論理矩形の枠・1px 線など
        // に隙間を作らず、少なくとも一デバイスピクセルの幅を保つ。
        const auto scale = destination_scale(destination);
        const auto left = to_pixel(rect.x, scale), top = to_pixel(rect.y, scale);
        auto right = to_pixel(rect.x + std::max<::cw::lib::Integer>(0, rect.width), scale);
        auto bottom = to_pixel(rect.y + std::max<::cw::lib::Integer>(0, rect.height), scale);
        if (rect.width > 0 && right <= left) right = left + 1;
        if (rect.height > 0 && bottom <= top) bottom = top + 1;
        rect = {left, top, right - left, bottom - top};
    }
    const auto left = std::max<::cw::lib::Integer>(0, rect.x);
    const auto top = std::max<::cw::lib::Integer>(0, rect.y);
    const auto right = std::min(destination->size.width, rect.x + std::max<::cw::lib::Integer>(0, rect.width));
    const auto bottom = std::min(destination->size.height, rect.y + std::max<::cw::lib::Integer>(0, rect.height));
    for (::cw::lib::Integer y = top; y < bottom; ++y)
        for (::cw::lib::Integer x = left; x < right; ++x) {
            const auto index = static_cast<std::size_t>((y * destination->size.width + x) * 4);
            destination->rgba[index] = color.r;
            destination->rgba[index + 1] = color.g;
            destination->rgba[index + 2] = color.b;
            destination->rgba[index + 3] = color.a;
        }
}

void blend_surface(const std::shared_ptr<::cw::lib::Surface>& destination,
                   const std::shared_ptr<::cw::lib::Surface>& source, ::cw::lib::Integer x,
                   ::cw::lib::Integer y, ::cw::lib::Integer width, ::cw::lib::Integer height,
                   std::optional<::cw::lib::Rect> clip)
{
    if (!destination || !source || source->size.width <= 0 || source->size.height <= 0) return;
    if (clip && (clip->width <= 0 || clip->height <= 0)) return;
    if (scaled_destination(destination)) {
        // 論理位置・寸法をデバイスピクセルへ変換する。 最終サイ
        // ズのラベルなど、既にこの倍率で描いた入力は一対一でコピ
        // ーし、論理寸法のアイコンは従来どおり一度だけ拡大する。
        const auto scale = destination_scale(destination);
        const auto source_scale = std::isfinite(source->draw_scale) && source->draw_scale > 0.0
            ? source->draw_scale : 1.0;
        if (width > 0 && height > 0) {
            width = to_pixel(x + width, scale) - to_pixel(x, scale);
            height = to_pixel(y + height, scale) - to_pixel(y, scale);
        } else if (std::abs(source_scale - scale) > 1e-6) {
            width = std::max<::cw::lib::Integer>(1, static_cast<::cw::lib::Integer>(
                std::lround(source->size.width * scale / source_scale)));
            height = std::max<::cw::lib::Integer>(1, static_cast<::cw::lib::Integer>(
                std::lround(source->size.height * scale / source_scale)));
        }
        x = to_pixel(x, scale);
        y = to_pixel(y, scale);
        if (clip) {
            const auto left = to_pixel(clip->x, scale), top = to_pixel(clip->y, scale);
            const auto right = to_pixel(clip->x + clip->width, scale);
            const auto bottom = to_pixel(clip->y + clip->height, scale);
            clip = ::cw::lib::Rect{left, top, right - left, bottom - top};
        }
    }
    const auto bounds = clip
        ? intersect_surface_rect({0, 0, destination->size.width, destination->size.height}, *clip)
        : ::cw::lib::Rect{0, 0, destination->size.width, destination->size.height};
    if (bounds.width <= 0 || bounds.height <= 0) return;
    const auto clipped_dependency = [&](AnimationDependency dependency) {
        if (clip) dependency.rect = intersect_surface_rect(dependency.rect, bounds);
        return dependency;
    };
    auto image = source;
    const auto nested_dependencies = animation_dependencies(source);
    if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(source);
        bitmap && ::cw::util::is_animated_wxbmp(bitmap)) {
        register_animation_dependency(destination, clipped_dependency({bitmap,
            {x, y, width > 0 ? width : source->size.width,
                   height > 0 ? height : source->size.height},
            ::cw::util::animated_wxbmp_frame_id(bitmap)}));
        image = ::cw::util::resolve_animated_wxbmp(bitmap);
    }
    for (auto dependency : nested_dependencies) {
        dependency.rect = mapped_dependency_rect(dependency.rect, source->size, x, y, width, height);
        register_animation_dependency(destination, clipped_dependency(std::move(dependency)));
    }
    if (width > 0 && height > 0 && (width != image->size.width || height != image->size.height)) {
        // wx と同様、透過色のピクセルは透明に描く。 拡縮で透過色が不
        // 透明にならないよう、フィルター前にマスクをアルファへ変換する。
        if (image && image->has_colorkey) {
            image = std::make_shared<::cw::lib::Surface>(*image);
            const auto [red, green, blue] = image->colorkey;
            ::cw::lib::simd_image::colorkey_alpha_zero(image->rgba.data(), image->rgba.size() / 4U,
                                                     red, green, blue);
            image->has_colorkey = false;
            image->has_pixel_alpha = true;
            image->known_opaque = false;
        }
        image = ::cw::image::smoothscale_card(image, {width, height}, true);
    }
    if (!image || image->rgba.size() < static_cast<std::size_t>(image->size.width) * image->size.height * 4) return;
    for (::cw::lib::Integer sy = 0; sy < image->size.height; ++sy) {
        const auto dy = y + sy;
        if (dy < bounds.y || dy >= bounds.y + bounds.height) continue;
        for (::cw::lib::Integer sx = 0; sx < image->size.width; ++sx) {
            const auto dx = x + sx;
            if (dx < bounds.x || dx >= bounds.x + bounds.width) continue;
            const auto si = static_cast<std::size_t>((sy * image->size.width + sx) * 4);
            const auto di = static_cast<std::size_t>((dy * destination->size.width + dx) * 4);
            if (image->has_colorkey) {
                const auto [red, green, blue] = image->colorkey;
                if (image->rgba[si] == red && image->rgba[si + 1] == green && image->rgba[si + 2] == blue)
                    continue;
            }
            const auto alpha = static_cast<unsigned>(image->rgba[si + 3]);
            if (!alpha) continue;
            if (alpha == 255) {
                destination->rgba[di] = image->rgba[si];
                destination->rgba[di + 1] = image->rgba[si + 1];
                destination->rgba[di + 2] = image->rgba[si + 2];
                destination->rgba[di + 3] = 255;
            } else {
                const auto inverse = 255U - alpha;
                const auto destination_alpha = static_cast<unsigned>(destination->rgba[di + 3]);
                const auto weight = alpha * 255U + destination_alpha * inverse;
                // アルファ非乗算形
                // 式のsource-over
                // 合成を、出力先
                // のアルファも
                // 含めて行う。
                for (std::size_t channel = 0; channel < 3; ++channel)
                    destination->rgba[di + channel] = static_cast<std::uint8_t>(
                        (image->rgba[si + channel] * alpha * 255U +
                         destination->rgba[di + channel] * destination_alpha * inverse) / weight);
                destination->rgba[di + 3] = static_cast<std::uint8_t>((weight + 127U) / 255U);
            }
        }
    }
}

void inherit_surface_animation(const std::shared_ptr<const ::cw::lib::Surface>& destination,
                               const std::shared_ptr<const ::cw::lib::Surface>& source)
{
    if (!destination || !source) return;
    // 入力元は読み取り専用として扱う。アニメーション情報を読み、元のビットマッ
    // プを保持するだけで、入力ピクセルは変更しない。 ダイアログの描画結果は意
    // 図的に shared_ptr<const Surface> で公開する。
    const auto mutable_destination = std::const_pointer_cast<::cw::lib::Surface>(destination);
    const auto mutable_source = std::const_pointer_cast<::cw::lib::Surface>(source);
    if (const auto bitmap = std::dynamic_pointer_cast<::cw::lib::Bitmap>(mutable_source);
        bitmap && ::cw::util::is_animated_wxbmp(bitmap))
        register_animation_dependency(mutable_destination, {bitmap,
            {0, 0, destination->size.width, destination->size.height},
            ::cw::util::animated_wxbmp_frame_id(bitmap)});
    for (auto dependency : animation_dependencies(mutable_source)) {
        dependency.rect = mapped_dependency_rect(dependency.rect, source->size, 0, 0,
            destination->size.width, destination->size.height);
        register_animation_dependency(mutable_destination, std::move(dependency));
    }
}

namespace {
// 性能のため文字の描画結果を保持する。Python では wx 自身
// が文字をキャッシュする。 従来は再描画ごとに各ラベルを描き直し、label_width()
// でも幅だけのために文字全体を描画していた。 設定タブ・説
// 明文などの全コントロールで使うため、解決済み書体をキーにし
// 、フォント表の変更時はキャッシュに一致しないようにする。
struct LabelCacheKey {
    ::cw::lib::String text, face;
    ::cw::lib::Integer size{}, r{}, g{}, b{};
    bool bold{}, italic{};
    bool operator<(const LabelCacheKey& other) const {
        return std::tie(text, face, size, r, g, b, bold, italic) <
            std::tie(other.text, other.face, other.size, other.r, other.g, other.b, other.bold, other.italic);
    }
};
struct LabelCache {
    std::mutex mutex;
    std::map<LabelCacheKey, std::pair<std::shared_ptr<const ::cw::lib::Surface>, std::uint64_t>> images;
    std::map<LabelCacheKey, ::cw::lib::Integer> widths;
    std::uint64_t clock{};
    std::size_t bytes{};
};
LabelCache& label_cache() { static LabelCache cache; return cache; }
constexpr std::size_t kLabelCacheBytes = 32U * 1024U * 1024U;
constexpr std::size_t kLabelWidthEntries = 8192;

LabelCacheKey label_key(const ::cw::lib::String& text, const ::cw::lib::Integer size, const bool bold,
                        const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                        const ::cw::lib::String& face, const bool italic)
{
    const auto [r, g, b] = color;
    return {text, ::cw::imageretouch::get_fontface(face), size, r, g, b, bold, italic};
}

std::shared_ptr<::cw::lib::Surface> render_label_uncached(const ::cw::lib::String& text,
    const ::cw::lib::Integer size, const bool bold,
    const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
    const ::cw::lib::String& face, const bool italic)
{
    auto font = std::make_shared<::cw::imageretouch::Font>();
    font->initialize(face, -std::max<::cw::lib::Integer>(1, size), bold, italic);
    return font->render(text, true, color);
}
}  // namespace

std::shared_ptr<::cw::lib::Surface> render_label(const ::cw::lib::String& text, const ::cw::lib::Integer size,
                                                 const bool bold,
                                                 const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                                                 const ::cw::lib::String& face, const bool italic)
{
    if (text.empty()) return {};
    auto key = label_key(text, size, bold, color, face, italic);
    auto& cache = label_cache();
    {
        std::scoped_lock lock(cache.mutex);
        if (const auto found = cache.images.find(key); found != cache.images.end()) {
            found->second.second = ++cache.clock;
            // 呼び出し側が draw_scale などを変更できるよう、結果はコピーを渡す。
            return std::make_shared<::cw::lib::Surface>(*found->second.first);
        }
    }
    auto rendered = render_label_uncached(text, size, bold, color, face, italic);
    if (!rendered) return rendered;
    // 通常のピクセルデータだけをコピーし、派生ビットマップ型はキャッシュしない。
    auto stored = std::make_shared<::cw::lib::Surface>(*rendered);
    std::scoped_lock lock(cache.mutex);
    const auto image_bytes = stored->rgba.size() + key.text.size() + 64U;
    if (image_bytes > kLabelCacheBytes / 4U) return rendered;
    auto [slot, inserted] = cache.images.try_emplace(std::move(key), stored, ++cache.clock);
    if (!inserted) return rendered;
    cache.bytes += image_bytes;
    while (cache.bytes > kLabelCacheBytes && cache.images.size() > 1) {
        auto oldest = cache.images.begin();
        for (auto it = cache.images.begin(); it != cache.images.end(); ++it)
            if (it->second.second < oldest->second.second) oldest = it;
        if (oldest == slot) break;
        cache.bytes -= std::min(cache.bytes, oldest->second.first->rgba.size() + oldest->first.text.size() + 64U);
        cache.images.erase(oldest);
    }
    return rendered;
}

::cw::lib::Integer label_width(const ::cw::lib::String& text, const ::cw::lib::Integer size, const bool bold,
                               const ::cw::lib::String& face, const bool italic)
{
    if (text.empty()) return 0;
    auto key = label_key(text, size, bold, {0, 0, 0}, face, italic);
    auto& cache = label_cache();
    {
        std::scoped_lock lock(cache.mutex);
        if (const auto found = cache.widths.find(key); found != cache.widths.end()) return found->second;
    }
    const auto rendered = render_label(text, size, bold, {0, 0, 0}, face, italic);
    const auto width = rendered ? rendered->size.width : 0;
    std::scoped_lock lock(cache.mutex);
    if (cache.widths.size() >= kLabelWidthEntries) cache.widths.clear();
    cache.widths.emplace(std::move(key), width);
    return width;
}

void draw_label(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::String text,
                const ::cw::lib::Integer x, const ::cw::lib::Integer y, const ::cw::lib::Integer size,
                const bool bold, const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                const ::cw::lib::String& face, const bool italic)
{
    if (scaled_destination(destination)) {
        // 等倍の画像を拡大せず、最終ピクセル
        // サイズで文字を描いて鮮明さを保つ。
        const auto scale = destination_scale(destination);
        auto rendered = render_label(text, std::max<::cw::lib::Integer>(1, to_pixel(size, scale)),
                                     bold, color, face, italic);
        if (rendered) rendered->draw_scale = scale;
        blend_surface(destination, rendered, x, y);
        return;
    }
    blend_surface(destination, render_label(text, size, bold, color, face, italic), x, y);
}

void draw_outline_label(const std::shared_ptr<::cw::lib::Surface>& destination, ::cw::lib::String text,
                        const ::cw::lib::Integer x, const ::cw::lib::Integer y, const ::cw::lib::Integer size,
                        const bool bold, const ::cw::lib::Color outline,
                        const std::tuple<::cw::lib::Integer, ::cw::lib::Integer, ::cw::lib::Integer> color,
                        const ::cw::lib::String& face, const bool italic)
{
    if (!destination || text.empty()) return;
    const auto outline_rgb = std::make_tuple(static_cast<::cw::lib::Integer>(outline.r),
                                             static_cast<::cw::lib::Integer>(outline.g),
                                             static_cast<::cw::lib::Integer>(outline.b));
    for (::cw::lib::Integer ox = -1; ox <= 1; ++ox)
        for (::cw::lib::Integer oy = -1; oy <= 1; ++oy)
            if (ox || oy) draw_label(destination, text, x + ox, y + oy, size, bold, outline_rgb, face, italic);
    draw_label(destination, std::move(text), x, y, size, bold, color, face, italic);
}

void draw_sunken_border(const std::shared_ptr<::cw::lib::Surface>& destination, const ::cw::lib::Rect rect)
{
    if (!destination) return;
    const auto right = rect.x + rect.width;
    const auto bottom = rect.y + rect.height;
    const ::cw::lib::Color shadow{128, 128, 128, 255};
    const ::cw::lib::Color highlight{255, 255, 255, 255};
    for (::cw::lib::Integer x = rect.x; x < right; ++x) {
        fill_rect(destination, {x, rect.y, 1, 1}, shadow);
        fill_rect(destination, {x, bottom - 1, 1, 1}, highlight);
    }
    for (::cw::lib::Integer y = rect.y; y < bottom; ++y) {
        fill_rect(destination, {rect.x, y, 1, 1}, shadow);
        fill_rect(destination, {right - 1, y, 1, 1}, highlight);
    }
}

void draw_raised_border(const std::shared_ptr<::cw::lib::Surface>& destination, const ::cw::lib::Rect rect)
{
    if (!destination || rect.width <= 0 || rect.height <= 0) return;
    const auto right = rect.x + rect.width;
    const auto bottom = rect.y + rect.height;
    const ::cw::lib::Color highlight{255, 255, 255, 255};
    const ::cw::lib::Color shadow{128, 128, 128, 255};
    for (::cw::lib::Integer x = rect.x; x < right; ++x) {
        fill_rect(destination, {x, rect.y, 1, 1}, highlight);
        fill_rect(destination, {x, bottom - 1, 1, 1}, shadow);
    }
    for (::cw::lib::Integer y = rect.y; y < bottom; ++y) {
        fill_rect(destination, {rect.x, y, 1, 1}, highlight);
        fill_rect(destination, {right - 1, y, 1, 1}, shadow);
    }
}

float button_corner_radius(const float width, const float height) noexcept
{
    // 等倍では高さ 24～30px のボタンに約 3px とし、拡大時は比例させる。
    return std::clamp(std::min(width, height) * 0.12F, 1.5F, 12.0F);
}

void draw_button_face(const std::shared_ptr<::cw::lib::Surface>& destination,
                      ::cw::lib::Rect rect, const ButtonFace face)
{
    if (!destination || rect.width <= 0 || rect.height <= 0) return;
    ::cw::lib::Real scale = 1.0;
    if (scaled_destination(destination)) {
        scale = destination_scale(destination);
        const auto left = to_pixel(rect.x, scale), top = to_pixel(rect.y, scale);
        auto right = to_pixel(rect.x + rect.width, scale);
        auto bottom = to_pixel(rect.y + rect.height, scale);
        if (right <= left) right = left + 1;
        if (bottom <= top) bottom = top + 1;
        rect = {left, top, right - left, bottom - top};
    }
    const ::cw::lib::Color fill = !face.enabled ? ::cw::lib::Color{245, 245, 245, 255}
        : face.pressed ? ::cw::lib::Color{204, 228, 247, 255}
                       : ::cw::lib::Color{253, 253, 253, 255};
    const ::cw::lib::Color frame = !face.enabled ? ::cw::lib::Color{220, 220, 220, 255}
        : (face.pressed || face.default_button) ? ::cw::lib::Color{0, 120, 215, 255}
                                                : ::cw::lib::Color{200, 200, 200, 255};
    const double frame_width = std::max(1.0, std::round(scale));
    const double radius = button_corner_radius(static_cast<float>(rect.width),
                                               static_cast<float>(rect.height));
    const double half_w = rect.width * 0.5, half_h = rect.height * 0.5;
    const double cx = rect.x + half_w, cy = rect.y + half_h;
    const auto left = std::max<::cw::lib::Integer>(0, rect.x);
    const auto top = std::max<::cw::lib::Integer>(0, rect.y);
    const auto right = std::min(destination->size.width, rect.x + rect.width);
    const auto bottom = std::min(destination->size.height, rect.y + rect.height);
    for (::cw::lib::Integer y = top; y < bottom; ++y)
        for (::cw::lib::Integer x = left; x < right; ++x) {
            // ピクセル中心から角丸矩形までの符号付き距離。
            const double qx = std::abs(x + 0.5 - cx) - (half_w - radius);
            const double qy = std::abs(y + 0.5 - cy) - (half_h - radius);
            const double outside = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0));
            const double distance = outside + std::min(std::max(qx, qy), 0.0) - radius;
            const double coverage = std::clamp(0.5 - distance, 0.0, 1.0);
            if (coverage <= 0.0) continue;
            // 枠の帯の内側は枠色、それより深い部分は背景色にする。
            const double inner = std::clamp(-distance - frame_width + 0.5, 0.0, 1.0);
            const auto index = static_cast<std::size_t>((y * destination->size.width + x) * 4);
            auto* pixel = destination->rgba.data() + index;
            const std::array<double, 3> colour{
                frame.r + (fill.r - frame.r) * inner,
                frame.g + (fill.g - frame.g) * inner,
                frame.b + (fill.b - frame.b) * inner};
            const double base_alpha = pixel[3] / 255.0;
            const double out_alpha = coverage + base_alpha * (1.0 - coverage);
            for (std::size_t channel = 0; channel < 3; ++channel) {
                const double blended = out_alpha > 0.0
                    ? (colour[channel] * coverage + pixel[channel] * base_alpha * (1.0 - coverage)) / out_alpha
                    : colour[channel];
                pixel[channel] = static_cast<std::uint8_t>(std::lround(std::clamp(blended, 0.0, 255.0)));
            }
            pixel[3] = static_cast<std::uint8_t>(std::lround(out_alpha * 255.0));
        }
}

void draw_checkbox(const std::shared_ptr<::cw::lib::Surface>& destination,
                   const ::cw::lib::Rect rect, const bool checked,
                   const bool enabled, const bool indeterminate)
{
    if (!destination || rect.width < 3 || rect.height < 3) return;
    const ::cw::lib::Color border = enabled
        ? ::cw::lib::Color{118, 118, 118, 255}
        : ::cw::lib::Color{172, 172, 172, 255};
    const bool marked = checked || indeterminate;
    const ::cw::lib::Color body = marked
        ? (enabled ? ::cw::lib::Color{0, 120, 215, 255}
                   : ::cw::lib::Color{166, 166, 166, 255})
        : (enabled ? ::cw::lib::Color{255, 255, 255, 255}
                   : ::cw::lib::Color{232, 232, 232, 255});
    fill_rect(destination, rect, border);
    fill_rect(destination, {rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2}, body);
    if (!marked) return;

    const ::cw::lib::Color mark{255, 255, 255, 255};
    if (indeterminate) {
        const auto thickness = std::max<::cw::lib::Integer>(1, rect.height / 7);
        fill_rect(destination,
                  {rect.x + 3, rect.y + (rect.height - thickness) / 2,
                   std::max<::cw::lib::Integer>(1, rect.width - 6), thickness}, mark);
        return;
    }

    const auto plot_line = [&](::cw::lib::Integer x0, ::cw::lib::Integer y0,
                               const ::cw::lib::Integer x1, const ::cw::lib::Integer y1) {
        const auto dx = std::abs(x1 - x0);
        const auto sx = x0 < x1 ? 1 : -1;
        const auto dy = -std::abs(y1 - y0);
        const auto sy = y0 < y1 ? 1 : -1;
        auto error = dx + dy;
        for (;;) {
            fill_rect(destination, {x0, y0, 1, 1}, mark);
            if (x0 == x1 && y0 == y1) break;
            const auto twice = 2 * error;
            if (twice >= dy) { error += dy; x0 += sx; }
            if (twice <= dx) { error += dx; y0 += sy; }
        }
    };
    const auto thickness = std::max<::cw::lib::Integer>(1, rect.height / 7);
    const auto start_x = rect.x + std::max<::cw::lib::Integer>(2, rect.width / 5);
    const auto start_y = rect.y + rect.height / 2;
    const auto middle_x = rect.x + rect.width / 2 - 1;
    const auto middle_y = rect.y + rect.height - std::max<::cw::lib::Integer>(3, rect.height / 4);
    const auto end_x = rect.x + rect.width - std::max<::cw::lib::Integer>(3, rect.width / 5);
    const auto end_y = rect.y + std::max<::cw::lib::Integer>(2, rect.height / 5);
    for (::cw::lib::Integer offset = 0; offset < thickness; ++offset) {
        plot_line(start_x, start_y - offset, middle_x, middle_y - offset);
        plot_line(middle_x, middle_y - offset, end_x, end_y - offset);
    }
}

void draw_radio(const std::shared_ptr<::cw::lib::Surface>& destination,
                const ::cw::lib::Rect rect, const bool selected, const bool enabled)
{
    if (!destination || rect.width < 5 || rect.height < 5) return;
    const auto cx = rect.x + (rect.width - 1) / 2.0;
    const auto cy = rect.y + (rect.height - 1) / 2.0;
    const auto rx = (rect.width - 1) / 2.0;
    const auto ry = (rect.height - 1) / 2.0;
    const ::cw::lib::Color border = selected && enabled
        ? ::cw::lib::Color{0, 120, 215, 255}
        : (enabled ? ::cw::lib::Color{118, 118, 118, 255}
                   : ::cw::lib::Color{172, 172, 172, 255});
    const ::cw::lib::Color body = enabled
        ? ::cw::lib::Color{255, 255, 255, 255}
        : ::cw::lib::Color{232, 232, 232, 255};
    const ::cw::lib::Color dot = enabled
        ? ::cw::lib::Color{0, 120, 215, 255}
        : ::cw::lib::Color{166, 166, 166, 255};
    for (::cw::lib::Integer y = rect.y; y < rect.y + rect.height; ++y) {
        for (::cw::lib::Integer x = rect.x; x < rect.x + rect.width; ++x) {
            const auto dx = (x - cx) / rx;
            const auto dy = (y - cy) / ry;
            const auto distance = dx * dx + dy * dy;
            if (distance > 1.0) continue;
            auto color = distance >= 0.66 ? border : body;
            if (selected && distance <= 0.23) color = dot;
            fill_rect(destination, {x, y, 1, 1}, color);
        }
    }
}

void draw_rounded_bar(const std::shared_ptr<::cw::lib::Surface>& destination, const ::cw::lib::Integer x,
                      const ::cw::lib::Integer y, const ::cw::lib::Integer width, const ::cw::lib::Integer height,
                      const ::cw::lib::Color light, const ::cw::lib::Color dark, const ::cw::lib::Integer fill_width,
                      const bool reverse)
{
    if (!destination || width <= 0 || height <= 0) return;
    const auto hr = std::max<::cw::lib::Integer>(2, height / 3);
    const ::cw::lib::Color frame{216, 216, 216, 255};
    fill_rect(destination, {x - 1, y - 1, width + 2, hr}, frame);
    fill_rect(destination, {x - 1, y - 1 + hr, width + 2, height + 2 - hr}, {255, 255, 255, 255});
    const auto clamped = std::min(width, std::max<::cw::lib::Integer>(0, fill_width));
    if (clamped <= 0) return;
    if (reverse) {
        fill_rect(destination, {x + width - clamped, y, clamped, hr}, dark);
        fill_rect(destination, {x + width - clamped, y + hr, clamped, height - hr}, light);
    } else {
        fill_rect(destination, {x, y, clamped, hr}, dark);
        fill_rect(destination, {x, y + hr, clamped, height - hr}, light);
    }
}

} // namespace cw::lib::ui
