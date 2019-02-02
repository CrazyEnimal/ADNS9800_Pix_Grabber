#include "mouse_form.h"
#include "ui_mouse_form.h"

#include <QSerialPortInfo>
#include <QDebug>
#include <QVariant>
#include <QIntValidator>
#include <QDir>
#include <QFileDialog>

static const QString rec_path = "PIX_RECORDS";

//=======================================================================================
Mouse_Form::Mouse_Form(QString sett_name, QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Mouse_Form)
    , settings( sett_name, QSettings::IniFormat )
{
    ui->setupUi(this);
    fill_ports();

    ui->speed->setValidator( new QIntValidator(0, 1000000000) );

    parser.received.link( this, &Mouse_Form::hdlc_received );

    connect( &play_timer, &QTimer::timeout, this, &Mouse_Form::play_timeout );

    ui->rate->setValue( settings.value("framerate", 40).toInt() );
}
//=======================================================================================
Mouse_Form::~Mouse_Form()
{
    settings.setValue( "framerate", ui->rate->value() );
    delete ui;
}
//=======================================================================================
void Mouse_Form::ready_read_port()
{
    //ui->msg->setText("has data...");
    assert( sport );
    parser.append( sport->readAll().toStdString() );
}
//=======================================================================================
void Mouse_Form::hdlc_received(const std::string &str)
{
    if (str.size() != 900)
    {
        ui->msg->setText("Bad message size :(((");
        return;
    }

    if ( rec_file ) rec_file->write( str.c_str(), int(str.size()) );

    QImage img( 30, 30, QImage::Format_Grayscale8 );
    auto pixel = str.begin();
    uint lite = 0;
    uchar max_pix = 0;
    for ( auto r = 0; r < 30; ++r )
    {
        for ( auto c = 0; c < 30; ++c )
        {
            auto line = img.scanLine( c );
            uchar pix = uchar(*pixel++);
            pix = pix == 0 ? 0 : uchar(int(pix) * 2 - 1);
            line[r] = pix;
            lite += pix;
            max_pix = std::max( max_pix, pix );
        }
    }
    ui->image->set_image( img );
    update_stamps_and_avg( lite / 900.0, int(max_pix) );
}
//=======================================================================================
void Mouse_Form::update_stamps_and_avg( double avg_lite, int max_pix )
{
    stamps.enqueue( QDateTime::currentDateTime() );
    if (stamps.count() > 500) stamps.dequeue();
    //if (stamps.size() == 1) return;

    auto from = stamps.front().toMSecsSinceEpoch();
    auto to   = stamps.back().toMSecsSinceEpoch();
    double delta = to - from;
    //delta /= 1000;
    double fps = delta/ (stamps.size());
    ui->info->setText( QString("FPS: %1; AVG: %2; MAX: %3")
                       .arg(fps, 6, 'f', 3)
                       .arg(avg_lite, 6, 'f', 3)
                       .arg(max_pix)
                       );
}
//=======================================================================================
void Mouse_Form::fill_ports()
{
    ui->ports->clear();

    QSerialPortInfo def_port;
    auto ports = QSerialPortInfo::availablePorts();

    for ( auto p: ports )
    {
        qDebug() << p.portName()
                 << p.manufacturer()
                 << p.productIdentifier()
                 << p.vendorIdentifier();
        if (p.productIdentifier() == 29987 && p.vendorIdentifier() == 6790)
            def_port = p;

        ui->ports->addItem( p.portName() );
    }

    if ( !def_port.isNull() )
        ui->ports->setCurrentText( def_port.portName() );
}
//=======================================================================================
void Mouse_Form::on_open_clicked()
{
    ui->msg->setText( "About to open port..." );

    bool ok;
    auto baudrate = ui->speed->text().toInt( &ok );
    if (!ok)
    {
        ui->msg->setText( "Cannot decode speed..." );
        return;
    }

    QSerialPortInfo port;
    auto ports = QSerialPortInfo::availablePorts();
    for ( auto p: ports )
    {
        if (p.portName() != ui->ports->currentText()) continue;
        port = p;
        break;
    }
    if ( port.isNull() )
    {
        ui->msg->setText( "Port not found..." );
        fill_ports();
        return;
    }
    ui->msg->setText( "Port found..." );

    sport.reset( new QSerialPort(port) );
    sport->setBaudRate( baudrate );
    if ( !sport->open(QIODevice::ReadOnly) )
    {
        ui->msg->setText( "Cannot open port." );
        fill_ports();
        return;
    }
    ui->msg->setText( "Port opened..." );

    connect( sport.get(), &QSerialPort::readyRead,
             this, &Mouse_Form::ready_read_port );

    parser.clear();
}
//=======================================================================================
void Mouse_Form::on_record_toggled( bool checked )
{
    if ( !checked )
    {
        rec_file.reset();
        return;
    }
    QDir().mkpath(rec_path);
    auto now = QDateTime::currentDateTime();
    auto fname = rec_path + "/" + now.toString("yyyy-dd-MM_hh_mm_ss_zzz") + ".pixels";

    rec_file.reset( new QFile(fname) );
    if ( !rec_file->open(QIODevice::WriteOnly) )
    {
        ui->msg->setText( "Cannot open file '" + fname + "'" );
        rec_file.reset();
        return;
    }
    ui->msg->setText("recording in file '" + fname + "'.");
}
//=======================================================================================
//      RECORD
//=======================================================================================


