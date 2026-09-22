# HID-Remapper dual firmware の回帰防止ルール

最終更新: 2026-09-23

## 動作確認済みの基準

`firmware/artifacts/remapper_dual_combined-current-features-historical-embedded-b-ab.uf2`
（SHA-256 `C144E09826BB9EBF63C989E08C0EEE983A7B5A7ACB63E729D3FD62686F05EAC2`）を
復元用の基準とする。ユーザーがHID-RemapperとPCの直結でRollerMouse・G700sの動作、
RollerMouse停止時の位置跳びがないこと、RealForceのテンキー・`=`・JISキーの動作を
実機確認した。WBT2-V4経由の同じ組合せまで確認済みという意味ではない。

この版の結合UF2には次が含まれる。

| 対象 | 固定する内容 | SHA-256 |
|---|---|---|
| A側に埋め込むB用バイナリ | 48,644 bytes | `AD90682D9B0AF74DC5BD8AA47C00874AD69158980D8D4B433E58AA06AA01D4E4` |
| 実行用B側RAM UF2 (`flash_b_side.uf2`) | 287 UF2 blocks | `03639D06680176AE07E403F29704805B32EAF69F7738AD5337FB8C02C5821C4E` |

この2つを混同しない。A側に埋め込むデータは、明示的な`FLASH_B_SIDE`コマンド時に
使用され、通常のマウス処理では実行されない。ただし、その内容とサイズを変えると
A側イメージの配置も変わる。位置跳びが消えた正確な機構は未確定であり、これらの
ハッシュが一致するだけで実機動作を保証するものではない。

## 今回の切り分けで確認したこと

- 現行A側と安定B側の組合せでは位置跳びが続いた。一方、上記基準版では
  テンキー・JISキー修正を維持したまま位置跳びが消えた。
- RAM traceでは、停止後の非ゼロCursor X/YがB側USB callbackのraw reportに既に
  含まれていた。A側のdecode・mapping・aggregation・USB送信queueで値が増幅・
  長時間滞留した証拠はなかった。
- Consumer Descriptor、Mouse Button 4/5 Descriptor、B側HCD公平化、A側UART
  drainを個別に変更しても位置跳びは続いた。
- `BDFEC306…`版と`DDBCACAE…`版の比較では、A側の埋込みBだけでなく、結合UF2の
  実行用B側も異なっていた。この組だけを「埋込みB単独の効果」の証明に使わない。

## FWを変更するときの必須手順

1. 基準UF2を変更・上書きしない。候補は別名で生成し、ソース差分、CMake設定、
   A/B入力、生成日時、SHA-256を記録する。
2. リポジトリ内の動作確認済みUF2から、固定B入力を再生成する。Git管理外の
   `build-*`にある古いHEXやUF2だけに依存しない。
3. A側は最新の`remapper_dual_a.elf`からUF2へ明示変換する。B側には
   `remapper_dual_b.uf2`ではなく固定した`flash_b_side.uf2`を使い、
   `firmware/hid-remapper/firmware/combine_uf2.py`で結合する。
   `PICO_NO_PICOTOOL=ON`のbuildではELFだけが更新されUF2が古いまま残り得る。
4. 書き込み前に下記の静的release gateを通す。A側BINとの一致、固定埋込みB、
   固定実行B、UF2のFlash/RAMブロック分離、診断option無効化を検査する。
5. `G:\INFO_UF2.TXT`でRP2ブートローダーを確認してから書き込み、書き込み後に
   ブートローダーが消えることを確認する。USB列挙確認と実機入力確認を別に行う。
6. 同じHID-Remapper・USBポート・保存設定で、RollerMouseとG700sを同時接続し、
   RollerMouseの移動→停止を少なくとも10回繰り返す。位置跳びと両マウス停止の
   有無を記録する。RealForce上段数字、NumLock ON/OFF時のテンキー、`=`、
   JIS `0x87`/`0x89`も確認する。WBT2-V4を変更対象に含めたときは直結結果と
   分けて同じキー・マウスを確認する。
7. 実機確認が未実施、または一項目でも失敗した候補を「動作確認済み版」として
   扱わない。基準UF2は復元可能なまま保持する。

### 固定B入力の再生成と静的release gate

リポジトリ直下でPython 3を使用する。以下の`python`コマンドが見つからない環境では、
このホストで確認済みの
`C:\Users\ruin_\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`
を指定する。次の`pinned-b.hex`と
`flash_b_side.uf2`はbuild用の一時ファイルであり、動作確認済みUF2を元に
SHA-256検証後に再生成される。

```powershell
python tools/verify-firmware-release.py extract-b-hex firmware/hid-remapper/firmware/build-release/pinned-b.hex
python tools/verify-firmware-release.py extract-b-uf2 firmware/hid-remapper/firmware/build-release/flash_b_side.uf2
```

CMakeでは`DUAL_B_BINARY_OVERRIDE`へ生成した`pinned-b.hex`の絶対パスを渡す。
`remapper_dual_a`をビルドしたら`picotool uf2 convert`でA側ELFからUF2を生成し、
生成したA側UF2と固定`flash_b_side.uf2`を`combine_uf2.py`へ渡す。
書き込み候補に対し、次を実行する。

```powershell
python tools/verify-firmware-release.py verify `
  --combined firmware/artifacts/remapper_dual_combined-current-features-historical-embedded-b-ab.uf2 `
  --a-bin firmware/hid-remapper/firmware/build-current-features-historical-embedded-b/remapper_dual_a.bin `
  --cmake-cache firmware/hid-remapper/firmware/build-current-features-historical-embedded-b/CMakeCache.txt
```

この例は動作確認済み版の検査である。新しい候補では3つの引数を候補の
combined UF2、最新A側BIN、同じbuildのCMakeCacheへ置き換える。
この検査は候補のSHA-256を表示する。基準版と同じハッシュである必要はない。
検査結果は実機の移動→停止試験の代わりにはならない。固定Bや診断optionを
意図的に変更する場合は、変更理由と新しい実機A/B結果を記録してから、
この文書と検査スクリプトの基準を更新する。

## 実機試験の記録欄

候補ごとに以下を保存する。失敗した試験と未実施の試験は区別する。

| 項目 | 記録する値 |
|---|---|
| 候補UF2、SHA-256、CMake設定 | |
| 試験日時、HID-Remapper個体、USBポート、保存設定 | |
| PC直結: RollerMouse＋G700s同時接続 | PASS / FAIL / 未実施 |
| RollerMouse移動→停止10回以上、位置跳び | 試行回数、跳び回数 |
| RealForce上段数字、NumLock ON/OFFテンキー、`=`、JIS `0x87`/`0x89` | 各項目の結果 |
| WBT2-V4経由（変更対象の場合） | 同じ項目を直結とは別に記録 |
| 問題発生時の復元UF2 | 動作確認済み版のSHA-256 |
