#include <iostream>
#include "sys/time.h"
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/thread/thread.hpp>
#include <boost/lexical_cast.hpp>
#include <thread>
#include "block/block_queue_use_vector.h"
#include <atomic>
#include <iomanip>
#include <fstream>
#include <stdio.h>
#include "image_util.h"
#include "flv_parase.h"

extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavutil/channel_layout.h"
    #include "libavutil/opt.h"
    #include "libavutil/mathematics.h"
    #include "libavutil/timestamp.h"
    #include "libswscale/swscale.h"
    #include "libavutil/imgutils.h"
    #include "libavutil/time.h"
    #include "libswresample/swresample.h"
    #include "libavfilter/avfilter.h"
    #include "SDL2/SDL.h"
    #include "SDL2/SDL_thread.h"
};

using namespace std;

// 是否保存关键帧
const bool isSaveKeyFrame = false;

const AVPixelFormat AV_PIX_FMT = AV_PIX_FMT_YUV420P;
// 要转换输出的音频声道
const uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
// 输出的音频单位采样大小
const int out_nb_samples = 1024;
// 输出采样率
// const int out_sample_rate = 44100;
// 音频帧大小
const int MAX_AUDIO_FRAME_SIZE = 192000;
// 输出采样格式S16
enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;

const double AV_SYNC_THRESHOLD = 0.01;
const double AV_NOSYNC_THRESHOLD = 10.0;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
SDL_Rect rect;
int win_width = 720;
int win_height = 480;
int video_width;
int video_height;

AVFormatContext *avFormatContext;

AVCodecContext *videoCodecCtx;
AVFrame *convertFrame;
SwsContext *swsContext;
int videoIndex;

AVCodecContext *audioCodecCtx;
int audioIndex;
SwrContext *swrContext;
int out_channels;
int out_sample_rate;
// 音频缓冲区大小
int out_buffer_size;
uint8_t *out_buffer;
int64_t int_channel_layout;
// 当前帧已播放的音频大小
int audio_buf_index = 0;

double audio_clock;
double video_clock;

double last_video_pts;
double last_video_delay;
double frame_timer;

uint8_t *audio_chunk;
uint32_t audio_len;
uint8_t *audio_pos;

BlockQueue<AVPacket *> video_packet_queue(100);
BlockQueue<AVPacket *> audio_packet_queue(100);
atomic_bool isFinished(false);

thread demuxThread;
thread decodeVideoThread;
thread decodeAudioThread;

SDL_mutex *pause_mutex;
SDL_cond *pause_cond;
atomic_bool isPause(false);

ofstream out;
FILE *pFile;
int a_c = 0;

void openFile();
int initSDL();
void freeSDL();
void updateTexture(AVFrame *frame);
void audioCallBack(void *data, Uint8 *stream, int len);
void setAudioOpt();

double getAudioClock();
double getVideoClock(AVFrame *videoFrame, double pts);
double syncVideoDelay();

void demuxPacket();
void decodeVideoFrame();
void decodeAudioFrame();

void rescaleVideoSize(int &srcW, int &srcH, int dsW, int dsH);

void saveFrameToYuv(AVFrame *avFrame);
int saveFrameToJpg(AVFrame *avFrame, const char *outPath);
void aac_parser(char *url);

int main()
{
    // 解析FLV文件
    // paraseFlv("/Users/steven/Movies/Video/S8.flv");

    // 要保存的yuv文件
    // const char *path = "/Users/steven/Dev_Project/Cpp-Study/MyVideoPlayer/test_u.yuv";
    // out = std::ofstream(path, std::ios_base::binary);
    // pFile = fopen(path,  "wb+");

    //打开文件
    openFile();
    return 0;
}

