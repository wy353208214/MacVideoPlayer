#ifndef FLV_PARASE_H
#define FLV_PARASE_H

#include <iostream>
#include <string>
#include <stdlib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <cmath>

using namespace std;

#define TAG_TYPE_AUDIO 8
#define TAG_TYPE_VIDEO 9
#define TAG_TYPE_SCRIPT 18

typedef unsigned char byte;

struct FlvHeader
{
    uint8_t signarture[3];
    uint8_t version;
    uint8_t flags;
    uint32_t headerSize;
};

struct TagHeader
{
    uint8_t tagType;
    uint8_t tagDataSize[3];
    uint8_t tagTimeStamp[3];
    uint8_t tagTimeStampEx;
    uint8_t tagStreamId[3];
    // uint8_t s1;
    // uint8_t s2;
    // uint8_t s3;
};

// 针对小大小端进行转换
uint32_t reverserBytes(char *p, char c){
	int r = 0;
	int i;
	for (i = 0; i < c; i++)
		r |= (*(p + i) << (((c - 1) * 8) - 8 * i));
	return r;
}


// FLV解析工具
void paraseFlv(char *url) {
    auto Logger = spdlog::rotating_logger_st("Flv Parase", "../log/parase.txt", 1024 * 1024 * 5, 10);
    Logger->set_level(spdlog::level::info);

    FILE *pFile = fopen(url, "rb+");
    FlvHeader flvHeader;
    fread(&flvHeader, 1, sizeof(FlvHeader), pFile);
    bitset<8> temp(flvHeader.flags);

    Logger->info("\nF: {}, L: {}, V: {} \nVersion: {} \nFlags: {}, 视频: {}, 音频: {} \nHeadSize: {}", 
                    flvHeader.signarture[0], flvHeader.signarture[1], flvHeader.signarture[2], 
                    flvHeader.version, flvHeader.flags, 
                    temp.test(2), temp.test(0), 
                    flvHeader.headerSize);
    fseek(pFile, flvHeader.headerSize, SEEK_SET);

    uint32_t preTagSize = 0;
    TagHeader tag;
    int audioVideoIndex = 0;
    do {
        // 1. 读取previous tag 4个字节
        preTagSize = getw(pFile);
        uint32_t preSize0 = (preTagSize&0xFF000000)>>24;
        uint32_t preSize1 = (preTagSize&0x00FF0000)>>16;
        uint32_t tempPreSize = preSize0 + preSize1 * pow(2, 8);

        // 2. 读取Tag Header 11个字节
        fread(&tag, 1, sizeof(TagHeader), pFile);
        int dataSize = tag.tagDataSize[2] + tag.tagDataSize[1] * pow(2, 8) + tag.tagDataSize[0] * pow(2, 16);
        int timeStamp = tag.tagTimeStamp[2] + tag.tagTimeStamp[1] * pow(2, 8) + tag.tagTimeStamp[0] * pow(2, 16);
        int streamId = tag.tagStreamId[2] + tag.tagStreamId[1] * pow(2, 8) + tag.tagStreamId[0] * pow(2, 16);

        if(feof(pFile)){
            cout<<"<<< Parase end >>>"<<endl;
            break;
        }

        // 获取tag type 18\9\8
        string tagTypeStr;
        switch (tag.tagType)
        {
            case TAG_TYPE_AUDIO:
            {
                tagTypeStr = "Audio";
                audioVideoIndex++;
                int tagDataFirstByte = getc(pFile);
                int audioFmt = (tagDataFirstByte&0xF0)>>4;
                cout<<"audioFmt: "<<audioFmt<<endl;
                fseek(pFile, -1, SEEK_CUR);
                break;
            }
            case TAG_TYPE_VIDEO:
            {
                tagTypeStr = "Video";
                audioVideoIndex++;
                break;
            }
            case TAG_TYPE_SCRIPT:
            {
                tagTypeStr = "Script";
                break;
            } 
            default:
            {
                tagTypeStr = "Default";
                break;
            }
        }

        Logger->info("\nPreTagSize: {}\nTagType: {} - {} \nDatasize: {} \nTimeStamp: {} \nTimeStampEx: {} \nStreamID: {}", 
        tempPreSize, tagTypeStr, audioVideoIndex, dataSize, timeStamp, tag.tagTimeStampEx, streamId);
        
        // 3. 读取tagData，tagheader中的datasize个字节大小
        fseek(pFile, dataSize, SEEK_CUR);
    } while(!feof(pFile));
}

#endif //