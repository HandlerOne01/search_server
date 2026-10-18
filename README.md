# search_server

一个基于 **C++11、cppjieba 和倒排索引** 实现的 Boost 文档站内搜索服务。HTTP 服务由项目自研的 **epoll + 多线程 Reactor 网络层**提供（见 `net/`），不依赖 cpp-httplib 等第三方 HTTP 库；项目将本地 HTML 文档转换为可检索语料，在服务启动时建立内存索引，并通过 HTTP 接口和网页提供关键词搜索。

适合用来学习 HTML 内容提取、中文分词、正排与倒排索引、搜索结果排序、epoll 事件驱动，以及 C++ 后端到网页的完整链路。

## 功能概览

- **文档预处理**：递归扫描 `.html` 文件，提取标题、去除 HTML 标签，并生成对应的 Boost 在线文档链接。
- **搜索模式分词**：使用 cppjieba 的 `CutForSearch` 对标题、正文和查询词分词，过滤停用词，并在索引和查询阶段转换为小写。
- **正排索引**：通过文档 ID 获取标题、正文和 URL。
- **倒排索引**：通过关键词查找命中文档及其词频权重。
- **结果去重与排序**：按文档 ID 合并命中项，再按权重降序返回。
- **摘要提取**：截取命中词附近的正文片段作为结果摘要。
- **HTTP 搜索接口**：由自研的 epoll + 多线程 Reactor 网络层提供 `/s?word=关键词` 接口，使用 JsonCpp 序列化结果。
- **网页展示**：单屏布局的搜索页（整页不滚动，仅结果区内部滚动），用原生 JavaScript 渲染标题、摘要与原文链接，不依赖任何第三方前端库。
- **命令行调试**：提供交互式 `debug` 程序，直接查看搜索结果 JSON。

## 工作流程

```text
本地 HTML 文档：data/input/
             │
             ▼
      parser：提取与清洗
             │
             ▼
  文档语料：data/raw_html/raw.txt
             │
             ▼
     服务启动时分词、建立索引
             │
       ┌─────┴─────┐
       ▼           ▼
   正排索引      倒排索引
       │           │
       └─────┬─────┘
             ▼
用户查询 → 分词 → 检索 → 去重与排序 → 摘要与 JSON
                                            │
                                            ▼
                            HTTP 接口 / 网页 / 命令行
```

`parser` 生成的是文档语料文件；正排索引和倒排索引由 `debug` 或 `http_server` 在启动时构建，并保存在进程内存中。

## 目录结构

```text
search_server/
├── parser.cc                # HTML 文档提取与语料生成
├── index.hpp                # 正排索引、倒排索引与词频权重
├── searcher.hpp             # 查询分词、检索、排序与 JSON 输出
├── util.hpp                 # 文件读取、字符串切分、分词与停用词过滤
├── log.hpp                  # 日志输出
├── debug.cc                 # 交互式命令行搜索入口
├── http_server.cc           # 服务入口：注册 /s 路由并启动 HTTP 服务
├── makefile                 # 构建 parser、debug、http_server
├── net/
│   ├── server.hpp           # 自研 TCP 网络层：Socket/Buffer/Channel/Poller/EventLoop/TimerWheel/TcpServer
│   └── http/
│       └── http.hpp         # HTTP 解析、路由注册与静态文件服务
├── cppjieba/
│   ├── include/cppjieba/    # 随仓库提供的分词库头文件
│   └── dict/               # 词典、模型、用户词典与停用词
├── data/
│   └── raw_html/raw.txt    # 已处理的文档语料（开箱即用）
├── wwwroot/
│   └── index.html          # 搜索页面
└── README.md
```

仓库包含 8,563 条语料记录（`data/raw_html/raw.txt`，约 16.6 MB），启动即可检索；原始 HTML 文档集未随仓库提供，需要重新生成语料时请自行准备 `data/input/` 目录（见「准备语料」）。仓库不再附带预编译程序，请自行构建。

## 环境与依赖

以下步骤面向 Linux。由于网络层使用了 `epoll`、`eventfd`、`timerfd` 等 Linux 专有接口，本项目只能在 Linux（或 WSL2）中构建运行；Windows 用户请在安装了编译工具的 WSL 发行版、Linux 虚拟机或 Linux 容器中执行。

| 依赖 | 用途 | 获取方式 |
| --- | --- | --- |
| g++、GNU Make | 使用 C++11 编译项目 | 系统软件包 |
| Boost.Filesystem / Boost.System | 递归遍历文档目录 | 系统开发包 |
| Boost.Algorithm | 字符串切分和大小写转换 | Boost 头文件 |
| JsonCpp | 搜索结果 JSON 序列化 | 系统开发包 |
| cppjieba | 分词与词典加载 | 已包含在仓库中 |
| curl | 验证 HTTP 接口 | 系统软件包 |

Ubuntu 22.04 / 24.04 的依赖安装示例：

```bash
sudo apt update
sudo apt install -y build-essential git curl pkg-config \
  libboost-filesystem-dev libboost-system-dev \
  libjsoncpp-dev
```

