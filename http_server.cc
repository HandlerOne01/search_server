#include "net/http/http.hpp" // 自研网络层（其内部 LOG 在此展开完毕）
#undef LOG                   // 避免与业务层 log.hpp 的 LOG 宏重定义
#include "searcher.hpp"      // 搜索引擎业务层

const std::string input = "data/raw_html/raw.txt";
const std::string root_path = "./wwwroot";

static ns_searcher::Searcher search;

// GET /s?word=关键字  -> JSON 搜索结果
static void SearchHandler(const HttpRequest &req, HttpResponse *rsp)
{
    if (!req.HasParam("word") || req.GetParam("word").empty())
    {
        rsp->_statu = 400;
        rsp->SetContent("必须要有搜索关键字！", "text/plain; charset=utf-8");
        return;
    }

    std::string word = req.GetParam("word");
    LOG(NORMAL, "用户搜索的：" + word);

    std::string json_string;
    search.Search(word, &json_string);
    rsp->SetContent(json_string, "application/json");
}

int main()
{
    // 启动前一次性建立正排与倒排索引（语料路径相对仓库根目录）
    search.InitSearcher(input);
    LOG(NORMAL, "服务器启动成功，监听 0.0.0.0:8080 ...");

    HttpServer svr(8080);
    svr.SetThreadCount(3);
    svr.SetBaseDir(root_path); // 静态资源目录，同样相对仓库根目录
    svr.Get("/s", SearchHandler);
    svr.Listen();
    return 0;
}
