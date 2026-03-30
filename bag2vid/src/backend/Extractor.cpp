#include "bag2vid/backend/Extractor.hpp"
#include "bag2vid/Types.hpp"

#include <opencv2/imgproc.hpp>

namespace bag2vid
{

    bool Extractor::compressedToCvMat(const sensor_msgs::CompressedImage::ConstPtr &msg, cv::Mat &out)
    {
        if (bag2vid::isH264Format(msg->format))
        {
            if (!h264_decoder_)
                h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();

            int w = 0, h = 0;
            if (!h264_decoder_->decode(msg->data.data(),
                                       static_cast<int>(msg->data.size()),
                                       h264_decode_buffer_, w, h))
                return false;

            // Wraps the persistent buffer — valid until next decode() call
            out = cv::Mat(h, w, CV_8UC3, h264_decode_buffer_.data());
            return true;
        }
        else
        {
            out = cv_bridge::toCvCopy(msg)->image;
            return true;
        }
    }

    bool Extractor::loadBag(const std::string &bag_file)
    {
        // Open the bag file
        try
        {
            bag_.open(bag_file, rosbag::bagmode::Read);
            // Find the image topics in the bag
            rosbag::View view(bag_);
            for (const rosbag::ConnectionInfo *info : view.getConnections())
            {
                if (info->datatype == "sensor_msgs/Image" || info->datatype == "sensor_msgs/CompressedImage")
                {
                    std::cout << "Found topic: " << info->topic << std::endl;
                    image_topics_.push_back(info->topic);
                }
            }
        }
        catch (rosbag::BagIOException &e)
        {
            std::cerr << "Error opening bag file: " << e.what() << std::endl;
            return false;
        }

        return true;
    }

    void Extractor::closeBag()
    {
        // Close the bag file and clear the image data
        bag_.close();
        image_data_.clear();
        image_topics_.clear();
    }

    std::vector<std::string> Extractor::getImageTopics()
    {
        return image_topics_;
    }

    std::vector<std::shared_ptr<rosbag::MessageInstance>> Extractor::extractMessages(const std::string &topic, const std::string &camera_name)
    {
        // Check if we have already extracted messages for this topic
        if (image_data_.find(camera_name) != image_data_.end())
        {
            std::cout << "Messages already extracted for topic: " << camera_name << std::endl;
            return image_data_.at(camera_name);
        }

        std::vector<std::shared_ptr<rosbag::MessageInstance>> messages;

        // Get the list of topics in the bag
        std::vector<std::string> topics;
        topics.push_back(topic);

        // Create a view for the topic
        rosbag::View view(bag_, rosbag::TopicQuery(topics));

        // Get begin and end times
        bag_start_time_ = view.getBeginTime();
        bag_end_time_ = view.getEndTime();
        std::cout << "Start time: " << bag_start_time_ << std::endl;
        std::cout << "End time: " << bag_end_time_ << std::endl;

        // Extract the messages
        for (const rosbag::MessageInstance &m : view)
        {
            messages.push_back(std::make_shared<rosbag::MessageInstance>(m));
        }

        // Add messages to the image_data_ map
        image_data_[camera_name] = messages;

        return messages;
    }

