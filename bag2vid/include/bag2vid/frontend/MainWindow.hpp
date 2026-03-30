#pragma once

#include <QWidget>
#include <QTabBar>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>

#include "bag2vid/frontend/Visualiser.hpp"

namespace bag2vid
{

    class MainWindow : public QWidget
    {
        Q_OBJECT

    public:
        explicit MainWindow(QWidget *parent = nullptr);
        ~MainWindow() = default;

    private slots:
        void addTab();
        void onTabCloseRequested(int index);
        void onCurrentChanged(int index);

    private:
        QTabBar *tab_bar_;
        QStackedWidget *stack_;
        int tab_counter_ = 0;

        void updateTabText(int index);
    };

} // namespace bag2vid
