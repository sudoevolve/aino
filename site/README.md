# 官网与 Wiki 阅读器

`index.html` 提供官网首页和 Wiki 阅读界面。正文直接 fetch 仓库的 `docs/wiki/*.md`，
侧栏从 `docs/wiki/README.md` 的教程表生成。修改教程不需要另写网页正文。
工程审查保留独立仓库入口，不列入正式教程。
页面参考 [Aino Player 官网](https://sudoevolve.github.io/aino-player/) 的品牌栏、导航、侧栏与文档式排版，支持深浅主题和手机阅读。

## 本地预览

从仓库根目录启动静态服务器：

```sh
python -m http.server 8000 --bind 127.0.0.1
```

打开 `http://127.0.0.1:8000/site/`；直达教程使用
`http://127.0.0.1:8000/site/?page=01-quickstart.md`。
浏览器会限制 `file://` 的 fetch，因此不要直接双击 HTML。

## 打包与发布

从仓库根目录打包静态网站：

```sh
python tools/build_site.py
python -m http.server 8000 --directory build/site --bind 127.0.0.1
```

打开 `http://127.0.0.1:8000/`。脚本只替换专用输出目录 `build/site/`，
将页面、渲染依赖、图标、截图与仓库原始 `docs/wiki/*.md` 一起打包。
教程仍在浏览器中按 Markdown 加载，源码只维护一份；修改教程后重新打包发布即可。

仓库提供手动触发的 [Website 工作流](../.github/workflows/pages.yml)：

1. 将网站源码提交到 GitHub 仓库。
2. 在仓库 Settings → Pages 中将 Source 设为 GitHub Actions。
3. 在 Actions → Website 中选择分支，执行 Run workflow。

工作流上传 `build/site/` 并部署到 GitHub Pages，不随普通 push 自动发布。
默认项目页地址为 `https://sudoevolve.github.io/aino/`，实际地址以部署输出为准。
工作流文件本身不会启用 Pages；首次发布需要完成上述仓库设置。

## 其他静态托管方式

同源托管时，保留 `site/`、`docs/wiki/`、`assets/`、`screenshots/` 的相对位置。
若仅托管 `site/` 内容，将 `index.html` 的 `aino-repository-base` 改为
`https://raw.githubusercontent.com/sudoevolve/aino/main/`，同时将 favicon 路径改为绝对资源地址。
在线教程和图片就直接读取仓库；需要网络和静态主机 CSP 的 connect-src/img-src 许可。
`aino-repository-ref` 控制“查看 Markdown”源码链接，读取固定 tag/commit 时应同步修改两个配置。
教程总览入口为 `?page=README.md`。

读取失败、HTTP 错误、未知教程和超时会显示错误及重试入口。教程支持代码块、表格、
图片、相对 Markdown 链接、章节锚点、前后篇、标题筛选、浏览器前进后退、深浅主题和窄屏目录。
筛选只搜索教程标题；没有全文索引或服务端搜索。

## 渲染依赖

固定版本的浏览器资源已放在 `vendor/`，运行时不访问 CDN，不需要 npm 或前端构建工具。
网站依赖不进入 C++ 核心或示例构建。

| 组件 | 版本 | 来源 | 许可证 | JS SHA-256 |
| --- | --- | --- | --- | --- |
| marked | 18.2.1 | npm `marked`，`lib/marked.umd.js` | [MIT](vendor/marked.LICENSE) | `bb95d4c15f1289f78486eaacebf3bb18688475ed58a040ef762ed49337090542` |
| DOMPurify | 3.4.18 | npm `dompurify`，`dist/purify.min.js` | [Apache-2.0（分发文件）](vendor/dompurify.LICENSE) | `8053ee2a6a09f9d50f7fcd6fb7f07072f52c7f3fa9aa12147cab4feaf65ee970` |

Marked 原始 HTML 渲染已禁用，DOMPurify 以标签和属性白名单过滤解析结果，
读取器再次限制链接和图片协议。更新依赖时保留许可证、更新版本与哈希，复核安全公告和 API 兼容性。
