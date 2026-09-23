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

## WBT2-V4テンキー数字優先の試験候補（主要条件FAIL）

最優先条件はWBT2-V4経由でテンキー数字1～0が意図せず連続・欠落・
ナビゲーション化せず、`/ * = - + . Enter`も正常に入力できること。
NumLock OFF時のHome・PgUp/PgDn・矢印は次点とする。
2026-09-23のRaw Inputでは、`VID_213F:PID_1109`から上段数字が出た後、
同じ入力経路でテンキーのClear・矢印・Home等へ変化した。UB23テンキー自身の
NumLock LEDはONだった。別キーボードのLED ONとWindows NumLock OFFが
併存したが、そのLED不一致だけではWBT2側の独立状態か伝送遅延かは断定しない。

候補`firmware/artifacts/remapper_dual_combined-wbt2-numeric-priority-test.uf2`
（SHA-256 `89DA521DB8241CBFD3AF035BAB1AC1CB4CD0D928759B2C86A7A48C6BE2849135`）は
`WBT2_NUMERIC_KEYPAD_PRIORITY=ON`で、推定NumLock OFFを理由に
Mapping後の上段数字を元のテンキーUsageへ戻す処理だけを無効化する。
`=`のAlt+Numpad変換、descriptor、マウス処理、Mapping、B側実行イメージは
変更しない。OFF時のナビゲーションはこの候補では保証しない。
既存ソースをそのまま再ビルドするとA側が最安定版と一致しなかったため、
旧Aアプリと旧Flash補助ソースを指定してから変更前のA BINが
SHA-256 `7D86008CEF6296C456D02816F347CCA72C6D1002ED01A4E1114B12E69D768C59`
に一致することを確認した。候補Aでは比較可能なA側オブジェクトのうち
`remapper.cc`だけが異なり、B側RAM段は最安定版UF2から抽出したものと一致する。
`tools/verify-firmware-release.py verify --wbt2-numeric-priority`による静的検査は
通過した。2026-09-23に2台目の`G:\INFO_UF2.TXT`でRP2を確認して書き込み、
Gドライブの消失とPC直結での`VID_CAFE:PID_BAF2`列挙を確認した。
WBT2-V4経由の初回試験では、RollerMouse位置跳びなし。
テンキー1は貼付Raw Input中の11回とも上段`1`としてDOWN/UPが対になり、
その範囲ではHome化や数字キーの押しっぱなしは見えなかった。
ただし各数字入力の前後にNumLockタップがWindowsへ到達しており、
同期信号の抑制は未解決である。G700sはWBT2-V4初回接続で完全停止が1回発生し、
抜き差し後の約10回では再発しなかった。低頻度停止がないとは言えない。
`/ * = - + . Enter`、他の数字、複数キーの素早い操作は未検証。
追加のWBT2-V4 Raw Inputでは、NumLockのDOWNが解放されない区間が
1つ目のログで約2.7秒、2つ目のログで約4.5秒と約4.9秒続いた。
その間約30ms間隔でDOWNが繰り返され、ユーザーはテンキーが動作しなかった。
独立した短いNumLockタップの連続ではなく、キーが押下状態に残る現象である。
`NumLock`のDOWN/UPはそれぞれ1つ目のログで70/2、2つ目で255/4。
ログ採取点はWBT2-V4受信後なので、HID-Remapperの出力が保持されたのか、
WBT2側で解放が失われたのかは未確定。候補は数字のHome化を防いだが
「テンキーが安定入力できる」という主要条件に失敗したため採用しない。
続く対照ログでは、同じ試験FWのPC直結出力`VID_CAFE:PID_BAF2`から
数字・演算キー・`.`・Enterが出て、NumLockイベントは0件だった。
WBT2経由出力`VID_213F:PID_1109`では、数字1を1回押した試行でも
NumLockのDOWNが計87回、UPが5回で、数字1は出なかった。
ただし直結ログには`NumpadDot`があり、両試験でWindows/UB23のNumLock状態が
同条件だったとは確認できなかった。後続のOS NumLock OFF・UB23 LED ONを
揃えたPC直結Raw Inputでは、`VID_CAFE:PID_BAF2`からテンキー1の前後に
NumLock DOWN/UPが出た。同期tapの漏れは少なくともHID-Remapper側で起きる。
WBT2経由では短いtapのUPが下流で失われる可能性があるが、その地点は未確定。