void openFile()
{
    string urls[4] = {
        "/Users/steven/Movies/Video/S8.mp4",
        "/Users/steven/Movies/ManyCam/My Recording.mp4",
        "http://localhost:8889/flv?port=1935&app=live&stream=test",
        "/Users/steven/Movies/Video/luoxiang.mp4"
    };

    string url;
    string selectStr;
    selectStr.append("1. ").append(urls[0]).append("\n")
            .append("2. ").append(urls[1]).append("\n")
            .append("3. ").append(urls[2]).append("\n")
            .append("4. ").append(urls[3]).append("\n")
            .append("Or enter the video url");
    cout<<"请选择或输入一个视频链接："<<endl;
    cout<<selectStr<<endl;
    cout<<"Enter url：";

    getline(cin, url);
    int selectNumber = 1;
    try
    {
        selectNumber = boost::lexical_cast<int>(url.c_str());
        if(selectNumber > 4 || selectNumber < 0) {
            spdlog::error("Enter error number! It will play the first one.");
            selectNumber = 1;
        }
        url = urls[selectNumber - 1];
    }
    catch(boost::bad_lexical_cast const&)
    {
        if (url == "n") {
            selectNumber = 1;
            url = urls[selectNumber - 1];
        }
    }
    spdlog::info("Video url is \"{}\"", url);

    pause_mutex = SDL_CreateMutex();
    pause_cond = SDL_CreateCond();

    // av_register_all(); 新版ffmpeg已经不需要注册了
    avformat_network_init();
    avFormatContext = avformat_alloc_context();
    // 转换后的视频帧
    convertFrame = av_frame_alloc();

    // 打开文件
    int ret = avformat_open_input(&avFormatContext, url.c_str(), NULL, NULL);
    if (ret != 0)
    {
        spdlog::error("打开文件失败！");
        return;
    }

    ret = avformat_find_stream_info(avFormatContext, NULL);
    if (ret != 0)
    {
        spdlog::error("获取音视频流信息失败！");
        return;
    }

    // 打印输出视频信息
    av_dump_format(avFormatContext, 0, url.c_str(), 0);

    videoIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    audioIndex = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (videoIndex == -1)
    {
        spdlog::error("获取视频索引失败！");
        return;
    }
    if (audioIndex == -1)
    {
        spdlog::error("获取音频索引失败！");
        return;
    }
    AVCodecParameters *videoCodecpar = avFormatContext->streams[videoIndex]->codecpar;
    AVCodecParameters *audioCodecpar = avFormatContext->streams[audioIndex]->codecpar;
    AVCodec *videoCodec = avcodec_find_decoder(videoCodecpar->codec_id);
    AVCodec *audioCodec = avcodec_find_decoder(audioCodecpar->codec_id);
    if (videoCodec == NULL)
    {
        spdlog::error("获取视频解码器失败！");
        return;
    }
    if (audioCodec == NULL)
    {
        spdlog::error("获取音频解码器失败！");
        return;
    }
    videoCodecCtx = avcodec_alloc_context3(videoCodec);
    audioCodecCtx = avcodec_alloc_context3(audioCodec);
    if (avcodec_parameters_to_context(videoCodecCtx, videoCodecpar) != 0)
    {
        spdlog::error("创建视频解码器失败！");
        return;
    }
    if (avcodec_parameters_to_context(audioCodecCtx, audioCodecpar) != 0)
    {
        spdlog::error("创建音频解码器失败！");
        return;
    }

    ret = avcodec_open2(videoCodecCtx, videoCodec, NULL);
    if (ret != 0)
    {
        spdlog::error("视频解码失败！");
        return;
    }
    ret = avcodec_open2(audioCodecCtx, audioCodec, NULL);
    if (ret != 0)
    {
        spdlog::error("音频解码失败！");
        return;
    }

    if (initSDL() < 0)
    {
        spdlog::error("初始化SDL失败！");
        return;
    }

    demuxThread = thread(demuxPacket);
    decodeVideoThread = thread(decodeVideoFrame);
    decodeAudioThread = thread(decodeAudioFrame);

    // 解封装线程
    demuxThread.detach();
    // 视频解码线程
    decodeVideoThread.detach();
    // 音频解码线程
    decodeAudioThread.detach();

    // SDL事件循环，播放视频
    while (!isFinished)
    {
        //监听事件，否则窗口不会创建
        SDL_Event event;
        // WaitEvent作用，事件发生的时候触发，没有事件的时候阻塞在这里。
        SDL_WaitEvent(&event);
        // PollEvent作用，直接查看事件队列，如果事件队列中有事件，直接返回事件，并删除此事件。如果没有事件也直接返回，所以Poll会把事件中的变量删除掉，因此这里不能使用Poll。
        // SDL_PollEvent(&event);
        switch (event.type)
        {
        case SDL_QUIT:
            cout << "SDL_QUIT event" << endl;
            isFinished = true;
            break;
        case SDL_WINDOWEVENT:
            // 这里可以监听窗口变化从而更新图像大小，适应窗口变化
            if (event.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                SDL_GetWindowSize(window, &win_width, &win_height);
                rescaleVideoSize(video_width, video_height, win_width, win_height);
                //宽高如果变的话，需要更新texture
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_width, video_height);
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_SPACE)
            {
                SDL_LockMutex(pause_mutex);
                isPause = !isPause;
                spdlog::info("视频{}", isPause ? "暂停中" : "播放中");
                if (!isPause)
                    SDL_CondSignal(pause_cond);
                SDL_UnlockMutex(pause_mutex);
            }

            break;
        case SDL_DISPLAYEVENT:
            AVFrame *videoFrame = (AVFrame *)event.user.data1;
            updateTexture(videoFrame);
            break;
        }
    }

    //释放内存
    avcodec_free_context(&videoCodecCtx);
    avcodec_free_context(&audioCodecCtx);
    avformat_free_context(avFormatContext);
    freeSDL();
}

