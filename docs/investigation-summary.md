# USB HID 無線ブリッジ 調査・実装総括

最終更新: 2026-09-15

この文書の「現在」「最新の実機状態」は調査当時の記録です。2026-09-23に
確認された動作版、位置跳びA/B結果、今後のFW改修ルールは
[firmware-release-policy.md](firmware-release-policy.md)を参照してください。

この文書は、HID Remapper V5.1 と WBT2-V4 を組み合わせた USB HID 無線ブリッジの調査記録です。ここでいう「確認済み」は実機観測または保存済みの成果物で裏付けられた事実です。「実装済み」はソースまたは UF2 成果物に変更があることを示すだけで、実機への書き込み・長時間動作確認まで済んだことは意味しません。

## 目的と構成

有線 USB HID キーボード／マウスを 2.4 GHz 無線化し、WBT2-V4 を経由して PC から通常の USB HID デバイスとして扱えるようにすることが目的です。

- **A側（PC側）**: HID Remapper の USB device 側。WBT2-V4 から見えるキーボード、マウス、Consumer Control、Config/Monitor の HID 出力を提供する。
- **B側（入力機器側）**: HID Remapper の USB host 側。物理キーボード／マウスを受け、シリアル経路を介して A側の remapping 処理へ渡す。
- **WBT2-V4**: A側と B側の間を無線化する既存の送受信機。USB descriptor と report 形式の解釈差が、本件の互換性問題の主な切り分け対象である。

用語と A/B の役割は [hid-report-compatibility.md](hid-report-compatibility.md) および [wbt2-v4.md](wbt2-v4.md) に合わせています。

## バックアップと利用ツール

書き換え前の購入時状態は `backups/backups-20260913-160849/` に保存済みです。

- RP2040 情報: `hid-remapper-v5.1-original-info.txt`
- Flash 全域: `hid-remapper-v5.1-original.uf2` と `hid-remapper-v5.1-original.bin`
- Web UI 設定: `hid-remapper-config.json`

保存には `tools/backup-hid-remapper.ps1` と同梱 `tools/picotool/picotool.exe` を使います。descriptor の静的検査は `tools/validate-wbt2-descriptor.ps1`、パッチ再現には `patches/wbt2-v4-simple-reports.patch` を用います。バックアップの存在は確認済みですが、ここでは復元を書き込み実施済みとは記録しません。

## 主な変更履歴

### 互換性調査と Boot interface 化

- 最初の A/B では、既存 interface を維持しつつ、Keyboard を 6KRO array、Mouse を 3 button + 8-bit X/Y/Wheel に単純化した。
- 次段階で `remapper_dual_a` の出力を、Report ID なしの Boot Keyboard、Boot Mouse、Consumer Control、既存 Config/Monitor の別 interface に分離した。内部の report ID と保存済み mapping 形式は維持し、USB 送信直前だけ interface へ振り分ける。
- Boot Mouse は、移動と左右クリックが確認できた 3 byte 版から、wheel を追加した 4 byte 版へ拡張した。
- B側は Boot subclass を一律 Boot protocol に切り替えず、Report protocol と descriptor に従って受信するようにした。複数 interface／collection を VID/PID 固有分岐なしで扱うための変更である。

### Keyboard/Mouse report と NumLock

- Keyboard descriptor は標準 Boot 範囲 `0x00`–`0x65` に合わせ、テンキー usage `0x59`–`0x63` を 6KRO array で送れるようにした。
- Keyboard LED Output は 5 bit をキャッシュし、既存の usage-based output mapping を通じて B側の物理キーボードへ戻す。Output `GET_REPORT` でも同じキャッシュ値を返す。
- descriptor 上は Array だが実データは 1-bit NKRO bitmap である矛盾を、usage 範囲・Report Count・logical range がそろう場合だけ Variable として扱う汎用補正を追加した。
- Realforce のテンキー同期列を対象に、完全な `NumLock tap → Keypad press/release → NumLock tap` のときだけ前後の NumLock tap を除去するロジックを追加した。不完全な列は NumLock DOWN/UP を対で再生する。

### RollerMouse、endpoint、UART の対策

