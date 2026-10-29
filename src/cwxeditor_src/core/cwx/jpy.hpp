#pragma once

// cwx/jpy.dのC++20移植。
// JPYは元のバイトを保つテキスト資源
// として扱う。 Editorと同じデ
// ータモデルを公開しつつ元の改行を保
// 持し、 ファイル名だけの変更で無関
// 係な部分を書き換えないようにする。

#include "cwxeditor_src/core/cwx/filesync.hpp"
#include "cwxeditor_src/core/cwx/path.hpp"
#include "cwxeditor_src/core/cwx/usecounter.hpp"
#include "cwxeditor_src/core/cwx/props.hpp"
#include "cwxeditor_src/core/cwx/structs.hpp"
#include "cwxeditor_src/lib/portable.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cwxeditor::cwx::usecounter {
class UseCounter;
}

namespace cwxeditor::cwx::jpy {

using String = ::cwxeditor::lib::String;
using StringView = ::cwxeditor::lib::StringView;
using CPoint = ::cwxeditor::cwx::structs::CPoint;
using CRect = ::cwxeditor::cwx::structs::CRect;
using CRGB = ::cwxeditor::cwx::structs::CRGB;
using FileSync = ::cwxeditor::cwx::filesync::FileSync;

enum class Animation : std::int32_t {
    NONE = 0,
    DRAW = 1,
    UNDO = 2,
    SMOOTH = 3,
    WAIT = 4,
};

// これら六つの列挙型はint32_tを基底にする。 同じ値を使
// うcwx/graphics.hpp側でも前方宣言している。
enum class Colormap : std::int32_t {
    NONE = 0,
    GRAY_SCALE = 1,
    SEPIA = 2,
    PINK = 3,
    SUNNY_RED = 4,
    LEAF_GREEN = 5,
    OCEAN_BLUE = 6,
    LIGHTNING = 7,
    PURPLE_LIGHT = 8,
    AQUA_LIGHT = 9,
    CRIMSON = 10,
    DARK_GREEN = 11,
    DARK_BLUE = 12,
    SWAMP = 13,
    DARK_PURPLE = 14,
    DARK_SKY = 15,
};

enum class Dirtype : std::int32_t {
    CURRENT = 1,
    TABLE = 2,
    SCHEME = 3,
    SCENARIO = 4,
    WAV = 5,
    PARENT = 6,
    PROGRAM = 7,
};

enum class Colorexchange : std::int32_t {
    NONE = 0,
    GBR = 1,
    BRG = 2,
    GRB = 3,
    BGR = 4,
    RBG = 5,
};

enum class Filter : std::int32_t {
    NONE = 0,
    SHADE = 1,
    SHARP = 2,
    SUN = 3,
    C_EMBOSS = 4,
    D_EMBOSS = 5,
    ELEC = 6,
    MONO = 7,
    DIFFUSION = 8,
    NEGA = 9,
    EMBOSS = 10,
};

enum class Cache : std::int32_t {
    NONE = 0,
    C1 = 1,
    C2 = 2,
    C3 = 3,
    C4 = 4,
    C5 = 5,
    C6 = 6,
    C7 = 7,
    C8 = 8,
};

enum class Mask : std::int32_t {
    NONE = 0,
    V_LINE = 1,
    H_LINE = 2,
    MESH = 3,
};

enum class Noise : std::int32_t {
    NONE = 0,
    LIGHT = 1,
    MONO = 2,
    NOISE = 3,
    C_NOISE = 4,
    MOSAIC = 5,
};

enum class Paintmode : std::int32_t {
    NONE = 0,
    AND = 1,
    OR = 2,
    BLEND = 3,
    NO_PAINT = 4,
};

enum class Turn : std::int32_t {
    NONE = 0,
    LEFT = 1,
    RIGHT = 2,
};

/// エフェクトブースター資源の読み込み中に蓄積したエラー。
class EffectBoosterError final : public std::exception {
public:
    struct EffectBoosterErrorInfo {
        String msg;
        String file;
        std::size_t line = 0;
    };

    EffectBoosterError();
    [[nodiscard]] const char* what() const noexcept override;
    [[nodiscard]] const std::vector<EffectBoosterErrorInfo>& errors() const
        noexcept;
    void add(String msg, String file, std::size_t line);

private:
    std::vector<EffectBoosterErrorInfo> errors_;
};

CPoint pointVal(StringView value, const props::CProps& prop, StringView file,
                std::size_t line, EffectBoosterError& errInfo);
CRect rectVal(StringView value, const props::CProps& prop, StringView file,
              std::size_t line, EffectBoosterError& errInfo);
CRGB rgbVal(StringView value, const props::CProps& prop, StringView file,
            std::size_t line, EffectBoosterError& errInfo);

template <typename Enum>
Enum enumVal(StringView value, const props::CProps& prop, StringView file,
             std::size_t line, EffectBoosterError& errInfo);

String strVal(StringView value, const props::CProps& prop, StringView file,
              std::size_t line, EffectBoosterError& errInfo);
std::int32_t intVal(StringView value, const props::CProps& prop,
                    StringView file, std::size_t line,
                    EffectBoosterError& errInfo);
bool boolVal(StringView value, const props::CProps& prop, StringView file,
             std::size_t line, EffectBoosterError& errInfo);

String readJPYFile(StringView path, const props::CProps& prop,
                   EffectBoosterError& errInfo, bool& isSJIS);
String readJPYFile(StringView path, const props::CProps& prop,
                   EffectBoosterError& errInfo);
void writeJPYFile(StringView path, StringView value, bool isSJIS,
                  const lib::Owner<FileSync>& sync = {});
String stripValue(StringView eqAfter);

class Jpy1Sec;

/// JPY1ファイルと解析済みのセクション。
struct Jpy1 {
    lib::Array<lib::Owner<Jpy1Sec>> sections;
    String sPath;
    String jpy1Path;
    lib::Array<String> lines;
    bool isSJIS = false;
    std::ptrdiff_t dirdepthIndex = -1;
    String dirdepthLine = "dirdepth=";
    std::int32_t dirdepth = 0;

