#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <regex>
#include <sys/stat.h>
#include "../server.hpp"

#define DEFALT_TIMEOUT 10

std::unordered_map<int, std::string> _statu_msg = {
    {100, "Continue"},
    {101, "Switching Protocol"},
    {102, "Processing"},
    {103, "Early Hints"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {207, "Multi-Status"},
    {208, "Already Reported"},
    {226, "IM Used"},
    {300, "Multiple Choice"},
    {301, "Moved Permanently"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {306, "unused"},
    {307, "Temporary Redirect"},
    {308, "Permanent Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Payload Too Large"},
    {414, "URI Too Long"},
    {415, "Unsupported Media Type"},
    {416, "Range Not Satisfiable"},
    {417, "Expectation Failed"},
    {418, "I'm a teapot"},
    {421, "Misdirected Request"},
    {422, "Unprocessable Entity"},
    {423, "Locked"},
    {424, "Failed Dependency"},
    {425, "Too Early"},
    {426, "Upgrade Required"},
    {428, "Precondition Required"},
    {429, "Too Many Requests"},
    {431, "Request Header Fields Too Large"},
    {451, "Unavailable For Legal Reasons"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {506, "Variant Also Negotiates"},
    {507, "Insufficient Storage"},
    {508, "Loop Detected"},
    {510, "Not Extended"},
    {511, "Network Authentication Required"}};

std::unordered_map<std::string, std::string> _mime_msg = {
    {".aac", "audio/aac"},
    {".abw", "application/x-abiword"},
    {".arc", "application/x-freearc"},
    {".avi", "video/x-msvideo"},
    {".azw", "application/vnd.amazon.ebook"},
    {".bin", "application/octet-stream"},
    {".bmp", "image/bmp"},
    {".bz", "application/x-bzip"},
    {".bz2", "application/x-bzip2"},
    {".csh", "application/x-csh"},
    {".css", "text/css"},
    {".csv", "text/csv"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".eot", "application/vnd.ms-fontobject"},
    {".epub", "application/epub+zip"},
    {".gif", "image/gif"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".ico", "image/vnd.microsoft.icon"},
    {".ics", "text/calendar"},
    {".jar", "application/java-archive"},
    {".jpeg", "image/jpeg"},
    {".jpg", "image/jpeg"},
    {".js", "text/javascript"},
    {".json", "application/json"},
    {".jsonld", "application/ld+json"},
    {".mid", "audio/midi"},
    {".midi", "audio/x-midi"},
    {".mjs", "text/javascript"},
    {".mp3", "audio/mpeg"},
    {".mpeg", "video/mpeg"},
    {".mpkg", "application/vnd.apple.installer+xml"},
    {".odp", "application/vnd.oasis.opendocument.presentation"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".oga", "audio/ogg"},
    {".ogv", "video/ogg"},
    {".ogx", "application/ogg"},
    {".otf", "font/otf"},
    {".png", "image/png"},
    {".pdf", "application/pdf"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".rar", "application/x-rar-compressed"},
    {".rtf", "application/rtf"},
    {".sh", "application/x-sh"},
    {".svg", "image/svg+xml"},
    {".swf", "application/x-shockwave-flash"},
    {".tar", "application/x-tar"},
    {".tif", "image/tiff"},
    {".tiff", "image/tiff"},
    {".ttf", "font/ttf"},
    {".txt", "text/plain"},
    {".vsd", "application/vnd.visio"},
    {".wav", "audio/wav"},
    {".weba", "audio/webm"},
    {".webm", "video/webm"},
    {".webp", "image/webp"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".xhtml", "application/xhtml+xml"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml", "application/xml"},
    {".xul", "application/vnd.mozilla.xul+xml"},
    {".zip", "application/zip"},
    {".3gp", "video/3gpp"},
    {".3g2", "video/3gpp2"},
    {".7z", "application/x-7z-compressed"}};

class Util
{
public:
    // 字符串分割函数,将src字符串按照sep字符进行分割，得到的各个字串放到arry中，最终返回字串的数量
    static size_t Split(const std::string &src, const std::string &sep, std::vector<std::string> *arry)
    {
        size_t offset = 0;
        while (offset < src.size())
        {
            ssize_t pos = src.find(sep, offset);
            if (pos == std::string::npos)
            {
                if (pos == src.size())
                    break;
                arry->push_back(src.substr(offset));
                return arry->size();
            }
            if (pos == offset)
            {
                offset = pos + sep.size();
                continue;
            }
            arry->push_back(src.substr(offset, pos - offset));
            offset = pos + sep.size();
        }
        return arry->size();
    }

    // 读取文件的所有内容，将读取的内容放到一个Buffer中
    static bool ReadFile(const std::string &filename, std::string *buf)
    {
        std::ifstream ifs(filename, std::ios::binary);
        if (ifs.is_open() == false) // 判断文件是否成功打开
        {
            printf("OPEN %s FILE FAILED!!", filename.c_str());
            return false;
        }

        size_t fsize = 0;
        ifs.seekg(0, ifs.end);       // 跳转到读写位置的末尾
        fsize = ifs.tellg();         // 获取当前读写位置相对于起始位置的偏移量，从末尾偏移刚好就是文件大小
        ifs.seekg(0, ifs.beg);       // 跳转到起始位置
        buf->resize(fsize);          // 开辟文件大小的空间
        ifs.read(&(*buf)[0], fsize); // 将文件内容整体读取到buf中
        if (ifs.good() == false)
        { // 检查读取过程中是否发生错误
            printf("READ %s FILE FAILED!!", filename.c_str());
            ifs.close();
            return false;
        }
        ifs.close();
        return true;
    }

    // 向文件写入数据
    static bool WriteFile(const std::string &filename, const std::string &buf)
    {
        // 以二进制模式打开文件并清空原有内容（trunc）
        std::ofstream ofs(filename, std::ios::binary | std::ios::trunc);
        if (ofs.is_open() == false)
        {
            printf("OPEN %s FILE FAILED!!", filename.c_str());
            return false;
        }
        ofs.write(buf.c_str(), buf.size()); // 将buf中的数据写入文件
        if (ofs.good() == false)
        {
            ERR_LOG("WRITE %s FILE FAILED!", filename.c_str());
            ofs.close();
            return false;
        }
        ofs.close();
        return true;
    }

    // URL编码，避免URL中资源路径与查询字符串中的特殊字符与HTTP请求中特殊字符产生歧义
    // 编码格式：将特殊字符的ascii值，转换为两个16进制字符，前缀%   C++ -> C%2B%2B
    //   不编码的特殊字符： RFC3986文档规定 . - _ ~ 字母，数字属于绝对不编码字符
    // RFC3986文档规定，编码格式 %HH
    // W3C标准中规定，查询字符串中的空格，需要编码为+， 解码则是+转空格
    static std::string UrlEncode(const std::string url, bool convert_space_to_plus)
    {
        std::string res;
        for (auto &c : url)
        {
            // 绝对不编码的字符直接累加
            if (c == '.' || c == '-' || c == '_' || c == '~' || isalnum(c))
            {
                res += c;
                continue;
            }
            // 处理空格字符的特殊转换
            if (c == ' ' && convert_space_to_plus)
            {
                res += "+";
                continue;
            }
            // 剩下的字符都是需要编码成为%HH格式
            char tmp[4] = {0};
            snprintf(tmp, 4, "%%%02x", c);
            res += tmp;
        }
        return res;
    }

    // 辅助函数：将单个十六进制字符转换为对应的十进制整数 (0-15)
    // 十六进制字符可能是 `'0'~'9'`、`'a'~'f'`、或 `'A'~'F'`，三段在 ASCII 里不连续，所以要写判断
    static char HEXTOT(char c)
    {
        if (c >= '0' && c <= '9')
            return c - '0';
        else if (c >= 'a' && c <= 'z')
            return c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z')
            return c - 'A' + 10;
        return -1;
    }

    static std::string UrlDecode(const std::string url, bool convert_plus_to_space)
    {
        // 遇到了%，则将紧随其后的2个字符，转换为数字，第一个数字左移4位，然后加上第二个数字  + -> 2b  %2b->2 << 4 + 11
        std::string res;
        for (int i = 0; i < url.size(); i++)
        {
            if (url[i] == '+' && convert_plus_to_space)
            {
                res += " ";
                continue;
            }
            // 处理%HH格式的解码
            if (url[i] == '%' && (i + 2) < url.size())
            {
                char v1 = HEXTOT(url[i + 1]);
                char v2 = HEXTOT(url[i + 2]);
                char v = v1 * 16 + v2;
                res += v;
                i += 2;
                continue;
            }
            // 其他不需要解码的字符直接追加
            res += url[i];
        }
        return res;
    }

    // 响应状态码的描述信息获取
    static std::string StatuDesc(int statu)
    {
        auto it = _statu_msg.find(statu);
        if (it != _statu_msg.end())
            return it->second;
        return "Unknow";
    }

    // 根据文件后缀名获取文件mime
    static std::string ExtMime(const std::string &filename)
    {
        // a.b.txt  先获取文件扩展名
        size_t pos = filename.find_last_of('.');
        if (pos == std::string::npos)
            return "application/octet-stream"; // 如果没有后缀，返回默认的二进制流类型
        std::string ext = filename.substr(pos);
        auto it = _mime_msg.find(ext);
        if (it == _mime_msg.end())
            return "application/octet-stream"; // 找不到对应后缀的类型，返回默认二进制流类型
        return it->second;
    }

    // 判断一个文件是否为一个目录
    static bool IsDirectory(const std::string &filename)
    {
        struct stat st;
        int ret = stat(filename.c_str(), &st); // 获取文件属性
        if (ret < 0)
            return false;
        return S_ISDIR(st.st_mode);
    }

    // 判断一个文件是否为普通文件
    static bool IsRegular(const std::string &filename)
    {
        struct stat st;
        int ret = stat(filename.c_str(), &st);
        if (ret < 0)
            return false;
        return S_ISREG(st.st_mode);
    }

    // http请求的资源路径有效性判断
    // 客户端只能请求相对根目录中的资源，其他地方的资源都不予理会
    // /../login, 这个路径中的..会让路径的查找跑到相对根目录之外，这是不合理的，不安全的
    static bool VaildPath(const std::string &path)
    {
        std::vector<std::string> subdir;
        Split(path, "/", &subdir);
        int level = 0;
        for (auto &dir : subdir)
        {
            if (dir == "..")
            {
                level--;
                if (level < 0)
                    return false;
                continue;
            }
            level++;
        }
        return true;
    }
};

class HttpRequest
{
public:
    std::string _method;                                   // 请求方法
    std::string _path;                                     // 资源路径
    std::string _version;                                  // 协议版本
    std::string _body;                                     // 请求正文
    std::smatch _matches;                                  // 资源路径的正则提取数据
    std::unordered_map<std::string, std::string> _headers; // 头部字段
    std::unordered_map<std::string, std::string> _params;  // 查询字符串

public:
    HttpRequest() : _version("HTTP/1.1") {}

    // 重置数据
    void ReSet()
    {
        _method.clear();
        _path.clear();
        _version = "HTTP/1.1";
        _body.clear();
        std::smatch match;
        _matches.swap(match);
        _headers.clear();
        _params.clear();
    }

    // 插入头部字段
    void SetHeader(const std::string &key, const std::string &val)
    {
        _headers.insert(std::make_pair(key, val));
    }

    // 判断是否存在指定头部字段
    bool HasHeader(const std::string &key) const
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
            return false;
        return true;
    }

    // 获取指定头部字段的值
    std::string GetHandler(const std::string &key) const
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
            return "";
        return it->second;
    }

    // 插入查询字符串
    void SetParam(const std::string &key, const std::string &val)
    {
        _params.insert(std::make_pair(key, val));
    }

    // 判断是否有某个指定的查询字符串
    bool HasParam(const std::string &key) const
    {
        auto it = _params.find(key);
        if (it == _params.end())
            return false;
        return true;
    }

    // 获取指定的查询字符串
    std::string GetParam(const std::string &key) const
    {
        auto it = _params.find(key);
        if (it == _params.end())
            return "";
        return it->second;
    }

    // 获取正文长度
    size_t ContentLength()
    {
        bool ret = HasHeader("Content-Length");
        if (ret == false)
            return 0;
        std::string clen = GetHandler("Content-Length");
        return stol(clen);
    }

    // 判断是否短链接
    bool Close()
    {
        if (HasHeader("Connection") == true && GetHandler("Connection") == "keep-alive")
            return false;
        return true;
    }
};

class HttpResponse
{
public:
    int _statu;                                            // HTTP状态码（200,404,302）
    bool _redirect_flag;                                   // 是否需要重定向
    std::string _body;                                     // 响应正文
    std::string _redirect_url;                             // 重定向的目标URL
    std::unordered_map<std::string, std::string> _headers; // 响应头部字典
public:
    HttpResponse() : _redirect_flag(false), _statu(200) {}
    HttpResponse(int statu) : _redirect_flag(false), _statu(statu) {}

    // 重置数据
    void ReSet()
    {
        _statu = 200;
        _redirect_flag = false;
        _body.clear();
        _redirect_url.clear();
        _headers.clear();
    }

    // 插入头部字段
    void SetHeader(const std::string &key, const std::string &val)
    {
        _headers.insert(std::make_pair(key, val));
    }

    // 判断是否存在指定头部字段
    bool HasHeader(const std::string &key)
    {
        return _headers.find(key) != _headers.end();
    }

    // 获取指定头部字段的值
    std::string GetHeader(const std::string &key)
    {
        auto it = _headers.find(key);
        if (it == _headers.end())
            return "";
        return it->second;
    }

    // 快速设置响应正文，并自动配置Content-Type类型（默认是网页html）
    void SetContent(const std::string &body, const std::string &type = "text/html")
    {
        _body = body;
        SetHeader("Content-Type", type);
    }

    // 快速配置重定向（默认状态码为302 临时重定向）
    void SetRedirect(const std::string &url, int statu = 302)
    {
        _statu = statu;
        _redirect_flag = true;
        _redirect_url = url;
    }

    // 判断当前连接是否为短连接
    bool Close()
    {
        if (HasHeader("Connection") && GetHeader("Connection") == "keep-alive")
        {
            return false;
        }
        return true;
    }
};

typedef enum
{
    RECV_HTTP_ERROR, // 解析出错
    RECV_HTTP_LINE,  // 带处理请求行
    RECV_HTTP_HEAD,  // 待处理头部
    RECV_HTTP_BODY,  // 待处理正文
    RECV_HTTP_OVER   // 处理完毕
} HttpRecvStatu;

#define MAX_LINE 8192 // 单行数据最大长度限制
class HttpContext
{
private:
    int _resp_statu;           // 响应状态码
    HttpRecvStatu _recv_statu; // 当前接收及解析的阶段
    HttpRequest _request;      // 已经解析得到的请求
private:
    // 解析HTTP请求行，例如 "GET /path?k=v HTTP/1.1"
    bool ParseHttpLine(const std::string &line)
    {
        std::smatch matches;
        // 捕获组：1.方法 2.路径 3.查询字符串（可选）4.版本
        std::regex e("(GET|HEAD|POST|PUT|DELETE) ([^?]*)(?:\\?(.*))? (HTTP/1\\.[01])(?:\n|\r\n)?", std::regex::icase);
        bool ret = std::regex_match(line, matches, e);
        if (ret == false)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400; // BAD REQUEST
            return false;
        }
        // 0 : GET /bitejiuyeke/login?user=xiaoming&pass=123123 HTTP/1.1
        // 1 : GET
        // 2 : /bitejiuyeke/login
        // 3 : user=xiaoming&pass=123123
        // 4 : HTTP/1.1

        // 请求方法的获取
        _request._method = matches[1];

        // 统一转大写
        std::transform(_request._method.begin(), _request._method.end(), _request._method.begin(), ::toupper);

        // 资源路径获取，需要进行URL解码操作，但不需要+转空格
        _request._path = Util::UrlDecode(matches[2], false);

        // 协议版本获取
        _request._version = matches[4];

        // 查询字符串点的获取与处理
        std::vector<std::string> query_string_arry;
        std::string quert_string = matches[3];
        // 没有查询字符串时为空串
        // 查询字符串的格式 key=val&key=val....., 先以 & 符号进行分割，得到各个字串
        Util::Split(quert_string, "&", &query_string_arry);
        // 针对各个子串，以=符号进行分割，得到key和val，得到之后还要进行URL解码
        for (auto &str : query_string_arry)
        {
            ssize_t pos = str.find("=");
            if (pos == std::string::npos)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 400; // BAD REQUEST
                return false;
            }
            std::string key = Util::UrlDecode(str.substr(0, pos), true);
            std::string val = Util::UrlDecode(str.substr(pos + 1), true);
            _request.SetParam(key, val);
        }
        return true;
    }

    // 从缓冲区取出并解析请求行，可能因数据不全而多次调用
    bool RecvHttpLine(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_LINE)
            return false;
        // 1.获取一行数据
        std::string line = buf->GetLineAndPop();
        // 2.需考虑缓冲区的数据不足一行，或一行数据过大
        if (line.size() == 0)
        {
            if (buf->ReadAbleSize() > MAX_LINE)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 414; // URI TOO LONG
                return false;
            }
            return true;
        }
        if (line.size() > MAX_LINE)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 414; // URI TOO LONG
            return false;
        }
        bool ret = ParseHttpLine(line);
        if (ret == false)
        {
            return false;
        }
        // 首行处理完毕，进入到头部获取阶段
        _recv_statu = RECV_HTTP_HEAD;
        return true;
    }

    // 解析单行头部字段，格式 "key: val"，结果存入 _request 的头部表
    bool ParseHttpHead(std::string &line)
    {
        // 单行头部格式：key: val
        if (line.back() == '\n')
            line.pop_back();
        if (line.back() == '\r')
            line.pop_back();
        ssize_t pos = line.find(": "); // 注意是冒号+空格作为分隔
        if (pos == std::string::npos)
        {
            _recv_statu = RECV_HTTP_ERROR;
            _resp_statu = 400;
            return false;
        }
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 2);
        _request.SetHeader(key, val);
        return true;
    }

    // 循环取出每一个头部字段，直到遇见空行
    bool RecvHttpHead(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_HEAD)
            return false;
        while (1)
        {
            std::string line = buf->GetLineAndPop();
            if (line.size() == 0)
            {
                if (buf->ReadAbleSize() > MAX_LINE)
                {
                    _recv_statu = RECV_HTTP_ERROR;
                    _resp_statu = 414;
                    return false;
                }
                return true;
            }
            if (buf->ReadAbleSize() > MAX_LINE)
            {
                _recv_statu = RECV_HTTP_ERROR;
                _resp_statu = 414;
                return false;
            }
            if (line == "\n" || line == "\r\n")
            {
                break;
            }
            bool ret = ParseHttpHead(line);
            if (ret == false)
            {
                return false;
            }
        }
        _recv_statu = RECV_HTTP_BODY;
        return true;
    }

    // 根据 Content-Length 接收正文，可能因数据不全而多次调用、分批拼接
    bool RecvHttpBody(Buffer *buf)
    {
        if (_recv_statu != RECV_HTTP_BODY)
            return false;
        // 1.获取正文长度
        ssize_t content_length = _request.ContentLength();
        if (content_length == 0)
        {
            // 没有正文，则请求接收解析完毕
            _recv_statu = RECV_HTTP_OVER;
            return true;
        }

        // 实际还需接收的正文长度
        ssize_t real_len = content_length - _request._body.size();
        if (buf->ReadAbleSize() >= real_len)
        {
            // 如果缓冲区有足够数据，则取出real_len大小数据出来，使正文提取完成
            _request._body.append(buf->ReadPosition(), real_len);
            buf->MoveReadOffset(real_len);
            _recv_statu = RECV_HTTP_OVER;
            return true;
        }
        // 如果数据不足则提取数据且等待下次数据到来
        _request._body.append(buf->ReadPosition(), buf->ReadAbleSize());
        buf->MoveReadOffset(buf->ReadAbleSize());
        return true; // 状态仍为RECV_HTTP_BODY，下次继续收
    }

