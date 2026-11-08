# 品牌字符串

本目录不再存放 grd/xtb 的静态快照。

`scripts/deploy_resources.py` 在 deploy 阶段直接从**当前 Chromium 树**里的上游文件派生品牌字符串：

- `chrome/app/chromium_strings.grd` + `chrome/app/resources/chromium_strings_*.xtb`
  → `chrome/app/{branding_path_component}_strings.*`
- `components/components_chromium_strings.grd` + `components/strings/components_chromium_strings_*.xtb`
  → `components/components_{branding_path_component}_strings.*`

派生规则：文件引用中的 `chromium_strings_` 换成品牌名，文本里的 `Chromium` 换成 `product_display_name`。

原因：旧做法保存了某个 Chromium 版本的整份 grd 快照，每升一个大版本就会缺消息、带着已被删除的 grit 变量
（例如 `enable_glic`），导致构建失败。

注意：xtb 里翻译条目的 id 是原文指纹，原文改成品牌名后部分翻译不再匹配，界面会回退为英文。
