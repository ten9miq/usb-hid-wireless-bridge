# テンキー二重入力と起動時のHID停止: 2026-09-24の検証記録

## 対象と結論

試験用2台目のHID-Remapperで、RealForceのDIP3 ON、
テンキー数字→上段数字のMappingを除いた35件設定を使用した。
リポジトリ内の
`backups/backups_20260923_224054/hid-remapper-config-dip3-linked-native-keypad.json`
（SHA-256 `4964EFD5B45FDE48CA5C24F3F755AC2B874FB8568C5F6EB09523DF8DC5232536`）は、
ユーザー提供export（SHA-256
`096E477D00376F1D1DEA9DD3710E06EB264D0DFDF0FD2244414B2932F94CF1D9`）と
JSONの設定値・35件のMappingが一致する。ファイルのバイト列だけが異なる。
最終試験候補は
`firmware/artifacts/remapper_dual_combined-native-keypad-dedup-completion-reserve-test.uf2`
（SHA-256 `72CCE415BFCBF8E5023AA40CB307DCDC3A552C511F6AF3F9EC90CB0AE98A224F`）。
A側のテンキー重複防止とB側のUSBホスト完了イベント用キュー予約を組み合わせた。
この二つは別の不具合への対策であり、片方だけでは全条件を満たさなかった。

## 確認できた事実

1. 従来A側と35件Mappingではテンキー数字だけが二重入力し、
   `/ * = - + . Enter`は各1回だった。数字のpass-throughとNumLock OFF補正が
   同じkeypad Usageを6KRO出力に追加し得るコード経路がある。
   `NATIVE_KEYPAD_DEDUP=ON`で、出力に既に存在するUsageを再追加しない。
2. 4機器を接続して起動するとキーボードと有線G700sが個別に停止することがあり、
   RollerMouseは動き続けた。止まった機器だけを抜き差しすると、その機器だけ回復した。
   キーボード停止時のB側診断はcallbacks 1、arms 2、arm failures 0、
   pending false、receive-ready false。G700sのcallbacksは進行していた。
3. 同じ失敗起動で64件のホストイベントキューに1983件の欠落が記録された。
   内訳はdeferred-function 1982件、転送完了イベント1件。起動時のSOF通知が
   キューを埋め、重要な完了通知が落ちる機序と整合する。転送完了の欠落記録には
   デバイスIDがないため、その1件がキーボードのものだったとまでは証明していない。
4. `HOST_COMPLETION_QUEUE_RESERVE=ON`では、64件キューの残りが16枠以下の時だけ
   SOF callbackの登録を見送る。通常の余裕がある時はSOF処理を変えない。
   診断版の一回の起動ではSOFの見送り2060件、ホストキュー欠落0件、
   転送完了欠落0件で、キーボードとG700sのcallbacksは進行した。
   この診断版の4機器接続USB再接続10回はすべて正常だった。
5. 診断を外したB側だけの候補はPC直結10回でキーボード・G700sが動き、
   RollerMouseの移動→停止で跳ばなかった。ただし従来A側のままだったため、
   35件Mappingではテンキー数字の二重入力が残った。
6. 最終組合せ候補はPC直結でテンキー数字が1回、記号・Enterも1回、
   NumLock OFFで矢印・Home・PgUp・PgDnが動作し、JIS `0x87`/`0x89`も正常。
   4機器接続でのUSB再接続10回すべてでRealForce通常キーとG700sが動作し、
   RollerMouseの停止時の位置跳びはなかった。WBT2-V4経由でも1回の基本確認と、
   4機器接続でのUSB再接続10回、数字・`/ * = - + . Enter`の連続操作で
   報告された不具合は再発しなかった。

## なぜ今回の変更が効いたと考えるか

従来のB側キュー64件化は欠落確率を下げても、起動時にSOF由来の補助イベントが
キューを使い切る余地を残していた。今回の予約は過負荷時だけ補助イベントを減らし、
転送完了通知のための空きを残す。完了通知を失うとHCD側の処理が終わっても
TinyUSBのendpointがbusyのままになり、入力reportのcallbackが来ない状態に
つながり得る。失敗時の「armsがcallbacksより1多い・receive-ready false」と、
1件の転送完了欠落はこの機序を強く支持するが、欠落したendpointの同定は未実施。
以前のSOF全面集約はRollerMouseの位置跳びを再発させたため採用していない。
今回の対策はキュー逼迫時だけに限定し、descriptor・マウスreport処理を変更しない。
RollerMouseの跳びがないことは実機結果であり、その根本原因を新たに証明した
という意味ではない。

## 固定値・保守上の注意

- A: `ALT_NUMPAD_EQUALS=ON`、`NATIVE_KEYPAD_DEDUP=ON`。
- B: `HOST_QUEUE_CAPACITY_64=ON`、`HOST_COMPLETION_QUEUE_RESERVE=ON`、
  `HOST_SOF_COALESCE=OFF`、`HOST_ONE_HOT_FAIRNESS=OFF`、診断OFF。
- 上記のキュー予約と診断をOFFにしてBを再ビルドすると、従来の検証済みB
  48,332バイト、SHA-256
  `7DB4B42D476825F6E0296E16B60BE66D64E6558BA312E95A4F152FEE3C206D12`
  とバイト単位で一致する。
- 元の最安定版
  `firmware/artifacts/remapper_dual_combined-verified-a-g700-queue64-only-candidate.uf2`
  （SHA-256 `8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`）は
  上書き・削除せず復元用に保持する。
- Aに埋め込まれたBは実行Bと異なる。WebUIの`Flash B Side`を押すと
  実行Bが旧版に戻るため、今回の組合せ試験では押さない。
- Bの厳密な再現に使ったTinyUSB sourceはGit管理外の
  `build-g700-clean-tinyusb-src`にある。
  `tools/apply-host-completion-reserve.py`は検証済みpre-reserve snapshotの
  3ファイルをSHA-256で照合し、実際にビルドしたTinyUSB sourceと同一になる
  差分を再適用する。`--check`はPASSした。差分自体はGit管理できるが、
  入力側のpre-reserve TinyUSB snapshotも現時点ではGit管理外である。
  新しいcheckoutでの完全なソース再ビルドには、このベース依存関係の固定がなお必要。
- 直結・WBT2経由とも10回の成功は低頻度停止の根絶を証明しない。
  1台目への書き込みは別途判断する。
