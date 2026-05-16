# C++ 实用编码规范

> 基于 Google C++ Style Guide，精选日常开发中最常用、最重要的规则，附代码示例。

---

## 一、头文件

### 1.1 #define 保护

每个头文件必须有 `#ifndef` 保护，格式为 `<PATH>_<FILE>_H_`：

```cpp
// http_server.h
#ifndef HTTP_SERVER_H_
#define HTTP_SERVER_H_

class HttpServer {
    // ...
};

#endif  // HTTP_SERVER_H_
```

### 1.2 只包含必需的头文件

不依赖间接包含——用到哪个符号就包含哪个头文件：

```cpp
// Bad — 依赖 request.h 间接包含了 url.h
#include "request.h"
Url* url = new Url("/api");  // Url 定义在 url.h，但没有直接包含

// Good
#include "request.h"
#include "url.h"
Url* url = new Url("/api");
```

### 1.3 包含顺序

各组之间空一行，组内按字母顺序：

```cpp
// http_server.cc

#include <sys/socket.h>        // 1. 系统头文件
#include <unistd.h>

#include <map>                 // 2. C++ 标准库
#include <string>
#include <vector>

#include <openssl/ssl.h>       // 3. 第三方库
#include <zlib.h>

#include "http_server.h"       // 4. 关联头文件

#include "log/logger.h"        // 5. 项目内其他头文件
#include "net/socket_util.h"
#include "utils/string_util.h"
```

---

## 二、命名规范

| 实体 | 规则 | 示例 |
|------|------|------|
| 文件名 | 小写下划线 | `http_server.cc`、`string_util.h` |
| 类 / 结构体 / 枚举类型 | UpperCamelCase | `HttpServer`、`UserConfig` |
| 普通变量 / 函数参数 | snake_case | `user_name`、`retry_count` |
| 类数据成员 | snake_case + 尾部 `_` | `host_`、`port_`、`status_` |
| 常量（constexpr / const 静态） | `k` + CamelCase | `kMaxRetry`、`kDefaultPort` |
| 函数 / 方法 | UpperCamelCase | `SendRequest()`、`GetStatus()` |
| 命名空间 | snake_case | `http_util`、`net_helper` |
| 枚举成员 | `k` + CamelCase | `kStatusOk`、`kColorRed` |
| 宏 | 全大写下划线 | `MAX_BUFFER_SIZE`、`CHECK_NULL` |

```cpp
// 命名示例
namespace http_util {

constexpr int kMaxRetryCount = 3;
constexpr int kDefaultPort = 8080;

enum class ConnStatus { kConnected, kDisconnected, kTimeout };

struct RequestInfo {
    std::string method;
    std::string path;
    int32_t timeout_ms;
};

class HttpClient {
public:
    bool Connect(const std::string& host, int32_t port);
    ConnStatus GetStatus() const;

private:
    std::string host_;
    int32_t port_;
    ConnStatus status_;
};

}  // namespace http_util
```

---

## 三、类设计

### 3.1 构造函数不做复杂初始化

```cpp
// Bad — 构造函数中打开文件，失败了怎么办？
class FileReader {
public:
    FileReader(const std::string& path) {
        fd_ = open(path.c_str(), O_RDONLY);  // 可能失败，无法信号化错误
    }
};

// Good — 构造函数只赋值，复杂逻辑放 Init()
class FileReader {
public:
    explicit FileReader(const std::string& path) : path_(path) {}

    bool Init() {
        fd_ = open(path_.c_str(), O_RDONLY);
        return fd_ >= 0;
    }

private:
    std::string path_;
    int fd_ = -1;
};
```

### 3.2 单参数构造函数加 explicit

```cpp
// Bad — 可能发生意外的隐式转换
class Buffer {
public:
    Buffer(int size) { data_.resize(size); }
};

void Process(Buffer buf);
Process(42);  // 意外地把 42 转成了 Buffer！

// Good
class Buffer {
public:
    explicit Buffer(int size) { data_.resize(size); }
};

Process(42);          // 编译报错，意图不清
Process(Buffer(42));  // 明确，OK
```

