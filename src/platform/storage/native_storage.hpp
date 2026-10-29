#pragma once

// ネイティブ実装を明示的に指定する呼び出し元向けの互換インク
// ルード。 実装は共通インターフェースの隣に置き、Android
// ・Webの実装追加でStorageの契約を変えない。
#include "storage.hpp"
