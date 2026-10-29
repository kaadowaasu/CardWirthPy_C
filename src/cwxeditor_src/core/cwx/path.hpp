#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <memory>
#include <span>
#include <string_view>

#include "cwxeditor_src/lib/portable.hpp"

namespace cwxeditor::cwx::path {

/// スタック上のアドレスを一時的に借りる、親リンク共通の無効化フラ
/// グ。 対象オブジェクトがトークンを所有し、参照側は弱参照だけを
/// 保持する。 借用アドレスを使う前に、トークンを確認すること。
struct OwnerLifetimeToken {
    std::atomic_bool alive{true};
};

/// 構築中や生ポインターの親も表せる、弱い親リンク。
/// 共有の親は実際の制御ブロックを弱参照し、 借用の
/// 親は所有者の寿命トークンが有効な間だけ使える。
template <typename Owner>
class OwnerLink {
public:
    class Access {
        friend class OwnerLink;
        lib::Owner<Owner> shared_;
        Owner* borrowed_{};
        std::shared_ptr<OwnerLifetimeToken> lifetime_;
        bool guarded_{};

        Access(lib::Owner<Owner> shared) : shared_(std::move(shared)) {}
        Access(Owner* borrowed, std::shared_ptr<OwnerLifetimeToken> lifetime,
               bool guarded)
            : borrowed_(borrowed), lifetime_(std::move(lifetime)),
              guarded_(guarded) {}

    public:
        Access() = default;

        [[nodiscard]] Owner* get() const noexcept {
            if (shared_) return shared_.get();
            if (!borrowed_) return nullptr;
            if (guarded_ && (!lifetime_ ||
                    !lifetime_->alive.load(std::memory_order_acquire))) return nullptr;
            return borrowed_;
        }
        [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }
        [[nodiscard]] Owner* operator->() const noexcept { return get(); }
        [[nodiscard]] const lib::Owner<Owner>& shared() const noexcept { return shared_; }
    };

    OwnerLink() = default;

    void setShared(const lib::Owner<Owner>& owner) {
        observer_ = owner;
        borrowed_ = nullptr;
        lifetime_.reset();
        guarded_ = false;
    }

    void setRaw(Owner* owner) noexcept {
        observer_.reset();
        borrowed_ = owner;
        lifetime_.reset();
        guarded_ = false;
    }

    void setBorrowed(Owner* owner,
                     std::weak_ptr<OwnerLifetimeToken> lifetime) noexcept {
        observer_.reset();
        borrowed_ = owner;
        lifetime_ = std::move(lifetime);
        guarded_ = owner != nullptr;
    }

    void setSharedBorrowed(const lib::Owner<Owner>& owner,
                           Owner* borrowed,
                           std::weak_ptr<OwnerLifetimeToken> lifetime) noexcept {
        observer_ = owner;
        borrowed_ = borrowed;
        lifetime_ = std::move(lifetime);
        guarded_ = borrowed != nullptr;
    }

    [[nodiscard]] Access lock() const {
        if (auto owner = observer_.lock()) return Access(std::move(owner));
        if (!borrowed_) return {};
        if (!guarded_) return Access(borrowed_, {}, false);
        auto lifetime = lifetime_.lock();
        if (!lifetime || !lifetime->alive.load(std::memory_order_acquire)) return {};
        return Access(borrowed_, std::move(lifetime), true);
    }

    /// 公開する弱い親参照には、常に実際の共有制御ブロックを使う。 借用し
    /// たスタック上の親には、Observerとして表せる値を持たせない。
    [[nodiscard]] lib::Observer<Owner> observer() const noexcept { return observer_; }

    /// 所有者を参照先へたどらず、アドレスとトークンの同一性
    /// で借用の逆リンクを確認する。 デストラクターがトーク
    /// ンを無効化した後、所有メンバーを解放する前に使う。
    [[nodiscard]] bool matchesBorrowed(
        Owner* owner, const std::weak_ptr<OwnerLifetimeToken>& lifetime) const noexcept {
        if (!owner || borrowed_ != owner || !guarded_) return false;
        const auto stored = lifetime_.lock();
        const auto expected = lifetime.lock();
        return stored && expected && stored == expected;
    }

private:
    lib::Observer<Owner> observer_;
    Owner* borrowed_{};
    std::weak_ptr<OwnerLifetimeToken> lifetime_;
    bool guarded_{};
};

/// 複数のCWXパスをまとめて保存するときの区切り文字。
inline constexpr std::string_view CWXPATH_SEP = "&";

/// CWXパスを構成できない場合を示す印。
inline constexpr std::string_view INVALID_CWX_PATH = "InvalidCWXPath";

/// 検索結果は共有所有のハ
/// ンドル、子一覧と親リ
/// ンクは弱参照で返す。
class CWXPath {
public:
    virtual ~CWXPath() = default;

