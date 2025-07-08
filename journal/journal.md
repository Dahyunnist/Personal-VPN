### 2025/7/1 Tue
1. 入职
2. 新电脑环境配置（VScode上的C/C++环境没有搭好）到下午
3. 下午在Windows上实现了最基础的客户端和服务端（服务端原样返回客户端发送的数据）（C:/tasks/task1）
4. 实现了服务端在WSL上运行
### 2025/7/2 Wed
1. 完善了VScode上的C/C++环境配置
2. 实现了通过select模型进行多客户端连接与交互
3. 下午换了新电脑，重新配置环境（配置WSL环境花了很长时间）
4. 开始学习TLS，完成了OpenSSL的下载安装，服务端和客户端安全证书的配置以及server的大部分TLS改造
### 2025/7/3 Thu
1. 安装了OpenSSL，完成了server和client的TLS改造
![alt text](./pictures/250703_1.png)
![alt text](./pictures/250703_2.png)
2. 安装WireShark并实现了抓包验证加密通信
![alt text](./pictures/250703_3.png)
![alt text](./pictures/250703_4.png)
### 2025/7/4 Fri
1. 完成了Windows和WSL上Boost库的安装
2. 使用Boost库对server程序进行了重构
![alt text](./pictures/250704_1.png)
### 2025/7/7
1. 使用Boost库对client程序进行了重构
2. 抓包验证了server和client加密通信
3. 学习了部分tun设备相关基础知识
###
>进入下一阶段：对现有CS进行基于TUN设备的VPN通信改造，主要任务如下：
>   1. client创建tun设备，为tun设备配置IP
>   2. 添加路由，将某个连接的数据进入到tun设备
>   3. 从tun设备中读取数据，然后将数据包发送到server，从server段读取数据包并写入到tun设备
>   4. server创建tun设备，并配置IP添加路由
>   5. 开启IP转发，通过iptables添加一条NAT规则
>   6. 从client端收数据包，然后将数据包写入到tun设备

### 2025/7/8