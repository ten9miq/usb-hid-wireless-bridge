# ビルド成果物

`remapper_dual_a-wbt2-simple-reports.uf2` はPC接続側（A側）用、
`remapper_dual_b-wbt2-simple-reports.uf2` は入力デバイス側（B側）用です。

`remapper_dual_a-wbt2-boot-interfaces.uf2` は、単純化版で解消しなかった
WBT2-V4互換性を切り分けるため、A側をReport IDなしのBoot Keyboard／
Boot Mouse別interfaceにした次段階の検証用です。B側は既存の
`remapper_dual_b-wbt2-simple-reports.uf2`をそのまま使用します。

Boot Mouse互換性を優先したため、このA/Bではwheel出力を省略しています。

書き込み前に、保存済みの購入時バックアップから復元できることを確認してください。
