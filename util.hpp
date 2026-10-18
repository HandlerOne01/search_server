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

        // 判断一个分词结果是否为空白（空串、纯空格、\t、\r、\n 等）
        // 说明：cppjieba 的 CutForSearch 对英文文本会把空格也切成一个词，如果不拦掉，
        //       空格会被建进倒排索引，任何带空格的查询都会命中"空格"，等于按文档长度排全表。
        static bool IsBlankToken(const std::string &s)
        {
            for (size_t i = 0; i < s.size(); i++)
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\v' && c != '\f')
                    return false;
            }
            return true; // 空串或纯空白
        }

        // 搜索模式分词 + 过滤停用词与空白词，最终结果追加到 out
        //  注意：这里不能一边遍历一边 out->erase()。空白词在英文语料里占比很高，
        //  边遍历边 erase 是 O(n^2) 的元素搬移，会让索引构建慢一个数量级（实测 31s -> 178s）。
        void CutStringHelper(const std::string &src, std::vector<std::string> *out)
        {
            std::vector<std::string> words;
            jieba.CutForSearch(src, words);
            for (size_t i = 0; i < words.size(); i++)
            {
                const std::string &w = words[i];
                if (stop_words.find(w) != stop_words.end() || IsBlankToken(w))
                    continue; // 停用词或空白词，丢弃
                out->push_back(w);
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