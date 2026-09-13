# HID report 互換性メモ

## 既定出力

上流 HID Remapper の既定キーボード出力は Usage Minimum `0x04` から始まる NKRO bitmap です。マウス出力は Report ID と複数の高機能フィールドを含みます。

## 最初の A/B

- 既存の interface 数と送信経路は変更しない。
- キーボードを 6KRO の modifier + 6 byte key array にする。
- マウスを Report ID なしの相対 X/Y/Wheel とボタンへ単純化する。

この A/B は「真の USB Boot protocol 実装」ではありません。受信機が descriptor の単純化だけで動くかを切り分けるための段階です。

## 次段階

A/B で解消しない場合は、Keyboard と Mouse を別 HID interface に分け、各 interface を Boot subclass/protocol として提示する必要があります。descriptor だけでなく configuration descriptor、descriptor callback、送信先 interface、LED 出力処理も同時に見直します。