別名の**実機入力FAIL候補**
`firmware/artifacts/remapper_dual_combined-wbt2-numeric-23ub-numlock-suppressed-test.uf2`
（SHA-256 `7005EBE11459E4F320568C08BF3F4B2DD6FB81B75005BF8FCF363E4558FDB1A7`）は
数字優先版に`WBT2_SUPPRESS_23UB_NUM_LOCK=ON`だけを追加し、
`VID_0853:PID_0117`のNumLock入力をPCへ出さない。
同じHID-Remapperにつながる他のキーボードのNumLockは変更しない。
`=`のAlt+Numpad変換、descriptor、マウス処理、Mapping、B側実行イメージは
変更しない。比較可能なA側オブジェクトの差は`remapper.cc`のみで、
`tools/verify-firmware-release.py verify --wbt2-numeric-priority --suppress-23ub-numlock`
による静的検査は通過した。実機のNumLock長押し解消や演算キー入力は
未検証である。2026-09-23にユーザーが試験用2台目と確認した
`G:\INFO_UF2.TXT`から書き込み、Gドライブの消失とPC直結での
`VID_CAFE:PID_BAF2`列挙を確認した。これだけでは入力正常の証拠ではない。
書き込み後のPC直結試験で、23UBテンキーの全キーが反応せず、
HID-Remapper Monitorにも同テンキーの入力が見えないと報告された。
ほかのキーボード・RollerMouse・G700sは動くと報告されたため、
全体停止ではなく23UB単体の入力経路に絞られる。この候補は入力要件に
失敗しており採用しない。
23UBだけを抜き差ししてもLEDは点灯する一方、テンキー1と`/`は
Monitorで無反応のままだった。単なる列挙待ちではなく、候補の回帰として
最安定版への復元を優先する。ただしこの結果だけでFWフィルターと
B側の23UB入力受信のどちらが止まったかまでは区別できない。
追加の試験版は書き込まない。2026-09-23に2台目のRP2ブートローダーを
`G:\INFO_UF2.TXT`で確認し、本書冒頭の最安定版UF2を再書き込みした。
Gドライブ消失とPC直結での`VID_CAFE:PID_BAF2`列挙を確認した。
ユーザーは復元後に23UBの入力が再び反応したと報告したため、
23UBだけが無反応になった事象は失敗候補への変更に伴う回帰と扱う。
ただし、フィルター処理・列挙順・メモリ配置のどれが直接原因かは未特定。
入力と両マウスの結果は直結とWBT2経由で
分けて記録する。

次の**実機入力FAIL候補**
`firmware/artifacts/remapper_dual_combined-wbt2-numeric-23ub-output-numlock-suppressed-test.uf2`
（SHA-256 `F9E5E2A25393C2A015C3466218A7B53FB6390715359C061F567BFA212CA490B1`）は
失敗した早期入力レポート分岐を削除し、23UBの通常report処理・Monitor入力経路を
数字優先版と同じに戻す。その後のNumLock状態再注入だけを23UBに限って止める。
23UBのデバイス識別には動的コンテナではなく固定長配列を使う。
ほかのキーボードのNumLock、Mapping、`=`、descriptor、マウス処理、
B側RAM段は変えない。数字優先版との比較可能なA側オブジェクトの差は
`remapper.cc`だけで、`verify --wbt2-numeric-priority --suppress-23ub-numlock`は
通過した。2026-09-23に試験用2台目の`G:\INFO_UF2.TXT`でRP2を確認して
書き込み、Gドライブ消失とPC直結での`VID_CAFE:PID_BAF2`列挙を確認した。
PC直結の実機試験では23UBの全キーが再びMonitorでもWindowsでも無反応。
ほかのキーボードには物理NumLockキーがなく、これを試験条件に含めたのは誤り。
この候補も採用しない。2026-09-23に試験用2台目の`G:\INFO_UF2.TXT`を確認して
本書冒頭の最安定版を再書き込みし、Gドライブ消失とPCでの
`VID_CAFE:PID_BAF2`列挙を確認した。その後、ユーザーは23UBのテンキー1と`/`が
Monitor・Windowsの両方で再び反応すると確認した。
2方式の抑制候補がともに23UB全キー無反応を起こしたので、以後は原因を
測定せずに別の抑制版を書き込まない。
最安定版FWとMappingを固定したまま、23UBのDIP3をON（NumLock連動）に
変更して確認したところ、PC直結とWBT2-V4経由の両方でテンキー入力は
問題なく動作しているようだとユーザーが報告した。
Windows NumLockがOFFの状態ではテンキー1がHomeになり、23UBのNumLockを
OFF/ONと切り替えた後は数字が入力された。そのときWindows側のNumLockも
ONになっており、WBT2-V4経由の23UB NumLockキー操作でWindowsの
NumLock状態が切り替わることをユーザーが確認した。
接続直後にWBT2とWindowsのLED状態がどう同期するかは未計測である。
DIP3 ONは現時点の有望な可逆対策であって、数字・`/ * = - + . Enter`の
連続操作、NumLock長押しの再発、RollerMouse・G700s同時動作を十分に
反復確認するまではWBT2経由の正式な合格扱いにしない。

