#ifndef ZKCLIENT_ZKCLIENT_H
#define ZKCLIENT_ZKCLIENT_H
#include <string>
#include <zookeeper/zookeeper.h>
#include <ZkClient/AsyncCallback.h>
#include <ZkClient/errors.h>

namespace zkcli
{

class ZkClient
{
public:
    explicit ZkClient(const std::string& servers, int timeout);

    void start_connect();

    ~ZkClient();

    void async_create(const std::string& path, const Slice& data, const StringCallback& cb);

    void async_get(const std::string& path, int watch, const StringCallback& cb);

    void async_set(const std::string& path, const Slice& data, const AsyncCallback& cb);

    void async_exists(const std::string& path, int watch, const ExistsCallback& cb);

private:


    static void zk_event_cb(zhandle_t* zh, int type,
                            int state, const char* path, void* watcherCtx);

    void do_watch_event_cb(zhandle_t* zh, int type, int state, const char* path);

    static void string_completion(int rc, const char* value, const void* data);

    static void data_completion(int rc, const char* value, int value_len,
                                const struct Stat* stat, const void* data);

    static void stat_completion(int rc, const struct Stat* stat, const void* data);

private:
    std::string servers_;
    int timeout_;

    zhandle_t* zk_;
};

}

#endif //ZKCLIENT_ZKCLIENT_H
