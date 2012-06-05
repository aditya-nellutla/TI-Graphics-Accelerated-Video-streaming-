/******************************************************************************
*****************************************************************************
 * widget.cpp 
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the
 *   distribution.
 *
 *   Neither the name of Texas Instruments Incorporated nor the names of
 *   its contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Contact: aditya.n@ti.com
 ****************************************************************************/

#include <stdio.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include "widget.h"
#include "ui_widget.h"

#include <QMouseEvent>

#include <stdio.h>

Widget::Widget(QWidget *parent) :
    QWidget(parent, Qt::FramelessWindowHint),
    ui(new Ui::Widget)
{
    ui->setupUi(this);
}

Widget::~Widget()
{
    delete ui;
}


void Widget::mousePressEvent(QMouseEvent *event)
{
    float nxpos, nypos;
    char CTRL_FIFO_NAME[]="gstcrtl_fifo";
    int fd, res;

    fd = open(CTRL_FIFO_NAME, O_WRONLY);

    nxpos = ((float)event->x())/width();
    nypos = ((float)event->y())/height();

    if((nxpos < 0.5) && (nypos < 0.5))
    {
        res = 0;
        write(fd, &res, sizeof(int));
        printf ("Quad-1  X Pos: %f \t Y Pos: %f\n", nxpos, nypos);
    }

    if((nxpos > 0.5) && (nypos < 0.5))
    {
        res = 1;
        printf ("Quad-2  X Pos: %f \t Y Pos: %f\n", nxpos, nypos);
        write(fd, &res, sizeof(int));
    }

    if((nxpos < 0.5) && (nypos > 0.5))
    {
        res = 2;
        printf ("Quad-3  X Pos: %f \t Y Pos: %f\n", nxpos, nypos);
        write(fd, &res, sizeof(int));
    }

    if((nxpos > 0.5) && (nypos > 0.5))
    {
        res = 3;
        printf ("Quad-4  X Pos: %f \t Y Pos: %f\n", nxpos, nypos);
        write(fd, &res, sizeof(int));
    }
}
