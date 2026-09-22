# usb-hid-wireless-bridge

有線 USB HID キーボード／マウスを、HID Remapper と 2.4 GHz ブリッジで無線化するための検証・互換性パッチ用リポジトリです。

2026-09-23時点のHID-Remapper直結での動作確認済みUF2と、改修時の必須検査・
実機試験は[`docs/firmware-release-policy.md`](docs/firmware-release-policy.md)に記録しています。
作業者向けのルールは[`AGENTS.md`](AGENTS.md)を参照してください。

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

PC接続側はReport IDなしのBoot Keyboard／Boot Mouse別interfaceを使用します。
HID-Remapper直結での最新の確認結果とWBT2-V4経由の確認範囲は
[`docs/firmware-release-policy.md`](docs/firmware-release-policy.md)を参照してください。
互換性調査の経緯は[`docs/hid-report-compatibility.md`](docs/hid-report-compatibility.md)にあります。

## 方針

1. 動作確認済みUF2と保存済み設定を復元できる状態で保持する。
2. 候補FWを別artifactとして生成し、`tools/verify-firmware-release.py`で静的検査する。
3. リリース前にRollerMouseの停止時の位置跳びとRealForceの入力を実機確認する。

このリポジトリには、実機で取得していない firmware やバックアップを推測して含めません。

上流ソースの固定先とA/B変更範囲は [`firmware/UPSTREAM.md`](firmware/UPSTREAM.md) を参照してください。

## ディレクトリ

```text
docs/       引き継ぎ内容、互換性仮説、ハードウェア手順
firmware/   固定した上流 firmware と派生実装
patches/    WBT2-V4 向け差分
tools/      バックアップ・検証補助
```
