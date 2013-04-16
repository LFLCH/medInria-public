// /////////////////////////////////////////////////////////////////
// Generated by dtkPluginGenerator
// /////////////////////////////////////////////////////////////////

#pragma once

#include <medCore/medAbstractDataSource.h>

#include "qtdcmDataSourcePluginExport.h"

class qtdcmDataSourcePrivate;

class QTDCMDATASOURCEPLUGIN_EXPORT qtdcmDataSource : public medAbstractDataSource
{
    Q_OBJECT

public:
    qtdcmDataSource();
    virtual ~qtdcmDataSource();

    virtual QString description() const;

    static bool registered();

    QWidget *mainViewWidget();
    QWidget *sourceSelectorWidget();
    QString tabName();

    QList<medToolBox*> getToolBoxes();

public slots:
    void onSaveLocalSettings();
    void onSerieMoved(QString directory);

private:
    qtdcmDataSourcePrivate *d;

    void initWidgets();
};

medAbstractDataSource *createQtdcmDataSource ( QWidget* );