    bool Extractor::captureScreenshot(const std::string &camera_name, const int &frame_id, const std::string &image_file)
    {
        // Check we have data for the topic
        if (image_data_.find(camera_name) == image_data_.end())
        {
            std::cerr << "No data found for topic: " << camera_name << std::endl;
            return false;
        }

        std::string image_type = image_data_.at(camera_name).at(frame_id)->getDataType();
        std::cout << "Image type: " << image_type << std::endl;

        cv::Mat image;
        if (image_type == "sensor_msgs/CompressedImage")
        {
            auto msg = image_data_.at(camera_name).at(frame_id)->instantiate<sensor_msgs::CompressedImage>();
            if (bag2vid::isH264Format(msg->format))
            {
                // Find nearest keyframe at or before frame_id
                int start_from = 0;
                for (int i = frame_id; i >= 0; --i)
                {
                    auto m = image_data_.at(camera_name).at(i)->instantiate<sensor_msgs::CompressedImage>();
                    if (bag2vid::H264Decoder::isKeyframe(m->data.data(),
                                                         static_cast<int>(m->data.size())))
                    {
                        start_from = i;
                        break;
                    }
                }

                h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();
                for (int i = start_from; i <= frame_id; ++i)
                {
                    auto m = image_data_.at(camera_name).at(i)->instantiate<sensor_msgs::CompressedImage>();
                    compressedToCvMat(m, image);
                }

                // If no frame was decoded (no keyframe found before this point),
                // scan forward from frame_id to find the next decodable frame
                if (image.empty())
                {
                    for (int i = frame_id + 1;
                         i < static_cast<int>(image_data_.at(camera_name).size()); ++i)
                    {
                        auto m = image_data_.at(camera_name).at(i)->instantiate<sensor_msgs::CompressedImage>();
                        if (compressedToCvMat(m, image))
                            break;
                    }
                }

                if (image.empty())
                {
                    std::cerr << "Could not decode any h264 frame near frame "
                              << frame_id << std::endl;
                    return false;
                }

                // Clone because compressedToCvMat now returns a view into the
                // decode buffer which would be invalidated by the next decode call
                image = image.clone();
            }
            else
            {
                image = cv_bridge::toCvCopy(msg)->image;
            }
        }
        else if (image_type == "sensor_msgs/Image")
        {
            image = cv_bridge::toCvCopy(image_data_.at(camera_name).at(frame_id)->instantiate<sensor_msgs::Image>())->image;
        }
        else
        {
            std::cerr << "Unsupported image type: " << image_type << std::endl;
            return false;
        }

        try
        {
            cv::imwrite(image_file, image);
        }
        catch (cv::Exception &e)
        {
            std::cerr << "Error writing image: " << e.what() << std::endl;
            return false;
        }

        return true;
    }

