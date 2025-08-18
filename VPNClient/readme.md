本项目同时支持qmake和cmake生成Makefile以构建运行<br/>
- 在vscode打开项目文件夹（Qt相关配置详见[此处](https://blog.csdn.net/WlkJiangYou/article/details/136262589)）后你可以选择**qmake方式**: 选择“终端 -> 运行任务 -> run-debug/run-release”而后就会在build/里面生成debug或release文件夹，不过需要你手动将根目录的**wintun.dll**复制到生成的VPNClient.exe同级目录。
- 也可以选择**cmake方式**: 在mingw64终端（或者别的什么终端，只要用mingw64编译就行）进入build文件夹，执行如下指令：
    ```
    rm -rf *
    cmake ..
    ninja
    ```
    或者：
    ```
    rm -rf *
    cmake .. -G "Unix Makefiles" # 会生成Makefile
    make
    ```
    这种方式的好处是你不需要手动复制wintun.dll