DIP3 ONのまま数字1～0→上段数字のMapping10件だけを除いた
`backups/backups_20260923_224054/hid-remapper-config-dip3-linked-native-keypad.json`
（SHA-256 `4964EFD5B45FDE48CA5C24F3F755AC2B874FB8568C5F6EB09523DF8DC5232536`）を
2台目に適用した試験では、数字だけが二重入力され、記号とEnterは1回だった。
キーボード全体の一時的な無反応は抜き差しで解消し、Mapping変更とは分離する。
数字Usageは未割当pass-throughで出力され、FWのNumLock OFF補正が同じUsageを
出力へ再追加する場合、6KRO arrayに重複する経路がコード上存在する。
この候補Mappingは不採用とし、通常運用では元のcomplete設定へ戻す。

### DIP3 ONでの旧fair-multi-input版の比較試験（試験用2台目・不採用）

ユーザーが以前の安定版として指定した
`firmware/artifacts/remapper_dual_combined-wbt2-fair-multi-input.uf2`
（SHA-256 `D4EF266CA7BA9ABFCF83A83C3C68980E95DAB825FE05941ECABB769A2F658980`）を
試験用2台目だけで比較する。保存済みUF2のA Flash payloadは244,992バイト、
SHA-256 `F558F83028C2E94D1AB2ABDE149CD2B8DCE6DA71764F83BE41C8C4133F354D00`、
B側RAM段は287ブロック、SHA-256
`03639D06680176AE07E403F29704805B32EAF69F7738AD5337FB8C02C5821C4E`。
旧Aの一致するELF/BINは残っていないため、
`tools/verify-firmware-release.py verify --historical-fair`で
アーカイブUF2全体・A/B各段・分離形式の固定値を検査し、静的検査を通した。
これは新規ビルドのA側BIN検査の代わりになるアーカイブ限定の検査である。
現行最安定版よりA/Bとも古く、`=`・JISキーやG700sの現行改善を維持するとは
限らない。DIP3 ONとMapping条件を記録し、直結とWBT2経由で
テンキー数字・記号・`=`、RollerMouse停止時の跳び、G700s停止を再確認する。
試験終了後に現行最安定版へ戻せるよう、そのUF2は変更しない。

2026-09-23、上記の固定ハッシュ・A/B構造検査を再実行してPASSを確認。
試験用2台目の`G:\INFO_UF2.TXT`でRP2ブートローダーを確認後、上記UF2を
`G:`へコピーした。コピー後にGドライブは消え、Windowsで`USB\VID_CAFE&PID_BAF2`の
列挙を確認した。これは起動・列挙の証拠のみであり、テンキーやマウスの動作結果ではない。
このアーカイブUF2はA/B双方を含むため、比較試験ではWebUIの`Flash B Side`を押さない。

ユーザーの初回実機報告では、keypad→数字のMappingを削除した状態で、
テンキー数字と`=`以外の記号は入力できた。サクラエディタで`=`は`U+0010`となり、
RollerMouseの位置跳びは発生しなかった。一方、キーボードとG700sの完全停止は
頻度が増えたように感じるとの報告があり、この版は現行最安定版の代替として不採用。
添付Raw InputはAltとNumpad1/6の押下を示すが、`VID_213F:PID_1109`のため
WBT2-V4側のログであり、「PC直結」の試験と同一経路かは未確認。
各症状の発生回数と再現条件も未計測で、因果関係は断定しない。