public:
    HttpContext() : _resp_statu(200), _recv_statu(RECV_HTTP_LINE) {}

    // 重置
    void ReSet()
    {
        _resp_statu = 200;
        _recv_statu = RECV_HTTP_LINE;
        _request.ReSet();
    }

    // 获取响应状态码
    int RespStatu() { return _resp_statu; }

    // 获取当前进度解析
    HttpRecvStatu RecvStatu() { return _recv_statu; }

    // 获取已解析的请求对象
    HttpRequest &Request() { return _request; }

    // 接收并解析HTTP请求
    void RecvHttpRequest(Buffer *buf)
    {
        // 不同的状态，做不同的事情，但是这里不要break， 因为处理完请求行后，应该立即处理头部，而不是退出等新数据
        switch (_recv_statu)
        {
        case RECV_HTTP_LINE:
            RecvHttpLine(buf);
        case RECV_HTTP_HEAD:
            RecvHttpHead(buf);
        case RECV_HTTP_BODY:
            RecvHttpBody(buf);
        }
        return;
    }
};

class HttpServer
{
private:
    using Handler = std::function<void(const HttpRequest &, HttpResponse *)>;
    using Handlers = std::vector<std::pair<std::regex, Handler>>;
    Handlers _get_route;    // GET路由表
    Handlers _post_route;   // POST路由表
    Handlers _put_route;    // PUT路由表
    Handlers _delete_route; // DELETE路由表