### 3.3 不需要拷贝时显式删除

```cpp
class Singleton {
public:
    static Singleton& Instance() {
        static Singleton instance;
        return instance;
    }

    Singleton(const Singleton&) = delete;
    Singleton& operator=(const Singleton&) = delete;

private:
    Singleton() = default;
};
```

### 3.4 重写虚函数用 override

```cpp
class Shape {
public:
    virtual double Area() const = 0;
    virtual void Draw() = 0;
    virtual ~Shape() = default;
};

class Circle : public Shape {
public:
    // Good — override 确保真的在重写，笔误（如 area() 拼错）会编译报错
    double Area() const override;
    void Draw() override;

    // Bad — 重复 virtual，且无编译验证
    // virtual void Draw();
};
```

### 3.5 数据成员必须是 private

```cpp
// Bad — 外部可随意修改，破坏不变量
class Rectangle {
public:
    int width;
    int height;
};

// Good
class Rectangle {
public:
    Rectangle(int width, int height) : width_(width), height_(height) {}
    int width() const { return width_; }
    int height() const { return height_; }
    int Area() const { return width_ * height_; }
    void SetWidth(int width) { width_ = width; }

private:
    int width_;
    int height_;
};
```

### 3.6 优先组合而非继承

```cpp
// Bad — 为了复用 Logger 而继承，耦合过深
class UserService : public Logger {
    // ...
};

// Good — 通过组合持有 Logger
class UserService {
public:
    explicit UserService(Logger* logger) : logger_(logger) {}

private:
    Logger* logger_;
};
```

---

## 四、函数设计

### 4.1 优先用返回值，而非输出参数

```cpp
// Bad — 输出参数不直观
void GetUserName(int id, std::string* name);

// Good — 直接返回
std::string GetUserName(int id);

// 需要表达失败时，用 optional（C++17）
std::optional<std::string> FindUserName(int id);

auto name = FindUserName(42);
if (name.has_value()) {
    Use(*name);
}
```

### 4.2 输入/输出参数约定

```cpp
// 输入参数：值 或 const 引用
void SendPacket(int fd, const std::string& data);

// 输出参数：指针（调用处一看 & 就知道是输出）
bool ParseHeader(const std::string& raw, int* content_length);

// 调用处一目了然
int content_length = 0;
if (ParseHeader(raw, &content_length)) {
    ReadBody(content_length);
}
```

### 4.3 const 方法

```cpp
class UserCache {
public:
    // 不修改成员，标 const
    std::string Get(const std::string& key) const;
    size_t Size() const { return data_.size(); }

    // 修改成员，不标 const
    void Set(const std::string& key, const std::string& value);
    void Clear();

private:
    std::unordered_map<std::string, std::string> data_;
};
```

---

## 五、现代 C++ 特性

### 5.1 auto 类型推导

类型明显时用 `auto`，类型重要时写明：

```cpp
// Good — 类型显而易见
auto server = std::make_unique<HttpServer>(port);
auto it = user_map.find(user_id);
auto [iter, ok] = user_map.insert({user_id, user_info});  // 结构化绑定 C++17

// Bad — 类型不明确，强迫读者去查函数签名
auto result = Compute();
auto cfg = Load();

// Good — 写明类型
std::string user_name = GetUserName(id);
ServerConfig cfg = LoadConfig(path);
```

### 5.2 智能指针（尽量不用裸 new/delete）

智能指针在析构时自动释放资源，极大减少内存泄漏风险：

