/*
 * @file Extractor.hpp
 * @author Stathi Weir (stathi.weir@gmail.com)
 * @brief
 * @version 0.1
 * @date 2024-06-09
 */

#pragma once

#include "bag2vid/Types.hpp"
#include "bag2vid/backend/H264Decoder.hpp"

#include <atomic>
#include <iostream>
#include <functional>
#include <memory>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/videoio.hpp>
#include <opencv2/imgcodecs.hpp>

#include <ros/ros.h>
#include <rosbag/bag.h>
#include <rosbag/view.h>

#include <sensor_msgs/CompressedImage.h>
#include <sensor_msgs/Image.h>

namespace bag2vid
{
  class Extractor
  {
  public:
    using ProgressCallback = std::function<void(int)>;

    inline Extractor() {}
    inline ~Extractor() { bag_.close(); }

    /**
     * @brief Loads the given rosbag file
     *
     * @param bag_file The path to the rosbag file
     * @return True if the bag was loaded successfully, false otherwise
     */
    bool loadBag(const std::string &bag_file);

    void closeBag();

    inline double getBagStartTime() { return bag_start_time_.toSec(); }

    inline double getBagEndTime() { return bag_end_time_.toSec(); }

    std::vector<std::string> getImageTopics();

    std::vector<bag2vid::MessageInstancePtr> extractMessages(const std::string &topic, const std::string &camera_name);

    /**
     * @brief Extracts all images from the given topic
     *
     * @param topic The topic to extract images from
     * @return A vector of shared pointers to the extracted images
     */
    std::vector<bag2vid::ImagePtr> extractImages(const std::string &topic, const std::string &camera_name);

    /**
     * @brief Extracts all compressed images from the given topic
     *
     * @param topic The topic to extract compressed images from
     * @return A vector of shared pointers to the extracted compressed images
     */
    std::vector<bag2vid::CompressedImagePtr> extractCompressedImages(const std::string &topic);

    /**
     * @brief Captures a screenshot of the current video frame
     *
     * @param frame_id The frame id of the screenshot
     * @param image_file The path to the image file
     * @return True if the screenshot was captured successfully, false otherwise
     */
    bool captureScreenshot(const std::string &camera_name, const int &frame_id, const std::string &image_file);

    /**
     * @brief Writes to video between the given timestamps from the given topic
     *
     * @param topic The topic to extract images from
     * @param start_time The start time of the video
     * @param end_time The end time of the video
     * @param video_file The path to the video file
     * @return True if the video was written successfully, false otherwise
     */
    bool writeVideo(const std::string &topic, const ros::Time &start_time,
                    const ros::Time &end_time, const std::string &video_file);

    void setProgressCallback(ProgressCallback callback);

    void cancelExtraction() { cancel_requested_ = true; }
    bool wasCancelled() const { return cancel_requested_; }

  private:
    std::atomic<bool> cancel_requested_{false};
    rosbag::Bag bag_;

    // Rosbag file path
    std::string bag_file_;

    // Bag start time and end time
    ros::Time bag_start_time_;
    ros::Time bag_end_time_;

    std::vector<std::string> image_topics_;

    // Dictionay of image topics
    // Maps topic names to a vector of shared pointers to the image messages
    std::map<std::string, std::vector<bag2vid::MessageInstancePtr>> image_data_;

    cv::VideoWriter video_writer_;

    ProgressCallback progress_callback_;

    /// Shared H.264 decoder for extraction operations
    std::unique_ptr<bag2vid::H264Decoder> h264_decoder_;

    /// Reusable buffer for H.264 decoded frame data
    std::vector<uint8_t> h264_decode_buffer_;

    /**
     * @brief Convert a CompressedImage to cv::Mat, handling both JPEG and H.264.
     *        For H.264, uses the internal decoder (stateful — call in order).
     * @return true on success, image written to out.
     */
    bool compressedToCvMat(const sensor_msgs::CompressedImage::ConstPtr &msg, cv::Mat &out);
  };

} // namespace bag2vid
