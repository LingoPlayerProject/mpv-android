#include <jni.h>
#include <stdlib.h>

#include "jni_utils.h"
#include "utils/cJSON.h"
#include "log.h"
#include "globals.h"

// 新增错误码定义
#define ERR_FIND_DECODER   -110
#define ERR_ALLOC_DEC_CTX  -111
#define ERR_COPY_DEC_PARAM -112
#define ERR_OPEN_DECODER   -113
#define ERR_FIND_ENCODER   -120
#define ERR_ALLOC_ENC_CTX  -121
#define ERR_OPEN_ENCODER   -122

// 当 C++ 代码需要调用 C 语言编写的库（如 FFmpeg、OpenSSL、SQLite 等）时，
// 必须通过 extern "C" 声明来禁用 C++ 的名称修饰（Name Mangling），否则会导致链接器找不到符号
extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

extern "C" {
    jni_func(jint, convertToSrt, jstring fromFilePath, jstring toFilePath, jint trackId);
    jni_func(jstring, parseTracks, jstring filePath);
}

// 辅助函数，处理单个字幕包
static int process_subtitle_packet(AVCodecContext *dec_ctx, AVCodecContext *enc_ctx, 
                                  AVFormatContext *out_ctx, AVStream *out_stream,
                                  AVStream *in_stream, AVPacket *pkt) {
    int ret = 0;
    AVSubtitle subtitle = {0};
    AVPacket *srt_pkt = NULL;
    int got_subtitle = 0;

    // 解码字幕包
    ret = avcodec_decode_subtitle2(dec_ctx, &subtitle, &got_subtitle, pkt);
    if (ret < 0 || !got_subtitle) {
        ret = (ret < 0) ? ret : AVERROR_INVALIDDATA;
        ALOGE("Decode subtitle failed: ret=%d got_sub=%d", ret, got_subtitle);
        goto cleanup;
    }

    // 分配SRT输出包
    if (!(srt_pkt = av_packet_alloc())) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }

    // 预分配缓冲区（4KB）
    srt_pkt->data = (uint8_t*)av_malloc(4096);
    if (!srt_pkt->data) {
        ret = AVERROR(ENOMEM);
        goto cleanup;
    }
    srt_pkt->size = 4096;

    // 转换时间基到输出流的时间基 (1/1000)
    srt_pkt->pts = av_rescale_q(pkt->pts, in_stream->time_base, out_stream->time_base);
    srt_pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
    srt_pkt->stream_index = 0;

    // 编码字幕到SRT格式
    if ((ret = avcodec_encode_subtitle(enc_ctx, srt_pkt->data, srt_pkt->size, &subtitle)) < 0) {
        ALOGE("Encode subtitle failed: ret=%d", ret);
        goto cleanup;
    }
    srt_pkt->size = ret; // 更新实际数据大小

    // 写入处理后的数据包
    if ((ret = av_interleaved_write_frame(out_ctx, srt_pkt)) < 0) {
        ALOGE("Write frame failed: ret=%d", ret);
        goto cleanup;
    }

cleanup:
    // 确保释放资源
    if (srt_pkt) {
        av_freep(&srt_pkt->data); // 显式释放数据缓冲区
        av_packet_free(&srt_pkt);
    }
    avsubtitle_free(&subtitle);
    return ret;
}