    std::string _basedir; // 静态资源根目录
    TcpServer _server;    // TCP服务器

private:
    // 生成简单的错误页面并写入响应体
    void ErrorHandler(const HttpRequest &req, HttpResponse *rsp)
    {
        // 1.组织一个错误的展示页面
        std::string body;
        body += "<html>";
        body += "<head>";
        body += "<meta http-equiv='Content-Type' content='text/html;charset=utf-8'>";
        body += "</head>";
        body += "<body>";
        body += "<h1>";
        body += std::to_string(rsp->_statu);
        body += " ";
        body += Util::StatuDesc(rsp->_statu);
        body += "</h1>";
        body += "</body>";
        body += "</html>";
        // 2. 将页面数据，当作响应正文，放入rsp中
        rsp->SetContent(body, "text/html");
    }

    // 将HttpResponse中的要素按照http协议格式进行组织，并发送
    //  [NOTE] 拼装响应行+响应头+空行+正文，按 HTTP 格式发送
    void WriteResponse(const PtrConnection &conn, HttpRequest &req, HttpResponse &rsp)
    {
        // 1.先完善头部字段
        if (req.Close())
            rsp.SetHeader("Connection", "close"); // 短连接
        else
            rsp.SetHeader("Connection", "keep-alive"); // 长连接

        if (rsp._body.size() && rsp.HasHeader("Content-length") == false)
        {
            rsp.SetHeader("Content-length", std::to_string(rsp._body.size()));
        }
        if (rsp._body.size() && rsp.HasHeader("Content-Type") == false)
        {
            rsp.SetHeader("Content-Type", "application/octet-stream"); // 默认二进制
        }
        if (rsp._redirect_flag == true)
        {
            rsp.SetHeader("Location", rsp._redirect_url); // 重定向地址
        }

        // 将rsp中的要素，按照http协议格式进行组织
        std::stringstream rsp_str;
        rsp_str << req._version << " " << std::to_string(rsp._statu) << " " << Util::StatuDesc(rsp._statu) << "\r\n";
        for (auto &head : rsp._headers)
        {
            rsp_str << head.first << ": " << head.second << "\r\n";
        }
        rsp_str << "\r\n";
        rsp_str << rsp._body;
        // 发送数据
        conn->Send(rsp_str.str().c_str(), rsp_str.str().size());
    }

