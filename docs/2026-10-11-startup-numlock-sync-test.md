# 起動時NumLock二重タップの試験候補

## 成果物と確認範囲

- 候補: `firmware/artifacts/remapper_dual_combined-startup-numlock-sync-test.uf2`
- SHA-256: `0118755803E93F9E163069FC9388BE87DE7DCAC0D3B511B32DC2550366D6A1CE`
- 基準: `remapper_dual_combined-native-keypad-dedup-completion-reserve-test.uf2`
  (SHA-256 `72CCE415BFCBF8E5023AA40CB307DCDC3A552C511F6AF3F9EC90CB0AE98A224F`)
- ビルド・静的検査: PASS。書き込みと候補の実機試験: 未実施。
- 実際の生成日時、入力ソースとCMake引数は同名の`.json`に記録。

ユーザーはWBT2起動直後に00だけ入力されず、物理NumLock二回操作で回復すると報告した。
HID-RemapperのF12からNumLockを二回出力するマクロでも00が回復した。
これは手動マクロの実機成功であり、本候補の自動送信の成功を意味しない。

## 変更内容

`STARTUP_NUMLOCK_SYNC=ON`をA側だけに定義する。通常ビルドの既定値はOFF。
RealForce 23UB (VID `0853`, PID `0117`) の接続とUSBホストの構成完了を確認し、
両条件が成立してから2秒以上待つ。標準キーボード出力が200ms以上空で、
既存マクロ・USB出力キューが空、keyboard endpointが送信可能なときだけ開始する。
押されたキーや修飾キー、既存マクロは開始を延期させる。

FW内部から既存マクロエンジンに次の4段を投入する。

1. NumLock押下
2. 空の段（解放）
3. NumLock押下
4. 空の段（解放）

各段は200 mapping frames（通常約200ms）。キューの送信待ちでは延び得る。
スリープや待機ループを追加せず、通常の入力・マウス処理を続ける。
保存済み`macro_entry_duration`、マクロ定義、Mappingには依存しない。
FW一回の起動につき一度だけ実行し、実行開始後のテンキー抜き差し・USB再列挙では
再実行しない。電源を切ってFWが再起動した場合は再び実行する。

USBデバイス側から接続先がWBT2かPCかを確実に区別できないため、
23UBをつないだPC直結時にも実行する。NumLockは送信中に一時的に切り替わり、
両方のタップが届いた場合に最終状態が元へ戻る。起動後約3秒は操作せず待って試す。
対象テンキーを検出しない場合と、キーボード以外の出力descriptorでは実行しない。

## 回帰を限定するための検査

- `STARTUP_NUMLOCK_SYNC=OFF`で新規ビルドしたA BINが基準A Flashに完全一致。
- 同じ入力と設定でONにしたとき、A側objectの変更は`src/remapper.cc.obj`だけ。
- A側BIN 247,852 bytesと、最新ELFから明示生成したA UF2を照合。
- 埋込みB 48,644 bytesは固定検証済みUF2から`extract-b-hex`で再生成。
- 実行Bは基準UF2から`extract-b-uf2 --keypad-completion-reserve`で再生成し、
  候補のRAM UF2段とバイト単位で一致。
- `verify-firmware-release.py verify --native-keypad-dedup --startup-numlock-sync`がPASS。
- 2秒待ち、押下中の延期、キューbusy時の延期、接続解除、起動あたり一回を
  `tools/startup-numlock-sync-test.cc`で確認。これはホスト側の制御ロジック試験で、
  WBT2や実機の入力成功は確認していない。

新規buildでSDKの一部sourceにRelease最適化が適用されない環境だったため、
ビルドスクリプトでARM CPU指定・O3・NDEBUGを固定した。
基準版との完全比較のため`SOURCE_DATE_EPOCH`を2026-09-23に固定し、
組込みbuild date文字列を基準と揃えた。実際の生成日はmanifestのJST日時を使う。
基準Aのhistorical application/flash sourceは日時付きbackupへ保存して使用した。
その同一ソースを`tools/firmware-baselines/`へ保存し、再ビルドスクリプトは
このGit管理対象を参照する。ハッシュは同ディレクトリーのREADMEに記録した。

