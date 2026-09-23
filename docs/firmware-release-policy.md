# HID-Remapper dual firmware の回帰防止ルール

最終更新: 2026-09-23

## 現時点の最安定運用版（2026-09-23）

`firmware/artifacts/remapper_dual_combined-verified-a-g700-queue64-only-candidate.uf2`
（SHA-256 `8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`）を
現在の実機運用・復元用の**最安定版**として固定する。A側Flashは下記の従来版と
バイト単位で同じであり、実行B側のUSBホストイベントキュー容量だけを
16件から64件へ増やした。SOF通知集約と一つずつのエンドポイント公平化は無効。

実機ではRollerMouseとG700sの両方が動作し、RollerMouse移動→停止で
位置跳びなし。HID-RemapperのUSB切断・接続10回でG700s完全停止は0回。
ユーザーは通常のキーボード入力も問題なさそうと報告し、この試験中に
HID Remapper Configurationの`Flash B Side`を押していないことを確認した。
起動直後のG700sの遅延は残るが、完全停止しなければ許容するという判断である。
上段数字・NumLock ON/OFFのテンキー・`=`・JISキーを個別に列挙した結果は
未記録であり、10回で0回という結果は低頻度停止の根絶を証明しない。
したがって「現在最も安定して使えた版」と「全項目のリリース検証完了」は分ける。
この版のAに埋め込まれた旧Bを`Flash B Side`で書くと実行Bが変わるため、
通常運用や本版の再現試験ではその操作を行わない。復元にはこの結合UF2を用いる。

## 従来の動作確認済み復元基準

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

### G700s停止対策のA専用復旧手順（試験中）

`tools/verify-firmware-release.py extract-a-uf2 <出力先>`は、上記の
SHA-256検証済みcombined UF2から、A側のFlashブロックだけを抽出する。
UF2の総ブロック数だけをA単独書き込み用に直し、Flash payload自体は変更しない。
生成済みの`firmware/artifacts/remapper_dual_a-verified-stable-only.uf2`
（SHA-256 `0D6C9595554DC82B9391B1A7F1AE7F7504B9B783E878BD209053FC511FA61E7C`）
がその復旧用Aである。

同じcombined UF2のA埋込みBは48,644バイトだが、実行されたBは
別の48,332バイトである。安定版のA/B組合せを正確に復旧する場合、
`tools/verify-firmware-release.py extract-runtime-b-hex <出力先>`で
B側RAM書き込み段に含まれる実行イメージを抽出し、SHA-256
`9D77CC7378FCEB10F692338198DB1F4682412A0E48028E81126FBF0C761B8219`
を検証する。A埋込みBをそのまま`FLASH_B_SIDE`しても、実行Bの
正確な復元にはならない。

新B候補をA側の一時イメージから設定コマンド`FLASH_B_SIDE`で書いた場合は、
B Flashの読み戻し一致と、A専用復旧後の実機動作を別々に記録する。
A専用復旧はB側Flashを書き換えない。ただし復旧後のAに埋め込まれるのは
上記の歴史的Bであるため、以後`FLASH_B_SIDE`を実行すると新B候補が旧Bへ
戻る。新B候補の正式採用までは、基準combined UF2とそのrelease gateを
変更しない。

2026-09-23の48,980バイト通常用B候補（ホストキュー64件＋SOF通知集約）は、
検証済みAとの組合せでRollerMouse位置跳びを再発させたため不採用とする。
その後、上記`extract-runtime-b-hex`による48,332バイトBの書き込み・
読み戻し一致と、検証済みA専用UF2の再書き込みを行った。
復旧後の実機確認では、RollerMouseの移動→停止10回で位置跳びは0回だった。
一方、G700sはHID-Remapper接続後に2回、完全に動かなくなった。
したがってRollerMouseの回帰は解消したが、G700s停止対策は未達である。
この2件を同じ「位置跳び」として数えない。

### 2026-09-23時点の復元基準

現在の実機は、上記48,332バイトの実行Bと、検証済みA専用UF2から復元した
Aの組合せである。以後の実験前には、Git管理済みの基準combined UF2
（SHA-256 `C144E09826BB9EBF63C989E08C0EEE983A7B5A7ACB63E729D3FD62686F05EAC2`）
を保全する。この結合UF2はAのFlash段と実行BのRAM段を含み、
個別復元にはA専用UF2と`extract-runtime-b-hex`で得る実行Bを用いる。
Aに埋め込まれた48,644バイトBを実行Bと取り違えない。
復元作業の成功判定は書き込み完了だけではなく、A/Bの実機確認で行う。
この基準はRollerMouse位置跳び対策の復元点であり、G700s完全停止が
解消した完成版ではない。

この旧版と新しい最安定運用版は異なるB側実行イメージを含む。
新しい版は`tools/verify-g700-queue-candidate.py`でA同一性、B実行イメージ、
UF2のRAM段、ビルドoptionを検査した。2026-09-23に`G:\INFO_UF2.TXT`でRP2を
確認して書き込み、Gドライブ消失とWindowsでの`VID_213F:PID_1109`列挙を
確認した。実機結果と未確認項目は本書冒頭の最安定運用版の節に記録する。
新しい版で回帰が出た場合でも、旧版combined UF2は変更せず復元可能に保つ。

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