```cpp
// 尽量不用 — 需要手动管理，中途 return 或异常都可能泄漏
Session* session = new Session(fd);
if (!session->Init()) {
    delete session;   // 容易漏写
    return false;
}
// ... 其他地方 delete session

// Good — 离开作用域自动释放
auto session = std::make_unique<Session>(fd);
if (!session->Init()) {
    return false;  // 自动释放，不用手动 delete
}

// 工厂函数返回智能指针，所有权清晰
std::unique_ptr<Session> CreateSession(int fd) {
    auto s = std::make_unique<Session>(fd);
    if (!s->Init()) {
        return nullptr;
    }
    return s;
}

// 共享所有权（谨慎使用，引用计数有开销）
std::shared_ptr<Config> cfg = std::make_shared<Config>();
// 多个模块持有同一份 cfg，最后一个释放时自动销毁
```

### 5.3 移动语义

```cpp
class Buffer {
public:
    explicit Buffer(size_t size) : data_(size) {}

    // 移动构造/赋值加 noexcept，vector 扩容时才会选择移动而非拷贝
    Buffer(Buffer&&) noexcept = default;
    Buffer& operator=(Buffer&&) noexcept = default;

    // 不需要拷贝时显式禁用
    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

private:
    std::vector<uint8_t> data_;
};

Buffer CreateBuffer(size_t size) {
    Buffer buf(size);
    // ... 填充数据
    return buf;  // NRVO 或移动，不发生拷贝
}
```

### 5.4 Lambda 表达式

```cpp
std::vector<int> scores = {85, 42, 96, 73, 58};

// 简单 lambda — 降序排序
std::sort(scores.begin(), scores.end(),
    [](int a, int b) { return a > b; });

// 捕获外部变量
int pass_line = 60;
auto it = std::find_if(scores.begin(), scores.end(),
    [pass_line](int score) { return score < pass_line; });

// 逃逸作用域时显式捕获（避免悬空引用）
auto callback = [&config, &logger]() {  // Good — 捕获内容一目了然
    logger.Info(config.GetServerAddr());
};

auto bad = [&]() {  // 尽量不用 — 不清楚捕获了哪些，若 lambda 逃逸作用域容易悬空
    logger.Info(config.GetServerAddr());
};
```

### 5.5 constexpr 替代宏常量

```cpp
// Bad — 宏没有类型，没有作用域，调试时看不到值
#define MAX_CONN 100
#define TIMEOUT_MS 5000

// Good — constexpr 有类型、有作用域、调试友好
constexpr int kMaxConnections = 100;
constexpr int kTimeoutMs = 5000;

// 类内常量
class TcpServer {
public:
    static constexpr int kDefaultPort = 8080;
    static constexpr int kBacklog = 128;
};
```

### 5.6 nullptr

```cpp
// Bad
int* p = NULL;
char* s = 0;
if (ptr == NULL) { /* ... */ }

// Good
int* p = nullptr;
char* s = nullptr;
if (ptr == nullptr) { /* ... */ }
```

### 5.7 Range-based for

```cpp
std::vector<std::string> hosts = {"192.168.1.1", "192.168.1.2"};

// Bad — 手动索引
for (size_t i = 0; i < hosts.size(); ++i) {
    Connect(hosts[i]);
}

// Good — 只读遍历
for (const auto& host : hosts) {
    Connect(host);
}

// Good — 需要修改元素
for (auto& host : hosts) {
    host = NormalizeHost(host);
}
```

### 5.8 override 标注虚函数重写

```cpp
class EventHandler {
public:
    virtual void OnConnect(int fd) = 0;
    virtual void OnDisconnect(int fd) = 0;
    virtual void OnData(int fd, const char* data, size_t len) = 0;
    virtual ~EventHandler() = default;
};

class MyHandler : public EventHandler {
public:
    // Good — override 确保真的在重写，拼错方法名会编译报错
    void OnConnect(int fd) override;
    void OnDisconnect(int fd) override;
    void OnData(int fd, const char* data, size_t len) override;
};
```

### 5.9 enum class（强类型枚举）

