
#include "bag2vid/frontend/Visualiser.hpp"

#include <iostream>
#include <thread>

#include <QPainter>
#include <QMouseEvent>
#include <QFileDialog>
#include <QThread>
#include <QMessageBox>
#include <QtConcurrent/QtConcurrent>

namespace bag2vid
{

    Visualiser::Visualiser(QWidget *parent) : QWidget(parent)
    {
        is_playing_ = false;
        extractor_ = std::make_unique<Extractor>();
        setupUI();

        // Connect the buttons to their slots
        connect(load_bag_button_, &QPushButton::clicked, this, &Visualiser::loadBag);
        connect(play_pause_button_, &QPushButton::clicked, this, &Visualiser::togglePlayPause);
        connect(extract_video_button_, &QPushButton::clicked, [this]()
                {
        if (is_extracting_)
            cancelExtraction();
        else
            extractVideo(); });
        connect(topic_dropdown_, SIGNAL(currentIndexChanged(int)), this, SLOT(updateTopicDropdown()));
        connect(capture_screenshot_button_, &QPushButton::clicked, this, &Visualiser::captureScreenshot);

        connect(video_player_, &VideoPlayer::newFrame, [this](const QImage &frame)
                {
        original_frame_ = frame;
        QSize label_size = image_label_->size();
        QImage resized_frame = original_frame_.scaled(label_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        last_frame_ = QPixmap::fromImage(resized_frame);
        image_label_->setPixmap(last_frame_); });

        connect(video_player_, &VideoPlayer::waitingForKeyframe, [this](bool waiting)
                {
        if (waiting)
        {
            QImage composite;
            if (!original_frame_.isNull())
            {
                composite = original_frame_.scaled(image_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                           .convertToFormat(QImage::Format_ARGB32);
            }
            else
            {
                // No frame has ever been displayed — pure grey background
                composite = QImage(image_label_->size(), QImage::Format_ARGB32);
                composite.fill(QColor(80, 80, 80));
            }

            QPainter painter(&composite);
            painter.fillRect(composite.rect(), QColor(128, 128, 128, 128));
            painter.setPen(Qt::white);
            QFont font = painter.font();
            font.setPixelSize(composite.height() / 12);
            font.setBold(true);
            painter.setFont(font);
            painter.drawText(composite.rect(), Qt::AlignCenter, "Waiting for keyframe...");
            painter.end();
            image_label_->setPixmap(QPixmap::fromImage(composite));
        } });

        connect(video_player_, &VideoPlayer::finishedPlaying, [this]()
                {
        is_playing_ = false;
        play_pause_button_->setText("Play"); });

        connect(video_player_, &VideoPlayer::currentTimestamp, [this](double time)
                { timeline_widget_->setCurrentTime(time); });

        connect(timeline_widget_, &TimelineWidget::currentTimeChanged, [this](double time)
                { video_player_->seekToTime(time); });
    }

    void Visualiser::setupUI()
    {
        // Set up the buttons
        load_bag_button_ = new QPushButton("Load Bag", this);
        topic_dropdown_ = new QComboBox(this);
        play_pause_button_ = new QPushButton("Play", this);
        extract_video_button_ = new QPushButton("Extract Video", this);
        capture_screenshot_button_ = new QPushButton("Capture Screenshot", this);

        // Set up the timeline widget
        timeline_widget_ = new TimelineWidget(this);

        // Set up the video widget
        video_player_ = new VideoPlayer(this);
        image_label_ = new QLabel(this);
        image_label_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        image_label_->setMinimumSize(320, 240);
        image_label_->setMaximumSize(1920, 1080);

        // Set up the layout
        QVBoxLayout *main_layout = new QVBoxLayout(this);

        // Header layout
        QHBoxLayout *header_layout = new QHBoxLayout();
        rosbag_filename_label_ = new QLabel("", this);
        header_layout->addWidget(rosbag_filename_label_);

        // Menu layout
        QHBoxLayout *top_layout = new QHBoxLayout();
        top_layout->addWidget(load_bag_button_);
        top_layout->addWidget(topic_dropdown_);
        top_layout->addWidget(extract_video_button_);
        top_layout->addWidget(capture_screenshot_button_);

        // Video extraction progress bar
        QHBoxLayout *progress_layout = new QHBoxLayout();
        extraction_progress_bar_ = new QProgressBar(this);
        extraction_progress_bar_->setMinimum(0);
        extraction_progress_bar_->setMaximum(100);
        extraction_progress_bar_->setValue(0);
        extraction_progress_bar_->setTextVisible(true);
        progress_layout->addWidget(extraction_progress_bar_);

        // Timeline layout
        QHBoxLayout *timeline_layout = new QHBoxLayout;
        play_pause_button_->setFixedWidth(50);
        timeline_layout->addWidget(play_pause_button_);
        timeline_layout->addWidget(timeline_widget_);
        // Don't allow the timeline to stretch vertically if the window is resized
        timeline_layout->setAlignment(Qt::AlignTop);

        // Video layout
        QVBoxLayout *video_layout = new QVBoxLayout;
        video_layout->addWidget(image_label_);
        // Allow the video to stretch to fill the available space
        video_layout->setAlignment(Qt::AlignCenter);

        main_layout->addLayout(progress_layout);
        main_layout->addLayout(header_layout);
        main_layout->addLayout(top_layout);
        main_layout->addLayout(timeline_layout);
        main_layout->addLayout(video_layout);

        setLayout(main_layout);
    }

    Visualiser::~Visualiser()
    {
        if (is_extracting_)
        {
            extractor_->cancelExtraction();
            extraction_watcher_.waitForFinished();
        }
    }

    void Visualiser::keyPressEvent(QKeyEvent *event)
    {
        // Block playback/seek shortcuts during extraction to avoid
        // concurrent rosbag reads (Bag file handle is not thread-safe).
        if (is_extracting_)
            return;

        if (event->key() == Qt::Key_Space)
        {
            togglePlayPause();
        }
        else if (event->key() == Qt::Key_Left)
        {
            video_player_->seekBackward();
        }
        else if (event->key() == Qt::Key_Right)
        {
            video_player_->seekForward();
        }
    }

    void Visualiser::resizeEvent(QResizeEvent *event)
    {
        QWidget::resizeEvent(event);

        if (!original_frame_.isNull())
        {
            QSize label_size = image_label_->size();
            QImage resized_frame = original_frame_.scaled(label_size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            last_frame_ = QPixmap::fromImage(resized_frame);
            image_label_->setPixmap(last_frame_);
        }
    }

    void Visualiser::loadBag()
    {
        std::cout << "Load Bag" << std::endl;

        // Pause video player if playing
        if (is_playing_)
        {
            togglePlayPause();
        }

        // Select rosbag file
        QString rosbag_path = QFileDialog::getOpenFileName(this, "Open rosbag", QDir::homePath(), "Ros bag files (*.bag)");

        // No file specified, cancel load
        if (rosbag_path.isEmpty())
        {
            return;
        }
        // Reset extractor
        extractor_ = std::make_unique<Extractor>();

        // Load rosbag
        if (extractor_->loadBag(rosbag_path.toStdString()))
        {
            std::cout << "Bag loaded successfully" << std::endl;
            rosbag_filename_label_->setText("<b>" + rosbag_path + "</b>");
            topic_dropdown_->clear();

            // Get topics
            std::vector<std::string> topics = extractor_->getImageTopics();
            // Sort topics
            std::sort(topics.begin(), topics.end());
            for (const auto &topic : topics)
            {
                topic_dropdown_->addItem(QString::fromStdString(topic));
            }
            // Set start and end time of timeline
            timeline_widget_->setBagStartTime(extractor_->getBagStartTime());
            timeline_widget_->setBagEndTime(extractor_->getBagEndTime());
            timeline_widget_->setStartTime(0.0);
            timeline_widget_->setEndTime(extractor_->getBagEndTime() - extractor_->getBagStartTime());
        }
        else
        {
            QMessageBox::warning(this, "Load Failed", "Failed to open the rosbag file.");
        }
    }

    void Visualiser::updateTopicDropdown()
    {
        std::cout << "Update Topic Dropdown" << std::endl;
        std::string current_topic = topic_dropdown_->currentText().toStdString();
        // Check dropdown is not empty
        if (current_topic.empty())
        {
            return;
        }
        std::cout << "selected topic: " << current_topic << std::endl;

        // Use full topic as cache key so different topics from the same camera
        // (e.g. /camera_2/image_raw_relay/compressed vs /camera_2/h264_relay/compressed)
        // don't collide in the message cache.
        std::cout << "Extracting messages for topic: " << current_topic << std::endl;
        std::vector<std::shared_ptr<rosbag::MessageInstance>> messages =
            extractor_->extractMessages(current_topic, current_topic);
        // Load messages into video player
        video_player_->loadMessages(messages);
        std::cout << "Messages extracted" << std::endl;
    }

    void Visualiser::togglePlayPause()
    {
        if (is_playing_)
        {
            std::cout << "Pause" << std::endl;
            video_player_->pause();
            play_pause_button_->setText("Play");
        }
        else
        {
            std::cout << "Play" << std::endl;
            video_player_->play();
            play_pause_button_->setText("Pause");
        }
        is_playing_ = !is_playing_;
    }

    void Visualiser::extractVideo()
    {
        std::cout << "Extract Video" << std::endl;

        // Get marker positions
        double start_time = timeline_widget_->getStartTime() + extractor_->getBagStartTime();
        double end_time = timeline_widget_->getEndTime() + extractor_->getBagStartTime();

        std::cout << "Start time: " << start_time << std::endl;
        std::cout << "End time: " << end_time << std::endl;

        // Check start time is before end time
        if (start_time >= end_time)
        {
            std::cout << "Invalid start and end times" << std::endl;
            return;
        }

        // Select output file
        QString video_path = QFileDialog::getSaveFileName(this, "Save video", QDir::homePath(), "Video files (*.mp4)");
        // No file specified, cancel extraction
        if (video_path.isEmpty())
        {
            return;
        }
        if (!video_path.endsWith(".mp4"))
        {
            video_path += ".mp4";
        }
        std::cout << "Video path: " << video_path.toStdString() << std::endl;

        // Use full topic name as cache key (matches updateTopicDropdown)
        std::string camera_name;
        if (!topic_dropdown_->currentText().isEmpty())
        {
            camera_name = topic_dropdown_->currentText().toStdString();
        }
        else
        {
            std::cout << "No topic selected" << std::endl;
            return;
        }

        if (is_extracting_)
        {
            std::cout << "Extraction already in progress" << std::endl;
            return;
        }

        // Pause playback — the background thread and the main thread both call
        // instantiate<>() on the same MessageInstance objects, which is not
        // thread-safe.  Disable all playback/scrub controls during extraction.
        if (is_playing_)
            togglePlayPause();
        play_pause_button_->setEnabled(false);
        timeline_widget_->setEnabled(false);
        topic_dropdown_->setEnabled(false);
        load_bag_button_->setEnabled(false);
        capture_screenshot_button_->setEnabled(false);

        extraction_progress_bar_->setValue(0);
        is_extracting_ = true;
        extraction_progress_ = 0;
        extract_video_button_->setText("Cancel");

        extractor_->setProgressCallback([this](int progress)
                                        {
        // Thread-safe: queue UI updates onto the main thread (Qt 5.9 compatible)
        QTimer::singleShot(0, this, [this, progress]()
        {
            extraction_progress_ = progress;
            updateProgressBar(progress);
            emit extractionProgressChanged(progress);
        }); });

        // Capture values for the background thread
        std::string video_path_str = video_path.toStdString();
        ros::Time ros_start(start_time);
        ros::Time ros_end(end_time);

        QFuture<void> future = QtConcurrent::run([this, camera_name, ros_start, ros_end, video_path_str]()
                                                 {
        bool ok = extractor_->writeVideo(camera_name, ros_start, ros_end, video_path_str);

        QTimer::singleShot(0, this, [this, ok]()
        {
            if (ok)
            {
                std::cout << "Video extracted successfully" << std::endl;
                extraction_progress_bar_->setValue(100);
            }
            else if (!extractor_->wasCancelled())
            {
                QMessageBox::warning(this, "Extraction Failed", "Failed to extract video.");
            }
            is_extracting_ = false;
            extraction_progress_ = 0;
            extract_video_button_->setText("Extract Video");
            extract_video_button_->setEnabled(true);
            play_pause_button_->setEnabled(true);
            timeline_widget_->setEnabled(true);
            topic_dropdown_->setEnabled(true);
            load_bag_button_->setEnabled(true);
            capture_screenshot_button_->setEnabled(true);
            if (extractor_->wasCancelled())
                extraction_progress_bar_->setValue(0);
            emit extractionFinished();
        }); });
        extraction_watcher_.setFuture(future);
    }

    void Visualiser::cancelExtraction()
    {
        if (is_extracting_)
        {
            extractor_->cancelExtraction();
            extract_video_button_->setEnabled(false); // grey out until thread finishes
        }
    }

    void Visualiser::captureScreenshot()
    {
        std::cout << "Capture Screenshot" << std::endl;

        // Select output file
        QString screenshot_path = QFileDialog::getSaveFileName(this, "Save screenshot", QDir::homePath(), "Image files (*.png)");
        // No file specified, cancel extraction
        if (screenshot_path.isEmpty())
        {
            return;
        }
        // If no .png extension, add it
        if (!screenshot_path.endsWith(".png"))
        {
            screenshot_path += ".png";
        }

        std::cout << "Screenshot path: " << screenshot_path.toStdString() << std::endl;

        // Get id of current frame
        int frame_id = video_player_->getCurrentFrameId();

        // Use full topic name as cache key (matches updateTopicDropdown)
        std::string camera_name;
        if (!topic_dropdown_->currentText().isEmpty())
        {
            camera_name = topic_dropdown_->currentText().toStdString();
        }
        else
        {
            std::cout << "No topic selected" << std::endl;
            return;
        }
        std::cout << "Topic: " << camera_name << std::endl;

        // Capture screenshot
        if (extractor_->captureScreenshot(camera_name, frame_id, screenshot_path.toStdString()))
        {
            std::cout << "Screenshot captured successfully" << std::endl;
        }
        else
        {
            QMessageBox::warning(this, "Screenshot Failed", "Failed to capture screenshot.");
        }
    }

    void Visualiser::updateProgressBar(int progress)
    {
        extraction_progress_bar_->setValue(progress);
    }

} // namespace bag2vid
