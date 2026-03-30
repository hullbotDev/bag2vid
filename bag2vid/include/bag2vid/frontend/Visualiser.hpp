#pragma once

#include <iostream>

#include <QMainWindow>
#include <QVideoWidget>
#include <QFileDialog>
#include <QLabel>
#include <QComboBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMediaPlayer>
#include <QSlider>
#include <QPushButton>
#include <QProgressBar>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>

#include <ros/ros.h>

#include <bag2vid/backend/Extractor.hpp>
#include <bag2vid/frontend/Timeline.hpp>
#include <bag2vid/frontend/VideoPlayer.hpp>

namespace bag2vid
{

    class Visualiser : public QWidget
    {
        Q_OBJECT

    public:
        Visualiser(QWidget *parent = nullptr);
        ~Visualiser();

        bool isExtracting() const { return is_extracting_; }
        int extractionProgress() const { return extraction_progress_; }

    signals:
        void extractionProgressChanged(int progress);
        void extractionFinished();

    private slots:
        void loadBag();
        void togglePlayPause();
        void extractVideo();
        void cancelExtraction();
        void updateTopicDropdown();
        void captureScreenshot();

    protected:
        void resizeEvent(QResizeEvent *event) override;
        void keyPressEvent(QKeyEvent *event) override;

    private:
        std::unique_ptr<Extractor> extractor_;

        QLabel *rosbag_filename_label_;
        QPushButton *load_bag_button_;
        QComboBox *topic_dropdown_;
        QPushButton *play_pause_button_;
        QPushButton *extract_video_button_;
        QPushButton *capture_screenshot_button_;
        QProgressBar *extraction_progress_bar_;
        TimelineWidget *timeline_widget_;
        VideoPlayer *video_player_;
        QLabel *image_label_;

        bool is_playing_;
        bool is_extracting_ = false;
        QFutureWatcher<void> extraction_watcher_;
        int extraction_progress_ = 0;

        QPixmap last_frame_;
        QImage original_frame_;

        void setupUI();
        void updateProgressBar(int progress);
    };
} // namespace bag2vid
