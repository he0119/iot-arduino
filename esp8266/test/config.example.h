// Config
const char *device_name = "test"; // 设备名称

const char *ssid = "";     // WiFi 名称
const char *password = ""; // WiFi 密码

const char *server_url = "iot.hemengyang.tk"; // 服务器地址
const int server_port = 443;                  // 服务器端口
const int device_id = 1;                      // 设备 ID，与服务器一致
#define DHT_VERSION_11                        // DHT 版本 11 或者 22
#define ENABLE_SSL                            // 是否使用 SSL 与服务器通讯
#define ENABLE_DEBUG                          // 是否开启 DEBUG

// TODO: 以后使用 TOKEN 与服务器进行认证
const char *admin_name = ""; // 网页管理员账户
const char *admin_password = "";
