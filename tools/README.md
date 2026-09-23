# ツール

`picotool/` には Raspberry Pi 公式配布の Windows x64 `picotool v2.3.1` を配置しています。

リポジトリルートから実行する場合:

```powershell
.\tools\picotool\picotool.exe info -a
```

バックアップスクリプトは、PATH より先にこのリポジトリ内の実行ファイルを自動使用します。
保存先は既定で `backups/backups_YYYYMMDD_HHMMSS/` とし、同名の既存フォルダーは
上書きしません。`-OutputDirectory` を指定する場合も、既存フォルダーは使えません。

```powershell
.\tools\backup-hid-remapper.ps1
```
