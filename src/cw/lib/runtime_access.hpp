#pragma once

#include <mutex>
#include <utility>

namespace cw::lib {

// Python は処理系のロックでオブジェクトグラフを保護するが、C++ に
// 同じ仕組みはない。モーダル表示は進行を止めても表示更新は止めないため、共通
// のゲーム・UI 境界で短いモデル操作を直列化する。ネイティブ UI は所有
// スレッドで実行する。受信待ち、モーダル待機、ワーカー join の間はロ
// ックを保持せず RuntimeAccessSuspension を使う。
inline std::recursive_mutex runtime_access_mutex;
inline thread_local unsigned runtime_access_depth = 0;

class RuntimeAccess final {
public:
    RuntimeAccess() { runtime_access_mutex.lock(); ++runtime_access_depth; }
    ~RuntimeAccess() { --runtime_access_depth; runtime_access_mutex.unlock(); }
    RuntimeAccess(const RuntimeAccess&) = delete;
    RuntimeAccess& operator=(const RuntimeAccess&) = delete;
};

// 入れ子のコールバックは複数段のトランザクションを所有できる。別の所
// 有者を待つ間は全段を解放し、例外伝播時も含め戻るときに同じ段数を
// 復元する。キュー・画像・中継の mutex は独立しているため、
// アクセスを一時解放する前に、それぞれの局所的な排他区間を抜ける。
class RuntimeAccessSuspension final {
public:
    RuntimeAccessSuspension() : depth_(std::exchange(runtime_access_depth, 0)) {
        for (unsigned i = 0; i < depth_; ++i) runtime_access_mutex.unlock();
    }
    ~RuntimeAccessSuspension() {
        for (unsigned i = 0; i < depth_; ++i) runtime_access_mutex.lock();
        runtime_access_depth += depth_;
    }
    RuntimeAccessSuspension(const RuntimeAccessSuspension&) = delete;
    RuntimeAccessSuspension& operator=(const RuntimeAccessSuspension&) = delete;
private:
    unsigned depth_;
};

template<class Function>
decltype(auto) without_runtime_access(Function&& function) {
    RuntimeAccessSuspension suspend;
    return std::forward<Function>(function)();
}

} // namespace cw::lib
