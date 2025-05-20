# 第一个阶段：构建阶段
FROM gcc:13 AS builder

# 设置工作目录
WORKDIR /app

# 复制源码和 Makefile
COPY main.cpp Makefile ./

# 编译程序
RUN make

# 第二个阶段：精简运行环境（仅包含可执行文件）
FROM ubuntu

# 安装运行时必要的库
RUN apt-get update && apt-get install -y libstdc++6 && rm -rf /var/lib/apt/lists/*

# 复制可执行文件
COPY --from=builder /app/mcdproxy /usr/local/bin/mcdproxy

# 设置默认启动命令
CMD ["mcdproxy", "{}.sducraft.top:25565", "bc.{}.svc.cluster.local:25565"]