### 最安定版＋native keypad重複防止候補（直結入力は部分PASS・完全停止はFAIL）

旧fair版をいったん最安定版へ戻す工程は不要。試験用2台目には次の候補を
直接書き込んで比較する。候補は
`firmware/artifacts/remapper_dual_combined-best-stable-native-keypad-dedup-test.uf2`
（SHA-256 `C6ADCB9BD12A4AA6D8FB315DD6E301BA56416BA0A0A49F1651E01739D94190AB`）。
最安定版の`NATIVE_KEYPAD_DEDUP=OFF`再ビルドBINから作った結合UF2が
SHA-256 `8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`
で最安定版と完全一致することを確認した。その後、このoptionだけをONにして
Aを再ビルドし、B RAM段は最安定版から固定抽出した。
`tools/verify-firmware-release.py verify --native-keypad-dedup`はPASS。
新しい処理は、Mappingなしで既に出力レポートにあるkeypad Usageを
NumLock OFF補正が二重追加しないようにする。Mappingが上段数字を出した場合は
従来どおりkeypadへ置換する。`=`・JISキー変換、RollerMouse/G700sに関わる
B段は変更しない。
静的検査だけではテンキーやマウスの実機合格を意味しない。
試験条件は2台目・DIP3 ON・keypad→上段数字のMapping削除状態を固定し、
PC直結とWBT2-V4経由を分けて数字、`/ * = - + . Enter`、
NumLock ON/OFF、RollerMouse停止時の跳び、G700s/キーボード完全停止を確認する。
候補のAに埋め込まれたBは実行Bと異なるため、`Flash B Side`を押さない。
2026-09-23、候補のSHA-256と`verify --native-keypad-dedup`のPASS、
試験用2台目の`G:\INFO_UF2.TXT`でRP2ブートローダーを再確認して書き込んだ。
コピー後にGドライブが消え、Windowsで`USB\VID_CAFE&PID_BAF2`が列挙された。
この時点では書き込みとUSB起動のみを確認し、入力とマウス動作は未確認だった。
続くユーザーのPC直結実機確認では、Mappingのkeypad→上段数字を削除した状態で
テンキー数字は1回ずつ入力され、`=`と記号も正常、RollerMouseの位置跳びはなし。
一方、G700sとキーボードの完全停止頻度は増えたとの報告であり、
全入力の安定性という必須条件はFAIL。停止回数・試行回数は未計測。
初回の比較報告は最安定版の実機確認時とMapping条件も異なったため、
その結果だけで停止増加をFW差の因果効果とは断定しない。
WBT2経由はこの時点で未確認。
ユーザーはその後、元のcomplete Mappingでも停止増加を検証済みと明言した。
したがってMapping再適用による同じ切り分けの再実施は不要とする。
ただし提出された`C:\Users\ruin_\Downloads\hid-remapper-config.json`は保存済み
complete設定よりテンキー数字1～0→上段数字の10件だけが少ない35件構成で、
添付のexportがどの試験時点のものかは不明。現在デバイスに保存された設定を
この添付だけで断定しない。旧最安定版との停止頻度の定量比較は未実施だが、
通常使用候補としては本版を不採用とする。
2026-09-23、試験用2台目の`G:\INFO_UF2.TXT`を確認し、SHA-256
`8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`の
最安定版combined UF2を書き戻した。Gドライブ消失とPC直結での
`USB\VID_CAFE&PID_BAF2`列挙を確認。実機入力と両マウス動作は復元後未確認。
UF2書き込みはデバイス内Mapping設定の確認・変更を意味しない。

### A側テンキー重複防止＋B側完了イベント用キュー予約候補（2台目でPC直結PASS）