// 解封装
void demuxPacket()
{
    // 设置audioCallback放在子线程，否则主线程又播放音频视频造成后续声音没声音
    setAudioOpt();
    while (!isFinished)
    {

        // SDL_LockMutex(pause_mutex);
        // if(isPause) {
        //     SDL_CondWait(pause_cond, pause_mutex);
        // }
        // SDL_UnlockMutex(pause_mutex);

        AVPacket *avPacket = av_packet_alloc();
        if (av_read_frame(avFormatContext, avPacket) != 0)
        {
            isFinished = true;
            av_packet_unref(avPacket);
            break;
        }
        int stream_index = avPacket->stream_index;
        if (stream_index == videoIndex)
        {
            video_packet_queue.push(avPacket);
        }
        else if (stream_index == audioIndex)
        {
            audio_packet_queue.push(avPacket);
        }
        else
        {
            av_packet_unref(avPacket);
        }
    }
    cout << "demux is finish" << endl;
}

void decodeVideoFrame()
{
    frame_timer = (double)av_gettime() / 1000000.0;
    last_video_delay = 40e-3;
    last_video_pts = 0.0;

    AVFrame *videoFrame = av_frame_alloc();
    double video_pts = 0;

    while (!isFinished)
    {
        AVPacket *avPacket = video_packet_queue.pop();
        int ret = avcodec_send_packet(videoCodecCtx, avPacket);
        if (ret < 0 || ret == AVERROR(EAGAIN))
        {
            av_packet_unref(avPacket);
            cout << "open video failed" << endl;
            isFinished = true;
            break;
        }
        if (ret == AVERROR_EOF)
        {
            av_packet_unref(avPacket);
            cout << "decode video finished" << endl;
            isFinished = true;
            break;
        }
        av_packet_unref(avPacket);

        if (avcodec_receive_frame(videoCodecCtx, videoFrame) != 0)
        {
            continue;
        }

        if ((video_pts = videoFrame->best_effort_timestamp) == AV_NOPTS_VALUE)
        {
            video_pts = 0;
        }
        // 获取视频pts
        video_pts *= av_q2d(avFormatContext->streams[videoIndex]->time_base);
        // 同步video_pts
        video_pts = getVideoClock(videoFrame, video_pts);
        // 计算当前帧与上一帧的延时
        double delay = video_pts - last_video_pts;
        if (delay <= 0 || delay >= 1.0)
        {
            delay = last_video_delay;
        }
        last_video_delay = delay;
        last_video_pts = video_pts;

        double audio_pts = getAudioClock();
        double diff = video_pts - audio_pts;
        double sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SAMPLE_FMT_DBL;
        if (fabs(diff) < AV_NOSYNC_THRESHOLD)
        {
            if (diff <= -sync_threshold)
            {
                delay = 0;
            }
            else if (diff >= sync_threshold)
            {
                delay = 2 * delay;
            }
        }

        frame_timer += delay;
        // 最终真正要延时的时间
        double actual_delay = frame_timer - (av_gettime() / 1000000.0);
        if (actual_delay < 0.01)
        {
            actual_delay = 0.01;
        }
        int video_delay = (int)(actual_delay * 1000 + 0.5);

        // saveFrameToYuv(videoFrame);
        if(videoFrame->key_frame && isSaveKeyFrame) {
            string path = "/Users/steven/Dev_Project/Cpp-Study/MyVideoPlayer/imgs/";
            path.append(to_string(av_gettime()));
            path.append(".jpg");
            // path.append(".png");
            saveFrameToImage(videoFrame, videoCodecCtx, path.c_str(), JPG);
        }
        
        //通知刷新页面，并将数据传递到SDL事件中
        SDL_Delay(video_delay);
        SDL_Event event;
        event.type = SDL_DISPLAYEVENT;
        event.user.data1 = videoFrame;
        SDL_PushEvent(&event);
    }
    cout << "decode video is finish" << endl;
}