    // コールバック中のネイティブの寿命管理。共有モデルは既存の制御
    // ブロックを返し、 スタック・生ポインターのモデルは呼び出し元
    // が寿命を管理する。 ツリーの所有権やUIの動作は変えない。
    [[nodiscard]] virtual lib::Owner<CWXPath> retainForCallback() { return {}; }

    /// このオブジェクトのシナリオパスを返す。必要に応じて固定IDを使う。
    [[nodiscard]] virtual lib::String cwxPath(bool id) = 0;

    /// このオブジェクトの配下を検索する。なければ空の所有ハンドルを返す。
    [[nodiscard]] virtual lib::Owner<CWXPath> findCWXPath(std::string_view path) = 0;

    /// 直接の子を、所有するツリーへの弱参照として返す。
    [[nodiscard]] virtual lib::Array<lib::Observer<CWXPath>> cwxChilds() = 0;

    /// 親を弱参照で返す。ルートでは期限切れの参照を返す。
    [[nodiscard]] virtual lib::Observer<CWXPath> cwxParent() = 0;

    /// このオブジェクトの変更を監視側へ通知する。
    virtual void changed() = 0;
};

[[nodiscard]] inline lib::Owner<CWXPath> retainCallbackOwner(lib::Owner<CWXPath> owner) {
    if (owner) {
        if (auto retained = owner->retainForCallback()) return retained;
    }
    return owner;
}

/// 作者向けのコメントを持つオブジェクトのインターフェース。
class Commentable {
public:
    virtual ~Commentable() = default;

    [[nodiscard]] virtual lib::String comment() const = 0;
    virtual void comment(lib::String value) = 0;
};

/// 既知のCWXパス種別の並べ替え順位を返す。未知なら-1。
[[nodiscard]] int cpTypeValue(std::string_view category) noexcept;

/// 属性を除いたパス本体を/で分割し、空の要素も保持する。
[[nodiscard]] lib::Array<lib::String> cpsplit(std::string_view path);

/// 分割済みのCWXパス要素を、種別の順位、数値の添字の順で比較する。
[[nodiscard]] int cpcmp(std::span<const lib::String> a,
                        std::span<const lib::String> b);

[[nodiscard]] lib::String cpjoin(const lib::Owner<CWXPath>& owner,
                                  std::size_t index,
                                  bool id);
[[nodiscard]] lib::String cpjoin(const lib::Owner<CWXPath>& owner,
                                  std::string_view category,
                                  std::size_t index,
                                  bool id);
[[nodiscard]] lib::String cpjoin(const lib::Owner<CWXPath>& owner,
                                  std::string_view name,
                                  bool id);
[[nodiscard]] lib::String cpjoinid(const lib::Owner<CWXPath>& owner,
                                    std::uint64_t id);
[[nodiscard]] lib::String cpjoinid(const lib::Owner<CWXPath>& owner,
                                    std::string_view category,
                                    std::uint64_t id);

[[nodiscard]] lib::String cpjoin(std::string_view ownerPath,
                                  std::string_view category,
                                  std::size_t index);
[[nodiscard]] lib::String cpjoin2(std::string_view ownerPath,
                                   std::string_view category,
                                   std::size_t index);
[[nodiscard]] lib::String cpjoinid(std::string_view ownerPath,
                                    std::string_view category,
                                    std::uint64_t id);
[[nodiscard]] lib::String cpjoin(std::string_view ownerPath,
                                  std::string_view subPath);
[[nodiscard]] lib::String cpjoin2(std::string_view ownerPath,
                                   std::string_view subPath);

/// D版と同じく、元とは逆の順で属性を返す。
[[nodiscard]] lib::Array<lib::String> cpattr(std::string_view path);

/// 先頭のセミコロン区切りの属性接尾部を取り出して削除する。
[[nodiscard]] lib::String cpattrRef(lib::String& path);

/// セミコロン以降の属性を除いたパス本体を返す。
[[nodiscard]] lib::String cpbody(std::string_view path);

[[nodiscard]] lib::String cpaddattr(std::string_view path, std::string_view attr);
[[nodiscard]] bool cphasattr(std::string_view path, std::string_view attr);
[[nodiscard]] bool cpeq(std::string_view path1, std::string_view path2);
[[nodiscard]] bool cpempty(std::string_view path);
[[nodiscard]] lib::String cpparent(std::string_view path);
[[nodiscard]] lib::String cptop(std::string_view path);
[[nodiscard]] lib::String cpbottom(std::string_view path);
[[nodiscard]] lib::String cpcategory(std::string_view path);
[[nodiscard]] std::size_t cpindex(std::string_view path);
[[nodiscard]] lib::String cplast(std::string_view path);
[[nodiscard]] bool cpdescendant(std::string_view path1, std::string_view path2);
[[nodiscard]] lib::String cprel(std::string_view path, std::string_view ancestor);

/// 親への弱参照をたどり、到達できる最上位のCWXオブジェクトを返す。
[[nodiscard]] lib::Owner<CWXPath> cwxTop(lib::Owner<CWXPath> cwxPath);

}  // namespace cwxeditor::cwx::path
