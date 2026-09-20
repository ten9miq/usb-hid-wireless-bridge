# ビルド成果物

`remapper_dual_a-wbt2-simple-reports.uf2` はPC接続側（A側）用、
`remapper_dual_b-wbt2-simple-reports.uf2` は入力デバイス側（B側）用です。
現行B側はTinyUSBの既定Boot protocol強制を無効にし、接続デバイスの
Report protocol descriptorをそのまま使用します。複数interface／collectionを
VID/PID固有quirkなしで扱うための変更です。

`remapper_dual_a-wbt2-boot-interfaces.uf2` は、単純化版で解消しなかった
WBT2-V4互換性を切り分けるため、A側をReport IDなしのBoot Keyboard／
Boot Mouse別interfaceにした次段階の検証用です。B側は既存の
`remapper_dual_b-wbt2-simple-reports.uf2`をそのまま使用します。

現行版は、動作確認済みの3 byte mouse reportへwheelを末尾追加した4 byte版です。
Keyboard descriptorは標準Boot範囲`0x00`–`0x65`を宣言し、範囲内のテンキー
usage `0x59`–`0x63`を6KRO arrayで送信します。
Keyboard Outputは5 LED bitsを保持し、NumLockなどをB側の物理キーボードへ
既存のusage-based mapping経路で転送します。

また、usage範囲とReport Countが一致する1-bit NKRO bitmapを誤ってArray宣言する
入力descriptorは、構造が一致する場合だけVariableとして解釈します。これは
Realforce系でNumLock／テンキーusageが別usageへ化ける問題に対する汎用補正で、
VID/PID固有分岐は使用しません。

入力usage状態差分で`NumLock tap → Keypad press/release → NumLock tap`という
完全な同期列を検出した場合だけ、前後のNumLock tapを除去します。列が完成しない
場合は保留したNumLock DOWN/UPを連続frameへ対で再生し、20 msを超えるNumLock
長押しも対応するreleaseを含めて通常転送します。判定にVID/PIDは使用しません。

`remapper_dual_combined-wbt2-multi-mouse.uf2`は、A/B両側を含む通常運用向けの
結合版です（`HID_HOST_DIAGNOSTICS=OFF`）。同じhub portへ複数のHID mouse
interfaceが対応する場合も、相対X/Y・wheelをmapping frame内で加算し、buttonは
interfaceごとの押下状態を保持して統合します。VID/PID固有分岐は使用しません。

`remapper_dual_combined-wbt2-fair-multi-input.uf2`は上記を含む通常運用向けの
更新版です（`HID_HOST_DIAGNOSTICS=OFF`）。B側UARTが混雑した場合、保留中の
HID interfaceをround-robinで先に転送し、保留が残っている間に高頻度interfaceの
新着reportがUART空きを先取りしないようにします。VID/PID固有分岐は使用しません。

`remapper_dual_combined-wbt2-hid-rearm.uf2`は上記を含む通常運用向けの更新版です
（`HID_HOST_DIAGNOSTICS=OFF`）。B側で接続中のInput interfaceとUART転送待ちreportを
`dev_addr + instance`ごとに保持し、別デバイスの列挙後もidleになった各interfaceを
独立に再armします。VID/PID固有分岐は使用しません。

`remapper_dual_combined-wbt2-hcd-polling-fairness.uf2`は上記を含む通常運用向けの
更新版です（`HID_HOST_DIAGNOSTICS=OFF`）。B側RP2040 HCDがactiveなasynchronous
endpointをSOFごとにone-hot round-robin選択し、1 endpointの連続再armやNAKが
他のInput interfaceを飢餓させないようにします。VID/PID固有分岐は使用しません。
SHA-256: `8AE0736398DDB30FCC9E325F63B65F49B1878EA8F202A16AE2513BE677AEF517`

`remapper_dual_combined-wbt2-mouse-buttons-45.uf2`は上記を含む通常運用向けの
更新版です（`HID_HOST_DIAGNOSTICS=OFF`）。PC側のcombined keyboard/mouse reportと
Boot Mouse interfaceのどちらもButtons 1–5を宣言し、Button 4/5を先頭byteのbit 3/4で
送ります。残り3 bitのpaddingにより、X/Y/Wheelを含むmouse payloadは従来どおり4 byteです。
SHA-256: `8179FB0D29233C9B7F7FEACFB24D277402632ABD017BA64372A2A89EB0416140`

`remapper_dual_combined-wbt2-hid-host-diagnostics-parsed-usage.uf2`は診断専用の
結合版です（`HID_HOST_DIAGNOSTICS=ON`）。A側でMonitorに渡した相対Cursor X/Yを
report ID `102`のevent `11`として記録します。通常版と混在させず、診断後は通常版へ
戻してください。

`remapper_dual_combined-wbt2-hid-host-diagnostics-hcd-snapshot.uf2`は、上記診断に
event `15`のRP2040 HCD状態を追加したA/B結合版です。Heartbeat直後にEPX、
`int_ep_ctrl`、15個のinterrupt endpoint slot、列挙失敗counter、HID instance数を
固定payloadで送ります。B側では通常diagnostic FIFOを介さずA側への優先枠で送るため、
HID report転送が継続していてもA側のHeartbeat直後に観測できます。G700s／RollerMouseの2台目接続前後を
`tools/hid-host-diagnostics.html`で比較する用途に限り、診断後は通常版へ戻してください。
SHA-256: `6F860B2FBAD9BAD3364BCDB43B2C50C41C1D90FEDFC7B07BA37ADEC58D4D654C`

書き込み前に、保存済みの購入時バックアップから復元できることを確認してください。