- RollerMouse のような **Output report だけの HID interface** に interrupt IN 受信を要求しない guard を追加した。control endpoint（アドレス 0）へ誤って受信転送を出さないための防御である。
- HID host の割当数を増やし、不要な interrupt OUT endpoint を開かず、Input report を持つ interface に限って IN endpoint を遅延確保する TinyUSB パッチを用意した。これは RP2040 host の endpoint 枯渇を切り分けるための変更である。
- RP2040 の EPX control transfer と自動ポーリング interrupt endpoint が競合し得る HCD の race 対策パッチを用意した。パッチ適用後の実機検証は別途必要である。
- B側ではシリアル送信が詰まった場合、HID report を interface ごとに保留し、`tuh_task()` をブロックせずに再送する経路を追加した。保留中は次の受信を再 arm しないため、key/button の release を途中で捨てたり順序を逆転させたりしないことを意図している。

この節の endpoint/HCD/UART 項目は、現在の作業ツリーにあるソース／パッチ上の実装状況です。ビルド済み UF2 への反映や実機書き込みは、この文書だけからは主張しません。

## 確認済みの事実

- Realforce 23UB を PC へ直接接続して Raw Input を取得した際、数字 5 の一回の操作で `NumLock DOWN/UP → Numpad5 DOWN/UP → NumLock DOWN/UP` の列を観測した。
- PC へ直結した状態では、G700s と RollerMouse の 2 台のマウスは同時に正常動作する。
- HID Remapper を介在させると、両マウスが同時に停止する事象を確認した。
- 事象は接続順に依存する。後述の現在の比較では、RollerMouse を先に接続してから G700s を追加した場合は動作している。
- Boot interface 分離版では、WBT2-V4 経由でマウス移動と左右クリックが動作した。Wheel とテンキーについては、この成功だけで解決済みとはしない。
- `c` / `v` の無反応は mappings 全削除後に解消した。firmware の descriptor 問題とは独立して、保存されたポート依存 mapping の影響を確認すべきである。

## 現在の既知状態

現在は購入時のオリジナル版へ戻して比較中です。以下はこの比較中に得られた観測であり、原因確定ではありません。

| 条件 | 観測 |
|---|---|
| G700s と RollerMouse を起動前から同時接続 | 両方とも停止する |
| RollerMouse を先に接続し、起動後に G700s を追加 | 動作する |
| PC 直結で 2 台を同時接続 | 正常に動作する |

追加のMonitor観測では、RollerMouseを先に接続してからG700sを追加した場合、RollerMouseの入力は継続するがG700sの入力は現れなかった。G700sを先に接続してからRollerMouseを追加した場合は、RollerMouseの接続直後に両方の入力が停止した。このため、G700s接続時の複合HID列挙失敗、またはG700s接続処理によるB側USB host停止が有力である。

したがって、単一マウスの基本機能だけでなく、HID Remapper host 側の複数 device/interface の列挙・endpoint 割当・report 再 arm・接続順を含めて比較する必要があります。

### 最新の実機状態

HCD interrupt polling の公平化版を書き込み、HID Remapper → WBT2-V4 → 2.4 GHz receiver → PC の構成で、RollerMouse と G700s の同時動作を確認した。G700sのマクロ入力 `_` もWBT2経由で正常に入力できるため、G700s Vendor HID対応は現時点で解決済みとして扱う。

残っている実機課題は次の3点である。

1. RollerMouse操作中にマウスポインタが跳ぶことがある。polling公平化後の相対レポートのタイミング・合成を要確認。
2. Realforceテンキーの数字入力時、数字は入るがNumLock同期イベント由来と思われる動作音が毎回鳴る。
3. RealforceキーボードのFn+F3電卓起動、および一部メディアキー（例: 一時停止）がWBT2経由で動作しない。

### 安定版と後続descriptor変更の比較

RollerMouseの位置跳びについて、問題が発生しない個体からフラッシュを読み取り、保存済みUF2と比較した。その個体は `remapper_dual_combined-wbt2-fair-multi-input.uf2`（SHA-256 `D4EF266CA7BA9ABFCF83A83C3C68980E95DAB825FE05941ECABB769A2F658980`）と完全一致した。

一方、後続のproduction版では、HID Hostのpolling処理ではなく、Consumer Control descriptorが変更されている。具体的には、AC Home、AL Email Reader、AL Consumer Control ConfigurationのUsageを追加し、Consumer reportの1-bit項目を9個から12個へ、paddingを6 bitから3 bitへ変更した。