HTTP 服务、路由与静态文件服务由仓库内的 `net/` 自研网络层提供，不需要安装任何第三方 HTTP 库。

## 快速开始

下面的程序均需在**仓库根目录**运行，语料文件、分词词典和网页目录都使用相对路径。

### 1. 获取代码

```bash
git clone https://gitee.com/a2186988646/search_engine.git
cd search_engine
```

### 2. 编译

```bash
make -B
```

默认构建三个程序：

| 程序 | 作用 |
| --- | --- |
| `parser` | 从本地 HTML 生成文档语料 |
| `debug` | 在命令行中交互式搜索 |
| `http_server` | 提供搜索接口与网页 |

`-B` 用于强制重新编译，避免直接使用仓库中已有的可执行文件。也可以使用 `make -B parser`、`make -B debug` 或 `make -B http_server` 单独构建。

### 3. 准备语料

仓库已包含 `data/raw_html/raw.txt`，可以直接使用。需要重新处理文档时执行：

```bash
mkdir -p data/raw_html
./parser
```

程序读取 `data/input/` 下的 `.html` 文件，并覆盖 `data/raw_html/raw.txt`。它处理本地文件，不会自动下载或抓取网页。

### 4. 启动 HTTP 服务

```bash
./http_server
```

程序会先读取语料并建立索引，再监听 `0.0.0.0:8080`。首次启动需要完成分词和索引构建，请等待初始化结束后再访问：

```text
http://127.0.0.1:8080/
```

在搜索框中输入 `asio`、`filesystem` 或 `thread`，点击“搜索一下”查看结果。结果链接指向对应的 Boost 在线文档。使用 `Ctrl+C` 停止服务。

### 5. 验证搜索接口

保持服务运行，在另一个终端执行：

```bash
curl -i --get --data-urlencode 'word=asio' \
  http://127.0.0.1:8080/s

curl -i --get --data-urlencode 'word=shared_ptr' \
  http://127.0.0.1:8080/s
```

`--data-urlencode` 可以正确编码中文、空格和特殊字符。

### 6. 命令行搜索

也可以单独运行：

```bash
./debug
```

等待索引建立后，按提示输入关键词并回车，程序会输出搜索结果 JSON。该程序循环读取输入，用于手动调试；使用 `Ctrl+C` 退出。

## HTTP 接口

### 搜索

- **方法**：`GET`
- **路径**：`/s`
- **参数**：`word`，搜索字符串
- **结果类型**：`application/json`
- **返回条数**：按权重降序，**最多返回 100 条**（`searcher.hpp` 中的 `MAX_RETURN`）。热门词（如 `unordered_map`）命中上万篇时也只返回前 100 条，避免产生数 MB 的响应体——那会让传输时间变长，也容易被中间链路中断。

有命中结果时，响应是 JSON 数组，单项结构如下。以下仅展示字段结构，实际标题、摘要、ID 和权重取决于语料及查询：

```json
[
  {
    "title": "文档标题",
    "desc": "命中词附近的正文摘要...",
    "url": "https://www.boost.org/doc/libs/1_82_0/doc/html/tools.html",
    "id": 0,
    "weight": 12
  }
]
```

| 字段 | 含义 |
| --- | --- |
| `title` | HTML 文档标题 |
| `desc` | 根据命中词生成的正文摘要 |
| `url` | 对应的 Boost 在线文档地址 |
| `id` | 文档在当前进程正排索引中的 ID |
| `weight` | 当前检索逻辑使用的排序权重 |

当前接口的特殊情况：

- 未传入 `word` 时，返回文本“必须要有搜索关键字！”，代码未显式设置错误状态码。
- 没有命中文档时，返回 JSON `null`，而不是空数组 `[]`。
- 空字符串或仅包含停用词的查询没有单独的参数校验。

### 页面

`GET /` 提供 `wwwroot/index.html`，页面通过 AJAX 请求搜索接口，并展示返回结果。

## 索引与排序

正排索引使用 `std::vector<DocInfo>`，文档 ID 对应数组下标；倒排索引使用 `std::unordered_map<std::string, InvertedList>`，记录每个词对应的文档 ID 与权重。

单个词在单篇文档中的权重计算为：

```text
weight = 标题词频 × 10 + 正文词频 × 1
```

标题中出现的词对权重影响更大。查询时，对每个分词分别查找倒排索引，将命中文档按 ID 去重，再按 `weight` 降序排序。

多关键词查询采用命中集合合并，文档命中任一查询词即可进入结果。当前合并代码使用 `item.weight = elem.weight`，同一文档后续命中词会覆盖前面的权重，尚未累加多个关键词的相关性。

## 语料格式与配置

### 语料格式

`raw.txt` 每行保存一篇文档，包含三个字段：

```text
标题\x03正文\x03URL\n
```

其中 `\x03` 表示实际值为 `0x03` 的不可见分隔字节，源码写作 `\3`；`\n` 表示记录换行。自行生成语料时，请写入实际分隔字节，不能用字面量字符串 `\x03` 或 `\3` 替代。