void decodeAudioFrame()
{
    while (!isFinished)
    {
        // boost::this_thread::sleep(boost::posix_time::microseconds(500));
        AVPacket *avPacket = audio_packet_queue.pop();
        int ret = avcodec_send_packet(audioCodecCtx, avPacket);
        if (ret < 0 || ret == AVERROR(EAGAIN))
        {
            av_packet_unref(avPacket);
            cout << "open audio failed" << endl;
            isFinished = true;
            break;
        }
        if (ret == AVERROR_EOF)
        {
            av_packet_unref(avPacket);
            cout << "decode audio finished" << endl;
            isFinished = true;
            break;
        }

        // 计算packet的音频时间
        if (avPacket->pts != AV_NOPTS_VALUE)
        {
            audio_clock = av_q2d(avFormatContext->streams[audioIndex]->time_base) * avPacket->pts;
        }

        // 由于音频可能有多个帧，所以要循环读取
        int len = 0;
        AVFrame *audioFrame = av_frame_alloc();
        while (avcodec_receive_frame(audioCodecCtx, audioFrame) == 0)
        {
            // len为重采样后的采样个数，由于有多个数据，所以要累计len
            len += swr_convert(swrContext, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)audioFrame->data, audioFrame->nb_samples);
        }

        // cout<<"采样个数:"<<len<<", 声道数："<<out_channels<<", 采样位数:"<<av_get_bytes_per_sample(out_sample_fmt)<<", 采样频率:"<<audioCodecCtx->sample_rate<<endl;
        // cout<<setprecision(8)<<"一帧播放时长毫秒："<<(long double)len * 1000/(long double)(audioCodecCtx->sample_rate)<<endl;
        // cout<<"一秒播放字节大小 = 采样率 * 采样通道 * 采样位数："<<audioCodecCtx->sample_rate * out_channels * av_get_bytes_per_sample(out_sample_fmt)<<endl;

        // 一帧数据大小 = 采样个数 * 采样通道 * 采样位数，对应audio_buf_size
        out_buffer_size = len * out_channels * av_get_bytes_per_sample(out_sample_fmt);
        audio_chunk = (Uint8 *)out_buffer;
        audio_len = out_buffer_size;
        audio_pos = audio_chunk;

        // packet中pts比真正播放的pts可能会早很多，所以这里要重新计算packet中的数据可以播放的时长，再次更新audio_clock;
        audio_clock += (double)out_buffer_size / (double)(audioCodecCtx->sample_rate * out_channels * av_get_bytes_per_sample(out_sample_fmt));

        while (audio_len > 0)
        {
            SDL_Delay(1);
        }

        av_packet_unref(avPacket);
    }
    cout << "decode audio is finish" << endl;
}

