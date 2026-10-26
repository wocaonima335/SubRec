# CLA Assistant 接入步骤（草稿，未执行）

仓库：新建干净开源仓库 `<OWNER>/SubRec`（占位符）。两种方案，推荐 A。

## 方案 A：cla-assistant.io（SAP 托管，免费，无需 workflow）
1. 【用户本人】写 CLA 文本：可用 Apache ICLA 改写或 Harmony 模板，中英双语。bot 可代起草，用户定稿。
2. 【用户本人】登录 GitHub，建一个 **公开 gist**，文件名如 `CLA.md`，内容为 CLA 文本。
3. 【用户本人】打开 https://cla-assistant.io ，用 GitHub 登录，授权 CLA Assistant OAuth App（需要 repo webhook 权限）。
4. 【用户本人】点 “Configure CLA”：选仓库 SubRec，选第 2 步的 gist，Link。
5. 可选：在 gist 里加 `metadata` 自定义字段（姓名、邮箱、公司）。
6. 【用户本人】仓库 Settings → Branches → 分支保护：Required status checks 勾选 `license/cla`。
7. 验证：用小号开测试 PR，看到 CLA 评论并能签署。
8. 白名单：在 cla-assistant 配置中把 bot 账号（dependabot、github-actions）加入 allowlist。

## 方案 B：contributor-assistant/github-action（签名存仓库内）
1. 【用户本人】同上写 CLA 文本，放到仓库 `CLA.md`（或 gist/URL）。
2. 【用户本人】建 Personal Access Token（fine-grained，仅本仓库 Contents: RW、Pull requests: RW），存为仓库 Secret `PERSONAL_ACCESS_TOKEN`。签名存在同仓库时只用 `GITHUB_TOKEN` 也可，可省此步。
3. 添加 `.github/workflows/cla.yml`（草稿如下，未启用）：
```yaml
name: CLA
on:
  issue_comment: { types: [created] }
  pull_request_target: { types: [opened, closed, synchronize] }
permissions: { actions: write, contents: write, pull-requests: write, statuses: write }
jobs:
  cla:
    runs-on: ubuntu-latest
    steps:
      - if: (github.event.comment.body == 'recheck' || github.event.comment.body == 'I have read the CLA Document and I hereby sign the CLA') || github.event_name == 'pull_request_target'
        uses: contributor-assistant/github-action@v2.6.1
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
          PERSONAL_ACCESS_TOKEN: ${{ secrets.PERSONAL_ACCESS_TOKEN }}
        with:
          path-to-signatures: 'signatures/cla.json'
          path-to-document: 'https://github.com/<OWNER>/SubRec/blob/main/CLA.md'
          branch: 'cla-signatures'
          allowlist: 'dependabot[bot],github-actions[bot]'
```
4. 【用户本人】建 `cla-signatures` 分支（不受保护），分支保护里把 `CLAAssistant` 设为必需检查。

## 需用户本人做的（汇总）
- 定稿 CLA 文本（法律文本，bot 只起草）
- 建 gist / 提交 CLA.md
- 授权 cla-assistant.io OAuth App 或创建 PAT 并设 Secret
- 设置分支保护必需检查
- 批准 push workflow 文件

费用：两方案均免费。
