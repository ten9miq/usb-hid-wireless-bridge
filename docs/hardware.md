# ハードウェア作業手順

## 書き換え前の保存

RP2040 を BOOTSEL モードで接続し、まず認識情報を保存します。

```powershell
picotool info -a > hid-remapper-v5.1-original-info.txt
picotool save -a -v hid-remapper-v5.1-original.uf2
picotool save -a -v hid-remapper-v5.1-original.bin
```

V5.1 が複数の RP2040 を持つ構成なら、各デバイスを個別に識別して同じ手順を行います。Web UI の Mapping 設定 JSON も別途 export します。

取得したバックアップは firmware のソース管理対象にせず、アクセス制限したローカル保管場所へ置きます。
