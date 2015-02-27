// File: cpupane.h
/*
    Pep9CPU is a CPU simulator for executing microcode sequences to
    implement instructions in the instruction set of the Pep/8 computer.

    Copyright (C) 2010  J. Stanley Warford, Pepperdine University

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef CPUPANE_H
#define CPUPANE_H

#include <QWidget>
#include <QGraphicsScene>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>

#include "enu.h"

#include "cpupanebasegraphicsitems.h"

namespace Ui {
    class CpuPane;
}

class CpuPane : public QWidget {
    Q_OBJECT
public:
    CpuPane(QWidget *parent = 0);
    ~CpuPane();

    void highlightOnFocus();
    bool hasFocus();
    void giveFocus();

    virtual void initModel();

    void startDebugging();
    void stopDebugging();

    void setRegister(Enu::EMnemonic reg, int value);
    void setRegisterByte(int reg, quint8 value);
    void setStatusBit(Enu::EMnemonic bit, bool value);

    void setRegPrecondition(Enu::EMnemonic reg, int value);
    void setStatusPrecondition(Enu::EMnemonic bit, bool value);
    bool testRegPostcondition(Enu::EMnemonic reg, int value);
    bool testStatusPostcondition(Enu::EMnemonic bit, bool value);

    virtual void clearCpu();
    virtual void clearCpuControlSignals();

    // These are used by the main window in order to allow it to use the <enter> key to step.
    void singleStep();
    void clock();

protected:
    void changeEvent(QEvent *e);

    QGraphicsScene *scene;

    CpuPaneBaseGraphicsItems *cpuPaneItems;

    //simulation helper
    virtual void updateMainBusState();
    // called by the push buttons to simulate a single step; returns true if there were no issues
    virtual bool step(QString& errorString);

private:
    Ui::CpuPane *ui;

protected slots:
    void regTextEdited(QString str);
    void regTextFinishedEditing();

    void zoomFactorChanged(int factor);

    void labelClicked();

    void clockButtonPushed();
    void singleStepButtonPushed();
    void resumeButtonPushed();

    virtual void on_copyToMicrocodePushButton_clicked();

    void ALUTextEdited(QString str);

    bool getALUOut(quint8& result, quint8& a, quint8& b, int& carry, int& overflow, QString& errorString);
    bool isCorrectALUInput(int ALUFn);
    bool getCSMuxOut(bool &out, QString& errorString);
    bool getCMuxOut(quint8& out, QString& errorString);
    virtual bool getAMuxOut(quint8& out, QString& errorString);
    bool getMDRMuxOut(quint8& out, QString& errorString); // doesn't get used in the other model
    bool getABusOut(quint8& out, QString& errorString);
    bool getBBusOut(quint8& out, QString& errorString);

    // new functions:
//    bool getMARMuxOut();
//    bool getMDROMuxOut();
//    bool getMDREMuxOut();
//    bool getEOMuxOut();
public slots:
    void run();

signals:
    void updateSimulation();
    void stopSimulation();
    void simulationFinished();
    void appendMicrocodeLine(QString line);
    void readByte(int address);
    void writeByte(int address);
};

#endif // CPUPANE_H
