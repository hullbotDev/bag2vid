#include "bag2vid/frontend/VideoPlayer.hpp"
#include "bag2vid/Types.hpp"

#include <QDebug>
#include <QImage>
#include <QThread>

#include <cv_bridge/cv_bridge.h>

namespace bag2vid
{

    VideoPlayer::VideoPlayer(QObject *parent) : QObject(parent),
                                                is_playing_(false),
                                                current_frame_(0),
                                                start_time_(0.0),
                                                end_time_(1.0),
                                                h264_awaiting_keyframe_(false)
    {
        connect(&playback_timer_, &QTimer::timeout, this, &VideoPlayer::playback);
    }

    VideoPlayer::~VideoPlayer()
    {
    }

    bool VideoPlayer::isCurrentTopicH264() const
    {
        if (messages_.empty())
            return false;
        if (!messages_[0]->isType<sensor_msgs::CompressedImage>())
            return false;
        auto msg = messages_[0]->instantiate<sensor_msgs::CompressedImage>();
        return bag2vid::isH264Format(msg->format);
    }

    void VideoPlayer::resetH264Decoder()
    {
        h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();
        h264_awaiting_keyframe_ = true;
        emit waitingForKeyframe(true);
    }

    void VideoPlayer::seekToTime(double time)
    {
        for (int i = 0; i < static_cast<int>(messages_.size()); i++)
        {
            if (messages_[i]->getTime().toSec() >= time + start_time_)
            {
                current_frame_ = i;
                break;
            }
        }

        // After seeking, reset the h264 decoder so we don't show corrupt frames
        if (isCurrentTopicH264())
            resetH264Decoder();
    }

