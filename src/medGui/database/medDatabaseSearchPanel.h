#ifndef medDatabaseSearchPanel_h__
#define medDatabaseSearchPanel_h__

#include <medToolBox.h>
#include "medGuiExport.h"


class medDatabaseSearchPanelPrivate;

class EditCombo : public QWidget
{
    Q_OBJECT
public:
    EditCombo(QString _text, int _column);
    ~EditCombo();
signals:

    void textChanged(const QString& , int);

protected slots:
    void onTextChanged(const QString&);

private:
    QLineEdit edit;
    QLabel label;
    int column;

};

class MEDGUI_EXPORT medDatabaseSearchPanel : public medToolBox
{
    Q_OBJECT
public:
    medDatabaseSearchPanel(QWidget *parent = 0);
    ~medDatabaseSearchPanel();

    void setColumnNames(const QStringList &columns);

signals:
    void filter(const QString &text, int column);

protected slots:

    void addBox();
    void removeBox();

private:
    medDatabaseSearchPanelPrivate* d;
};

#endif // medDatabaseSearchPanel_h__
