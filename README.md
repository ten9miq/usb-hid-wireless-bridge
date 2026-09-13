# usb-hid-wireless-bridge

有線 USB HID キーボード／マウスを、HID Remapper と 2.4 GHz ブリッジで無線化するための検証・互換性パッチ用リポジトリです。

## 現在の対象構成

```text
USB keyboard / mouse
        |
        v
HID Remapper V5.1 (RP2040)
        |
        v
WBT2-V4  ->  2.4 GHz receiver  ->  PC
```

現在確認したい互換性問題は、HID Remapper 経由でキーボード Usage が 4 小さく見えることと、マウス入力が受信側で動作しないことです。詳細な仮説と検証順序は [`docs/context.md`](docs/context.md) を参照してください。

## 方針

1. 実機を書き換える前に、現行 RP2040 Flash と設定を保存する。
2. まず既存の USB interface 構成を維持した最小 descriptor A/B を試す。
3. 受信機が Boot protocol 前提の場合だけ、Keyboard/Mouse の別 interface 化へ進む。

このリポジトリには、実機で取得していない firmware やバックアップを推測して含めません。

上流ソースの固定先とA/B変更範囲は [`firmware/UPSTREAM.md`](firmware/UPSTREAM.md) を参照してください。

## ディレクトリ

```text
docs/       引き継ぎ内容、互換性仮説、ハードウェア手順
firmware/   固定した上流 firmware と派生実装
patches/    WBT2-V4 向け差分
tools/      バックアップ・検証補助
```
