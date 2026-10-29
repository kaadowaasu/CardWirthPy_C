#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

// 利用可能な日本語対応フォントの絶対
// パスを返す。Web/Linuxは同
// 梱フォントを使い、他の環境は各プ
// ラットフォームの解決規則に従う。
std::string find_platform_font(std::string_view family = {});

// WebとLinuxはホストのフォントを使わず、同じ構成のフォント群を一つのルートへ同梱する （web/font_manifest.json
// ）。   <root>/NotoSansCJKjp-Regular.otf：UI用・代替用
// フォント   <root>/Font/{gothic,uigothic,mincho,pgothic,pmincho}.ttf
// ：IPAフォント 同じファイルで文字を描くため、Webの文字描画テストはLinux
// にも適用できる。 Webの配置先は/program（web_fonts.js）
// 、Linuxは<実行ファイルのディレクトリ>/resource。 OSのフォン
// トを使うWindows・Apple・Androidではnulloptを返す。
std::optional<std::filesystem::path> bundled_font_root();
// <root>/NotoSansCJKjp-Regular.otf。同梱フォントのルートがなければ空のパス。
std::filesystem::path bundled_ui_font();
// CardWirthの論理フォント名（gothic・IPAゴシック・MS Gothic・ＭＳ 明朝など）
// に 対応する<root>/Font/<file>。その他のフォント名やルート未設定の場合は空のパス。
std::filesystem::path bundled_logical_font(std::string_view family);
