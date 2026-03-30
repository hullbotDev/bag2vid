cmake_minimum_required(VERSION 3.0.2)
project(bag2vid)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-std=c++17)

## Find catkin macros and libraries
find_package(catkin REQUIRED COMPONENTS
  roscpp
  rosbag
  sensor_msgs
  std_msgs
  cv_bridge
)

find_package(OpenCV REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(Qt5PrintSupport REQUIRED)
find_package(Qt5Multimedia REQUIRED)
find_package(Qt5MultimediaWidgets REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC  REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL   REQUIRED libavutil)
pkg_check_modules(SWSCALE  REQUIRED libswscale)

###################################
## catkin specific configuration ##
###################################
catkin_package(
  CATKIN_DEPENDS roscpp sensor_msgs std_msgs rosbag
  INCLUDE_DIRS include
  LIBRARIES bag2vid
  DEPENDS Qt5Widgets Qt5PrintSupport Qt5Multimedia Qt5MultimediaWidgets
)

###########
## Build ##
###########

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${Qt5Widgets_INCLUDE_DIRS}
  ${Qt5Multimedia_INCLUDE_DIRS}
  ${Qt5MultimediaWidgets_INCLUDE_DIRS}
  ${AVCODEC_INCLUDE_DIRS}
  ${AVFORMAT_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
  ${SWSCALE_INCLUDE_DIRS}
)

## Generate MOC sources for Qt classes
qt5_wrap_cpp(MOC_SOURCES
  include/bag2vid/frontend/MainWindow.hpp
  include/bag2vid/frontend/Visualiser.hpp
  include/bag2vid/frontend/Timeline.hpp
  include/bag2vid/frontend/VideoPlayer.hpp
)

## Declare the shared library (includes MOC sources)
add_library(${PROJECT_NAME} SHARED
  src/backend/Extractor.cpp
  src/backend/H264Decoder.cpp
  src/frontend/MainWindow.cpp
  src/frontend/Visualiser.cpp
  src/frontend/Timeline.cpp
  src/frontend/VideoPlayer.cpp
  ${MOC_SOURCES}
)

target_link_libraries(${PROJECT_NAME}
  ${catkin_LIBRARIES}
  ${OpenCV_LIBS}
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::Multimedia
  Qt5::MultimediaWidgets
  ${AVCODEC_LIBRARIES}
  ${AVFORMAT_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWSCALE_LIBRARIES}
)

## Declare executables
add_executable(bag2vid_node nodes/bag2vid_node.cpp)
add_executable(bag2vid_gui nodes/bag2vid_gui.cpp)

target_link_libraries(bag2vid_node
  ${PROJECT_NAME}
  ${catkin_LIBRARIES}
  ${OpenCV_LIBS}
)

target_link_libraries(bag2vid_gui
  ${PROJECT_NAME}
  ${catkin_LIBRARIES}
  ${OpenCV_LIBS}
  Qt5::Core
  Qt5::Gui
  Qt5::Widgets
  Qt5::Multimedia
  Qt5::MultimediaWidgets
)
