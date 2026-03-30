
#include <QApplication>

#include <ros/ros.h>

#include "bag2vid/frontend/MainWindow.hpp"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    bag2vid::MainWindow window;
    window.setMinimumSize(640, 480);
    window.show();

    return app.exec();
}
