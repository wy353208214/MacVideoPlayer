cmake_minimum_required(VERSION 3.2)
#项目名称
PROJECT(VideoPlayer)

# 设置spdlog目录
set(SPDLOG_DIR /usr/local/Cellar/spdlog/1.8.5)
# 设置fmt目录
set(FMT_DIR /usr/local/Cellar/fmt/7.1.3)
# 设置基础库目录
set(XCODE_DIR /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/)

# ffmpeg安装目录
set(FFMPEG /usr/local/Cellar/ffmpeg/4.3.1_9)
# 设置SDL目录
set(SDL /usr/local/Cellar/sdl2/2.0.14_1)

# 设置编译模式为DEBUG，否则不可debug
set(CMAKE_BUILD_TYPE DEBUG) 

# 设置包含源文件的目录，这里将src目录赋给APP_SRC变量
aux_source_directory(./src APP_SRC)
# 如文件少的话，也可以单独设置，如main.cpp赋给APP_SRC
# set(APP_SRC main.cpp)

# 可以设置可执行文件的输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/runtime)

# 设置gcc额外编译参数
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# Boost
set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 
# 设置要查找的库，如system库
find_package(Boost 1.79.0 COMPONENTS system thread)
if(Boost_FOUND)
    message("Boost not found")
endif()
# Boost

#头文件
include_directories(
    ${SPDLOG_DIR}/include
    ${FMT_DIR}/include
    ${FFMPEG}/include
    ${SDL}/include
    ${Boost_INCLUDE_DIRS}
)
# 声明动态库位置
link_directories(
    ${SPDLOG_DIR}/lib
    ${FMT_DIR}/lib/
    # ${XCODE_DIR}
    ${FFMPEG}/lib
    ${SDL}/lib
)

# 设置可执行文件
add_executable(worker ${APP_SRC})

target_link_libraries(
    worker 
    curl 
    fmt 
    spdlog
    avcodec
    avdevice
    avfilter
    avformat
    avresample
    avutil
    postproc
    swresample
    SDL2
    swscale
    ${Boost_LIBRARIES}
)
# 用g++编译指令如下：g++ worker.cpp -o test -std=c++11 -lcurl -lspdlog -lfmt   