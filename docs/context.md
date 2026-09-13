# 実装引き継ぎコンテキスト

## 目的

- 有線 USB HID キーボード／マウスを 2.4 GHz 無線化する。
- HID Remapper V5.1 と WBT2-V4 の互換性を検証し、再現可能なパッチとして管理する。

## 観測された問題

- HID Remapper 経由ではマウスが動作しない。
- キーボード Usage が観測上 4 ずれる（期待値より -4）。

## 追加で確認できたこと

- `c` / `v` が無反応になる現象は、HID Remapper の mappings を全削除すると解消した。
- 物理的にUSB3へ接続していても、USB4向けに保存した mapping が影響した可能性がある。設定の `source_port` と実際の入力経路番号が一致するとは限らないため、ポート依存mappingを追加する前にMonitorで経路を確認する。

## 現時点の仮説（未確定）

1. HID Remapper の通常キーボード出力は Usage Minimum `0x04` の NKRO bitmap である。
2. WBT2-V4 側が bitmap の先頭を Usage 0 と解釈すると、Usage が 4 小さく見える。
3. HID Remapper の通常マウス descriptor は Report ID、8 ボタン、16-bit 軸などを含むため、単純な Boot Mouse 前提の受信機では無視される可能性がある。

これは静的な整合性であり、実機での原因確定ではない。

## 安全な実装・検証順序

1. 現行 firmware の `picotool info -a`、Flash 全域の UF2/BIN、Web 設定 JSON を保存する。
2. 上流 HID Remapper を commit SHA 固定で `firmware/hid-remapper` に取り込む。
3. 既存 interface 構成を維持した A/B descriptor（Report ID ありの 6KRO array + Simple Mouse）を作る。
4. 直結 PC でキー、修飾キー、6 キー超過、ボタン、軸、wheel、LED を確認する。
5. WBT2-V4 経由で Usage ずれとマウス不動が解消するか確認する。
6. 解消しない場合のみ、Report ID なしの Keyboard/Mouse 別 interface と Boot subclass/protocol を検討する。

## 未実施

- `picotool` の導入と Flash 取得
- firmware のビルド／書き込み
- WBT2-V4 経由の実機入力試験