    // 判断该请求是否应该走静态文件处理逻辑
    bool IsFileHandler(const HttpRequest &req)
    {
        // 1.必须设置静态资源根目录
        if (_basedir.empty())
            return false;

        // 2.请求方法必须是GET/HEAD
        if (req._method != "GET" && req._method != "HEAD")
            return false;

        // 3.请求的资源路径必须是合法路径
        if (Util::VaildPath(req._path) == false)
            return false;

        // 4.请求资源必须存在，且是一个普通文件
        std::string req_path = _basedir + req._path;
        if (req._path.back() == '/')
            req_path += "index.html"; // 结尾是/默认访问首页
        if (Util::IsRegular(req_path) == false)
            return false;

        return true;
    }

    // 静态资源的请求处理--将静态资源文件数据读取到rsp._body中并设置MIME类型
    void FileHandler(const HttpRequest &req, HttpResponse *rsp)
    {
        std::string req_path = _basedir + req._path;
        if (req_path.back() == '/')
            req_path += "index.html";

        bool ret = Util::ReadFile(req_path, &rsp->_body);
        if (!ret)
            return; // 读取失败
        std::string mime = Util::ExtMime(req_path);
        rsp->SetHeader("Content-Type", mime);
        return;
    }

    // 功能性请求的分类处理
    // 在指定路由表中按正则匹配查找处理函数并调用，未命中则404
    void Dispatcher(HttpRequest &req, HttpResponse *rsp, Handlers &handlers)
    {
        for (auto &handler : handlers)
        {
            const std::regex &re = handler.first;
            const Handler &functor = handler.second;
            bool ret = std::regex_match(req._path, req._matches, re);
            if (ret == false)
                continue;
            return functor(req, rsp); // 传入请求信息，和空rsp，执行处理函数
        }
        rsp->_statu = 404; // 没有路由匹配
    }

