#include <iostream>
#include <unistd.h>
#include <cstring>
#include <string>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <regex>

const int BUFFER_SIZE = 4096;
std::string frontend_hostname_templ;
std::string backend_hostname_templ;
int listen_port;

// 将模板转为正则表达式
std::string pattern_to_regex(const std::string& pattern) {
    std::string result;

    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 1 < pattern.size() && pattern[i + 1] == '}') {
            result += "(.+)";
            ++i; // 跳过 '}'
        } else {
            const std::string special = R"(\.^$|()[]{}*+?)";
            if (special.find(pattern[i]) != std::string::npos) {
                result += '\\';
            }
            result += pattern[i];
        }
    }

    return result;
}

// 从输入中提取匹配变量
std::string extract_variable(const std::string& input, const std::string& pattern) {
    std::regex re(pattern_to_regex(pattern));
    std::smatch match;

    if (std::regex_search(input, match, re)) {
        return match[1];  // 捕获第一个 ()
    }

    return "";  // 未匹配
}

// 拼接目标地址，例如：bc.abc.local
std::string build_target(const std::string& variable, const std::string& target_template) {
    std::string result = target_template;
    size_t pos = result.find("{}");
    if (pos != std::string::npos) {
        result.replace(pos, 2, variable);
    }
    return result;
}

int connect_to_host(const std::string& hostname, const std::string& port) {
    struct addrinfo hints{}, *res, *p;
    int sockfd;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;        // 只用 IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    int status = getaddrinfo(hostname.c_str(), port.c_str(), &hints, &res);
    if (status != 0) {
        std::cerr << "getaddrinfo 错误: " << gai_strerror(status) << std::endl;
        return -1;
    }

    for (p = res; p != nullptr; p = p->ai_next) {
        sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sockfd == -1) {
            continue;
        }

        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }

        break;
    }

    freeaddrinfo(res);

    if (p == nullptr) {
        std::cerr << "无法连接到 " << hostname << " 的任何地址" << std::endl;
        return -1;
    }

    return sockfd;  // 返回连接的 socket fd
}

// 简单的解析函数，根据 payload 决定后端地址
std::string choose_backend(const std::string& payload) {
    std::string mc_hostname_start = payload.substr(5);
    std::string var = extract_variable(mc_hostname_start, frontend_hostname_templ);
    return build_target(var, backend_hostname_templ);
}

int connect_backend(const std::string& payload){
    std::string backend_hostname_port = choose_backend(payload);
    std::cout << "choose backend: " << backend_hostname_port << std::endl;
    size_t colon_pos = backend_hostname_port.find(':');

    std::string backend_host = backend_hostname_port.substr(0, colon_pos);
    std::string backend_port_str = backend_hostname_port.substr(colon_pos + 1);

    return connect_to_host(backend_host, backend_port_str);
}

void forward(int from, int to) {
    char buffer[BUFFER_SIZE];
    ssize_t n;
    while ((n = recv(from, buffer, BUFFER_SIZE, 0)) > 0) {
        send(to, buffer, n, 0);
    }
    shutdown(from, SHUT_RDWR);
    shutdown(to, SHUT_RDWR);
}

void handle_client(int client_fd) {
    // 读取前几个字节作为判断依据
    char peek[64];
    int n = recv(client_fd, peek, sizeof(peek), MSG_PEEK); // 不消费
    if (n <= 0) {
        close(client_fd);
        return;
    }

    int backend_fd = connect_backend(std::string(peek, n));
    if (backend_fd < 0) {
        close(client_fd);
        return;
    }

    // 开始双向转发
    std::thread t1(forward, client_fd, backend_fd);
    std::thread t2(forward, backend_fd, client_fd);
    t1.join();
    t2.join();

    close(client_fd);
    close(backend_fd);
}

int parse_arg(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "用法: mcdproxy 前端域名模板:监听端口 后端域名端口模板" << std::endl;
        std::cerr << "例如: mcdproxy {}.abc.com:25565 bc.{}.local:25565" << std::endl;
        return 1;
    }

    // 解析参数
    std::string arg = argv[1];
    size_t colon_pos = arg.find(':');
    if (colon_pos == std::string::npos) {
        std::cerr << "前端参数格式错误，应为 域名:端口" << std::endl;
        return 1;
    }
    frontend_hostname_templ = arg.substr(0, colon_pos);
    std::string frontend_port_str = arg.substr(colon_pos + 1);
    listen_port = std::stoi(frontend_port_str);

    backend_hostname_templ = argv[2];

    std::cout << "listen port: " << frontend_port_str << std::endl;
    std::cout << "frontend_tmpl: " << frontend_hostname_templ << std::endl;
    std::cout << "backend_tmpl: " << backend_hostname_templ << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
    if (parse_arg(argc, argv)) {
        return 1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(listen_port);

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 128);

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        std::thread(handle_client, client_fd).detach();
    }

    close(server_fd);
    return 0;
}