// 这里计算真正的音频时钟
double getAudioClock()
{
    // 解码音频时获取到的音频时钟
    double pts = audio_clock;
    // 音频缓冲区剩余未播放的数据 = 缓冲区要播放的数据大小 - 已播放的数据大小
    int remain_buffer_size = out_buffer_size - audio_buf_index;
    // 一秒播放字节大小 = 采样率 * 采样通道 * 采样位数
    int bytes_per_sec = audioCodecCtx->sample_rate * out_channels * av_get_bytes_per_sample(out_sample_fmt);
    if (bytes_per_sec != 0)
        // 真正的pts = 实际pts - 缓冲区中还剩余未播放的音频数据时长
        pts = pts - (double)remain_buffer_size / bytes_per_sec;
    return pts;
}

double getVideoClock(AVFrame *videoFrame, double pts)
{
    double frame_delay;
    if (pts != 0)
    {
        video_clock = pts;
    }
    else
    {
        pts = video_clock;
    }
    // 时间基是分数，单位为妙，如time_base为:(1,50)，为1/50秒；以帧率为例，表示1/50秒显示一帧数据，即帧率为50FPS
    // 所以这里计算出每帧间的时间间隔
    frame_delay = av_q2d(videoCodecCtx->time_base);
    frame_delay += videoFrame->repeat_pict * (frame_delay * 0.5);
    video_clock += frame_delay;
    return pts;
}

// 播放音频
void audioCallBack(void *udata, Uint8 *stream, int len)
{
    audio_buf_index = 0;
    SDL_memset(stream, 0, len);
    while (len > 0)
    {
        if (audio_len == 0)
            continue;
        int temp = (len > audio_len ? audio_len : len);
        SDL_MixAudio(stream, audio_pos, temp, SDL_MIX_MAXVOLUME);

        audio_pos += temp;
        audio_len -= temp;
        stream += temp;
        len -= temp;
        audio_buf_index += temp;
    }
}

void updateTexture(AVFrame *frame)
{
    //要转换的视频
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT, video_width, video_height, 1);
    uint8_t *video_out_buffer = (uint8_t *)av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(convertFrame->data, convertFrame->linesize, video_out_buffer, AV_PIX_FMT,
                         video_width, video_height, 1);
    // sws_getGaussianVec(0.5, 1);
    swsContext = sws_getContext(videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt, video_width,
                                video_height, AV_PIX_FMT, SWS_BILINEAR, NULL, NULL, NULL);
    sws_scale(swsContext, (const uint8_t *const *)frame->data, frame->linesize, 0, videoCodecCtx->height, convertFrame->data, convertFrame->linesize);
    av_free(video_out_buffer);

    // SDL渲染显示视频
    SDL_UpdateYUVTexture(texture, NULL,
                         convertFrame->data[0], convertFrame->linesize[0],
                         convertFrame->data[1], convertFrame->linesize[1],
                         convertFrame->data[2], convertFrame->linesize[2]);

    // 设置渲染的位置（0，0）代表window的左上角，以下方式计算可以保证居中显示
    rect.x = (win_width - video_width) / 2;
    rect.y = (win_height - video_height) / 2;
    rect.w = video_width;
    rect.h = video_height;
    //显示图片
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    // SDL_RenderCopyEx(renderer, texture, NULL, NULL, 90, NULL, SDL_FLIP_VERTICAL);
    SDL_RenderPresent(renderer);
    // av_frame_free(&convertFrame);
}

