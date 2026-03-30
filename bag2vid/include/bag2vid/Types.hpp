/*
 * @file Types.hpp
 * @author Stathi Weir (stathi.weir@gmail.com)
 *
 * @brief This file contains the type definitions used in the project.
 * @date 2024-05-25
 */

#pragma once

#include <sensor_msgs/CompressedImage.h>
#include <sensor_msgs/Image.h>
#include <rosbag/message_instance.h>

namespace bag2vid
{

    /**
     * @brief Check if a CompressedImage format string indicates H.264 encoding.
     */
    inline bool isH264Format(const std::string &format)
    {
        return format.find("h264") != std::string::npos ||
               format.find("H264") != std::string::npos;
    }

    using ImagePtr = std::shared_ptr<sensor_msgs::Image>;
    using CompressedImagePtr = std::shared_ptr<sensor_msgs::CompressedImage>;
    using MessageInstancePtr = std::shared_ptr<rosbag::MessageInstance>;

} // namespace bag2vid
