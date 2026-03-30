
#pragma once

#include <ros/ros.h>
#include <sensor_msgs/CompressedImage.h>
#include <sensor_msgs/Image.h>
#include <rosbag/bag.h>

#include "bag2vid/backend/H264Decoder.hpp"

#include <QImage>
#include <QObject>
#include <QTimer>

#include <memory>

namespace bag2vid
{

    class VideoPlayer : public QObject
    {
        Q_OBJECT

    public:
        explicit VideoPlayer(QObject *parent = nullptr);
        ~VideoPlayer();

        int getCurrentFrameId() const { return current_frame_; }

    public slots:
        void seekToTime(double time);
        void play();
        void pause();
        void loadMessages(const std::vector<std::shared_ptr<rosbag::MessageInstance>> &messages);
        void seekBackward();
        void seekForward();

    signals:
        void newFrame(const QImage &frame);
        void currentTimestamp(double time);
        void finishedPlaying();
        /// Emitted when h264 decoder is waiting for a keyframe after seek/start
        void waitingForKeyframe(bool waiting);

    private slots:
        void playback();

    private:
        bool is_playing_;
        int current_frame_;
        double start_time_;
        double end_time_;
        double fps_ = 30.0;
        QTimer playback_timer_;
        std::vector<std::shared_ptr<rosbag::MessageInstance>> messages_;

        void processImageMessage(const sensor_msgs::Image::ConstPtr &msg);
        void processCompressedImageMessage(const sensor_msgs::CompressedImage::ConstPtr &msg);

        /// H.264 decoder — created lazily on first h264 message
        std::unique_ptr<bag2vid::H264Decoder> h264_decoder_;

        /// True when the h264 decoder has been reset and hasn't yet produced a
        /// clean frame (waiting for an IDR/keyframe).
        bool h264_awaiting_keyframe_ = false;

        /// Checks if the current topic contains h264 messages
        bool isCurrentTopicH264() const;

        /// Resets the h264 decoder and enters the "waiting for keyframe" state
        void resetH264Decoder();
    };

} // namespace bag2vid