void rescaleVideoSize(int &srcW, int &srcH, int dsW, int dsH)
{
    // 重新计算视频比例尺寸
    double video_aspect_ratio = (double)srcW / srcH;
    double win_aspect_ratio = (double)dsW / dsH;
    if (video_aspect_ratio > win_aspect_ratio)
    {
        srcW = dsW;
        srcH = dsW / video_aspect_ratio;
    }
    else if (video_aspect_ratio < win_aspect_ratio)
    {
        srcH = dsH;
        srcW = dsH * video_aspect_ratio;
    }

    srcW = (video_width >> 4) << 4;
    srcH = (video_height >> 4) << 4;

    spdlog::info("rescal video size \n window width：{}, window height：{} \n video width：{}, video height：{} \n video scale width：{}, video scale height：{}",
                 win_width, win_height,
                 videoCodecCtx->width, videoCodecCtx->height,
                 srcW, srcH);
    // cout<<"window width："<<win_width<<", window height："<<win_height<<endl;
    // cout<<"video width："<<videoCodecCtx->width<<"，video_height："<<videoCodecCtx->height<<endl;
    // cout<<"video scale width："<<srcW<<"，video scale height："<<srcH<<endl;
    // cout<<"<<================================>>"<<endl;
}

int initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        cout << "SDL init failed" << endl;
        return -1;
    }
    //创建播放窗口
    window = SDL_CreateWindow("Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, win_width,
                              win_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window)
        return -1;

    //创建渲染器
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer)
        return -1;

    video_width = videoCodecCtx->width;
    video_height = videoCodecCtx->height;
    rescaleVideoSize(video_width, video_height, win_width, win_height);

    //创建纹理，放到刷新里面
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_width, video_height);
    if (!texture)
        return -1;

    return 0;
}

void freeSDL()
{
    // if (window)
    //     SDL_DestroyWindow(window);
    // if (renderer)
    //     SDL_DestroyRenderer(renderer);
    // if (texture)
    //     SDL_DestroyTexture(texture);
    // SDL_CloseAudio();
    SDL_Quit();
    cout << "free SDL" << endl;
}

void setAudioOpt()
{
    // 初始化音频播放参数
    out_sample_rate = audioCodecCtx->sample_rate;
    out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 4);
    int_channel_layout = av_get_default_channel_layout(audioCodecCtx->channel_layout);
    swrContext = swr_alloc_set_opts(NULL, out_channel_layout, out_sample_fmt, out_sample_rate, int_channel_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate, 0, NULL);
    swr_init(swrContext);

    SDL_AudioSpec spec;
    spec.freq = out_sample_rate;
    spec.format = AUDIO_S16SYS;
    spec.channels = out_channels;
    spec.samples = out_nb_samples;
    spec.silence = 0;
    spec.callback = audioCallBack;
    spec.userdata = audioCodecCtx;

    if (SDL_OpenAudio(&spec, NULL) < 0)
    {
        spdlog::error("打开音频设备失败");
        return;
    }
    SDL_PauseAudio(0);
}

