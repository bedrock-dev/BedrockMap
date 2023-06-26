#include "chunkeditorwidget.h"
#include "ui_chunkeditorwidget.h"

ChunkEditorWidget::ChunkEditorWidget(QWidget *parent) : QWidget(parent), ui(new Ui::ChunkEditorWidget) {
    ui->setupUi(this);
    ui->base_info_label->setText("无数据");
}

ChunkEditorWidget::~ChunkEditorWidget() { delete ui; }

void ChunkEditorWidget::on_close_btn_clicked() { this->setVisible(false); }
