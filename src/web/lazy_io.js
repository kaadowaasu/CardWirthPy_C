// 従来の Asyncify と I/O サーバービルドは同じ FS 操作を共有する
// 。ASYNCIFY=1 では Emscripten が pthread 代理コー
// ドの追加前に __async:auto を包み、ASYNCIFY=0 では同期の
// 外側関数が代理呼び出しに対応する。互換入口は残すが I/O 実装は分岐させない。
#include "lazy_io_server.js"