```cpp
// Bad — 成员污染外层命名空间，TIMEOUT 与其他枚举容易冲突
enum ConnState { CONNECTED, DISCONNECTED, TIMEOUT };
enum ReadState { OK, ERROR, TIMEOUT };  // 编译报错：TIMEOUT 重复定义

// Good — 成员需要限定名，不会冲突
enum class ConnState { kConnected, kDisconnected, kTimeout };
enum class ReadState { kOk, kError, kTimeout };

ConnState cs = ConnState::kConnected;
ReadState rs = ReadState::kTimeout;  // 不冲突
```

### 5.10 结构化绑定（C++17）

```cpp
std::map<std::string, int> port_map;

// Bad — .first / .second 不直观
auto result = port_map.insert({"http", 80});
if (result.second) {
    printf("inserted at %s\n", result.first->first.c_str());
}

// Good — 有意义的名字
auto [iter, inserted] = port_map.insert({"http", 80});
if (inserted) {
    printf("inserted: %s\n", iter->first.c_str());
}

// 遍历 map
for (const auto& [service, port] : port_map) {
    printf("%s -> %d\n", service.c_str(), port);
}
```

### 5.11 std::optional（C++17）

```cpp
// Bad — 用 -1 表示"未找到"，-1 是魔法数字
int FindPort(const std::string& service) {
    auto it = port_map.find(service);
    return (it != port_map.end()) ? it->second : -1;
}

// Good — 意图明确
std::optional<int> FindPort(const std::string& service) {
    auto it = port_map.find(service);
    if (it == port_map.end()) return std::nullopt;
    return it->second;
}

auto port = FindPort("https");
if (port.has_value()) {
    Connect(host, *port);
} else {
    printf("service not found\n");
}
```

### 5.12 std::string_view（C++17）

```cpp
// Bad — 传入 const char* 时会构造临时 std::string，发生拷贝
void Log(const std::string& msg);
Log("server started");  // 一次拷贝

// Good — string_view 是只读视图，零拷贝，兼容 string 和 const char*
void Log(std::string_view msg);
Log("server started");     // 无拷贝
Log(some_std_string);      // 无拷贝
Log(str.substr(0, 10));    // 无拷贝
```

### 5.13 类型转换

```cpp
double ratio = 3.75;
int fd = 5;

// Bad — C 风格强转，不安全，grep 也难搜索
int r = (int)ratio;
void* p = (void*)fd;

// Good — C++ 风格，意图明确
int r = static_cast<int>(ratio);              // 值转换，有截断
void* p = reinterpret_cast<void*>(           // 危险转换，标注清楚
    static_cast<intptr_t>(fd));

// 花括号初始化防窄化
int32_t small_val{static_cast<int32_t>(ratio)};  // 需要显式转换才能编译
```

---

## 六、异常与错误处理

### 尽量不用异常，推荐用错误码或 optional

C++ 异常会使控制流难以追踪，嵌入式 / 实时系统中通常编译时关闭异常支持：

```cpp
// 尽量不用
void LoadConfig(const std::string& path) {
    if (!FileExists(path)) throw std::runtime_error("file not found");
}
try {
    LoadConfig("/etc/app.conf");
} catch (const std::exception& e) {
    fprintf(stderr, "error: %s\n", e.what());
}

// 推荐 — 错误码，调用方必须显式处理
int LoadConfig(const std::string& path) {
    if (!FileExists(path)) return -1;
    // ...
    return 0;
}
if (LoadConfig("/etc/app.conf") != 0) {
    fprintf(stderr, "failed to load config\n");
}

// 推荐 — optional，表达"成功有值 / 失败无值"
std::optional<Config> LoadConfig(const std::string& path) {
    if (!FileExists(path)) return std::nullopt;
    // ...
    return Config{...};
}
auto cfg = LoadConfig("/etc/app.conf");
if (!cfg) {
    fprintf(stderr, "failed to load config\n");
}
```

---

## 七、应尽量避免的写法

