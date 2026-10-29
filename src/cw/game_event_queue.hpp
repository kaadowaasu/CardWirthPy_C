#pragma once

namespace cw::thread {
class CWPy;

// ネイティブイベントとexec_funcの受け渡しを、取り出さずに確認する共通処理。 user_only
// はPyのUSEREVENT/FORCE_USEREVENTの選別に対応する。
bool peek_pending_events(CWPy& arg_runtime, bool arg_user_only = false);
}
