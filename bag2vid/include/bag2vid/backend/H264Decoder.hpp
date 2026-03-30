#pragma once

#include <cstdint>
#include <vector>
#include <memory>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace bag2vid
{

  /**
   * @brief RAII wrapper around an FFmpeg H.264 decoder.
   *
   * Feed it raw NAL-unit data (the `data` field of a CompressedImage whose
   * format is "h264") and it returns decoded BGR24 frames.
   */
  class H264Decoder
  {
  public:
    H264Decoder();
    ~H264Decoder();

    H264Decoder(const H264Decoder &) = delete;
    H264Decoder &operator=(const H264Decoder &) = delete;

    /**
     * @brief Decode one CompressedImage payload.
     * @return true if a complete frame was decoded.
     */
    bool decode(const uint8_t *data, int size,
                std::vector<uint8_t> &bgr,
                int &width, int &height);

    /**
     * @brief Reset the decoder state. Call when switching topics
     *        so stale reference frames from a different stream
     *        don't corrupt the output.
     */
    void reset();

    /**
     * @brief Check if a CompressedImage payload contains an IDR (keyframe).
     *        Scans for NAL unit type 5 (IDR slice) in Annex-B byte stream.
     */
    static bool isKeyframe(const uint8_t *data, int size);

  private:
    void initSws(int w, int h);
    bool decodePacket(std::vector<uint8_t> &bgr, int &width, int &height);

    const AVCodec *codec_ = nullptr;
    AVCodecContext *ctx_ = nullptr;
    AVCodecParserContext *parser_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVFrame *frameBgr_ = nullptr;
    AVPacket *pkt_ = nullptr;
    SwsContext *sws_ = nullptr;
    int swsW_ = 0;
    int swsH_ = 0;
  };

} // namespace bag2vid