    static Jpy1 load(const props::CProps& prop, StringView sPath,
                     StringView jpy1Path);
    void setUseCounter(const usecounter::CounterOwner& uc);
    void setUseCounter(usecounter::UseCounter* uc);
    void setUseCounter(usecounter::UseCounter& uc) {
        setUseCounter(&uc);
    }
    void removeUseCounter();
    [[nodiscard]] bool renameFile(StringView oldPath, StringView newPath);
    bool updateJpy1File(const props::CProps& prop, bool rewrite,
                        const lib::Owner<FileSync>& sync = {});
};

/// JPY1ファイル内の一つのセクション。
class Jpy1Sec final : public path::CWXPath,
                      public std::enable_shared_from_this<Jpy1Sec> {
public:
    Jpy1Sec();
    ~Jpy1Sec() override = default;
    // カウンターのコールバック中は、PathUserの背後にある実際のモデルを保持できるようにする。
    [[nodiscard]] lib::Owner<path::CWXPath> retainForCallback() override {
        return std::static_pointer_cast<path::CWXPath>(weak_from_this().lock());
    }

    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(
        StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds()
        override;
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override;
    bool change(StringView newValue);
    bool change(usecounter::PathId newValue);
    void path(StringView value);
    [[nodiscard]] String path() const noexcept;
    [[nodiscard]] String pathValue() const noexcept { return path(); }
    void changed() override;

    void setUseCounter(const usecounter::CounterOwner& uc);
    void setUseCounter(usecounter::UseCounter* uc);
    void setUseCounter(usecounter::UseCounter& uc) {
        setUseCounter(&uc);
    }
    void removeUseCounter();
    [[nodiscard]] usecounter::UseCounter* useCounter() const noexcept;

    String sPath;
    String fPath;
    std::ptrdiff_t filenameIndex = -1;
    String filenameLine = "filename=";
    bool needUpdate = false;
    String label;

    std::int32_t backwidth = -1;
    std::int32_t backheight = -1;
    CRGB backcolor{0, 0, 0};
    std::int32_t width = -1;
    std::int32_t height = -1;
    CRGB color{0, 0, 0};

    String filename;
    Dirtype dirtype = Dirtype::CURRENT;
    Cache loadcache = Cache::NONE;
    Cache savecache = Cache::NONE;
    bool visible = true;
    CPoint position{};
    bool transparent = false;
    CRect clip{};
    Paintmode paintmode = Paintmode::NONE;
    std::int32_t alpha = 255;
    CRect animeclip{};
    Animation animation = Animation::NONE;
    CPoint animeposition{};
    CPoint animemove{};
    std::int32_t wait = 0;
    std::int32_t animespeed = 0;
    bool smooth = false;
    Colorexchange colorexchange = Colorexchange::NONE;
    Colormap colormap = Colormap::NONE;
    Filter filter = Filter::NONE;
    Mask mask = Mask::NONE;
    Noise noise = Noise::NONE;
    std::int32_t noisepoint = 0;
    Turn turn = Turn::NONE;
    bool flip = false;
    bool mirror = false;
    String comment;

    [[nodiscard]] String toMaterialPath() const;
    [[nodiscard]] static String toMaterialPathImpl(
        StringView filename, StringView sPath, StringView fPath,
        Dirtype dirtype, std::int32_t dirdepth);
    [[nodiscard]] String fromMaterialPath(StringView value) const;
    [[nodiscard]] static String fromMaterialPathImpl(
        StringView filename, StringView sPath, StringView fPath,
        Dirtype dirtype, std::int32_t dirdepth);

private:
    friend struct Jpy1;
    lib::Owner<usecounter::PathUser> path_user_;
    std::int32_t dirdepth = 0;
};

struct JptxTag {
    String name;
    std::int32_t tagValue = 0;
    std::unordered_map<String, String> attr;

    static JptxTag parse(StringView startTag, const props::CProps& prop,
                         StringView file, std::size_t line,
                         EffectBoosterError& errInfo);
};

class JptxParser {
public:
    bool autoline = true;
    std::function<void(String)> onText;
    std::function<void()> onBR;
    std::function<void()> onB;
    std::function<void()> onI;
    std::function<void()> onU;
    std::function<void()> onS;
    std::function<void(std::int32_t)> onShiftx;
    std::function<void(std::int32_t)> onShifty;
    std::function<void(std::int32_t)> onLineheight;
    std::function<void(String, CRGB, std::int32_t)> onFont;
    std::function<void()> onEndB;
    std::function<void()> onEndI;
    std::function<void()> onEndU;
    std::function<void()> onEndS;
    std::function<void()> onEndShiftx;
    std::function<void()> onEndShifty;
    std::function<void()> onEndLineheight;
    std::function<void()> onEndFont;

    void parse(StringView text, const props::CProps& prop, StringView file,
               std::size_t line, EffectBoosterError& errInfo);
    void parse(StringView text, const props::CProps* prop, StringView file,
               std::size_t line, EffectBoosterError& errInfo);

 public:
    void startTag(StringView tagText, const props::CProps& prop,
                  StringView file, std::size_t line,
                  EffectBoosterError& errInfo);
    void endTag(StringView tagText);
};

struct JptxParam {
    bool b = false;
    bool i = false;
    bool u = false;
    bool s = false;
    std::int32_t shiftx = 0;
    std::int32_t shifty = 0;
    std::int32_t lineheight = 100;
    String face;
    CRGB color{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::int32_t pixels = -1;
};

struct Jptx {
    String text;
    String file;
    std::size_t textLine = 0;
    const props::CProps* props = nullptr;

    CRGB backcolor{0, 0, 0};
    std::int32_t backwidth = -1;
    std::int32_t backheight = -1;
    bool autoline = true;
    std::int32_t lineheight = 100;
    std::int32_t fontpixels = 12;
    CRGB fontcolor{255, 255, 255};
    String fontface = "ＭＳ Ｐゴシック";
    std::int32_t antialias = 0;
    bool fonttransparent = false;

    void parse(const std::function<void(String, const JptxParam&)>& onText);
    static Jptx load(const props::CProps& prop, StringView path);
};

String jptxText(StringView jptxAll);
String jptxText(StringView jptxAll, StringView replacement);

enum class Copymode : std::int32_t {
    AUTO = 0,
    BEFORE_SCREEN = 1,
    SCREEN = 2,
    PAINT_OUT = 3,
};

inline constexpr StringView JPDC_COMMENT_FILE = "file";
inline constexpr StringView JPDC_COMMENT_DIR = "dir";

class Jpdc final : public path::CWXPath,
                   public std::enable_shared_from_this<Jpdc> {
public:
    Jpdc();
    ~Jpdc() override = default;
    // カウンターのコールバック中は、PathUserの背後にある実際のモデルを保持できるようにする。
    [[nodiscard]] lib::Owner<path::CWXPath> retainForCallback() override {
        return std::static_pointer_cast<path::CWXPath>(weak_from_this().lock());
    }

    String sPath;
    String jpdcPath;
    lib::Array<String> lines;
    bool isSJIS = false;
    CRect clip{0, 0, 632, 420};
    Copymode copymode = Copymode::AUTO;
    String saveFileName;
    String savecomment;

    static lib::Owner<Jpdc> load(const props::CProps& prop, StringView sPath,
                                 StringView fPath);
    [[nodiscard]] String cwxPath(bool id) override;
    [[nodiscard]] lib::Owner<path::CWXPath> findCWXPath(
        StringView value) override;
    [[nodiscard]] lib::Array<lib::Observer<path::CWXPath>> cwxChilds()
        override;
    [[nodiscard]] lib::Observer<path::CWXPath> cwxParent() override;
    void changed() override;
    bool change(StringView newValue);
    bool change(usecounter::PathId newValue);
    void path(StringView value);
    [[nodiscard]] String path() const noexcept;
    [[nodiscard]] String pathValue() const noexcept { return path(); }
    void setUseCounter(const usecounter::CounterOwner& uc);
    void setUseCounter(usecounter::UseCounter* uc);
    void setUseCounter(usecounter::UseCounter& uc) {
        setUseCounter(&uc);
    }
    [[nodiscard]] usecounter::UseCounter* useCounter() const noexcept;
    void removeUseCounter();

    [[nodiscard]] String toMaterialPath() const;
    [[nodiscard]] static String toMaterialPathImpl(StringView filename,
                                                    StringView sPath,
                                                    StringView fPath);
    [[nodiscard]] String fromMaterialPath(StringView value) const;
    [[nodiscard]] static String fromMaterialPathImpl(StringView filename,
                                                      StringView sPath,
                                                      StringView fPath);
    [[nodiscard]] bool renameFile(StringView oldPath, StringView newPath);
    bool updateJpdcFile(const props::CProps& prop, bool rewrite,
                        const lib::Owner<FileSync>& sync = {});

private:
    std::ptrdiff_t savefilenameIndex = -1;
    String savefilenameLine = "savefilename=";
    bool needUpdate = false;
    lib::Owner<usecounter::PathUser> path_user_;
};

}  // namespace cwxeditor::cwx::jpy
