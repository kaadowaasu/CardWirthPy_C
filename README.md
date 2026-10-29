# CardWirthPy_C

CardWirthPy RebootとXEditorをもとにしたC++移植版です。

エンジンのクローン元: [CardWirthPy-Reboot](https://github.com/k4nagatsuki/CardWirthPy-Reboot)

現在はテスト用途限定です。配布を目的とした使用は禁止します。[利用条件](LICENSE.md)をご確認ください。

## Windowsでのビルド

Visual Studio 2022のC++開発環境、CMake、Gitが必要です。

```powershell
./src/tools/bootstrap_dependencies.ps1
./build.ps1 -Variant Engine -Configuration Release
./build.ps1 -Variant Editor -Configuration Release
```

実行用のData・スキン・シナリオ・宿は別途用意してください。