    bool Extractor::writeVideo(const std::string &camera_name, const ros::Time &start_time, const ros::Time &end_time, const std::string &video_file)
    {
        cancel_requested_ = false;
        std::cout << "Writing video for topic: " << camera_name << std::endl;

        // Check we have data for the topic
        if (image_data_.find(camera_name) == image_data_.end())
        {
            std::cerr << "No data found for topic: " << camera_name << std::endl;
            return false;
        }

        // Get image type from first image
        std::string image_type = image_data_.at(camera_name).front()->getDataType();
        std::cout << "Image type: " << image_type << std::endl;

        // For h264, check the format field of the first compressed message
        bool is_h264 = false;
        if (image_type == "sensor_msgs/CompressedImage")
        {
            auto first_msg = image_data_.at(camera_name).front()->instantiate<sensor_msgs::CompressedImage>();
            is_h264 = bag2vid::isH264Format(first_msg->format);
        }

        // For h264, find the nearest keyframe at or before start_time so the
        // decoder has the reference frames it needs for clean output.
        int h264_decode_from = 0;
        if (is_h264)
        {
            const auto &msgs = image_data_.at(camera_name);
            int last_keyframe = -1;
            for (int i = 0; i < static_cast<int>(msgs.size()); ++i)
            {
                if (msgs[i]->getTime() >= start_time && start_time != end_time)
                    break;
                auto cmsg = msgs[i]->instantiate<sensor_msgs::CompressedImage>();
                if (bag2vid::H264Decoder::isKeyframe(cmsg->data.data(),
                                                     static_cast<int>(cmsg->data.size())))
                    last_keyframe = i;
            }
            // If a preceding keyframe exists, start decoding from it.
            // Otherwise start from 0 (beginning of stream) so the decoder
            // can pick up the first keyframe whenever it appears.
            h264_decode_from = (last_keyframe >= 0) ? last_keyframe : 0;
            std::cout << "H264: decoding from frame " << h264_decode_from << " (keyframe)" << std::endl;
            h264_decoder_ = std::make_unique<bag2vid::H264Decoder>();
        }

        // Estimate FPS from message timestamps
        const auto &msgs = image_data_.at(camera_name);
        double fps = 30.0;
        if (msgs.size() > 1)
        {
            double duration = msgs.back()->getTime().toSec() - msgs.front()->getTime().toSec();
            if (duration > 0)
            {
                fps = static_cast<double>(msgs.size() - 1) / duration;
                if (fps < 1.0)
                    fps = 1.0;
                if (fps > 120.0)
                    fps = 120.0;
            }
        }
        std::cout << "Estimated FPS: " << fps << std::endl;

        // Determine image size from first decodable frame
        cv::Mat first_image;
        bool got_first = false;
        {
            // Use a temporary decoder to probe the first frame size
            auto probe_decoder = std::make_unique<bag2vid::H264Decoder>();
            for (const auto &msg : msgs)
            {
                if (image_type == "sensor_msgs/CompressedImage")
                {
                    auto cmsg = msg->instantiate<sensor_msgs::CompressedImage>();
                    if (is_h264)
                    {
                        int w = 0, h = 0;
                        if (probe_decoder->decode(cmsg->data.data(),
                                                  static_cast<int>(cmsg->data.size()),
                                                  h264_decode_buffer_, w, h))
                        {
                            first_image = cv::Mat(h, w, CV_8UC3, h264_decode_buffer_.data());
                            got_first = true;
                            break;
                        }
                    }
                    else
                    {
                        first_image = cv_bridge::toCvCopy(cmsg)->image;
                        got_first = true;
                        break;
                    }
                }
                else if (image_type == "sensor_msgs/Image")
                {
                    first_image = cv_bridge::toCvCopy(msg->instantiate<sensor_msgs::Image>())->image;
                    got_first = true;
                    break;
                }
            }
        }

        if (!got_first)
        {
            std::cerr << "Could not decode any frame from topic: " << camera_name << std::endl;
            return false;
        }

        cv::Size image_size(first_image.cols, first_image.rows);
        std::cout << "Image size: " << image_size << std::endl;

        // Write to a temporary file, then rename on success.
        // This prevents a partial/corrupt file at the final path if extraction
        // is cancelled, crashes, or the app is killed.
        // Insert ".tmp" before the extension so OpenCV still sees ".mp4"
        std::string temp_file;
        auto dot = video_file.rfind('.');
        if (dot != std::string::npos)
            temp_file = video_file.substr(0, dot) + ".tmp" + video_file.substr(dot);
        else
            temp_file = video_file + ".tmp.mp4";

        video_writer_.open(temp_file, cv::VideoWriter::fourcc('a', 'v', 'c', '1'), fps, image_size);
        if (!video_writer_.isOpened())
        {
            std::cerr << "Failed to open video writer for: " << temp_file << std::endl;
            return false;
        }
        std::cout << "Video writer opened (temp: " << temp_file << ")" << std::endl;

        int count = 0;
        int total = 0;
        for (const auto &msg : msgs)
        {
            if (start_time != end_time && msg->getTime() >= end_time)
                break;
            else if (msg->getTime() >= start_time)
                total++;
        }
        std::cout << "Total frames to write: " << total << std::endl;

        int last_progress = -1;
        for (int i = (is_h264 ? h264_decode_from : 0);
             i < static_cast<int>(msgs.size()); ++i)
        {
            if (cancel_requested_)
            {
                std::cout << "Extraction cancelled by user" << std::endl;
                video_writer_.release();
                h264_decoder_.reset();
                std::remove(temp_file.c_str());
                return false;
            }

            const auto &msg = msgs[i];
            bool in_range = (msg->getTime() >= start_time && msg->getTime() < end_time) || start_time == end_time;

            // Past the requested range — stop processing
            if (start_time != end_time && msg->getTime() >= end_time)
                break;

            if (image_type == "sensor_msgs/CompressedImage")
            {
                cv::Mat image;
                auto cmsg = msg->instantiate<sensor_msgs::CompressedImage>();

                if (is_h264)
                {
                    // Decode every frame from the keyframe onwards to maintain
                    // decoder state. Only write frames that are in the requested range.
                    if (!compressedToCvMat(cmsg, image))
                        continue;
                    if (!in_range)
                        continue;
                }
                else
                {
                    if (!in_range)
                        continue;
                    image = cv_bridge::toCvCopy(cmsg)->image;
                }
                video_writer_.write(image);
            }
            else if (image_type == "sensor_msgs/Image")
            {
                if (!in_range)
                    continue;
                cv::Mat image = cv_bridge::toCvCopy(msg->instantiate<sensor_msgs::Image>())->image;
                video_writer_.write(image);
            }

            if (in_range)
            {
                count++;
                int new_progress = (total > 0) ? static_cast<int>(static_cast<double>(count) / total * 100) : 0;
                if (new_progress != last_progress)
                {
                    last_progress = new_progress;
                    std::cout << count << " / " << total << " frames written\r";
                    std::cout.flush();
                    if (progress_callback_)
                        progress_callback_(new_progress);
                }
            }
        }

        video_writer_.release();

        if (std::rename(temp_file.c_str(), video_file.c_str()) != 0)
        {
            std::cerr << "Failed to move temp file to: " << video_file << std::endl;
            std::remove(temp_file.c_str());
            return false;
        }

        std::cout << "Video written to: " << video_file << std::endl;
        return true;
    }

    void Extractor::setProgressCallback(ProgressCallback callback)
    {
        progress_callback_ = callback;
    }

} // namespace bag2vid