| 写法 | 问题 | 替代方案 |
|------|------|---------|
| 裸 `new` / `delete` | 所有权不清，容易泄漏 | `make_unique` / `make_shared` |
| `throw` / `try` / `catch` | 控制流复杂，嵌入式通常禁用 | 错误码、`std::optional` |
| `using namespace xxx` | 污染命名空间，引发名称冲突 | 显式写 `std::string`、`ns::Foo` |
| C 风格强转 `(int)x` | 不安全，难以搜索 | `static_cast<int>(x)` |
| `NULL` / `0` 表示空指针 | 类型不安全 | `nullptr` |
| `#define` 定义常量 | 无类型无作用域，难调试 | `constexpr` |
| 多重实现继承 | 菱形继承，歧义 | 组合，或只继承纯虚接口 |
| `long`、`short` | 位宽因平台而异 | `int32_t`、`int16_t` |
| `long double` | 不同平台精度不一致 | `double` |
| 普通 `enum` | 成员污染外层命名空间 | `enum class` |

---

## 八、代码格式

### 8.1 缩进与行宽

- **缩进**：4 个空格，不用 Tab
- **行宽**：最多 80 字符

### 8.2 大括号（K&R 风格）

```cpp
// Good
if (condition) {
    DoSomething();
} else {
    DoOther();
}

for (int i = 0; i < n; ++i) {
    Process(i);
}

// Bad — 左括号换行
if (condition)
{
    DoSomething();
}
```

### 8.3 指针和引用靠近类型

```cpp
// Good
int* p;
const std::string& s;
void Func(char* buf, int* out_len);

// Bad
int *p;
const std::string &s;
```

### 8.4 switch 必须有 default

```cpp
switch (conn_state) {
    case ConnState::kConnected:
        HandleConnected();
        break;
    case ConnState::kDisconnected:
        HandleDisconnected();
        break;
    default:
        fprintf(stderr, "unknown state: %d\n",
                static_cast<int>(conn_state));
        break;
}
```

有意 fall-through 加 `[[fallthrough]]`：

```cpp
switch (event_type) {
    case EventType::kKeyDown:
        RecordKeyDown();
        [[fallthrough]];  // 继续执行下一 case
    case EventType::kKeyRepeat:
        HandleKeyInput();
        break;
    default:
        break;
}
```

### 8.5 空行约定

- 函数间 1 个空行
- 逻辑块间适当空行
- 不超过 2 个连续空行
- 代码块开头 / 结尾不加空行

---

## 九、注释

### 9.1 函数注释

```cpp
// 声明处（.h）：描述做什么、参数约束、返回值含义

// 在连接池中查找空闲连接。
// @param timeout_ms  超时时间（毫秒），0 表示不等待
// @return            成功返回连接 fd，无可用连接返回 -1
int AcquireConnection(int timeout_ms);

// 实现处（.cc）：描述怎么做、算法选择原因
int ConnectionPool::AcquireConnection(int timeout_ms) {
    // 优先复用已建立的空闲连接，避免频繁握手开销
    std::lock_guard<std::mutex> lock(mutex_);
    if (!free_list_.empty()) {
        int fd = free_list_.front();
        free_list_.pop_front();
        return fd;
    }
    // 池已满时阻塞等待，而不是直接返回失败，提高连接复用率
    return WaitForFreeConnection(timeout_ms);
}
```

### 9.2 不写废话注释

```cpp
// Bad — 注释重复了代码本身
retry_count++;          // 重试次数加一
return conn_fd;         // 返回连接 fd

// Good — 注释解释为什么，而不是做什么
retry_count++;          // 首次失败不告警，三次后才触发告警
// 此处不加锁，fd 在创建后只读，多线程安全
return conn_fd;
```

### 9.3 TODO 注释

```cpp
// TODO(wangwu): 等旧协议客户端全部升级后，删除这段兼容逻辑
// TODO: bug #2345 — 弱网环境下偶现粘包，待复现后修复
```

---

> **参考**：[Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