### 常用配置

| 配置 | 默认值 | 位置 |
| --- | --- | --- |
| HTML 输入目录 | `data/input` | `parser.cc` 中的 `src_path` |
| 语料输出文件 | `data/raw_html/raw.txt` | `parser.cc` 中的 `output` |
| 搜索语料文件 | `data/raw_html/raw.txt` | `http_server.cc`、`debug.cc` 中的 `input` |
| 在线文档 URL 前缀 | Boost 1.82.0 文档地址 | `parser.cc` 中的 `ParseUrl` |
| 网页目录 | `./wwwroot` | `http_server.cc` 中的 `root_path` |
| 监听地址与端口 | `0.0.0.0:8080` | `http_server.cc` 中的 `HttpServer svr(8080)` |
| 分词资源 | `./cppjieba/dict/` 下的词典与模型 | `util.hpp` 中的路径常量 |
| 标题 / 正文权重系数 | `10` / `1` | `index.hpp` 中的 `X`、`Y` |

更换文档集合时，需要同时调整输入文件、URL 前缀和相对目录映射；修改语料或词典后，需要重新启动搜索程序构建索引。修改源码配置后需重新编译。

## 常见问题

**找不到 Boost 或 JsonCpp**

请安装相应开发包。项目使用 `<jsoncpp/json/json.h>`，只有运行时库不足以完成编译。网络层是仓库自带的头文件，不需要额外安装。

**编译时提示 `<any>` 或其它 C++17 相关错误**

网络层已移除对 `<any>` 的依赖，Makefile 使用 `-std=c++11` 即可构建。若你自行修改网络层并引入了 C++17 特性，请把 Makefile 中的 `-std=c++11` 调整为 `-std=c++17`。

**提示语料文件或分词词典不存在**

请在仓库根目录运行程序，并确认 `data/raw_html/raw.txt` 和 `cppjieba/dict/` 中的文件存在。必要时先运行 `./parser`。

**日志显示索引构建成功，但搜索没有结果**

`InitSearcher` 当前未检查 `BuildIndex` 的返回值。请同时查看前面的文件打开错误和语料是否为空，不能仅凭成功日志判断索引已经建立。

**页面能打开，但搜索一直转圈或报错**

页面只依赖浏览器原生 `fetch`，不需要外网。请打开开发者工具看 `/s` 请求：若状态码是 502、或 200 但响应体不完整，通常是本机到服务器的网络链路在长传输中途断开，刷新或重试即可（前端已内置一次自动重试）；若连接被拒，先确认进程是否还在（`ps -p $(cat run.pid)`）并查看 `run.log`。

## 当前边界

- HTML 清洗基于简单标签状态机，不是完整的 HTML 解析器；没有专门处理脚本、样式、HTML 实体或各种标题标签写法。
- 索引保存在内存中，每次启动都重新构建；没有索引持久化、增量更新或结果分页。
- 摘要按字节截取命中词前约 50 字节、后约 100 字节的内容，多字节字符边界仍需完善；找不到摘要时可能返回 `None1` 或 `None2`。
- 结果只返回权重最高的前 100 条，没有分页或「加载更多」；要查看更多结果只能调整 `searcher.hpp` 的 `MAX_RETURN` 并重新编译。
- `debug` 是手动调试入口，仓库未提供统一自动化测试或检索性能基准。
- 本项目只能在 Linux / WSL2 构建运行：网络层依赖 `epoll`、`eventfd`、`timerfd` 等 Linux 专有接口。
- 索引在 `Listen()` 之前同步构建，8,563 条语料启动约 30 秒，期间服务不接受连接。
- 搜索在事件循环线程内同步执行，单次查询会占用该线程；当前使用 3 个子事件循环线程轮转分配连接。
- 页面为单屏布局（整页不滚动，只有结果区内部滚动），移动端只做了基础适配，未针对小屏做专门设计。
- 静态资源优先于路由匹配：`GET` 请求会先检查 `wwwroot/` 下是否存在同名文件，业务路径不要与静态文件重名。
- HTTP 实现为教学级：不支持 chunked、HTTPS 与 HTTP/2；请求头键名区分大小写；空响应体不补 `Content-Length: 0`；`HEAD` 仍会输出正文。

## 项目来源

本项目合并自两个仓库：搜索业务、语料与页面来自 [search_engine](https://gitee.com/a2186988646/search_engine)，
TCP/Reactor 网络层与 HTTP 解析、路由注册来自 [my_server](https://gitee.com/a2186988646/my_server)。
合并时移除了第三方 HTTP 库 cpp-httplib，改由自研网络层承载 `/s` 接口与静态页面；两边的业务逻辑与网络层实现均保留原样。

## 参与贡献

欢迎围绕搜索排序、解析准确性、摘要处理、空结果展示和自动化测试提出 Issue 或 Pull Request。提交修改时，请附上复现输入、预期结果与实际验证方式。

## 许可证

当前仓库根目录未提供项目 LICENSE 文件。第三方库与 Boost 文档的使用和分发应遵循各自的授权条款。
