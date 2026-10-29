app/ は配布時に /app へ展開される読み取り用データです。
user/ は初回のみ /user へ配置される初期データです。
通常は build_web.ps1 が test_env/Data をビルド出力側へステージします。
