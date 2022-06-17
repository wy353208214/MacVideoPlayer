// 保存AVFrame为图片的工具
#ifndef IMAGE_UTIL_H
#define IMAGE_UTIL_H

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
};

enum IMAGE_FMT {
    PNG,
    JPG
} ImageFmt;

AVFrame* convertAvFrame(AVFrame *avFrame, AVCodecContext *codecCtx, AVPixelFormat dsFmt);
void saveFrameToImage(AVFrame *avFrame, AVCodecContext *codecCtx, const char *outPath, IMAGE_FMT fmt);


void saveFrameToImage(AVFrame *avFrame, AVCodecContext *codecCtx, const char *outPath, IMAGE_FMT fmt) {
    int width = codecCtx->width;
    int height = codecCtx->height;

    AVCodec *codecId;
    AVPixelFormat pixFmt;
    AVFrame *convertFrame;
    switch (fmt)
    {
    case PNG:
        codecId = avcodec_find_encoder(AV_CODEC_ID_PNG);
        pixFmt = AV_PIX_FMT_RGB24;
        // png需要先将原始frame转换为RGB格式的
        convertFrame = convertAvFrame(avFrame, codecCtx, pixFmt);
        break;
    case JPG:
        codecId = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        pixFmt = AV_PIX_FMT_YUVJ420P;
        convertFrame = av_frame_clone(avFrame);
        break;
    }
    
    AVCodecContext *outCodecCtx = avcodec_alloc_context3(codecId); 
    outCodecCtx->width = width;
    outCodecCtx->height = height;
    outCodecCtx->pix_fmt = pixFmt;
    outCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    outCodecCtx->time_base.num = codecCtx->time_base.num;
    outCodecCtx->time_base.den = codecCtx->time_base.den;

    int re = avcodec_open2(outCodecCtx, codecId, NULL);
    if(re != 0) {
        spdlog::error("Avcodec open error：{}", re);
        return;        
    }

    AVPacket *outPacket = av_packet_alloc();
    re = avcodec_send_frame(outCodecCtx, convertFrame);
    if(re != 0) {
        spdlog::error("Send Frame error：{}", re);
        return;
    }

    FILE * outImgFile = fopen(outPath, "wb");
    while(true) {
        int ret = avcodec_receive_packet(outCodecCtx, outPacket);
        if(ret == AVERROR(EAGAIN)){
            // spdlog::error("receive packet AVERROR(EAGAIN)");
            break;
        }
        if(ret == AVERROR_EOF){
            spdlog::error("receive packet AVERROR_EOF");
            break;
        }
        if(ret == AVERROR(EINVAL)){
            spdlog::error("receive packet AVERROR(EINVAL)");
            break;
        }
        fwrite(outPacket->data, outPacket->size, 1, outImgFile);
    }
    fclose(outImgFile);
    av_frame_free(&convertFrame);
    av_packet_unref(outPacket);
    avcodec_close(outCodecCtx);
}


AVFrame* convertAvFrame(AVFrame *avFrame, AVCodecContext *codecCtx, AVPixelFormat dsFmt) {
    int width = codecCtx->width;
    int height = codecCtx->height;
    
    SwsContext *swCtx = sws_getContext(width,
                                       height,
                                       codecCtx->pix_fmt,
                                       width,
                                       height,
                                       dsFmt,
                                       SWS_BILINEAR, 0, 0, 0);
    //要转换的视频
    AVFrame *convertFrame = av_frame_alloc();
    convertFrame->format = dsFmt;
    convertFrame->width = width;
    convertFrame->height = height;

    int numBytes = av_image_get_buffer_size(dsFmt, width, height, 1);
    uint8_t *video_out_buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));
    av_image_fill_arrays(convertFrame->data, convertFrame->linesize, video_out_buffer, dsFmt,
                         width, height, 1);
    sws_scale(swCtx, (const uint8_t *const *) avFrame->data, avFrame->linesize, 0, height, convertFrame->data, convertFrame->linesize);
    return convertFrame;
}

#endif