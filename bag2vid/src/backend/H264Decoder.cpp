#include "bag2vid/backend/H264Decoder.hpp"

#include <stdexcept>
#include <cstring>
#include <iostream>

namespace bag2vid
{

  H264Decoder::H264Decoder()
  {
    av_register_all();
    av_log_set_level(AV_LOG_FATAL);

    codec_ = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec_)
      throw std::runtime_error("H264Decoder: FFmpeg H.264 decoder not found");

    parser_ = av_parser_init(AV_CODEC_ID_H264);
    if (!parser_)
      throw std::runtime_error("H264Decoder: failed to init H.264 parser");

    ctx_ = avcodec_alloc_context3(codec_);
    if (!ctx_)
      throw std::runtime_error("H264Decoder: failed to alloc context");

    ctx_->thread_count = 1;
    ctx_->thread_type = FF_THREAD_SLICE;
    ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx_->flags2 |= AV_CODEC_FLAG2_FAST;

    if (avcodec_open2(ctx_, codec_, nullptr) < 0)
      throw std::runtime_error("H264Decoder: avcodec_open2 failed");

    frame_ = av_frame_alloc();
    frameBgr_ = av_frame_alloc();
    pkt_ = av_packet_alloc();

    if (!frame_ || !frameBgr_ || !pkt_)
      throw std::runtime_error("H264Decoder: failed to alloc frame/packet");
  }

  H264Decoder::~H264Decoder()
  {
    if (sws_)
      sws_freeContext(sws_);
    if (pkt_)
      av_packet_free(&pkt_);
    if (frameBgr_)
      av_frame_free(&frameBgr_);
    if (frame_)
      av_frame_free(&frame_);
    if (parser_)
      av_parser_close(parser_);
    if (ctx_)
      avcodec_free_context(&ctx_);
  }

  void H264Decoder::reset()
  {
    if (ctx_)
      avcodec_flush_buffers(ctx_);
  }

  void H264Decoder::initSws(int w, int h)
  {
    if (sws_)
      sws_freeContext(sws_);

    sws_ = sws_getContext(w, h, ctx_->pix_fmt,
                          w, h, AV_PIX_FMT_BGR24,
                          SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_)
      throw std::runtime_error("H264Decoder: sws_getContext failed");

    av_frame_unref(frameBgr_);
    frameBgr_->format = AV_PIX_FMT_BGR24;
    frameBgr_->width = w;
    frameBgr_->height = h;
    if (av_frame_get_buffer(frameBgr_, 32) < 0)
      throw std::runtime_error("H264Decoder: av_frame_get_buffer failed");

    swsW_ = w;
    swsH_ = h;
  }

  bool H264Decoder::decodePacket(std::vector<uint8_t> &bgr,
                                 int &width, int &height)
  {
    int ret = avcodec_send_packet(ctx_, pkt_);
    if (ret < 0)
      return false;

    bool got_frame = false;
    while (true)
    {
      ret = avcodec_receive_frame(ctx_, frame_);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        break;
      if (ret < 0)
        break;

      width = frame_->width;
      height = frame_->height;

      if (frame_->width != swsW_ || frame_->height != swsH_)
        initSws(frame_->width, frame_->height);

      sws_scale(sws_,
                frame_->data, frame_->linesize, 0, frame_->height,
                frameBgr_->data, frameBgr_->linesize);

      const int rowBytes = width * 3;
      bgr.resize(static_cast<size_t>(rowBytes) * height);
      for (int y = 0; y < height; ++y)
      {
        std::memcpy(bgr.data() + y * rowBytes,
                    frameBgr_->data[0] + y * frameBgr_->linesize[0],
                    rowBytes);
      }
      got_frame = true;
    }
    return got_frame;
  }

  bool H264Decoder::decode(const uint8_t *data, int size,
                           std::vector<uint8_t> &bgr,
                           int &width, int &height)
  {
    bool got_frame = false;
    const uint8_t *buf = data;
    int remaining = size;

    while (remaining > 0)
    {
      uint8_t *out_buf = nullptr;
      int out_size = 0;

      int consumed = av_parser_parse2(parser_, ctx_,
                                      &out_buf, &out_size,
                                      buf, remaining,
                                      AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
      if (consumed < 0)
        break;

      buf += consumed;
      remaining -= consumed;

      if (out_size > 0)
      {
        av_packet_unref(pkt_);
        pkt_->data = out_buf;
        pkt_->size = out_size;

        if (decodePacket(bgr, width, height))
          got_frame = true;
      }
    }
    return got_frame;
  }

  bool H264Decoder::isKeyframe(const uint8_t *data, int size)
  {
    // Scan for Annex-B start codes and check NAL unit type.
    // NAL type 5 = IDR slice (keyframe), type 7 = SPS (often precedes IDR).
    for (int i = 0; i < size - 4; ++i)
    {
      // 3-byte start code: 0x00 0x00 0x01
      // 4-byte start code: 0x00 0x00 0x00 0x01
      bool start3 = (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1);
      bool start4 = (i + 4 < size && data[i] == 0 && data[i + 1] == 0 &&
                     data[i + 2] == 0 && data[i + 3] == 1);
      if (start3 || start4)
      {
        int nal_offset = start4 ? i + 4 : i + 3;
        if (nal_offset < size)
        {
          uint8_t nal_type = data[nal_offset] & 0x1F;
          if (nal_type == 5)
            return true; // IDR slice
        }
      }
    }
    return false;
  }

} // namespace bag2vid