    void VideoPlayer::playback()
    {
        if (is_playing_ && current_frame_ < static_cast<int>(messages_.size()))
        {
            if (messages_[current_frame_]->isType<sensor_msgs::Image>())
            {
                processImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::Image>());
            }
            else if (messages_[current_frame_]->isType<sensor_msgs::CompressedImage>())
            {
                processCompressedImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::CompressedImage>());
            }
            double frame_timestamp = messages_[current_frame_]->getTime().toSec();
            emit currentTimestamp(frame_timestamp - start_time_);
            current_frame_++;
        }
        else if (is_playing_ && current_frame_ >= static_cast<int>(messages_.size()))
        {
            // Reached the end — stop playback
            is_playing_ = false;
            playback_timer_.stop();
            emit finishedPlaying();
        }
    }

    void VideoPlayer::play()
    {
        if (!is_playing_)
        {
            // If at the end, restart from the beginning
            if (current_frame_ >= static_cast<int>(messages_.size()) && !messages_.empty())
            {
                current_frame_ = 0;
                if (isCurrentTopicH264())
                    resetH264Decoder();
            }
            is_playing_ = true;
            playback_timer_.start(static_cast<int>(1000.0 / fps_));
        }
    }

    void VideoPlayer::pause()
    {
        is_playing_ = false;
        playback_timer_.stop();
    }

    void VideoPlayer::seekBackward()
    {
        if (current_frame_ > 0)
        {
            current_frame_--;

            if (isCurrentTopicH264())
            {
                // Find nearest keyframe at or before current_frame_
                int keyframe_idx = 0;
                for (int i = current_frame_; i >= 0; --i)
                {
                    auto cmsg = messages_[i]->instantiate<sensor_msgs::CompressedImage>();
                    if (bag2vid::H264Decoder::isKeyframe(cmsg->data.data(),
                                                         static_cast<int>(cmsg->data.size())))
                    {
                        keyframe_idx = i;
                        break;
                    }
                }

                // Reset decoder and decode from keyframe to target
                h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();
                h264_awaiting_keyframe_ = false;
                emit waitingForKeyframe(false);

                // Silently decode frames from keyframe up to (but not including) target
                for (int i = keyframe_idx; i < current_frame_; ++i)
                {
                    if (messages_[i]->isType<sensor_msgs::CompressedImage>())
                    {
                        auto cmsg = messages_[i]->instantiate<sensor_msgs::CompressedImage>();
                        std::vector<uint8_t> bgr;
                        int w = 0, h = 0;
                        h264_decoder_->decode(cmsg->data.data(),
                                              static_cast<int>(cmsg->data.size()),
                                              bgr, w, h);
                    }
                }

                // Decode and display the target frame
                if (messages_[current_frame_]->isType<sensor_msgs::CompressedImage>())
                    processCompressedImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::CompressedImage>());
            }
            else
            {
                if (messages_[current_frame_]->isType<sensor_msgs::Image>())
                    processImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::Image>());
                else if (messages_[current_frame_]->isType<sensor_msgs::CompressedImage>())
                    processCompressedImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::CompressedImage>());
            }

            double frame_timestamp = messages_[current_frame_]->getTime().toSec();
            emit currentTimestamp(frame_timestamp - start_time_);
        }
    }

    void VideoPlayer::seekForward()
    {
        if (current_frame_ < static_cast<int>(messages_.size()))
        {
            if (messages_[current_frame_]->isType<sensor_msgs::Image>())
            {
                processImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::Image>());
            }
            else if (messages_[current_frame_]->isType<sensor_msgs::CompressedImage>())
            {
                processCompressedImageMessage(messages_[current_frame_]->instantiate<sensor_msgs::CompressedImage>());
            }
            double frame_timestamp = messages_[current_frame_]->getTime().toSec();
            emit currentTimestamp(frame_timestamp - start_time_);
            current_frame_++;
        }
    }

    void VideoPlayer::loadMessages(const std::vector<std::shared_ptr<rosbag::MessageInstance>> &messages)
    {
        current_frame_ = 0;
        messages_ = messages;
        h264_awaiting_keyframe_ = false;

        if (messages_.empty())
            return;

        // Create a fresh decoder when switching topics
        if (isCurrentTopicH264())
            resetH264Decoder();
        else
            h264_decoder_.reset();

        start_time_ = messages_[0]->getTime().toSec();
        end_time_ = messages_.back()->getTime().toSec();

        // Estimate FPS from message timestamps
        if (messages_.size() > 1)
        {
            double duration = end_time_ - start_time_;
            if (duration > 0)
            {
                fps_ = static_cast<double>(messages_.size() - 1) / duration;
                if (fps_ < 1.0)
                    fps_ = 1.0;
                if (fps_ > 120.0)
                    fps_ = 120.0;
            }
        }

        // Process first frame
        if (messages_[0]->isType<sensor_msgs::Image>())
        {
            processImageMessage(messages_[0]->instantiate<sensor_msgs::Image>());
        }
        else if (messages_[0]->isType<sensor_msgs::CompressedImage>())
        {
            processCompressedImageMessage(messages_[0]->instantiate<sensor_msgs::CompressedImage>());
        }
    }

    void VideoPlayer::processImageMessage(const sensor_msgs::Image::ConstPtr &msg)
    {
        if (msg == nullptr)
            return;

        try
        {
            cv_bridge::CvImageConstPtr cv_img = cv_bridge::toCvShare(msg, "rgb8");
            const cv::Mat &image = cv_img->image;
            emit newFrame(QImage(image.data, image.cols, image.rows,
                                 static_cast<int>(image.step), QImage::Format_RGB888)
                              .copy());
        }
        catch (cv_bridge::Exception &)
        {
            // Unsupported encoding — skip frame
        }
    }

    void VideoPlayer::processCompressedImageMessage(const sensor_msgs::CompressedImage::ConstPtr &msg)
    {
        if (msg == nullptr || msg->data.empty())
            return;

        if (bag2vid::isH264Format(msg->format))
        {
            if (!h264_decoder_)
                h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();

            std::vector<uint8_t> bgr;
            int w = 0, h = 0;
            if (h264_decoder_->decode(msg->data.data(),
                                      static_cast<int>(msg->data.size()),
                                      bgr, w, h))
            {
                if (h264_awaiting_keyframe_)
                {
                    h264_awaiting_keyframe_ = false;
                    emit waitingForKeyframe(false);
                }
                QImage frame(bgr.data(), w, h, w * 3, QImage::Format_RGB888);
                emit newFrame(frame.rgbSwapped());
            }
            // If decode returns false and we're awaiting keyframe, the signal
            // was already emitted — the Visualiser keeps showing the overlay.
        }
        else
        {
            emit newFrame(QImage::fromData(msg->data.data(), static_cast<int>(msg->data.size()), "JPEG"));
        }
    }

} // namespace bag2vid