Button 4/5対応を追加する前のproduction版でもRollerMouseの跳びが発生したため、Button descriptor変更だけでは説明できない。診断ログは安定版・production版の双方で無効であることから、ログ削除が原因とは考えにくい。現時点では、Consumer Control descriptor変更が複合HIDの解釈またはレポート処理タイミングへ副作用を与えた可能性を有力仮説として記録する。ただし、同じfair版を基準にConsumer descriptorだけを戻した比較版による実機A/Bが未実施のため、確定原因ではない。

## 未解決事項と次の切り分け

## Upstream HID Remapper の既知情報（2026-09-15調査）

公式READMEは、USBハブ経由で複数デバイスと無線レシーバーを接続できる仕様を掲げている。一方、公式Issueには次のような近い事例が残っている。

- [Issue #248](https://github.com/jfedor2/hid-remapper/issues/248): v3ボードでキーボードとマウスをUSBハブ経由で同時接続できない報告。解決済みの修正や具体的な解決策は記録されていない。
- [Issue #200](https://github.com/jfedor2/hid-remapper/issues/200): 起動時から接続していたデバイスが反応せず、起動後に抜き差しすると動作する報告。今回の「起動前同時接続」と「後から追加」の差に近い。
- [Issue #267](https://github.com/jfedor2/hid-remapper/issues/267): 特定マウスが起動時には認識されず、起動後の接続では動作する報告。

リリース状況も確認した。ローカルに取り込んでいる `r2026-05-25` は公式の安定版リリース（コミット `722ea05`）と一致する。2026-08-29の公式プレリリース `r2026-08-29-test`（コミット `7df72c1`）はPico W/Pico 2 W向けBluetooth Classic版の追加が中心で、RP2040 dual USB hostの複数レシーバー列挙問題を解消したリリースノートはない。

したがって、現時点で「最新版に更新すれば今回の2レシーバー問題が解消する」と判断できる公式根拠はない。公式仕様上は複数デバイス対応だが、複数HID・起動時列挙・レシーバー相性に関する未解決報告があり、今回の症状はHID Remapper本体／RP2040 USB host側の既知カテゴリと整合する。ただし、G700sとRollerMouse固有の組み合わせが公式Issueで確認されたわけではない。

### 未解決

- 起動前同時接続で両マウスが停止する直接原因は未確定である。endpoint 容量、EPX/interrupt race、Output-only interface への受信要求、または複数 report の転送滞留はいずれも候補であり、現時点では仮説にとどまる。
- Wheel とテンキーが WBT2-V4 経由で安定して動作するかは、条件をそろえた再試験が必要である。
- NumLock 同期列の除去と LED 逆方向転送はソースに実装されているが、対象実機での長時間・例外操作を含む動作確認は、この文書では未実施扱いとする。
- endpoint/HCD/UART 対策を含む現在の作業ツリーについて、対象 UF2 のビルド・A/B への書き込み・実機確認は未記録である。

### 次の切り分け

1. オリジナル版で、接続順を固定した再現表を作る。G700s 単独、RollerMouse 単独、両方を起動前接続、RollerMouse 先行後に G700s 接続、逆順を同じ USB port/hub 条件で比較する。
2. 両マウス停止時に、各 device/interface の列挙、endpoint 割当、IN report の再 arm 成否、serial queue の保留状態を取得する。単に「認識した」かではなく、両 device の report が A側まで到達するかを確認する。
3. Output-only guard を含む最小変更版をビルドし、購入時バックアップを保持したまま A/B に限定して書き込む。書き込み実施日時、対象ボード、UF2 のハッシュ、接続順、結果を記録する。
4. endpoint 容量／遅延 IN endpoint パッチ、次に HCD race パッチ、最後に UART backpressure を一つずつ加え、各段階で同じ再現表を比較する。複数の候補を同時に有効化して原因を混ぜない。
5. WBT2-V4 経由では、Keyboard、テンキー、Mouse X/Y、左右クリック、Wheel、NumLock 単独操作、Realforce の数字入力を個別に確認し、PC 直結結果と分離して記録する。

詳細な descriptor と protocol の根拠は [hid-report-compatibility.md](hid-report-compatibility.md)、実機互換性の試験表は [wbt2-v4.md](wbt2-v4.md)、書き換え前の保存手順は [hardware.md](hardware.md) を参照してください。
