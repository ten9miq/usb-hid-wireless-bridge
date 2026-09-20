# WBT2: Consumer Control を Right Shift + Fキーへ変換する

WBT2-V4 経由で Consumer Control がアプリに届かない場合、HID Remapper の設定だけで、通常のキーボードusageへ変換するプリセットです。ファームウェアやdescriptorは変更しません。

| 入力 Consumer usage | キーボード出力 | WBT2アプリで割り当てる組み合わせ |
| --- | --- | --- |
| Calculator `0x000c0192` | Right Shift `0x000700e5` + F3 `0x0007003c` | Right Shift + F3 |
| Volume Up `0x000c00e9` | Right Shift `0x000700e5` + F9 `0x00070042` | Right Shift + F9 |
| Volume Down `0x000c00ea` | Right Shift `0x000700e5` + F10 `0x00070043` | Right Shift + F10 |
| Mute `0x000c00e2` | Right Shift `0x000700e5` + F11 `0x00070044` | Right Shift + F11 |

## 安全な適用方法

[`presets/wbt2-consumer-to-rshift-fkeys.json`](../presets/wbt2-consumer-to-rshift-fkeys.json) は **mapping断片** です。HID Remapper Web UIの通常の設定importは設定全体を置き換えるため、この断片を単独でimportしないでください。

まず Web UI の Actions から現在の設定をJSONへexportします。次に、次のコマンドでexport済み設定を複製し、既存の設定値・mapping・macro・expressionを保ったまま、重複しない8 mappingだけを加えます。

```powershell
.\tools\merge-wbt2-consumer-preset.ps1 `
  -InputConfig .\my-hid-remapper-export.json `
  -OutputConfig .\my-hid-remapper-export-with-wbt2-media.json
```

出力先ファイルが既にある場合は停止するため、既存exportを上書きしません。作成した `*-with-wbt2-media.json` を Web UI でimportしてください。import前のexportは復元用として残します。

このプリセットの `source_port: 0` は全入力portを対象にします。特定の入力機器だけに限定したい場合は、Web UIのMonitorでConsumer usageを押してportを確認してから、8件すべての `source_port` をその番号にそろえてください。出力はA側の通常キーボード出力なので `target_port: 0` のままにします。

## Web UIで手入力する場合

1. Chromium系ブラウザで HID Remapper の設定ページを開き、対象デバイスへ接続する。
2. Actions から設定をexportして退避する。
3. Monitorで各メディアキーを押し、上表の Consumer usage とportを確認する。
4. Mappingsで、各Consumer usageにつき2件ずつ、上表のRight Shift usageとFキーusageを出力として追加する。Layerは `0`、Scalingは `1000`、Sticky/Tap/Holdはすべてオフにする。
5. Save/Persist後、WBT2-V4経由で各キーを押し、WBT2アプリ側がそれぞれのRight Shift + Fキーを検出することを確認する。

同時に複数のメディアキーを押す運用は想定していません。WBT2アプリの既存ショートカットと競合する場合は、アプリ側の割り当てを先に変更します。
