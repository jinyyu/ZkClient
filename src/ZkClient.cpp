#include <assert.h>
#include "ZkClient/ZkClient.h"
#include "ZkClient/DebugLog.h"

namespace zkcli
{

static const char* zk_state_to_str(int state)
{
    if (state == ZOO_EXPIRED_SESSION_STATE) {
        return "EXPIRED_SESSION_STATE";
    }
    if (state == ZOO_AUTH_FAILED_STATE) {
        return "AUTH_FAILED_STATE";
    }
    if (state == ZOO_CONNECTING_STATE) {
        return "CONNECTING_STATE";
    }
    if (state == ZOO_ASSOCIATING_STATE) {
        return "ASSOCIATING_STATE";
    }
    if (state == ZOO_CONNECTED_STATE) {
        return "CONNECTED_STATE";
    }
    if (state == ZOO_READONLY_STATE) {
        return "READONLY_STATE";
    }
    if (state == ZOO_NOTCONNECTED_STATE) {
        return "NOTCONNECTED_STATE";
    }
    return "INVALID_STATE";
}

const char* zk_type_to_str(int type)
{
    if (type == ZOO_CREATED_EVENT) {
        return "CREATED_EVENT";
    }
    if (type == ZOO_DELETED_EVENT) {
        return "DELETED_EVENT";
    }
    if (type == ZOO_CHANGED_EVENT) {
        return "CHANGED_EVENT";
    }
    if (type == ZOO_CHILD_EVENT) {
        return "CHILD_EVENT";
    }
    if (type == ZOO_SESSION_EVENT) {
        return "SESSION_EVENT";
    }
    if (type == ZOO_NOTWATCHING_EVENT) {
        return "NOTWATCHING_EVENT";
    }
    return "INVALID_TYPE";
}

const char* ZkClient::err_to_string(int err)
{
    return err_string(err);
}

ZkClient::ZkClient(boost::asio::io_service& io_service, const std::string& servers, int timeout)
    : io_service_(io_service),
      servers_(servers),
      timeout_(timeout),
      zk_(nullptr),
      client_id_(nullptr),
      thread_id_(0)
{
    //disable log
    zoo_set_debug_level((ZooLogLevel) 0);
}

ZkClient::~ZkClient()
{
    zookeeper_close(zk_);
}

void ZkClient::set_connected_callback(const VoidCallback& cb)
{
    run_in_loop([cb, this] {
        connected_cb_ = cb;
    });

}

void ZkClient::set_session_expired_callback(const VoidCallback& cb)
{
    run_in_loop([cb, this]() {
        session_expired_cb_ = cb;
    });
}

void ZkClient::start_connect()
{
    run_in_loop([this]() {
        zk_ = zookeeper_init(servers_.c_str(), ZkClient::zk_event_cb, timeout_, client_id_, this, 0);
        if (!zk_) {
            LOG_DEBUG("zookeeper_init error %s", strerror(errno));
            return;
        }
    });
}

void ZkClient::run_in_loop(const VoidCallback& cb)
{
    if (pthread_self() == thread_id_) {
        cb();
    }
    else {
        io_service_.post(cb);
    }
}

void ZkClient::zk_event_cb(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx)
{
    std::string path_copy(path);
    ZkClient* zk = (ZkClient*) watcherCtx;
    zk->run_in_loop([zk, zh, type, state, path_copy]() {
        zk->do_watch_event_cb(zh, type, state, path_copy.c_str());
    });
}

void ZkClient::do_watch_event_cb(zhandle_t* zh, int type, int state, const std::string& path)
{
    LOG_DEBUG("state %s, type = %s, path = %s", zk_state_to_str(state), zk_type_to_str(type), path.c_str())

    if (type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
        thread_id_ = pthread_self();
        client_id_ = zoo_client_id(zh);
        if (connected_cb_) {
            connected_cb_();
        }
    }

    if (state == ZOO_EXPIRED_SESSION_STATE && session_expired_cb_) {
        session_expired_cb_();
        return;
    }

    if (path.empty()) {
        return;
    }

    if (type == ZOO_CHANGED_EVENT || type == ZOO_DELETED_EVENT || type == ZOO_CREATED_EVENT) {
        auto it = data_changes_cb_.find(path);
        if (it != data_changes_cb_.end()) {
            DataChangesEvent event;
            if (type == ZOO_CHANGED_EVENT) {
                event = DataChangesEvent::CHANGES;
            }
            else if (type == ZOO_DELETED_EVENT) {
                event = DataChangesEvent::DELETE;
            }
            else if (type == ZOO_CREATED_EVENT) {
                event = DataChangesEvent::CREATE;
            }
            else {
                event = DataChangesEvent::ERROR;
            }

            it->second.operator()(ZOK, event);
            do_subscribe_data_changes(it->first);
        }
    }

    if (type == ZOO_CHILD_EVENT) {
        auto it = child_changes_cb_.find(path);
        if (it != child_changes_cb_.end()) {
            async_get_children(path, 1, it->second);
        }
    }
}

void ZkClient::async_create(const std::string& path, const Slice& data, const StringCallback& cb)
{
    std::string data_copy(data.data(), data.size());
    run_in_loop([this, path, data_copy, cb]() {
        StringCallback* callback = new StringCallback(cb);
        int ret = zoo_acreate(zk_,
                              path.c_str(),
                              data_copy.c_str(),
                              data_copy.size(),
                              &ZOO_OPEN_ACL_UNSAFE,
                              0,
                              ZkClient::string_completion,
                              callback);
        if (ret != ZOK) {
            cb((ZOO_ERRORS) ret, Slice(nullptr, 0));
            delete (callback);
        }
    });
}

void ZkClient::async_get(const std::string& path, int watch, const StringCallback& cb)
{
    run_in_loop([path, watch, cb, this]() {
        StringCallback* callback = new StringCallback(cb);
        int ret = zoo_aget(zk_, path.c_str(), watch, ZkClient::data_completion, callback);
        if (ret != ZOK) {
            cb((ZOO_ERRORS) ret, Slice(nullptr, 0));
            delete (callback);
        }
    });
}

void ZkClient::async_set(const std::string& path, const Slice& data, const AsyncCallback& cb)
{
    std::string data_copy(data.data(), data.size());
    run_in_loop([this, path, cb, data_copy]() {
        StateCallback* callback = new StateCallback([cb](int err, const Stat* state) {
            cb(err);
        });
        int ret =
            zoo_aset(zk_, path.c_str(), data_copy.data(), data_copy.size(), -1, ZkClient::stat_completion, callback);
        if (ret != ZOK) {
            cb(ret);
            delete (callback);
        }
    });
}

void ZkClient::async_exists(const std::string& path, int watch, const ExistsCallback& cb)
{
    run_in_loop([this, path, watch, cb]() {
        StateCallback* callback = new StateCallback([cb](int err, const Stat* state) {
            if (err == ZOK) {
                cb(ZOK, true);
            }
            else if (err == ZNONODE) {
                cb(ZOK, false);
            }
            else {
                cb(err, false);
            }
        });
        int ret = zoo_aexists(zk_, path.c_str(), watch, ZkClient::stat_completion, callback);
        if (ret != ZOK) {
            cb(ret, false);
            delete (callback);
        }
    });
}

void ZkClient::async_get_children(const std::string& path, int watch, const StringsCallback& cb)
{
    run_in_loop([this, path, watch, cb]() {
        StringsCallback* callback = new StringsCallback(cb);
        int ret = zoo_aget_children(zk_, path.c_str(), watch, ZkClient::strings_completion, callback);
        if (ret != ZOK) {
            cb(ret, nullptr);
            delete (callback);
        }
    });

}

void ZkClient::subscribe_data_changes(const std::string& path, const DataChangesCallback& cb)
{
    run_in_loop([this, path, cb]() {
        data_changes_cb_[path] = cb;
        do_subscribe_data_changes(path);
    });
}

void ZkClient::unsubscribe_data_changes(const std::string& path)
{
    run_in_loop([this, path]() {
        if (data_changes_cb_.find(path) != data_changes_cb_.end()) {
            data_changes_cb_.erase(path);
        }
    });
}

void ZkClient::subscribe_child_changes(const std::string& path, const StringsCallback& cb)
{
    run_in_loop([this, path, cb]() {
        child_changes_cb_[path] = cb;
        do_subscribe_child_changes(path);
    });
}

void ZkClient::string_completion(int rc, const char* value, const void* data)
{
    StringCallback* cb = (StringCallback*) data;
    Slice result;
    if (rc == ZOK) {
        result = Slice(value, strlen(value));
    }
    cb->operator()((ZOO_ERRORS) rc, result);
    delete (cb);
}

void ZkClient::data_completion(int rc, const char* value, int value_len,
                               const struct Stat* stat, const void* data)
{
    StringCallback* cb = (StringCallback*) data;
    Slice result;
    if (rc == ZOK) {
        result = Slice(value, value_len);
    }
    cb->operator()((ZOO_ERRORS) rc, result);
    delete (cb);
}

void ZkClient::stat_completion(int rc, const struct Stat* stat, const void* data)
{
    StateCallback* cb = (StateCallback*) data;
    cb->operator()(rc, stat);
    delete (cb);
}

void ZkClient::strings_completion(int rc, const struct String_vector* strings, const void* data)
{
    StringsCallback* cb = (StringsCallback*) data;
    if (rc != ZOK) {
        cb->operator()(rc, nullptr);
    }
    else {
        StringVectorPtr string_list(new std::vector<std::string>());

        for (auto i = 0; i < strings->count; ++i) {
            string_list->push_back(strings->data[i]);
        }
        cb->operator()(rc, string_list);
    }
    delete (cb);
}

void ZkClient::do_subscribe_data_changes(const std::string& path)
{
    assert(thread_id_ == pthread_self());

    async_exists(path, 1, [this, path](int err, bool exists) {
        if (err != ZOK) {
            LOG_DEBUG("subscribe error %s, %s", path.c_str(), err_string(err));
            auto it = data_changes_cb_.find(path);
            if (it != data_changes_cb_.end()) {
                it->second.operator()(err, DataChangesEvent::ERROR);
                data_changes_cb_.erase(path);
            }
            return;
        }
        else {
            LOG_DEBUG("subscribe data changes success %s", path.c_str());
        }
    });
}

void ZkClient::do_subscribe_child_changes(const std::string& path)
{
    assert(thread_id_ == pthread_self());

    async_get_children(path, 1, [this, path](int err, StringVectorPtr children) {
        if (err != ZOK) {
            LOG_DEBUG("subscribe error %s, %s", path.c_str(), err_string(err));
            auto it = child_changes_cb_.find(path);
            if (it != child_changes_cb_.end()) {
                it->second.operator()(err, nullptr);
                child_changes_cb_.erase(path);
            }
        }
        else {
            LOG_DEBUG("subscribe child success %s", path.c_str());
        }
    });
}

}