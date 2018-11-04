#include <ZKEvent/ZKEvent.h>
#include <thread>
#include <unistd.h>


int main(int argc, char* argv[])
{
    std::vector<std::string> servers;
    servers.emplace_back("127.0.0.1:2181");
    servers.emplace_back("192.168.242.128:2181");

    ZKEvent* client = new ZKEvent(servers, 5000);

    client->set_connected_callback([client]() {
        client->create("/test", "data", 0, [](const Status& status, const Slice& path) {
            if (status.is_ok()) {
                fprintf(stderr, "create success, path = %s\n", path.to_string().c_str());
            }
            else {
                fprintf(stderr, "create error %s\n", status.to_string().c_str());
            }
        });

        client->get("/test", [](const Status& status, const Slice& data) {
            if (status.is_ok()) {
                fprintf(stderr, "get success, data = %s\n", data.to_string().c_str());
            }
            else {
                fprintf(stderr, "get error %s\n", status.to_string().c_str());
            }
        });


        client->exists("/test", [](const Status& status, bool exists) {
            if (status.is_ok()) {
                fprintf(stderr, "exists success, %d\n", exists);
            }
            else {
                fprintf(stderr, "exists error, %d\n", exists);
            }
        });

        client->children("/", [](const Status& status, StringVectorPtr strings){
            if (status.is_ok()) {
                fprintf(stderr, "children success");
                for (int i = 0; i < strings->size(); ++i) {
                    fprintf(stderr, "path = %s\n", (*strings)[i].c_str());
                }
            }
            else {
                fprintf(stderr, "children error");
            }
        });

        client->subscribe_data_changes("/test", [](const Status& status, const std::string& data){
            if (status.is_ok()) {
                fprintf(stderr, "data changes %s\n", data.c_str());
            }
            else {
                fprintf(stderr, "delete error");
            }
        });
    });


    client->start_connect();

    client->loop();
    delete client;
}
