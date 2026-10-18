#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_map>
#include <mutex>
#include "util.hpp"
#include "log.hpp"

namespace ns_index
{

    struct DocInfo
    {
        std::string title;   // 文档的标题
        std::string content; // 文档对应的去标签之后的内容
        std::string url;     // 官网文档url
        uint64_t doc_id;     // 文档的ID，暂时先不做过多理解
    };

    // 倒排元素：记录某个词在某个文档中的出现信息及权重
    struct InvertedElem
    {
        uint64_t doc_id;
        std::string word;
        int weight;
        InvertedElem() : weight(0) {}
    };

    // 倒排拉链
    typedef std::vector<InvertedElem> InvertedList;

    // 索引单例：维护正排索引(文档ID→文档信息)和倒排索引(关键词→倒排拉链)
    class Index
    {
    private:
        // 正排索引的数据结构用数组，数组的下标天然是文档的ID
        std::vector<DocInfo> forward_index; // 正排索引

        // 倒排索引
        std::unordered_map<std::string, InvertedList> inverted_index;

    private:
        Index() {}
        Index(const Index &) = delete;
        Index &operator=(const Index &) = delete;
        static Index *instance;
        static std::mutex mtx;

    public:
        ~Index() {}

    public:
        //// 双重检查锁：保证多线程下只创建一个索引实例
        static Index *GetInstance()
        {
            if (nullptr == instance)
            {
                mtx.lock();
                if (nullptr == instance)
                {
                    instance = new Index();
                }
                mtx.unlock();
            }
            return instance;
        }

        // 根据doc_id找到文档内容
        DocInfo *GetForwardIndex(uint64_t doc_id)
        {
            if (doc_id >= forward_index.size())
            {
                std::cerr << "doc_id out range, error!" << std::endl;
                return nullptr;
            }
            return &forward_index[doc_id];
        }

        // 根据关键字string，获得倒排拉链
        InvertedList *GetInvertedList(const std::string &word)
        {
            auto iter = inverted_index.find(word);
            if (iter == inverted_index.end())
            {
                std::cerr << word << " have no InvertedList" << std::endl;
                return nullptr;
            }
            return &(iter->second);
        }

        // 根据去标签，格式化之后的文档，构建正排和倒排索引
        // data/raw_html/raw.txt
        bool BuildIndex(const std::string &input)
        {
            std::ifstream in(input, std::ios::in | std::ios::binary);

            if (!in.is_open())
            {
                std::cerr << "sorry, " << input << " open error" << std::endl;
                return false;
            }
            std::string line;
            int count = 0;
            while (std::getline(in, line))
            {
                DocInfo *doc = BuildForwardIndex(line);
                if (nullptr == doc)
                {
                    std::cerr << "build " << line << " error" << std::endl; // for deubg
                    continue;
                }
                BuildInvertedIndex(*doc);
                count++;
                LOG(NORMAL, "当前的已经建立的索引文档: " + std::to_string(count));
            }
            return true;
        }

    private:
        // 解析一行原始数据，构造正排索引条目（DocInfo），插入 forward_index 并返回
        DocInfo *BuildForwardIndex(const std::string &line)
        {
            // 1. 解析line，字符串切分
            // line -> 3 string, title, content, url
            std::vector<std::string> results;
            const std::string sep = "\3"; // 行内分隔符
            ns_util::StringUtil::Split(line, &results, sep);
            if (results.size() != 3)
                return nullptr;

            // 2.字符串进行填充到DocIinfo
            DocInfo doc;
            doc.title = results[0];
            doc.content = results[1];
            doc.url = results[2];
            doc.doc_id = forward_index.size();

            // 插入到正排索引的vector
            forward_index.push_back(std::move(doc));
            return &forward_index.back();
        }

        // 对文档的标题和内容分词、统计词频，构建/更新倒排索引
        bool BuildInvertedIndex(const DocInfo &doc)
        {
            // DocInfo{title, content, url, doc_id}
            // word -> 倒排拉链
            //  暂存当前文档中每个词的标题出现次数和正文出现次数
            struct word_cnt
            {
                int title_cnt;
                int content_cnt;
                word_cnt() : title_cnt(0), content_cnt(0) {}
            };
            std::unordered_map<std::string, word_cnt> word_map; // 用来暂存词频的映射表

            // 对标题进行分词
            std::vector<std::string> title_words;
            ns_util::JiebaUtil::CutString(doc.title, &title_words);
            for (std::string s : title_words)
            {
                boost::to_lower(s);
                word_map[s].title_cnt++; // 如果存在就获取，如果不存在就新建
            }

            // 对文档内容进行分词
            std::vector<std::string> content_words;
            ns_util::JiebaUtil::CutString(doc.content, &content_words);
            // 对内容进行词频统计
            for (std::string s : content_words)
            {
                boost::to_lower(s);
                word_map[s].content_cnt++;
            }

// 权重系数：标题词频权重远高于正文词频，体现标题关键词的重要性
#define X 10
#define Y 1
            for (auto &word_pair : word_map)
            {
                InvertedElem item;
                item.doc_id = doc.doc_id;
                item.word = word_pair.first;
                item.weight = X * word_pair.second.title_cnt + Y * word_pair.second.content_cnt;
                InvertedList &inverted_list = inverted_index[word_pair.first]; // 拿一下这个词对应的拉链，有则取出无则创建
                inverted_list.push_back(std::move(item));                      // 尾插入拉链
            }
            return true;
        }
    };
    Index *Index::instance = nullptr;
    std::mutex Index::mtx;

}