//=======================================================================================
//      PLAY
//=======================================================================================
void Mouse_Form::on_fopen_clicked()
{
    QString sel_filter = "*.pixels";
    auto fname = QFileDialog::getOpenFileName( this,
                                               "Open pixel file",
                                               rec_path,
                                               "*.pixels",
                                               &sel_filter );
    if ( fname.isEmpty() ) return;

    pause();

    play_file.reset( new QFile(fname) );
    if ( !play_file->open(QIODevice::ReadOnly) )
    {
        ui->msg1->setText("Cannot open file '" + fname + "'");
        play_file.reset();
        return;
    }

    auto fsize = play_file->size();
    if (fsize % 900 != 0)
        ui->msg1->setText("Bad file size, file may be broken...");

    ui->pos->setValue(0);
    cur_frame = 0;

    auto frames = fsize / 900;
    ui->pos->setMinimum(0);
    ui->pos->setMaximum( int(frames) );
    ui->play->setChecked( true );
}
//=======================================================================================
void Mouse_Form::on_play_toggled( bool checked )
{
    if ( !checked ) pause();
    else            play();
}
//=======================================================================================
void Mouse_Form::on_left_clicked()
{
    pause();
    seek_to_frame( cur_frame - 2 );
    show_cur_frame();
}
//=======================================================================================
void Mouse_Form::on_right_clicked()
{
    pause();
    seek_to_frame( cur_frame );
    show_cur_frame();
}
//=======================================================================================
void Mouse_Form::play_timeout()
{
    show_cur_frame();
}
//=======================================================================================
void Mouse_Form::play()
{
    play_timer.start( ui->rate->value() );
}
//=======================================================================================
void Mouse_Form::pause()
{
    play_timer.stop();
    ui->play->setChecked( false );
}
//=======================================================================================
void Mouse_Form::show_cur_frame()
{
    if (!play_file )
    {
        pause();
        return;
    }

    if ( play_file->atEnd() ) { pause(); return; }

    auto frame = play_file->read( 900 );
    if (frame.size() != 900)
    {
        ui->msg1->setText( "Cannot read frame -> paused." );
        pause();
        return;
    }

    QImage img( 30, 30, QImage::Format_Grayscale8 );
    auto pixel = frame.begin();
    for ( auto r = 0; r < 30; ++r )
    {
        for ( auto c = 0; c < 30; ++c )
        {
            auto line = img.scanLine( c );
            line[r] = uchar(*pixel++);
        }
    }
    ui->play_img->set_image( img );
    ++cur_frame;
    ui->pos->setValue( cur_frame );

    ui->msg2->setText( QString("frame %1").arg(cur_frame) );
}
//=======================================================================================
void Mouse_Form::seek_to_frame( int f )
{
    if ( !play_file ) return;
    play_file->seek( f * 900 );
    cur_frame = f;
}
//=======================================================================================
void Mouse_Form::on_rate_valueChanged(int i)
{
//    bool plaing = ui->play->isChecked();
//    pause();
    ui->left->setAutoRepeatInterval( i );
    ui->right->setAutoRepeatInterval( i );
    play_timer.setInterval( i );
//    if ( play_file && plaing ) play();
}
//=======================================================================================
void Mouse_Form::on_pos_sliderMoved(int position)
{
    if ( position == cur_frame ) return;
    pause();
    seek_to_frame( position );
    show_cur_frame();
}
//=======================================================================================
