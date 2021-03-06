// Config
const char *device_name = "auto_watering"; // 设备名称

const char *ssid = "";     // WiFi 名称
const char *password = ""; // WiFi 密码

const char *server_url = "127.0.0.1"; // 服务器地址
const int server_port = 5000;         // 服务器端口
const int device_id = 1;              // 设备 ID，与服务器一致
#define DHT_VERSION_22                // DHT 版本 11 或者 22
// #define ENABLE_SSL                    // 是否使用 SSL 与服务器通讯

const char *admin_name = ""; // 网页管理员账户
const char *admin_password = "";
// TODO: 以后使用TOKEN与服务器进行认证