jni_func(jint, convertToSrt, jstring fromFilePath, jstring toFilePath, jint trackId) {
    const char *input_path = env->GetStringUTFChars(fromFilePath, NULL);
    const char *output_path = env->GetStringUTFChars(toFilePath, NULL);

    ALOGV("ffmpeg_utils convertToSrt (%s) -> %s", input_path, output_path);
    AVFormatContext *in_ctx = NULL;
    AVFormatContext *out_ctx = NULL;
    AVStream *in_stream = NULL;
    AVStream *out_stream = NULL;
    int ret = 0;
    AVPacket* pkt = NULL;
    int frame_count = 0;
    AVCodecContext *dec_ctx = NULL, *enc_ctx = NULL;
    const AVCodec *dec_codec = NULL, *enc_codec = NULL;
    
    // 打开输入文件
    ret = avformat_open_input(&in_ctx, input_path, NULL, NULL);
    if (ret < 0) {
        ALOGE("ffmpeg_utils convertToSrt open_input failed (%d)", ret);
        goto end;
    }

    // 查找流信息
    ret = avformat_find_stream_info(in_ctx, NULL);
    if (ret < 0) {
        ALOGE("ffmpeg_utils convertToSrt find_stream_info failed (%d)", ret);
        goto end;
    }

    // 验证轨道ID有效性
    if (trackId < 0 || trackId >= in_ctx->nb_streams) {
        ret = AVERROR(EINVAL);
        ALOGE("ffmpeg_utils convertToSrt trackId (%d) invalid ", trackId);
        goto end;
    }
    in_stream = in_ctx->streams[trackId];
    if (in_stream->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
        ret = AVERROR(ENOSYS);
        ALOGE("ffmpeg_utils convertToSrt trackId (%d) is not subtitle ", trackId);
        goto end;
    }

    // 创建输出上下文
    if ((ret = avformat_alloc_output_context2(&out_ctx, NULL, "srt", output_path)) < 0) {
        ALOGE("ffmpeg_utils convertToSrt alloc_output_context2 failed %d ", ret);
        goto end;
    }

    // 创建输出流
    out_stream = avformat_new_stream(out_ctx, NULL);
    if (!out_stream) {
        ret = AVERROR(ENOMEM);
        ALOGE("ffmpeg_utils convertToSrt create out_stream failed %d ", ret);
        goto end;
    }

    // 配置输出流参数
    out_stream->time_base = (AVRational){1, 1000};  // SRT 使用毫秒时间基
    out_stream->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    out_stream->codecpar->codec_id = AV_CODEC_ID_SUBRIP;

    // 打开输出文件
    if ((ret = avio_open(&out_ctx->pb, output_path, AVIO_FLAG_WRITE)) < 0) {
        ALOGE("ffmpeg_utils convertToSrt avio_open failed %d ", ret);
        goto end;
    }

    // 解码器
    dec_codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
    if (!dec_codec) {
        ALOGE("Failed to find decoder for codec %d", in_stream->codecpar->codec_id);
        ret = ERR_FIND_DECODER;
        goto end;
    }
    dec_ctx = avcodec_alloc_context3(dec_codec);
    if (!dec_ctx) {
        ALOGE("Failed to allocate decoder context");
        ret = ERR_ALLOC_DEC_CTX;
        goto end;
    }
    if ((ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar)) < 0) {
        char error_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, error_buf, sizeof(error_buf));
        ALOGE("Failed to copy decoder parameters: %s", error_buf);
        ret = ERR_COPY_DEC_PARAM;
        goto end;
    }
    if ((ret = avcodec_open2(dec_ctx, dec_codec, NULL)) < 0) {
        char error_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, error_buf, sizeof(error_buf));
        ALOGE("Failed to open decoder: %s", error_buf);
        ret = ERR_OPEN_DECODER;
        goto end;
    }

    // 编码器。srt每一句字幕的头格式是在muxer里面，encoder只管内容，所以用text而不是subrip，可以避免出现奇怪的html标签
    enc_codec = avcodec_find_encoder(AV_CODEC_ID_TEXT);
    if (!enc_codec) {
        ALOGE("Failed to find SUBRIP encoder");
        ret = AVERROR_ENCODER_NOT_FOUND;
        goto end;
    }

    enc_ctx = avcodec_alloc_context3(enc_codec);
    if (!enc_ctx) {
        ALOGE("Failed to allocate encoder context");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    if (dec_ctx->subtitle_header) {
        // 移除 const 限定符进行复制
        enc_ctx->subtitle_header =  (uint8_t*) av_memdup((void*)dec_ctx->subtitle_header, dec_ctx->subtitle_header_size);
        if (!enc_ctx->subtitle_header) {
            ALOGE("Failed to copy subtitle header");
            ret = AVERROR(ENOMEM);
            goto end;
        }
        enc_ctx->subtitle_header_size = dec_ctx->subtitle_header_size;
        // ALOGV("ffmpeg_utils convertToSrt copy dec_ctx->subtitle_header to enc_ctx (size=%d): %.*s", dec_ctx->subtitle_header_size,  dec_ctx->subtitle_header_size,  (const char*)dec_ctx->subtitle_header);
    } else {
        // 若解码器无头，显式设置空头
        enc_ctx->subtitle_header = (uint8_t*)av_strdup("");
        enc_ctx->subtitle_header_size = 0;
    }

    // 配置编码器参数
    enc_ctx->time_base = out_stream->time_base; // 使用输出流的时间基
    enc_ctx->codec_type = AVMEDIA_TYPE_SUBTITLE;

    if ((ret = avcodec_open2(enc_ctx, enc_codec, NULL)) < 0) {
        char error_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, error_buf, sizeof(error_buf));
        ALOGE("Failed to open encoder: %s", error_buf);
        goto end;
    }
    // 将编码器参数复制到输出流
    avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);

    // 关联到输出流
    ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
    if (ret < 0) {
        ALOGE("Failed to copy encoder parameters");
        goto end;
    }

    // 写入文件头
    if ((ret = avformat_write_header(out_ctx, NULL)) < 0) {
        ALOGE("ffmpeg_utils convertToSrt write_header failed %d ", ret);
        goto end;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        ret = AVERROR(ENOMEM);
        ALOGE("ffmpeg_utils convertToSrt failed to allocate packet");
        goto end;
    }

    for (int i = 0; i < in_ctx->nb_streams; i++) {
        in_ctx->streams[i]->discard = (i == trackId) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    }

    while (av_read_frame(in_ctx, pkt) >= 0) {
       if (pkt->stream_index == trackId) {
            int process_ret = process_subtitle_packet(dec_ctx, enc_ctx, out_ctx, 
                                                     out_stream, in_stream, pkt);
            if (process_ret < 0) {
                ALOGE("Process subtitle packet failed: %d", process_ret);
                ret = process_ret;
                av_packet_unref(pkt);
                goto end; // 发生错误时跳转到整体清理
            }
            frame_count++;
        }
        av_packet_unref(pkt);
    }

    av_write_trailer(out_ctx);
    ALOGV("ffmpeg_utils convertToSrt success (%s) -> %s, frame count %d ", input_path, output_path, frame_count);

