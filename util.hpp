#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include "log.hpp"
#include "cppjieba/include/cppjieba/Jieba.hpp"

namespace ns_util
{
    // 文件操作类工具
    class FileUtil
    {
    public:
        // 按行读取整个文件内容，并拼接到 out 参数中
        static bool ReadFile(const std::string &file_path, std::string *out)
        {
            std::ifstream in(file_path, std::ios::in);
            if (!in.is_open())
            {
                std::cerr << "open file" << file_path << "error" << std::endl;
                return false;
            }
            std::string line;
            while (std::getline(in, line))
            {
                *out += line;
            }
            in.close();
            return true;
        }
    };

    // 字符串切分工具：基于 boost::split 按分隔符切分
    class StringUtil
    {
    public:
        static void Split(const std::string &target, std::vector<std::string> *out, const std::string &sep)
        {
            boost::split(*out, target,boost::is_any_of(sep), boost::token_compress_on);
        }
    };

    // 分词所需词典/模型/停用词路径常量
    const char *const DICT_PATH = "./cppjieba/dict/jieba.dict.utf8";
    const char *const HMM_PATH = "./cppjieba/dict/hmm_model.utf8";
    const char *const USER_DICT_PATH = "./cppjieba/dict/user.dict.utf8";
    const char *const IDF_PATH = "./cppjieba/dict/idf.utf8";
    const char *const STOP_WORD_PATH = "./cppjieba/dict/stop_words.utf8";

    // 分词工具单例：封装 cppjieba 分词 + 停用词过滤
    class JiebaUtil
    {
    private:
        cppjieba::Jieba jieba;
        std::unordered_map<std::string, bool> stop_words; // 停用词表，用于过滤无意义词
    private:
        JiebaUtil() : jieba(DICT_PATH, HMM_PATH, USER_DICT_PATH, IDF_PATH, STOP_WORD_PATH) {}
        JiebaUtil(const JiebaUtil &) = delete;
        static JiebaUtil *instance;

    public:
        // 双重检查锁：保证多线程下只创建一个实例
        static JiebaUtil *get_instance()
        {
            static std::mutex mtx;
            if (nullptr == instance)
            {
                mtx.lock();
                if (nullptr == instance)
                {
                    instance = new JiebaUtil();
                    instance->InitJiebaUtil();
                }
                mtx.unlock();
            }
            return instance;
        }

        // 加载停用词文件到内存，供分词后过滤使用
        void InitJiebaUtil()
        {
            std::ifstream in(STOP_WORD_PATH);
            if (!in.is_open())
            {
                LOG(FATAL, "load stop words file error");
                return;
            }
            std::string line;
            while (std::getline(in, line))
            {
                stop_words.insert({line, true});
            }
            in.close();
        }

        // 搜索模式分词 + 过滤停用词，最终结果写入 out
        void CutStringHelper(const std::string &src, std::vector<std::string> *out)
        {
            jieba.CutForSearch(src, *out);
            for (auto iter = out->begin(); iter != out->end();)
            {
                auto it = stop_words.find(*iter);
                if (it != stop_words.end())
                {
                    // 说明当前的string 是暂停词，需要去掉
                    iter = out->erase(iter);
                }
                else
                {
                    iter++;
                }
            }
        }

    public:
        // 对外统一接口：获取单例并执行分词
        static void CutString(const std::string &src, std::vector<std::string> *out)
        {
            ns_util::JiebaUtil::get_instance()->CutStringHelper(src, out);
        }
    };
    JiebaUtil *JiebaUtil::instance = nullptr;
}