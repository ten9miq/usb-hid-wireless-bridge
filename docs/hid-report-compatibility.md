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
| 1 | Boot Mouse subclass | Report IDなし、3 buttons + X/Y/Wheel（4 byte） | `0x82` |
| 2 | None | Report IDなし、Consumer Control（1 byte） | `0x83` |
| 3 | None | 既存のConfig/Monitor report | `0x84` |

内部のマッピング処理と送信キューでは従来の Report ID `1`（Mouse）、`2`（Keyboard）、`3`（Consumer）を維持し、USB送信直前にinterfaceへ振り分けてReport IDを除去します。これにより保存済み設定のusage、`our_descriptor_number: 0`、Flash上の設定形式は変更しません。Keyboard LED出力はinterface 0のIDなしOutput reportを内部の`REPORT_ID_LEDS`へ変換します。

3 byte Boot Mouse版でマウス移動と左右クリックがWBT2-V4経由でも動作したため、同じReport IDなしinterfaceの末尾にwheelを加えた4 byte reportへ拡張します。

KeyboardはWBT2-V4が解釈しやすい標準Boot Keyboardのarray範囲に合わせ、Usage Minimum/MaximumとLogical Minimum/Maximumを`0x00`–`0x65`にします。テンキーusage `0x59`–`0x63`はこの範囲内であり、内部6KRO reportの各slotにusage IDそのものを格納してUSB上の8 byte reportへそのまま渡します。`c`/`v`の無反応は保存設定内のPageUp/PageDown mappingを削除すると解消したため、descriptor問題とは分離します。

この分離はPC接続側の`remapper_dual_a`だけに`WBT2_BOOT_INTERFACES`として有効化します。入力デバイス側`remapper_dual_b`、保存済み購入時UF2、設定JSONには変更を加えません。

## 入力側のReport protocol

TinyUSB hostは既定ではBoot subclassのKeyboard/Mouseを列挙時にBoot protocolへ切り替えます。これは単純な3 byte mouseや8 byte keyboardには適しますが、Report protocol側に複数のtop-level collection、Report ID、追加キーを持つデバイスでは、PCへ直結した場合とHID Remapper B側へ接続した場合のreport形式が変わります。

B側は列挙前に`tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT)`を指定し、VID/PIDに依存せず各HID interfaceのreport descriptorどおりに受信します。既存コードは最大16 HID instanceを確保し、descriptor取得時とreport受信時の両方で同じTinyUSB `instance`をA側へ渡すため、複数interfaceは`dev_addr + instance`ごとに分離されます。同一interface内の複数collectionはReport IDごとのusage mapとして解析されます。

この変更でデバイス固有quirkは追加しません。実機確認では、HID RemapperのMonitorにKeyboard/Keypad usage `0x00070059`–`0x00070063`とnavigation usageが出ることを先に確認し、その後WBT2-V4経由のRaw Inputと照合します。Monitorにも出ない場合は、B側が受信したreport descriptorとreport byte列の採取が次の切り分けになります。
