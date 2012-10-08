/* 
 * The Biomechanical ToolKit
 * Copyright (c) 2009-2012, Arnaud Barr�
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *     * Redistributions of source code must retain the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name(s) of the copyright holders nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "RemoveAnalogOffset.h"
#include "RemoveAnalogOffsetDialog.h"
#include "UndoCommands.h"

#include <btkAnalogOffsetRemover.h>
#include <btkSubAcquisitionFilter.h>
#include <btkAcquisitionFileReader.h>

#include <QFileDialog>

void RemoveAnalogOffset::RegisterTool(ToolsManager* manager)
{
  QMenu* menuAnalogRemoveOffset = new QMenu("Remove Offset", manager->parentWidget());
  manager->menuAnalog()->addMenu(menuAnalogRemoveOffset);
  manager->addTool(menuAnalogRemoveOffset, tr("From Reference File"), ToolFactory<RemoveAnalogOffsetFromReferenceFile>);
  manager->addTool(menuAnalogRemoveOffset, tr("From Selected Frames"), ToolFactory<RemoveAnalogOffsetFromSelectedFrames>);
};

RemoveAnalogOffset::RemoveAnalogOffset(Method m, QWidget* parent)
: AbstractTool("Remove Analog Offset", parent)
{
  this->m_Method = m;
};
  
bool RemoveAnalogOffset::run(ToolCommands* cmds, ToolsData* const data)
{
  bool res = false;
  QVector<int> ids;
  btk::AnalogCollection::Pointer analogs;
  
  if (this->m_Method == FromReferenceFile)
  {
    QStringList formats;
    data->acquisition()->supportedReadFileFormats(formats);
    QStringList allFormats;
    QString byFormat;
    foreach(const QString& str, formats)
    {
      QString ext = "*." + str.split(" ")[0].toLower();
      if (allFormats.indexOf(ext) == -1)
        allFormats.append(ext);
      byFormat += (!byFormat.isEmpty() ? ";;" : "") + str + " (" + ext + ")";
    }
    QString allFormat = "Acquisition Files (" + allFormats.join(" ") + ");;";
    QString filename = QFileDialog::getOpenFileName(this->parentWidget(), "", QFileInfo(data->acquisition()->fileName()).absolutePath(), allFormat + byFormat);
    if (!filename.isEmpty())
    {
      QMessageBox error(QMessageBox::Warning, "File error", "Error occurred during the reading of the reference file.", QMessageBox::Ok, this->parentWidget());
      error.setInformativeText("<nobr>Check the logger for more informations.</nobr>");
      
      QList<int> channelsIds;
      btk::AcquisitionFileReader::Pointer reader = btk::AcquisitionFileReader::New();
      reader->SetFilename(filename.toStdString());
      try
      {
        reader->Update();
        analogs = btk::AnalogCollection::New();
        int id = 0;
        for (btk::AnalogCollection::ConstIterator it = data->acquisition()->btkAcquisition()->BeginAnalog() ; it != data->acquisition()->btkAcquisition()->EndAnalog() ; ++it)
        {
          if (reader->GetOutput()->FindAnalog((*it)->GetLabel()) != reader->GetOutput()->EndAnalog())
          {
            analogs->InsertItem(*it);
            channelsIds.push_back(id);
          }
          ++id;
        }
      }
      catch (btk::Exception& e)
      {
        LOG_ERROR(e.what());
        error.exec();
        return false;
      }
      catch (std::exception& e)
      {
        LOG_ERROR("Unexpected error: " + QString(e.what()));
        error.exec();
        return false;
      }
      catch (...)
      {
        LOG_ERROR("Unknown error.");
        error.exec();
        return false;
      }
      ids = channelsIds.toVector();
    }
  }
  else if (this->m_Method == FromSelectedFrames)
  {
    RemoveAnalogOffsetDialog dialog(this->parentWidget());
    dialog.initialize(data->explorerSelectedItems(AnalogType), data->acquisition());
    if (dialog.exec() == QDialog::Accepted)
    {
      QList<int> channelsIds;
      QTreeWidgetItem* analogsRoot = dialog.treeWidget->topLevelItem(0);
      for (int i = 0 ; i < analogsRoot->childCount() ; ++i)
      {
        QTreeWidgetItem* analogItem = analogsRoot->child(i);
        if (analogItem->checkState(0) == Qt::Checked)
          channelsIds.push_back(analogItem->data(0, Qt::UserRole).toInt());
      }
      
      int framesIndex[2] = {-1,-1};
      if (dialog.firstFramesButton->isChecked())
      {
        int numberOfFrames = dialog.firstFramesSpinBox->value();
        framesIndex[0] = 0;
        framesIndex[1] = numberOfFrames - 1;
      }
      else if (dialog.lastFramesButton->isChecked())
      {
        int numberOfFrames = dialog.lastFramesSpinBox->value();
        framesIndex[0] = data->acquisition()->lastFrame() - numberOfFrames + 1 - data->acquisition()->firstFrame();
        framesIndex[1] = data->acquisition()->lastFrame() - data->acquisition()->firstFrame();
      }
      
      btk::SubAcquisitionFilter::Pointer subAnalogs = btk::SubAcquisitionFilter::New();
      subAnalogs->SetInput(data->acquisition()->btkAcquisition());
      subAnalogs->SetFramesIndex(framesIndex[0], framesIndex[1]);
      subAnalogs->SetExtractionOption(btk::SubAcquisitionFilter::AnalogsOnly, channelsIds.toStdList());
      subAnalogs->Update();
      analogs = subAnalogs->GetOutput()->GetAnalogs();
      ids = channelsIds.toVector();
    }
    else
      return false;
  }
  else
  {
    TOOL_LOG_ERROR("Unknown method.");
    return false;
  }

  if (ids.count() == analogs->GetItemNumber())
  {
    QString log;
    if (ids.count() == 1)
      log = "A total of " + QString::number(ids.count()) + " offset was removed for the following channel:";
    else
      log = "A total of " + QString::number(ids.count()) + " offsets were removed for the following channels:";
    QVector<double> offsets(ids.count());
    int inc = 0;
    for (btk::AnalogCollection::ConstIterator it = analogs->Begin() ; it != analogs->End() ; ++it)
    {
      double dc = (*it)->GetValues().sum() / (*it)->GetValues().rows();
      offsets[inc] = -1.0 * dc; // Because the undo command make only a positive shift
      log += "\n\t- " + QString::fromStdString((*it)->GetLabel()) + ": " + QString::number(dc) + " " + QString::fromStdString((*it)->GetUnit());
      ++inc;
    }
    new ShiftAnalogsValues(data->acquisition(), ids, offsets, cmds->acquisitionCommand());
    res = true;
    TOOL_LOG_INFO(log);
  }
  else
    TOOL_LOG_ERROR("Unexpected error: The number of IDs is not equal to the number of offsets. Please contact the developers.");

  return res;
};

// ------------------------------------------------------------------------- //

RemoveAnalogOffsetDialog::RemoveAnalogOffsetDialog(QWidget* parent)
: AnalogToolOptionDialog("Remove Analog Offset", parent)
{
  QWidget* referenceFrames = new QWidget(this);
  this->firstFramesButton = new QRadioButton(this);
  this->firstFramesButton->setChecked(true);
  this->firstFramesSpinBox = new QSpinBox(this);
  this->firstFramesSpinBox->setMinimum(1);
  this->firstFramesSpinBox->setMaximum(9999);
  this->firstFramesSpinBox->setValue(10);
  this->lastFramesButton = new QRadioButton(this);
  this->lastFramesSpinBox = new QSpinBox(this);
  this->lastFramesSpinBox->setMinimum(1);
  this->lastFramesSpinBox->setMaximum(9999);
  this->lastFramesSpinBox->setValue(10);
  this->allFramesButton = new QRadioButton(this);
  this->firstFramesButton->setText(tr("First frames:"));
  this->lastFramesButton->setText(tr("Last frames:"));
  this->allFramesButton->setText(tr("All frames"));
  
  QGridLayout* gridLayout = new QGridLayout(referenceFrames);
  gridLayout->addWidget(this->firstFramesButton, 0, 0, 1, 1);
  gridLayout->addWidget(this->firstFramesSpinBox, 0, 1, 1, 1);
  gridLayout->addWidget(this->lastFramesButton, 1, 0, 1, 1);
  gridLayout->addWidget(this->lastFramesSpinBox, 1, 1, 1, 1);
  gridLayout->addWidget(this->allFramesButton, 2, 0, 1, 1);
  QSpacerItem* horizontalSpacer = new QSpacerItem(40, 10, QSizePolicy::Expanding, QSizePolicy::Minimum);
  gridLayout->addItem(horizontalSpacer, 0, 2, 1, 1);
  
#ifndef Q_OS_WIN
  QFont f = this->font();
  f.setPointSize(11);
  this->firstFramesButton->setFont(f);
  this->lastFramesButton->setFont(f);
  this->allFramesButton->setFont(f);
  this->firstFramesSpinBox->setStyleSheet("QSpinBox {font-size: 12px;};");
  this->lastFramesSpinBox->setStyleSheet("QSpinBox {font-size: 12px;};");
  this->allFramesButton->setMinimumHeight(30);
  referenceFrames->layout()->setSpacing(0);
#endif

  this->setDataProcessingVisible(false);
  this->addOption("Reference Frames", referenceFrames);
};

void RemoveAnalogOffsetDialog::initializeOptions(const Acquisition* const acq)
{
  QSettings settings;
  this->firstFramesSpinBox->setMaximum(acq->pointFrameNumber());
  this->lastFramesSpinBox->setMaximum(acq->pointFrameNumber());
  this->firstFramesSpinBox->setValue(settings.value(this->toolSettingsPath() + "firstFramesNumber", 10).toInt());
  this->lastFramesSpinBox->setValue(settings.value(this->toolSettingsPath() + "lastFramesNumber", 10).toInt());
  int lastReferenceFrames =  settings.value(this->toolSettingsPath() + "lastReferenceFrames", 0).toInt();
  if (lastReferenceFrames == 1)
    this->lastFramesButton->setChecked(true);
  else if (lastReferenceFrames == 2)
    this->allFramesButton->setChecked(true);
  else
    this->firstFramesButton->setChecked(true);
};

void RemoveAnalogOffsetDialog::saveOptionsSettings()
{
  QSettings settings;
  int lastReferenceFrames = 2;
  if (this->firstFramesButton->isChecked())
  {
    lastReferenceFrames = 0;
    int numberOfFrames = this->firstFramesSpinBox->value();
    settings.setValue(this->toolSettingsPath() + "firstFramesNumber", numberOfFrames);
  }
  else if (this->lastFramesButton->isChecked())
  {
    lastReferenceFrames = 1;
    int numberOfFrames = this->lastFramesSpinBox->value();
    settings.setValue(this->toolSettingsPath() + "lastFramesNumber", numberOfFrames);
  }
  settings.setValue(this->toolSettingsPath() + "lastReferenceFrames", lastReferenceFrames);
};