// clientcallerthread.cpp
#include "client_thread.h"
#include "client.h"  // 包含 client 静态库接口
#include <cstdio>
#include <sstream>
#include <iostream>
#include <streambuf>
#include <fstream>
#include <windows.h>

class StdoutRedirector {
public:
    StdoutRedirector(ClientCallerThread* thread) : m_thread(thread) {
        // 1. 保存原 stdout/stderr 和 C++ 流缓冲
        m_oldStdout = GetStdHandle(STD_OUTPUT_HANDLE);
        m_oldStderr = GetStdHandle(STD_ERROR_HANDLE);
        m_oldCoutBuf = std::cout.rdbuf();
        m_oldCerrBuf = std::cerr.rdbuf();

        // 2. 创建匿名管道（读端用于捕获，写端用于重定向输出）
        SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE}; // 允许子进程继承句柄
        CreatePipe(&m_pipeRead, &m_pipeWrite, &sa, 0);

        // 3. 重定向系统 stdout/stderr 到管道写端
        SetStdHandle(STD_OUTPUT_HANDLE, m_pipeWrite);
        SetStdHandle(STD_ERROR_HANDLE, m_pipeWrite);

        // 4. 重定向 C++ std::cout/std::cerr 到自定义流缓冲（通过回调写入管道）
        m_customCoutBuf = std::make_unique<CustomStreamBuf>(m_pipeWrite);
        std::cout.rdbuf(m_customCoutBuf.get());
        std::cerr.rdbuf(m_customCoutBuf.get()); // 若需捕获 cerr
    }

    ~StdoutRedirector() {
        // 恢复原输出
        SetStdHandle(STD_OUTPUT_HANDLE, m_oldStdout);
        SetStdHandle(STD_ERROR_HANDLE, m_oldStderr);
        std::cout.rdbuf(m_oldCoutBuf);
        std::cerr.rdbuf(m_oldCerrBuf);

        // 关闭管道句柄
        CloseHandle(m_pipeRead);
        CloseHandle(m_pipeWrite);
    }

    void readOutput() {
        char buffer[1024];
        DWORD bytesRead;
        while (ReadFile(m_pipeRead, buffer, sizeof(buffer)-1, &bytesRead, nullptr) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            m_thread->logOutput(QString::fromLocal8Bit(buffer));
        }
    }

private:
    // 自定义流缓冲：将 C++ 流输出通过 WriteFile 写入管道
    class CustomStreamBuf : public std::streambuf {
    public:
        CustomStreamBuf(HANDLE pipeWrite) : m_pipeWrite(pipeWrite) {}

        // 当流缓冲区满或调用 std::endl 时触发，写入管道
        int overflow(int c) override {
            if (c != EOF) {
                char ch = static_cast<char>(c);
                DWORD bytesWritten;
                WriteFile(m_pipeWrite, &ch, 1, &bytesWritten, nullptr);
            }
            return c;
        }

        // 刷新缓冲区（如 std::flush）
        int sync() override {
            return 0; // 无需额外操作，overflow 已即时写入
        }

    private:
        HANDLE m_pipeWrite;
    };

    ClientCallerThread* m_thread;
    HANDLE m_oldStdout = INVALID_HANDLE_VALUE, m_oldStderr = INVALID_HANDLE_VALUE;
    HANDLE m_pipeRead = INVALID_HANDLE_VALUE, m_pipeWrite = INVALID_HANDLE_VALUE;
    std::streambuf* m_oldCoutBuf = nullptr;
    std::streambuf* m_oldCerrBuf = nullptr;
    std::unique_ptr<CustomStreamBuf> m_customCoutBuf; // 自定义流缓冲
};

ClientCallerThread::ClientCallerThread(const QString& configPath, const QString& routeIp, QObject* parent)
    : QThread(parent), m_configPath(configPath), m_routeIp(routeIp) {}

void ClientCallerThread::run() {
    StdoutRedirector redirector(this);
    std::thread logThread(&StdoutRedirector::readOutput, &redirector);


    int exitCode = start_vpn_client(m_configPath.toLocal8Bit().constData(),  m_routeIp.toLocal8Bit().constData());

    logThread.join();
    emit finished(exitCode);
}

void ClientCallerThread::stop(){
    m_stopRequested = true;
    stop_vpn_client();
    wait();
}