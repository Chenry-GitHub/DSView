/*
 * This file is part of the PulseView project.
 * DSView is based on PulseView.
 * 
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
 * Copyright (C) 2014 DreamSourceLab <support@dreamsourcelab.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <libsigrokdecode4DSL/libsigrokdecode.h>

#include "../dsvdef.h"

 
#include <boost/functional/hash.hpp>

#include <QAction> 
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QApplication>

#include "decodetrace.h"

#include "../sigsession.h"
#include "../data/decoderstack.h"
#include "../data/decode/decoder.h"
#include "../data/logic.h"
#include "../data/logicsnapshot.h"
#include "../data/decode/annotation.h"
#include "../view/logicsignal.h"
#include "../view/view.h"
#include "../widgets/decodergroupbox.h"
#include "../widgets/decodermenu.h"
#include "../device/devinst.h"
#include "../view/cursor.h"
#include "../toolbars/titlebar.h"
#include "../dsvdef.h"
#include "../ui/dscombobox.h"
#include "../ui/msgbox.h"
#include "../appcontrol.h"
#include <QDebug>

using namespace boost;
using namespace std;
 
namespace pv {
namespace view {

const QColor DecodeTrace::DecodeColours[4] = {
	QColor(0xEF, 0x29, 0x29),	// Red
	QColor(0xFC, 0xE9, 0x4F),	// Yellow
	QColor(0x8A, 0xE2, 0x34),	// Green
	QColor(0x72, 0x9F, 0xCF)	// Blue
};

const QColor DecodeTrace::ErrorBgColour = QColor(0xEF, 0x29, 0x29);
const QColor DecodeTrace::NoDecodeColour = QColor(0x88, 0x8A, 0x85);

const int DecodeTrace::ArrowSize = 4;
const double DecodeTrace::EndCapWidth = 5;
const int DecodeTrace::DrawPadding = 100;

const QColor DecodeTrace::Colours[16] = {
	QColor(0xEF, 0x29, 0x29),
	QColor(0xF6, 0x6A, 0x32),
	QColor(0xFC, 0xAE, 0x3E),
	QColor(0xFB, 0xCA, 0x47),
	QColor(0xFC, 0xE9, 0x4F),
	QColor(0xCD, 0xF0, 0x40),
	QColor(0x8A, 0xE2, 0x34),
	QColor(0x4E, 0xDC, 0x44),
	QColor(0x55, 0xD7, 0x95),
	QColor(0x64, 0xD1, 0xD2),
	QColor(0x72, 0x9F, 0xCF),
	QColor(0xD4, 0x76, 0xC4),
	QColor(0x9D, 0x79, 0xB9),
	QColor(0xAD, 0x7F, 0xA8),
	QColor(0xC2, 0x62, 0x9B),
	QColor(0xD7, 0x47, 0x6F)
};

const QColor DecodeTrace::OutlineColours[16] = {
	QColor(0x77, 0x14, 0x14),
	QColor(0x7B, 0x35, 0x19),
	QColor(0x7E, 0x57, 0x1F),
	QColor(0x7D, 0x65, 0x23),
	QColor(0x7E, 0x74, 0x27),
	QColor(0x66, 0x78, 0x20),
	QColor(0x45, 0x71, 0x1A),
	QColor(0x27, 0x6E, 0x22),
	QColor(0x2A, 0x6B, 0x4A),
	QColor(0x32, 0x68, 0x69),
	QColor(0x39, 0x4F, 0x67),
	QColor(0x6A, 0x3B, 0x62),
	QColor(0x4E, 0x3C, 0x5C),
	QColor(0x56, 0x3F, 0x54),
	QColor(0x61, 0x31, 0x4D),
	QColor(0x6B, 0x23, 0x37)
};

const QString DecodeTrace::RegionStart = QT_TR_NOOP("Start");
const QString DecodeTrace::RegionEnd = QT_TR_NOOP("End  ");

DecodeTrace::DecodeTrace(pv::SigSession *session,
	pv::data::DecoderStack *decoder_stack, int index) :
	Trace(QString::fromUtf8(decoder_stack->stack().front()->decoder()->name), index, SR_CHANNEL_DECODER)
{
    assert(decoder_stack);

    _colour = DecodeColours[index % countof(DecodeColours)];
    _start_comboBox = NULL;
    _end_comboBox = NULL;
    _pub_input_layer = NULL;
    _progress = 0;

    _decode_start = 0;
    _decode_end  = INT64_MAX; 
    _end_count = 0;
    _start_count = 0;
    _end_index = 0;
    _start_index = 0; 
    _decoder_container = NULL;
    _form_base_height = 0;

    _decoder_stack = decoder_stack;
    _session = session;
    _delete_flag = false;

    connect(_decoder_stack, SIGNAL(new_decode_data()), this, SLOT(on_new_decode_data()));

    connect(_decoder_stack, SIGNAL(decode_done()), this, SLOT(on_decode_done()));
}

DecodeTrace::~DecodeTrace()
{   _cur_row_headings.clear();
    _decoder_forms.clear();
    _probe_selectors.clear();
    _bindings.clear();

    DESTROY_OBJECT(_decoder_stack);
}

bool DecodeTrace::enabled()
{
	return true;
}

pv::data::DecoderStack* DecodeTrace::decoder()
{
	return _decoder_stack;
}

void DecodeTrace::set_view(pv::view::View *view)
{
	assert(view);
	Trace::set_view(view);
}

void DecodeTrace::paint_back(QPainter &p, int left, int right, QColor fore, QColor back)
{
    (void)back;

    QColor backFore = fore;
    backFore.setAlpha(View::BackAlpha);
    QPen pen(backFore);
    pen.setStyle(Qt::DotLine);
    p.setPen(pen);
    const double sigY = get_y() - (_totalHeight - _view->get_signalHeight())*0.5;
    p.drawLine(left, sigY, right, sigY);

    // --draw decode region control
    const double samples_per_pixel = _session->cur_snap_samplerate() * _view->scale();
    const double startX = _decode_start/samples_per_pixel - _view->offset();
    const double endX = _decode_end/samples_per_pixel - _view->offset();
    const double regionY = get_y() - _totalHeight*0.5 - ControlRectWidth;

    p.setBrush(View::Blue);
    p.drawLine(startX, regionY, startX, regionY + _totalHeight + ControlRectWidth);
    p.drawLine(endX, regionY, endX, regionY + _totalHeight + ControlRectWidth);
    const QPointF start_points[] = {
        QPointF(startX-ControlRectWidth, regionY),
        QPointF(startX+ControlRectWidth, regionY),
        QPointF(startX, regionY+ControlRectWidth)
    };
    const QPointF end_points[] = {
        QPointF(endX-ControlRectWidth, regionY),
        QPointF(endX+ControlRectWidth, regionY),
        QPointF(endX, regionY+ControlRectWidth)
    };
    p.drawPolygon(start_points, countof(start_points));
    p.drawPolygon(end_points, countof(end_points));

    // --draw headings
    const int row_height = _view->get_signalHeight();
    for (size_t i = 0; i < _cur_row_headings.size(); i++)
    {
        const int y = i * row_height + get_y() - _totalHeight * 0.5;

        p.setPen(QPen(Qt::NoPen));
        p.setBrush(QApplication::palette().brush(QPalette::WindowText));

        const QRect r(left + ArrowSize * 2, y,
            right - left, row_height / 2);
        const QString h(_cur_row_headings[i]);
        const int f = Qt::AlignLeft | Qt::AlignVCenter |
            Qt::TextDontClip;
        const QPointF points[] = {
            QPointF(left, r.center().y() - ArrowSize),
            QPointF(left + ArrowSize, r.center().y()),
            QPointF(left, r.center().y() + ArrowSize)
        };
        p.drawPolygon(points, countof(points));

        // Draw the outline
        QFont font=p.font();
        font.setPointSize(DefaultFontSize);
        p.setFont(font);
//		p.setPen(QApplication::palette().color(QPalette::Base));
//		for (int dx = -1; dx <= 1; dx++)
//			for (int dy = -1; dy <= 1; dy++)
//				if (dx != 0 && dy != 0)
//					p.drawText(r.translated(dx, dy), f, h);

        // Draw the text
        p.setPen(fore);
        p.drawText(r, f, h);
    }
}

void DecodeTrace::paint_mid(QPainter &p, int left, int right, QColor fore, QColor back)
{
    using namespace pv::data::decode;

    assert(_decoder_stack);
    const QString err = _decoder_stack->error_message();
    if (!err.isEmpty())
    {
        draw_error(p, err, left, right);
    }

    const double scale = _view->scale();
    assert(scale > 0);

    double samplerate = _decoder_stack->samplerate();

    _cur_row_headings.clear();

    // Show sample rate as 1Hz when it is unknown
    if (samplerate == 0.0)
        samplerate = 1.0;

    const int64_t pixels_offset = _view->offset();
    const double samples_per_pixel = samplerate * scale;

    uint64_t start_sample = (uint64_t)max((left + pixels_offset) *
        samples_per_pixel, 0.0);
    uint64_t end_sample = (uint64_t)max((right + pixels_offset) *
        samples_per_pixel, 0.0);

    for(auto &dec : _decoder_stack->stack()) {
        start_sample = max(dec->decode_start(), start_sample);
        end_sample = min(dec->decode_end(), end_sample);
        break;
    }

    if (end_sample < start_sample)
        return;

    const int annotation_height = _view->get_signalHeight();

    // Iterate through the rows
    assert(_view);
    int y =  get_y() - (_totalHeight - annotation_height)*0.5;

    assert(_decoder_stack);

    for(auto &dec :_decoder_stack->stack()) {
        if (dec->shown()) {
            const std::map<const pv::data::decode::Row, bool> rows = _decoder_stack->get_rows_gshow();
            for (std::map<const pv::data::decode::Row, bool>::const_iterator i = rows.begin();
                i != rows.end(); i++) {
                if ((*i).first.decoder() == dec->decoder() &&
                    _decoder_stack->has_annotations((*i).first)) {
                    if ((*i).second) {
                        const Row &row = (*i).first;

                        const uint64_t min_annotation =
                                _decoder_stack->get_min_annotation(row);
                        const double min_annWidth = min_annotation / samples_per_pixel;

                        const uint64_t max_annotation =
                                _decoder_stack->get_max_annotation(row);
                        const double max_annWidth = max_annotation / samples_per_pixel;
                        
                        if ((max_annWidth > 100) ||
                            (max_annWidth > 10 && min_annWidth > 1) ||
                            (max_annWidth == 0 && samples_per_pixel < 10)) {
                            std::vector<Annotation> annotations;
                            _decoder_stack->get_annotation_subset(annotations, row,
                                start_sample, end_sample);

                            if (!annotations.empty()) {
                                for(Annotation &a : annotations)
                                    draw_annotation(a, p, get_text_colour(),
                                        annotation_height, left, right,
                                        samples_per_pixel, pixels_offset, y,
                                        0, min_annWidth, fore, back);
                            }
                        } else {
                            draw_nodetail(p, annotation_height, left, right, y, 0, fore, back);
                        }
                        y += annotation_height;
                        _cur_row_headings.push_back(row.title());
                    }
                }
            }
        } else {
            draw_unshown_row(p, y, annotation_height, left, right, tr("Unshown"), fore, back);
            y += annotation_height;
            _cur_row_headings.push_back(dec->decoder()->name);
        }
    }
}

void DecodeTrace::paint_fore(QPainter &p, int left, int right, QColor fore, QColor back)
{
	using namespace pv::data::decode;

    (void)p;
    (void)left;
	(void)right;
    (void)fore;
    (void)back;
}

//to show decoder's property setting dialog
bool DecodeTrace::create_popup()
{ 
    int ret = false;   //setting have changed flag
    _form_base_height = 0;
    _decoder_container = NULL;
    
    QWidget *topWindow = AppControl::Instance()->GetTopWindow();
    dialogs::DSDialog dlg(topWindow);
    //dlg.setMinimumSize(500,600);
    create_popup_form(&dlg);

    if (QDialog::Accepted == dlg.exec())
    {
        for(auto &dec : _decoder_stack->stack())
        {
            if (dec->commit() || _decoder_stack->options_changed()) {
                _decoder_stack->set_options_changed(true);
                _decode_start = dec->decode_start();
                _decode_end = dec->decode_end();
                ret = true;
            }
        }
    }

    _decoder_container = NULL;
    _decoder_panels.clear();
    return ret;
}

void DecodeTrace::create_popup_form(dialogs::DSDialog *dlg)
{
    // Transfer the layout and the child widgets to a temporary widget
    // which then goes out of scope destroying the layout and all the child
    // widgets. 
    QFormLayout *form = new QFormLayout();
    form->setVerticalSpacing(5);
    form->setFormAlignment(Qt::AlignLeft);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    _decoder_container = new QWidget(dlg);
    dlg->layout()->addWidget(_decoder_container);
    
    QVBoxLayout *decoder_lay = new QVBoxLayout();
    decoder_lay->setContentsMargins(0, 0, 0, 0);
    decoder_lay->setDirection(QBoxLayout::TopToBottom);
    _decoder_container->setLayout(decoder_lay);
    
    dlg->layout()->addLayout(form);
    dlg->setTitle(tr("Decoder Options"));

    populate_popup_form(dlg, form);
}

void DecodeTrace::load_all_decoder_property(std::list<pv::data::decode::Decoder*> &ls)
{ 
    assert(_decoder_container); 
    QVBoxLayout *lay = dynamic_cast<QVBoxLayout*>(_decoder_container->layout());
    assert(lay);
 
    for(auto &dec : ls) 
    { 
        QWidget *panel = new QWidget(_decoder_container);
        QFormLayout *form = new QFormLayout();
        form->setContentsMargins(0,0,0,0);
        panel->setLayout(form);
        lay->addWidget(panel);
       
        create_decoder_form(_decoder_stack, dec, panel, form);

        //spacing
        if (ls.size() > 1){
            QWidget *spc = new QWidget();
            spc->setMinimumHeight(15);
            lay->addWidget(spc);
        }

        decoder_panel_item inf;
        inf.decoder_handle = dec; 
        inf.panel = panel;
        inf.panel_height = 0;
        _decoder_panels.push_back(inf); 
	}
}

void DecodeTrace::populate_popup_form(QWidget *parent, QFormLayout *form)
{
	using pv::data::decode::Decoder;

	assert(form);
	assert(parent);
	assert(_decoder_stack);

	// Add the decoder options
	_bindings.clear();
	_probe_selectors.clear();
	_decoder_forms.clear();

    load_all_decoder_property(_decoder_stack->stack());

    if (_decoder_stack->stack().size() > 0){
        form->addRow(new QLabel(tr("<i>* Required channels</i>"), parent));
    }

    //Add region combobox
    _start_comboBox = new DsComboBox(parent);
    _end_comboBox = new DsComboBox(parent);
    _start_comboBox->addItem(RegionStart);
    _end_comboBox->addItem(RegionEnd);

    if (_view) {
        int index = 1;
        for(std::list<Cursor*>::iterator i = _view->get_cursorList().begin();
            i != _view->get_cursorList().end(); i++) {
            QString curCursor = tr("Cursor ")+QString::number(index);
            _start_comboBox->addItem(curCursor);
            _end_comboBox->addItem(curCursor);
            index++;
        }
    }

    if (_start_count > _start_comboBox->count())
        _start_index = 0;
    if (_end_count > _end_comboBox->count())
        _end_index = 0;
    _start_count = _start_comboBox->count();
    _end_count = _end_comboBox->count();

    _start_comboBox->setCurrentIndex(_start_index);
    _end_comboBox->setCurrentIndex(_end_index);

    on_region_set(_start_index);
    form->addRow(_start_comboBox, new QLabel(
                     tr("Decode Start From")));
    form->addRow(_end_comboBox, new QLabel(
                     tr("Decode End to")));

   
    // Add ButtonBox (OK/Cancel)
    QDialogButtonBox *button_box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                Qt::Horizontal, parent);


    QHBoxLayout *confirm_button_box = new QHBoxLayout;
    confirm_button_box->addWidget(button_box, 0, Qt::AlignRight);
    form->addRow(confirm_button_box);

    connect(_start_comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(on_region_set(int)));
    connect(_end_comboBox, SIGNAL(currentIndexChanged(int)), this, SLOT(on_region_set(int))); 
    connect(button_box, SIGNAL(accepted()), parent, SLOT(accept()));
    connect(button_box, SIGNAL(rejected()), parent, SLOT(reject()));
}

void DecodeTrace::draw_annotation(const pv::data::decode::Annotation &a,
    QPainter &p, QColor text_color, int h, int left, int right,
    double samples_per_pixel, double pixels_offset, int y,
    size_t base_colour, double min_annWidth, QColor fore, QColor back)
{
    const double start = max(a.start_sample() / samples_per_pixel -
        pixels_offset, (double)left);
    const double end = min(a.end_sample() / samples_per_pixel -
        pixels_offset, (double)right);

    const size_t colour = ((base_colour + a.type()) % MaxAnnType) % countof(Colours);
	const QColor &fill = Colours[colour];
	const QColor &outline = OutlineColours[colour];

	if (start > right + DrawPadding || end < left - DrawPadding)
		return;

    if (_decoder_stack->get_mark_index() == (int64_t)(a.start_sample()+ a.end_sample())/2) {
        p.setPen(View::Blue);
        int xpos = (start+end)/2;
        int ypos = get_y()+_totalHeight*0.5 + 1;
        const QPoint triangle[] = {
            QPoint(xpos, ypos),
            QPoint(xpos-1, ypos + 1),
            QPoint(xpos, ypos + 1),
            QPoint(xpos+1, ypos + 1),
            QPoint(xpos-2, ypos + 2),
            QPoint(xpos-1, ypos + 2),
            QPoint(xpos, ypos + 2),
            QPoint(xpos+1, ypos + 2),
            QPoint(xpos+2, ypos + 2),
        };
        p.drawPoints(triangle, 9);
    }

	if (a.start_sample() == a.end_sample())
		draw_instant(a, p, fill, outline, text_color, h,
            start, y, min_annWidth);
    else {
		draw_range(a, p, fill, outline, text_color, h,
            start, end, y, fore, back);
        if ((a.type()/100 == 2) && (end - start > 20)) {
            for(auto &dec : _decoder_stack->stack()) {
                for (auto& iter: dec->channels()) {
                    int type = dec->get_channel_type(iter.first);
                    if ((type == SRD_CHANNEL_COMMON) ||
                        ((type%100 != a.type()%100) && (type%100 != 0)))
                        continue;
                   
                    LogicSignal *logic_sig = NULL;

                    for(auto &sig : _session->get_signals()) {
                        if((sig->get_index() == iter.second) &&
                           (logic_sig = dynamic_cast<view::LogicSignal*>(sig))) {
                            logic_sig->paint_mark(p, start, end, type/100);
                            break;
                        }
                    }
                }
            }
        }
    }
}

void DecodeTrace::draw_nodetail(QPainter &p,
    int h, int left, int right, int y,
    size_t base_colour, QColor fore, QColor back)
{
    (void)base_colour;
    (void)back;

    const QRectF nodetail_rect(left, y - h/2 + 0.5, right - left, h);
    QString info = tr("Zoom in for details");
    int info_left = nodetail_rect.center().x() - p.boundingRect(QRectF(), 0, info).width();
    int info_right = nodetail_rect.center().x() + p.boundingRect(QRectF(), 0, info).width();
    int height = p.boundingRect(QRectF(), 0, info).height();

    p.setPen(fore);
    p.drawLine(left, y, info_left, y);
    p.drawLine(info_right, y, right, y);
    p.drawLine(info_left, y, info_left+5, y - height/2 + 0.5);
    p.drawLine(info_left, y, info_left+5, y + height/2 + 0.5);
    p.drawLine(info_right, y, info_right-5, y - height/2 + 0.5);
    p.drawLine(info_right, y, info_right-5, y + height/2 + 0.5);

    p.setPen(fore);
    p.drawText(nodetail_rect, Qt::AlignCenter | Qt::AlignVCenter, info);
}

void DecodeTrace::draw_instant(const pv::data::decode::Annotation &a, QPainter &p,
    QColor fill, QColor outline, QColor text_color, int h, double x, int y, double min_annWidth)
{
    (void)outline;

	const QString text = a.annotations().empty() ?
		QString() : a.annotations().back();
//	const double w = min((double)p.boundingRect(QRectF(), 0, text).width(),
//		0.0) + h;
    const double w = min(min_annWidth, (double)h);
	const QRectF rect(x - w / 2, y - h / 2, w, h);

    //p.setPen(outline);
    p.setPen(QPen(Qt::NoPen));
	p.setBrush(fill);
	p.drawRoundedRect(rect, h / 2, h / 2);

	p.setPen(text_color);
    QFont font=p.font();
    font.setPointSize(DefaultFontSize);
    p.setFont(font);
	p.drawText(rect, Qt::AlignCenter | Qt::AlignVCenter, text);
}

void DecodeTrace::draw_range(const pv::data::decode::Annotation &a, QPainter &p,
	QColor fill, QColor outline, QColor text_color, int h, double start,
    double end, int y, QColor fore, QColor back)
{
    (void)fore;

	const double top = y + .5 - h / 2;
	const double bottom = y + .5 + h / 2;
	const std::vector<QString> annotations = a.annotations();

    p.setPen(outline);
    p.setBrush(fill);

    // If the two ends are within 2 pixel, draw a vertical line
    if (start + 2.0 > end)
	{
		p.drawLine(QPointF(start, top), QPointF(start, bottom));
		return;
	}

    double cap_width = min((end - start) / 4, EndCapWidth);

	QPointF pts[] = {
		QPointF(start, y + .5f),
		QPointF(start + cap_width, top),
		QPointF(end - cap_width, top),
		QPointF(end, y + .5f),
		QPointF(end - cap_width, bottom),
		QPointF(start + cap_width, bottom)
	};

    p.setPen(back);
    p.drawConvexPolygon(pts, countof(pts));

	if (annotations.empty())
		return;

	QRectF rect(start + cap_width, y - h / 2,
		end - start - cap_width * 2, h);
	if (rect.width() <= 4)
		return;

	p.setPen(text_color);

	// Try to find an annotation that will fit
	QString best_annotation;
	int best_width = 0;

	for(auto &&a : annotations) {
		const int w = p.boundingRect(QRectF(), 0, a).width();
		if (w <= rect.width() && w > best_width)
			best_annotation = a, best_width = w;
	}

	if (best_annotation.isEmpty())
		best_annotation = annotations.back();

	// If not ellide the last in the list
    QFont font=p.font();
    font.setPointSize(DefaultFontSize);
    p.setFont(font);
    p.drawText(rect, Qt::AlignCenter, p.fontMetrics().elidedText(
        best_annotation, Qt::ElideRight, rect.width()));
}

void DecodeTrace::draw_error(QPainter &p, const QString &message,
	int left, int right)
{
	const int y = get_y();
    const int h = get_totalHeight();

    const QRectF text_rect(left, y - h/2 + 0.5, right - left, h);
    const QRectF bounding_rect = p.boundingRect(text_rect,
            Qt::AlignCenter, message);
    p.setPen(Qt::red);
    QFont font=p.font();
    font.setPointSize(DefaultFontSize);
    p.setFont(font);
    if (bounding_rect.width() < text_rect.width())
        p.drawText(text_rect, Qt::AlignCenter, tr("Error: ")+message);
    else
        p.drawText(text_rect, Qt::AlignCenter, tr("Error: ..."));
}

void DecodeTrace::draw_unshown_row(QPainter &p, int y, int h, int left,
    int right, QString info, QColor fore, QColor back)
{
    (void)back;

    const QRectF unshown_rect(left, y - h/2 + 0.5, right - left, h);
    int info_left = unshown_rect.center().x() - p.boundingRect(QRectF(), 0, info).width();
    int info_right = unshown_rect.center().x() + p.boundingRect(QRectF(), 0, info).width();
    int height = p.boundingRect(QRectF(), 0, info).height();

    p.setPen(fore);
    p.drawLine(left, y, info_left, y);
    p.drawLine(info_right, y, right, y);
    p.drawLine(info_left, y, info_left+5, y - height/2 + 0.5);
    p.drawLine(info_left, y, info_left+5, y + height/2 + 0.5);
    p.drawLine(info_right, y, info_right-5, y - height/2 + 0.5);
    p.drawLine(info_right, y, info_right-5, y + height/2 + 0.5);

    p.setPen(fore);
    p.drawText(unshown_rect, Qt::AlignCenter | Qt::AlignVCenter, info);
}

void DecodeTrace::create_decoder_form(
    pv::data::DecoderStack *decoder_stack,
    data::decode::Decoder *dec, QWidget *parent,
    QFormLayout *form)
{
	const GSList *l;

    assert(dec);
    assert(decoder_stack);
    
	const srd_decoder *const decoder = dec->decoder();
	assert(decoder);

    QFormLayout *const decoder_form = new QFormLayout();
    decoder_form->setContentsMargins(0,0,0,0);
    decoder_form->setVerticalSpacing(5);
    decoder_form->setFormAlignment(Qt::AlignLeft);
    decoder_form->setLabelAlignment(Qt::AlignLeft);
    decoder_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
 
	// Add the mandatory channels
	for(l = decoder->channels; l; l = l->next) {
		const struct srd_channel *const pdch =
			(struct srd_channel *)l->data;
		DsComboBox *const combo = create_probe_selector(parent, dec, pdch);

		decoder_form->addRow(tr("<b>%1</b> (%2) *")
			.arg(QString::fromUtf8(pdch->name))
			.arg(QString::fromUtf8(pdch->desc)), combo);

		const ProbeSelector s = {combo, dec, pdch};
		_probe_selectors.push_back(s);

         connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(on_probe_selected(int)));
	} 

	// Add the optional channels
	for(l = decoder->opt_channels; l; l = l->next) {
		const struct srd_channel *const pdch =
			(struct srd_channel *)l->data;
		DsComboBox *const combo = create_probe_selector(parent, dec, pdch);
		
		decoder_form->addRow(tr("<b>%1</b> (%2)")
			.arg(QString::fromUtf8(pdch->name))
			.arg(QString::fromUtf8(pdch->desc)), combo);

		const ProbeSelector s = {combo, dec, pdch};
		_probe_selectors.push_back(s);

        connect(combo, SIGNAL(currentIndexChanged(int)), this, SLOT(on_probe_selected(int)));
	}

	// Add the options
    auto binding = new prop::binding::DecoderOptions(decoder_stack, dec);
    binding->add_properties_to_form(decoder_form, true);
	_bindings.push_back(binding);
 
    pv::widgets::DecoderGroupBox *const group =
        new pv::widgets::DecoderGroupBox(decoder_stack, dec, decoder_form, parent);
 
	form->addRow(group);
	_decoder_forms.push_back(group);
}

DsComboBox* DecodeTrace::create_probe_selector(
    QWidget *parent, const data::decode::Decoder *dec,
	const srd_channel *const pdch)
{
	assert(dec);
    const auto &sigs = _session->get_signals();
	assert(_decoder_stack);

    data::decode::Decoder *_dec = const_cast<data::decode::Decoder*>(dec);
    auto probe_iter = _dec->channels().find(pdch);
	DsComboBox *selector = new DsComboBox(parent);
    selector->addItem("-", QVariant::fromValue(-1));

	if (probe_iter == _dec->channels().end())
		selector->setCurrentIndex(0);

	for(size_t i = 0; i < sigs.size(); i++) {
        const auto s = sigs[i];
		assert(s);

		if (dynamic_cast<LogicSignal*>(s) && s->enabled())
		{
			selector->addItem(s->get_name(),
                QVariant::fromValue(s->get_index()));
            if (probe_iter != _dec->channels().end()) {
                if ((*probe_iter).second == s->get_index())
                    selector->setCurrentIndex(i + 1);
            }
		}
	}

	return selector;
}

void DecodeTrace::commit_decoder_probes(data::decode::Decoder *dec)
{
	assert(dec);

    std::map<const srd_channel*, int> probe_map;
    const auto &sigs = _session->get_signals();

    _index_list.clear();
	for(auto &s : _probe_selectors)
	{
		if(s._decoder != dec)
			break;

        const int selection = s._combo->itemData(
                s._combo->currentIndex()).value<int>();

        for(auto &sig : sigs)
            if(sig->get_index() == selection) {
                probe_map[s._pdch] = selection;
                _index_list.push_back(selection);
				break;
			}
	}

	dec->set_probes(probe_map);
}

void DecodeTrace::commit_probes()
{
	assert(_decoder_stack);
    for(auto &dec : _decoder_stack->stack())
		commit_decoder_probes(dec); 
}

void DecodeTrace::on_new_decode_data()
{
    uint64_t real_end = min(_decoder_stack->sample_count(), _decode_end+1);
    const int64_t need_sample_count = real_end - _decode_start;
    if (real_end == 0) {
        _progress = 0;
    } else if (need_sample_count <= 0) {
        _progress = 100;
    } else {
        const uint64_t samples_decoded = _decoder_stack->samples_decoded();
        _progress = floor(samples_decoded * 100.0 / need_sample_count);
    }
    decoded_progress(_progress);

    if (_view && _view->session().get_capture_state() == SigSession::Stopped)
        _view->data_updated();
    if (_totalHeight/_view->get_signalHeight() != rows_size())
        _view->signals_changed();
}

int DecodeTrace::get_progress()
{
    return _progress;
}

void DecodeTrace::on_decode_done()
{ 
    on_new_decode_data();
    _session->decode_done();
}
 

void DecodeTrace::on_probe_selected(int)
{
	commit_probes();
} 

void DecodeTrace::on_add_stack(srd_decoder *decoder)
{
	assert(decoder);
	assert(_decoder_stack);

    auto dec = new data::decode::Decoder(decoder);

    _decoder_stack->add_sub_decoder(dec);
    std::list<pv::data::decode::Decoder*> items;
    items.push_back(dec);

    if (_decoder_panels.size()){
        auto it = _decoder_panels.end();
        --it;
        if ((*it).panel_height == 0){
            (*it).panel_height = (*it).panel->geometry().height();
        }
    }
    
    if (_decoder_panels.size() == 1 && _decoder_container && _form_base_height == 0){
        QWidget *dlg = dynamic_cast<QWidget*>(_decoder_container->parent());
        assert(dlg);
        _form_base_height = dlg->geometry().height(); 
    }
 
    load_all_decoder_property(items);

    on_region_set(_start_index);
}
 
void DecodeTrace::on_del_stack(data::decode::Decoder *dec)
{
    assert(dec);
    assert(_decoder_stack);

    if (MsgBox::Confirm("Are you sure to remove the sub protocol?") == false){
        return;
    }

    _decoder_stack->remove_sub_decoder(dec);

    std::list<decoder_panel_item> dels; 
    std::list<pv::data::decode::Decoder*> adds;

    for (auto it = _decoder_panels.begin(); it != _decoder_panels.end(); it++){
        if ((*it).decoder_handle == dec){
            dels.push_back((*it));
            auto del_it = it;

            //rebuild the panel
            it++;
            while (it != _decoder_panels.end())
            { 
                 dels.push_back((*it));
                 adds.push_back((pv::data::decode::Decoder*)(*it).decoder_handle);
                 it++;
            } 
            _decoder_panels.erase(del_it); 
            break;
        }
    }   
  
    while (dels.size() > 0)
    { 
        auto it = dels.end();
        it--;
        auto inf = (*it);
        assert(inf.panel);     
 
        inf.panel->deleteLater();
        inf.panel = NULL;
        dels.erase(it);

       for (auto fd = _decoder_panels.begin(); fd != _decoder_panels.end(); ++fd){
           if ((*fd).decoder_handle == inf.decoder_handle){
               _decoder_panels.erase(fd);
               break;
           }
       }
    }

     if (adds.size() > 0){
        load_all_decoder_property(adds);
    } 

    on_region_set(_start_index);
}
 

int DecodeTrace::rows_size()
{
    using pv::data::decode::Decoder;

    int size = 0;
    for(auto &dec : _decoder_stack->stack()) {
        if (dec->shown()) {

            auto rows = _decoder_stack->get_rows_gshow();

            for (auto i = rows.begin(); i != rows.end(); i++) {
                pv::data::decode::Row _row = (*i).first;
                if (_row.decoder() == dec->decoder() &&
                    _decoder_stack->has_annotations((*i).first) &&
                    (*i).second)
                    size++;
            }
        } else {
            size++;
        }
    }
    return size == 0 ? 1 : size;
}

void DecodeTrace::paint_type_options(QPainter &p, int right, const QPoint pt, QColor fore)
{
    (void)pt;

    int y = get_y();
    const QRectF group_index_rect = get_rect(CHNLREG, y, right);
    QString index_string;
    int last_index;
    p.setPen(QPen(fore, 1, Qt::DashLine));
    p.drawLine(group_index_rect.bottomLeft(), group_index_rect.bottomRight());
    std::list<int>::iterator i = _index_list.begin();
    last_index = (*i);
    index_string = QString::number(last_index);
    while (++i != _index_list.end()) {
        if ((*i) == last_index + 1 && index_string.indexOf("-") < 3 && index_string.indexOf("-") > 0)
            index_string.replace(QString::number(last_index), QString::number((*i)));
        else if ((*i) == last_index + 1)
            index_string = QString::number((*i)) + "-" + index_string;
        else
            index_string = QString::number((*i)) + "," + index_string;
        last_index = (*i);
    }
    p.setPen(fore);
    p.drawText(group_index_rect, Qt::AlignRight | Qt::AlignVCenter, index_string);
}

QRectF DecodeTrace::get_rect(DecodeSetRegions type, int y, int right)
{
    const QSizeF name_size(right - get_leftWidth() - get_rightWidth(), SquareWidth);

    if (type == CHNLREG)
        return QRectF(
            get_leftWidth() + name_size.width() + Margin,
            y - SquareWidth / 2,
            SquareWidth * SquareNum, SquareWidth);
    else
        return QRectF(0, 0, 0, 0);
}

void DecodeTrace::on_region_set(int index)
{
    (void)index;
    const uint64_t last_samples = _session->cur_samplelimits() - 1;
    const int index1 = _start_comboBox->currentIndex();
    const int index2 = _end_comboBox->currentIndex();
    uint64_t decode_start, decode_end;

    if (index1 == 0) {
        decode_start = 0;
    } else {
        decode_start = _view->get_cursor_samples(index1-1);
    }
    if (index2 == 0) {
        decode_end = last_samples;
    } else {
        decode_end = _view->get_cursor_samples(index2-1);
    }

    if (decode_start > last_samples)
        decode_start = 0;
    if (decode_end > last_samples)
        decode_end = last_samples;

    if (decode_start > decode_end) {
        uint64_t tmp = decode_start;
        decode_start = decode_end;
        decode_end = tmp;
    }
    _start_index = index1;
    _end_index = index2;

    for(auto &dec : _decoder_stack->stack()) {
        dec->set_decode_region(decode_start, decode_end);
    }
}

void DecodeTrace::frame_ended()
{
    const uint64_t last_samples = _session->cur_samplelimits() - 1;
    if (_decode_start > last_samples) {
        _decode_start = 0;
        _start_index = 0;
    }
    if (_end_index ==0 ||
        _decode_end > last_samples) {
        _decode_end = last_samples;
        _end_index = 0;
    }
    for(auto &dec : _decoder_stack->stack()) {
        dec->set_decode_region(_decode_start, _decode_end);
        dec->commit();
    }
}

void* DecodeTrace::get_key_handel()
{
    return _decoder_stack->get_key_handel();
}


} // namespace view
} // namespace pv
