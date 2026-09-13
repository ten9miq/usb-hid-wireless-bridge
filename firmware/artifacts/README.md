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

書き込み前に、保存済みの購入時バックアップから復元できることを確認してください。