`firmware/artifacts/remapper_dual_combined-native-keypad-dedup-completion-reserve-test.uf2`
（SHA-256 `72CCE415BFCBF8E5023AA40CB307DCDC3A552C511F6AF3F9EC90CB0AE98A224F`）は、
単独試験でテンキー数字の二重入力を止めたA段と、ホストキュー逼迫時だけ
SOF通知を間引いて転送完了用の空きを確保するB段を組み合わせた候補。
`tools/verify-native-keypad-reserve-candidate.py`はA/B各段とbuild optionを検査してPASS。
試験用2台目でDIP3 ON、テンキー数字→上段数字の10件を除いた35件Mapping
（ユーザー提供export SHA-256
`096E477D00376F1D1DEA9DD3710E06EB264D0DFDF0FD2244414B2932F94CF1D9`）の下、
PC直結でテンキー1/0は1文字ずつ、`/ * = - + . Enter`は各1回、NumLock OFFの
矢印・Home・PgUp・PgDnは正常。4機器同時接続でUSB-C再接続10回すべて
RealForce通常キーとG700sが動き、RollerMouse移動→停止の位置跳びは0回。
低頻度停止の根絶までは証明しない。ユーザーは続いてPC直結のJIS
`0x87`/`0x89`の正常入力を報告した。WBT2-V4経由については「どれも起きない」
との回答があり、その後「すべて正常」と明示された。したがってWBT2-V4経由の
1回の接続では、テンキー数字・記号の単発入力、NumLock OFF時の矢印など、
キーボード・G700s動作、RollerMouse停止時の位置跳びなしを確認済みとする。
ただしWBT2経由の反復USB再接続と記号・数字の連続操作は未実施であり、
過去のNumLock押しっぱなし・G700s停止の再発がないとはまだ言えない。
その後ユーザーは、WBT2-V4経由でも4機器接続のUSB-C再接続10回と、
テンキー数字・`/ * = - + . Enter`の連続入力で問題が発生しなかったと報告した。
PC直結とWBT2経由の報告済み回帰条件を通過した、現時点で最も広く実機確認された
**新運用候補**とする。ただし10回で低頻度停止の根絶を証明したわけではない。
診断数値、A/Bそれぞれの修正機序、確認範囲は
`docs/2026-09-24-keypad-host-queue-resolution.md`に分けて記録した。
旧最安定UF2は上書き・削除せず復元用に保持する。新候補のソース固定と
A埋込みB/実行Bの不一致という保守課題を整理する前に、これを正式リリースや
1台目への自動展開として扱わない。
したがって本書冒頭の最安定運用版はまだ置き換えず、1台目へ書き込まない。
この候補もA埋込みBと実行Bが異なるので`Flash B Side`を押さない。
ビルドに使った最安定BのTinyUSB sourceはGit管理外の保存コピーであり、
`tools/apply-host-completion-reserve.py`で今回の3ファイル差分と前後ハッシュを
固定した。ただしpre-reserveのTinyUSB source自体はGit管理外のため、
新規checkoutからの完全再ビルドにはそのベース依存関係の固定が残る。

## 未解決課題: A側の埋込みBと実行Bを一致させる

状態: **未着手。現在の最安定運用版は変更しない。** 現在A側に埋め込まれている
Bイメージは従来の48,644バイト版、結合UF2のRAM書き込み段がB側へ入れるのは
キュー64件の48,332バイト版である。通常動作では後者が使われるが、
HID Remapper Configurationの`Flash B Side`を押すと前者でB側が上書きされる。
これが操作ミスや2台の構成差を生む保守上の課題である。

将来の整理では、Aに埋め込むBをキュー64件版と揃え、`Flash B Side`後も
同じ実行Bになる候補を**別名UF2**で作る。ただしA側イメージも変化するため、
現行版のRollerMouse位置跳びなしという実機結果を流用しない。
descriptor・マウスレポート・Mappingなど、埋込みB以外の変更は同時に行わない。
候補のB実行イメージとA埋込みBの一致を静的検査し、HID-Remapper直結で
RollerMouse移動→停止10回以上、G700sを接続したままUSB再接続10回以上、
RealForce通常キー・上段数字・NumLock ON/OFFテンキー・`=`・JISキーを
それぞれ実機確認する。低頻度停止は10回だけで根絶を証明できない点も記録する。
失敗時は本書冒頭の最安定運用版（SHA-256 `8BC16D3D69B0ECD2149AC03A50E4256EB67357439FB081CE4DEF09E0AD6F6DD3`）へ戻す。

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
