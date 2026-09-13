# HID report 互換性メモ

## 既定出力

上流 HID Remapper の既定キーボード出力は Usage Minimum `0x04` から始まる NKRO bitmap です。マウス出力は Report ID と複数の高機能フィールドを含みます。

## 最初の A/B

- 既存の interface 数と送信経路は変更しない。
- キーボードを 6KRO の modifier + 6 byte key array にする。array は Usage/Logical Minimum `0x04` から始め、格納値をUsage IDそのものにする（テンキー `0x59`–`0x63` を含む）。空きslotの`0x00`は範囲外のNull Stateとして宣言する。
- マウスを 3 ボタン + 8-bit 相対 X/Y/Wheel へ単純化する。
- Keyboard/Mouse/Consumer/LED が同じ HID interface を共有するため、この段階では Report ID を維持する。

この A/B は「真の USB Boot protocol 実装」ではありません。受信機が descriptor の単純化だけで動くかを切り分けるための段階です。

同一 HID interface で一部の report だけを Report ID なしにすることはできません。マウスの Report ID 自体が非互換要因だった場合は、次段階の別 interface 化が必要です。

送信側は TinyUSB が interrupt endpoint busy を返したreportをキューに残す必要があります。特に相対マウスreportは再生成前にclearされるため、busy時にキューから捨てると移動量が失われます。現在のSimple MouseはReport ID `1`、payload `4` byteであり、USB上のreportはID込みで`5` byteです。

## 次段階

A/B の実機確認では一般キーの大半だけが動作し、テンキーとマウスは動作しませんでした。一方、キーボード、テンキー、マウスを WBT2-V4 へ直接接続するとすべて動作したため、WBT2 の入力対応ではなく HID Remapper の複合 report 形式が非互換要因です。

`remapper_dual_a` は次の4 interface構成でビルドします。

| interface | subclass/protocol | USB上のreport | endpoint |
|---:|---|---|---:|
| 0 | Boot Keyboard | Report IDなし、modifier + reserved + 6 keys（8 byte） | `0x81` |
| 1 | Boot Mouse | Report IDなし、3 buttons + X/Y（3 byte） | `0x82` |
| 2 | None | Report IDなし、Consumer Control（1 byte） | `0x83` |
| 3 | None | 既存のConfig/Monitor report | `0x84` |

内部のマッピング処理と送信キューでは従来の Report ID `1`（Mouse）、`2`（Keyboard）、`3`（Consumer）を維持し、USB送信直前にinterfaceへ振り分けてReport IDを除去します。これにより保存済み設定のusage、`our_descriptor_number: 0`、Flash上の設定形式は変更しません。Keyboard LED出力はinterface 0のIDなしOutput reportを内部の`REPORT_ID_LEDS`へ変換します。

Boot Mouse互換性を優先し、この段階のMouse reportにはwheelを含めません。マウス移動・ボタンの実機確認後、WBT2がReport Protocolの4 byte mouse（wheel付き）も受け入れる場合に限って拡張します。

この分離はPC接続側の`remapper_dual_a`だけに`WBT2_BOOT_INTERFACES`として有効化します。入力デバイス側`remapper_dual_b`、保存済み購入時UF2、設定JSONには変更を加えません。
