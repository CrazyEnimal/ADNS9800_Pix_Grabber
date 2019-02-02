#ifndef MOUSE_FORM_H
#define MOUSE_FORM_H

#include <QWidget>
#include <QSerialPort>
#include <memory>
#include "hdlc_parser.h"
#include <QQueue>
#include <QDateTime>
#include <QFile>
#include <QTimer>
#include <QSettings>

namespace Ui {
class Mouse_Form;
}

class Mouse_Form : public QWidget
{
    Q_OBJECT

public:
    explicit Mouse_Form( QString sett_name, QWidget *parent = nullptr);
    ~Mouse_Form();

private slots:
    void on_open_clicked();
    void ready_read_port();
    void hdlc_received( const std::string& str );

    void on_record_toggled(bool checked);
    void on_fopen_clicked();
    void on_left_clicked();
    void on_right_clicked();
    void on_play_toggled(bool checked);
    void on_rate_valueChanged(int);
    void on_pos_sliderMoved(int position);

private:
    Ui::Mouse_Form *ui;
    void fill_ports();
    std::shared_ptr<QSerialPort> sport;
    HDLC_Parser parser;

    void update_stamps_and_avg(double avg_lite, int max_pix);
    QQueue<QDateTime> stamps;

    std::shared_ptr<QFile> rec_file;

    std::shared_ptr<QFile> play_file;
    QTimer play_timer;
    int cur_frame = -1;
    void play_timeout();
    void play();
    void pause();

    void show_cur_frame();
    void seek_to_frame(int f);

    QSettings settings;
};

#endif // MOUSE_FORM_H
