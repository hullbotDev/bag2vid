#include "bag2vid/frontend/MainWindow.hpp"

namespace bag2vid
{

    MainWindow::MainWindow(QWidget *parent)
        : QWidget(parent)
    {
        tab_bar_ = new QTabBar(this);
        tab_bar_->setTabsClosable(true);
        tab_bar_->setExpanding(false);
        tab_bar_->setDrawBase(false);
        tab_bar_->setFixedHeight(24);
        tab_bar_->setStyleSheet(
            "QTabBar { background: transparent; }"
            "QTabBar::tab {"
            "  background: #3a3a3a; color: #ccc; border: none;"
            "  padding: 3px 12px; margin-right: 1px;"
            "  font-size: 11px;"
            "}"
            "QTabBar::tab:selected {"
            "  background: #555; color: white;"
            "}"
            "QTabBar::tab:hover {"
            "  background: #4a4a4a;"
            "}"
            "QTabBar::close-button {"
            "  border: none; subcontrol-position: right; padding: 0px 4px;"
            "  font-size: 10px;"
            "}"
            "QTabBar::close-button:hover {"
            "  background: #666;"
            "}");

        // "+" button as the last pseudo-tab
        QPushButton *add_btn = new QPushButton("+", this);
        add_btn->setFixedSize(24, 24);
        add_btn->setStyleSheet(
            "QPushButton { background: #3a3a3a; color: #ccc; border: none;"
            "  font-size: 14px; font-weight: bold; }"
            "QPushButton:hover { background: #4a4a4a; }");
        connect(add_btn, &QPushButton::clicked, this, &MainWindow::addTab);

        QHBoxLayout *tab_row = new QHBoxLayout();
        tab_row->setContentsMargins(0, 0, 0, 0);
        tab_row->setSpacing(0);
        tab_row->addWidget(tab_bar_, 1);
        tab_row->addWidget(add_btn, 0);

        stack_ = new QStackedWidget(this);

        QVBoxLayout *main_layout = new QVBoxLayout(this);
        main_layout->setContentsMargins(0, 0, 0, 0);
        main_layout->setSpacing(0);
        main_layout->addLayout(tab_row);
        main_layout->addWidget(stack_, 1);

        connect(tab_bar_, &QTabBar::tabCloseRequested, this, &MainWindow::onTabCloseRequested);
        connect(tab_bar_, &QTabBar::currentChanged, this, &MainWindow::onCurrentChanged);

        // Create the first tab
        addTab();
    }

    void MainWindow::addTab()
    {
        tab_counter_++;
        QString base_name = QString("Tab %1").arg(tab_counter_);
        auto *vis = new Visualiser(this);
        int idx = stack_->addWidget(vis);
        tab_bar_->insertTab(idx, base_name);

        // Track extraction progress to update tab text when not selected
        connect(vis, &Visualiser::extractionProgressChanged, [this, vis, base_name](int progress)
                {
        int idx = stack_->indexOf(vis);
        if (idx >= 0)
        {
            tab_bar_->setTabText(idx, base_name + " [" + QString::number(progress) + "%]");
        } });

        connect(vis, &Visualiser::extractionFinished, [this, vis, base_name]()
                {
        int idx = stack_->indexOf(vis);
        if (idx >= 0)
        {
            tab_bar_->setTabText(idx, base_name);
        } });

        tab_bar_->setCurrentIndex(idx);
        stack_->setCurrentIndex(idx);
    }

    void MainWindow::onTabCloseRequested(int index)
    {
        // Don't close the last tab
        if (tab_bar_->count() <= 1)
            return;

        auto *vis = qobject_cast<Visualiser *>(stack_->widget(index));
        if (vis && vis->isExtracting())
        {
            auto result = QMessageBox::warning(this, "Extraction in progress",
                                               "This tab is currently extracting a video.\nClose it anyway?",
                                               QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (result != QMessageBox::Yes)
                return;
        }

        tab_bar_->removeTab(index);
        QWidget *w = stack_->widget(index);
        stack_->removeWidget(w);
        w->deleteLater();
    }

    void MainWindow::onCurrentChanged(int index)
    {
        if (index >= 0 && index < stack_->count())
        {
            stack_->setCurrentIndex(index);
        }
    }

} // namespace bag2vid