## Mappingと試験手順

自動送信に追加Mappingは不要。保存済み設定は本候補で書き換えない。
F12の試験用Mappingを残すと手動でも再送できるので、自動試験ではF12を押さない。
自動復旧を確認してからF12→MacroのMappingを削除し、必要なら試験マクロも削除する。
SettingsのMacro step durationも試験前の値に戻せる。本候補の200 framesには影響しない。

1. 書き込みは`docs/firmware-release-policy.md`に従い、RP2ブートローダーを
   `G:\INFO_UF2.TXT`で確認してから行う。
2. WBT2にHID-Remapperと23UBをつなぎ、電源投入。物理NumLockとF12を押さず約3秒待つ。
3. Raw InputでNumLock DOWN/UPが二組出たか、その後00で0が二文字になるか確認。
4. 起動反復でも二回だけ送られるか、通常0は一文字か、数字・記号・`=`が正常か確認。
5. 直結とWBT2経由を分けて、RealForceのNumLock・テンキー・JISキー、
   RollerMouseとG700s同時接続、移動→停止10回以上、再接続を確認。

位置跳びや入力停止、NumLockのUP欠落が出れば候補は不採用とし、基準UF2へ戻す。
本候補もA埋込みBと実行Bは異なる。`Flash B Side`は押さない。

## 再ビルド

```powershell
& 'C:/Users/ruin_/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' `
  tools/build-startup-numlock-candidate.py
```

このWindows環境ではARM compilerが制限環境内でDLLエラーになるため、
通常の実行環境が必要。スクリプトはUF2生成と静的検査のみで、書き込みを行わない。

## 2026-10-11 書き込み記録

ユーザーがブートモードにしたGドライブの`INFO_UF2.TXT`で、UF2 Bootloader v3.0、
Model Raspberry Pi RP2、Board-ID RPI-RP2を確認した。候補のrelease gateと
SHA-256を再確認してPASS後、上記候補をGドライブへコピーした。コピーは成功し、
3秒後にGドライブが消えた。これはブートローダー離脱の確認である。
通常USB列挙、自動NumLock出力、00復旧、既存入力・マウスの実機確認は未実施。

## 2026-10-11 自動復旧の実機確認

ユーザーは候補書き込み後、WBT2経由で電源を入れ直し、F12も物理NumLockも
押さずに00を入力できたと報告した。今回の目的である起動時の00自動復旧は
報告された試行でPASS。NumLock二組のRaw Input、試行回数、通常0・数字・記号・
JISキー・RollerMouse/G700sの回帰試験結果は未記録であり、正式リリース合格とはしない。
自動動作にF12のMappingは不要なので、試験用Mappingは削除できる。

## 2026-10-11 運用版として採用

ユーザーは既存の数字・記号・NumLock操作、RollerMouseとG700sも問題なく動作すると
報告し、このFWを運用版として記録するよう明示した。00自動復旧と今回報告された
既存動作の確認範囲をPASSとし、上記SHA-256のUF2を現在の運用版に指定した。
UF2の名前・内容とFWソースは変更せず、release gateを再実行してPASSを確認した。
過去節の未実施・未記録は各時点の記録として残す。PC直結での別試験、JIS個別キー、
移動→停止・USB再接続の反復回数は今回未記録で、全release項目の検証完了とはしない。
運用版の指定と生成条件はartifactのmanifestとrelease policyにも反映した。

## 2026-10-11 もう一台への書き込み

ユーザーが「もう一台」と指定してブートモードにしたGドライブのRP2情報を確認し、
運用版UF2のrelease gate PASSとSHA-256を再確認して同じUF2をコピーした。
コピー完了後3秒でGドライブが消えた。個体シリアルは未記録。
この個体の通常USB列挙、00自動復旧、キー・マウス動作は未確認であり、
先に確認済みの個体の実機結果をこの個体へ転用しない。
