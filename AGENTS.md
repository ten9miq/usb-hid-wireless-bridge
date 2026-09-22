# Repository rules: HID-Remapper dual firmware

- FWの変更・生成・書き込みでは、最初に`docs/firmware-release-policy.md`を読む。
- 動作確認済みの`remapper_dual_combined-current-features-historical-embedded-b-ab.uf2`
  （SHA-256 `C144E09826BB9EBF63C989E08C0EEE983A7B5A7ACB63E729D3FD62686F05EAC2`）を
  上書き・削除しない。候補UF2は別名で作る。
- `DUAL_B_BINARY_OVERRIDE`に使うB用データは動作確認済みUF2から
  `tools/verify-firmware-release.py extract-b-hex`で再生成する。
  実行用B側は同スクリプトの`extract-b-uf2`で再生成する。
  Git管理外の古いbuild出力を唯一の入力にしない。
- A側UF2は最新の`remapper_dual_a.elf`から明示生成し、B側は
  `flash_b_side.uf2`を使って`combine_uf2.py`で結合する。
  `remapper_dual_b.uf2`をB側として結合しない。
- 候補の書き込み前に`tools/verify-firmware-release.py verify`を通し、
  A側BIN、固定埋込みB、実行B、UF2分離形式、release用CMake optionを検査する。
  静的検査だけで位置跳び解消や入力正常を宣言しない。
- リリース判定にはHID-Remapper直結でRollerMouseとG700sを同時接続し、
  RollerMouseの移動→停止を10回以上、RealForceのNumLock・テンキー・`=`・
  JISキーを実機確認する。WBT2-V4に影響する変更ならWBT2経由も別に確認する。
  未実施・失敗項目を成功と記載しない。
- 書き込みは`G:\INFO_UF2.TXT`でRP2ブートローダーを確認した場合に限る。
  書き込み後のGドライブ消失はブートローダー離脱の証拠であり、USB列挙や入力動作の
  成功証拠ではない。
