# ツール

`picotool/` には Raspberry Pi 公式配布の Windows x64 `picotool v2.3.1` を配置しています。

リポジトリルートから実行する場合:

```powershell
.\tools\picotool\picotool.exe info -a
```

バックアップスクリプトは、PATH より先にこのリポジトリ内の実行ファイルを自動使用します。

```powershell
.\tools\backup-hid-remapper.ps1
```