    // 请求分发入口：静态资源->功能路由->405
    void Route(HttpRequest &req, HttpResponse *rsp)
    {
        if (IsFileHandler(req) == true)
            return FileHandler(req, rsp);
        if (req._method == "GET" || req._method == "HEAD")
            return Dispatcher(req, rsp, _get_route);
        else if (req._method == "POST")
            return Dispatcher(req, rsp, _post_route);
        else if (req._method == "PUT")
            return Dispatcher(req, rsp, _put_route);
        else if (req._method == "DELETE")
            return Dispatcher(req, rsp, _delete_route);

        rsp->_statu = 405; // Method Not Allowed
        return;
    }

    // 设置上下文
    //  新连接建立时，绑定一个 HttpContext 用于解析状态保存
    void OnConnected(const PtrConnection &conn)
    {
        conn->SetContext(HttpContext());
        DBG_LOG("NEW CONNECTION %p", conn.get());
    }

    // 缓冲区数据解析+处理
    // 收到数据时的主处理逻辑：解析请求->出错则错误响应并断联->完整则路由处理并响应
    void OnMessage(const PtrConnection &conn, Buffer *buffer)
    {
        while (buffer->ReadAbleSize() > 0)
        {
            // 获取上下文
            HttpContext *context = conn->Getcontext()->Get<HttpContext>();

            context->RecvHttpRequest(buffer);
            HttpRequest &req = context->Request();
            HttpResponse rsp(context->RespStatu());

            // 如果出错则进行错误响应，关闭连接
            if (context->RespStatu() >= 400)
            {
                ErrorHandler(req, &rsp);
                WriteResponse(conn, req, rsp);
                context->ReSet();
                buffer->MoveReadOffset(buffer->ReadAbleSize());
                conn->ShutDown();
                return;
            }
            // 当前请求还没有接收完整,则退出，等新数据到来再重新继续处理
            if (context->RecvStatu() != RECV_HTTP_OVER)
            {
                return;
            }
            // 到这里说明成功
            // 请求路由，业务处理
            Route(req, &rsp);
            // 对HttpResponse进行组织并发送
            WriteResponse(conn, req, rsp);
            // 重置上下文
            context->ReSet();
            // 短连接则直接关闭
            if (rsp.Close())
                conn->ShutDown();
        }
        return;
    }

public:
    // 构造：初始化TCP服务器、设置空闲超时回收、绑定连接/消息回调
    HttpServer(int port, int timeout = DEFALT_TIMEOUT) : _server(port)
    {
        _server.EnableInactiveRelease(timeout);
        _server.SetConnectedCallBack(std::bind(&HttpServer::OnConnected, this, std::placeholders::_1));
        _server.SetMessageCallBack(std::bind(&HttpServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }
    // 设置静态资源根目录（必须是已存在目录）
    void SetBaseDir(const std::string &path)
    {
        assert(Util::IsDirectory(path));
        _basedir = path;
    }

    // 设置/添加，请求（请求的正则表达式）与处理函数的映射关系
    // 注册Get路由
    void Get(const std::string &pattern, const Handler &handler)
    {
        _get_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册POST路由
    void Post(const std::string &pattern, const Handler &handler)
    {
        _post_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册PUT路由
    void Put(const std::string &pattern, const Handler &handler)
    {
        _put_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    // 注册DELETE路由
    void Delete(const std::string &pattern, const Handler &handler)
    {
        _delete_route.push_back(std::make_pair(std::regex(pattern), handler));
    }

    void SetThreadCount(int count)
    {
        _server.SetThreadCount(count);
    }
    void Listen()
    {
        _server.Start();
    }
};