end:
    // 逆序释放资源
    if (pkt) {
        av_packet_free(&pkt);
    }
    if (enc_ctx) {
        av_freep(&enc_ctx->subtitle_header); // 显式释放
        avcodec_free_context(&enc_ctx); // 内部会调用avcodec_close()
    }
    if (dec_ctx) {
        avcodec_free_context(&dec_ctx);
    }
    if (in_ctx) {
        avformat_close_input(&in_ctx);
    }
    if (out_ctx) {
        if (out_ctx->pb) avio_closep(&out_ctx->pb);
        avformat_free_context(out_ctx);
    }
    env->ReleaseStringUTFChars(fromFilePath, input_path);
    env->ReleaseStringUTFChars(toFilePath, output_path);
    return ret;
}

jni_func(jstring, parseTracks, jstring filePath) {
    const char *input_path = env->GetStringUTFChars(filePath, NULL);
    ALOGV("ffmpeg_utils parseTracks (%s)", input_path);

    AVFormatContext *in_ctx = NULL;
    cJSON *root = cJSON_CreateObject();
    int ret = 0;
    const char *json_str = NULL;
    jstring result = NULL;
    cJSON *tracks = NULL;

    // 打开输入文件
    if ((ret = avformat_open_input(&in_ctx, input_path, NULL, NULL)) < 0) {
        ALOGE("parseTracks avformat_open_input failed: %d", ret);
        cJSON_AddNumberToObject(root, "code", ret);
        goto final;
    }

    // 获取流信息
    if ((ret = avformat_find_stream_info(in_ctx, NULL)) < 0) {
        ALOGE("parseTracks avformat_find_stream_info failed: %d", ret);
        cJSON_AddNumberToObject(root, "code", ret);
        goto final;
    }

    // 构建轨道信息数组
    tracks = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "tracks", tracks);

    for (int i = 0; i < in_ctx->nb_streams; i++) {
        AVStream *stream = in_ctx->streams[i];
        AVCodecParameters *codecpar = stream->codecpar;

        cJSON *track = cJSON_CreateObject();
        cJSON_AddItemToArray(tracks, track);

        // 基础信息
        cJSON_AddNumberToObject(track, "index", i);
        cJSON_AddStringToObject(track, "type", 
            av_get_media_type_string(codecpar->codec_type));
        cJSON_AddStringToObject(track, "codec", 
            avcodec_get_name(codecpar->codec_id));

        // 元数据（语言/标题等）
        AVDictionaryEntry *tag = NULL;
        if ((tag = av_dict_get(stream->metadata, "language", NULL, 0))) {
            cJSON_AddStringToObject(track, "language", tag->value);
        }
        if ((tag = av_dict_get(stream->metadata, "title", NULL, 0))) {
            cJSON_AddStringToObject(track, "title", tag->value);
        }
    }

    cJSON_AddNumberToObject(root, "count", in_ctx->nb_streams);
    cJSON_AddNumberToObject(root, "code", ret);
final:
    // 生成JSON字符串
    json_str = cJSON_PrintUnformatted(root);
    result = env->NewStringUTF(json_str ? json_str : "{}");

    // 释放资源
    if (in_ctx) avformat_close_input(&in_ctx);
    if (root) cJSON_Delete(root);
    if (json_str) free((void*)json_str);
    env->ReleaseStringUTFChars(filePath, input_path);

    return result;
}