#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <alsa/asoundlib.h>

int main()
{
    int server_fd;
    struct sockaddr_in server_addr;

    server_fd = socket(AF_INET, SOCK_DGRAM, 0);

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(8080);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char trigger_msg[10];

    recvfrom(server_fd, trigger_msg, sizeof(trigger_msg), 0,
             (struct sockaddr *)&client_addr, &client_len);

    if (fork() == 0)
    {
        snd_pcm_t *mic;
        snd_pcm_open(&mic, "default", SND_PCM_STREAM_CAPTURE, 0);

        snd_pcm_set_params(mic,
                           SND_PCM_FORMAT_S16_LE,         // 16-bit audio
                           SND_PCM_ACCESS_RW_INTERLEAVED, // Standard data layout
                           1,                             // 1 Channel (Mono)
                           44100,                         // 44,100 Hz Sample Rate
                           1, 50000);                     // Latency settings

        int audio_sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in audio_client = client_addr; // Inherit client IP
        audio_client.sin_port = htons(1235);           // Force audio to Port 1235

        short audio_buf[2048]; // Bucket to hold 2048 audio frames (4096 bytes)
        while (1)
        {
            int frames = snd_pcm_readi(mic, audio_buf, 2048);

            if (frames > 0)
            {
                sendto(audio_sock, audio_buf, frames * 2, 0,
                       (struct sockaddr *)&audio_client, sizeof(audio_client));
            }
        }
    }
    else
    {
        int cam_fd = open("/dev/video0", O_RDWR);
        if (cam_fd < 0)
        {
            perror("Failed to open camera");
            return 1;
        }

        struct v4l2_format fmt;
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = 640;
        fmt.fmt.pix.height = 480;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
        ioctl(cam_fd, VIDIOC_S_FMT, &fmt);

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof(req));
        req.count = 1;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        ioctl(cam_fd, VIDIOC_REQBUFS, &req);

        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = 0;
        ioctl(cam_fd, VIDIOC_QUERYBUF, &buf);

        void *frame_buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, cam_fd, buf.m.offset);

        ioctl(cam_fd, VIDIOC_QBUF, &buf);

        int type = buf.type;

        ioctl(cam_fd, VIDIOC_STREAMON, &type);

        AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        AVCodecContext *c_ctx = avcodec_alloc_context3(codec);
        av_opt_set(c_ctx->priv_data, "preset", "ultrafast", 0);
        av_opt_set(c_ctx->priv_data, "tune", "zerolatency", 0);
        c_ctx->width = 640;
        c_ctx->height = 480;
        c_ctx->time_base = (AVRational){1, 30};
        c_ctx->framerate = (AVRational){30, 1};
        c_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        avcodec_open2(c_ctx, codec, NULL);

        AVFrame *frame = av_frame_alloc();
        frame->format = c_ctx->pix_fmt;
        frame->width = c_ctx->width;
        frame->height = c_ctx->height;
        av_frame_get_buffer(frame, 0);

        AVPacket *pkt = av_packet_alloc();
        int frame_count = 0;

        struct SwsContext *sws_ctx = sws_getContext(
            640, 480, AV_PIX_FMT_YUYV422, // Input: Camera format
            640, 480, AV_PIX_FMT_YUV420P, // Output: H.264 format
            SWS_FAST_BILINEAR, NULL, NULL, NULL);

        while (1)
        {
            ioctl(cam_fd, VIDIOC_DQBUF, &buf);

            uint8_t *in_data[1] = {(uint8_t *)frame_buffer};
            int in_linesize[1] = {640 * 2}; // YUYV is 2 bytes per pixel
            sws_scale(sws_ctx, in_data, in_linesize, 0, 480, frame->data, frame->linesize);

            frame->pts = frame_count++;

            avcodec_send_frame(c_ctx, frame);
            while (avcodec_receive_packet(c_ctx, pkt) == 0)
            {

                int bytes_sent = 0;
                while (bytes_sent < pkt->size)
                {
                    int send_size = pkt->size - bytes_sent;
                    if (send_size > 60000)
                        send_size = 60000;

                    sendto(server_fd, pkt->data + bytes_sent, send_size, 0,
                           (struct sockaddr *)&client_addr, client_len);
                    bytes_sent += send_size;
                }
                av_packet_unref(pkt);
            }

            ioctl(cam_fd, VIDIOC_QBUF, &buf);
        }

        ioctl(cam_fd, VIDIOC_STREAMOFF, &type);
        munmap(frame_buffer, buf.length);
        close(cam_fd);
    }
}