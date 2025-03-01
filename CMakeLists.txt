project(myWebServer)
cmake_minimum_required(VERSION 3.5)
# 从./include中寻找头文件
include_directories(include)
# 使用c++17标准
set(CMAKE_CXX_STANDARD 17)
# 添加thread库
find_package(Threads REQUIRED)
# 添加src目录下的所有文件
file(GLOB_RECURSE SRC_FILES "src/*.cpp")
add_executable(myWebServer ${SRC_FILES})