void saveFrameToYuv(AVFrame *avFrame)
{
    uint32_t pitchY = avFrame->linesize[0];
    uint32_t pitchU = avFrame->linesize[1];
    uint32_t pitchV = avFrame->linesize[2];

    uint8_t *avY = avFrame->data[0];
    uint8_t *avU = avFrame->data[1];
    uint8_t *avV = avFrame->data[2];

    // yuv420，Y：w * h
    for (uint32_t i = 0; i < avFrame->height; i++)
    {
        // 降低亮度需要降低y分量值的一半
        uint8_t *dy = avY + i * pitchY;
        // uint8_t temp = *dy;
        // *dy = temp / 2;
        // memset(dy, 128, avFrame->width);
        fwrite(dy, avFrame->width, 1, pFile);
    }

    for (uint32_t i = 0; i < avFrame->height / 2; i++)
    {
        // 消除U分量
        // memset(avU + i * pitchU, 128, avFrame->width / 2);
        fwrite(avU + i * pitchU, avFrame->width / 2, 1, pFile);
    }

    for (uint32_t i = 0; i < avFrame->height / 2; i++)
    {
        // 消除V分量
        // memset(avV + i * pitchV, 128, avFrame->width / 2);
        fwrite(avV + i * pitchV, avFrame->width / 2, 1, pFile);
    }
}

void aac_parser(char *url)
{
    FILE *pfile = fopen(url, "rb");
    if (!pfile)
    {
        spdlog::error("Can't open file");
        return;
    }

    // 这段代码是为了获取文件长度，从而分配char内存；也可以使用string.append()实现
    fseek(pfile, 0, SEEK_END); //先用fseek将文件指针移到文件末尾
    int n = ftell(pfile);
    fseek(pfile, 0, 0);
    char *buffer = new char[n];

    int data_size = 0;
    while (!feof(pfile))
    {
        data_size += fread(buffer + data_size, 1, n, pfile);
    }
    spdlog::info(buffer);
    delete[] buffer;
    fclose(pfile);
}



int saveFrameToJpg(AVFrame *pFrame, const char *out_name) {
    
    int width = pFrame->width;
    int height = pFrame->height;
    AVCodecContext *pCodeCtx = NULL;
    
    
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // 设置输出文件格式
    pFormatCtx->oformat = av_guess_format(NULL, NULL, "image/jpeg");

    // 创建并初始化输出AVIOContext
    if (avio_open(&pFormatCtx->pb, out_name, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Couldn't open output file.");
        return -1;
    }

    // 构建一个新stream
    AVStream *pAVStream = avformat_new_stream(pFormatCtx, 0);
    if (pAVStream == NULL) {
        return -1;
    }

    AVCodecParameters *parameters = pAVStream->codecpar;
    parameters->codec_id = pFormatCtx->oformat->video_codec;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->format = AV_PIX_FMT_YUVJ420P;
    parameters->width = pFrame->width;
    parameters->height = pFrame->height;

    AVCodec *pCodec = avcodec_find_encoder(pAVStream->codecpar->codec_id);

    if (!pCodec) {
        printf("Could not find encoder\n");
        return -1;
    }

    pCodeCtx = avcodec_alloc_context3(pCodec);
    if (!pCodeCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if ((avcodec_parameters_to_context(pCodeCtx, pAVStream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return -1;
    }

    pCodeCtx->time_base = (AVRational) {1, 25};

    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        printf("Could not open codec.");
        return -1;
    }

    int ret = avformat_write_header(pFormatCtx, NULL);
    if (ret < 0) {
        printf("write_header fail\n");
        return -1;
    }

    int y_size = width * height;

    //Encode
    // 给AVPacket分配足够大的空间
    AVPacket pkt;
    av_new_packet(&pkt, y_size * 3);

    // 编码数据
    ret = avcodec_send_frame(pCodeCtx, pFrame);
    if (ret < 0) {
        printf("Could not avcodec_send_frame.");
        return -1;
    }

    // 得到编码后数据
    ret = avcodec_receive_packet(pCodeCtx, &pkt);
    if (ret < 0) {
        printf("Could not avcodec_receive_packet");
        return -1;
    }

    ret = av_write_frame(pFormatCtx, &pkt);

    if (ret < 0) {
        printf("Could not av_write_frame");
        return -1;
    }

    av_packet_unref(&pkt);

    //Write Trailer
    av_write_trailer(pFormatCtx);


    avcodec_close(pCodeCtx);
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    return 0;
}