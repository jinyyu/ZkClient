#include <ZkClient/ZkClient.h>
#include <ZkClient/DebugLog.h>
#include <memory>
#include <unistd.h>
using namespace zkcli;

typedef std::shared_ptr<ZkClient> ZkClientPtr;

class Subscriber
{
public:
    explicit Subscriber(const std::string& server, int timeout)
        : server_(server),
          timeout_(timeout)
    {

    }

    ~Subscriber()
    {

    }

    void init_zk_client()
    {
        ZkClient* zk = new ZkClient(server_, timeout_);
        zk->set_connected_callback([zk, this]() {
            LOG_DEBUG("connected");
            ZkClientPtr p(zk);
            zk_ = p;
            this->subscriber_data_changes();
            this->subscriber_child_changes();
        });

        zk->set_session_expired_callback([this]() {
            LOG_DEBUG("session expired")
            this->init_zk_client();
        });
        zk->start_connect();

        zk->run();
    }

private:
    void subscriber_data_changes()
    {
        zk_->subscribe_data_changes("/test_zkcli", [this](int err, const Slice& data) {
            if (err != ZOK) {
                LOG_DEBUG("subscribe data error %s", ZkClient::err_to_string(err));
                return;
            }
            std::string path(data.data(), data.size());
            zk_->async_get(path, 0, [path, this](int err, const Slice& data) {
                if (err != ZOK) {
                    LOG_DEBUG("async get  error %s", ZkClient::err_to_string(err));
                    return;
                }
                this->on_data_changes(path, data);
            });
        });
    }

    void subscriber_child_changes()
    {
        const char* path = "/test_zkcli";
        zk_->subscribe_child_changes("/test_zkcli", [this, path](int err, StringVectorPtr strings) {
            if (err != ZOK) {
                LOG_DEBUG("subscribe data error %s", ZkClient::err_to_string(err));
                return;
            }
            this->on_child_changes(path, strings);
        });
    }


    void on_data_changes(const std::string& path, const Slice& data)
    {
        LOG_DEBUG("data changes path = %s, data = %s", path.c_str(), data.data());
    }

    void on_child_changes(const std::string& path, StringVectorPtr child)
    {
        LOG_DEBUG("child changed path = %s", path.c_str());

        for (auto it = child->begin(); it != child->end(); ++it) {
            LOG_DEBUG("path = %s", it->c_str());
        }
    }
private:

    std::string server_;
    int timeout_;
    ZkClientPtr zk_;

};

int main(int argc, char* argv[])
{
    Subscriber subscriber("127.0.0.1:2181", 5000);
    subscriber.init_zk_client();
    sleep(10000);
    //getchar();
}
