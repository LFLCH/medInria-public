#ifndef MEDXMLTOLUTREADER_H
#define MEDXMLTOLUTREADER_H

#include "medClutEditor.h"
#include <qiodevice.h>
#include <qstring.h>
#include <qlist.h>


class medXMLToLUTReaderPrivate;

class medXMLToLUTReader {
public:
    medXMLToLUTReader(QList<medClutEditorTable*> * tables);
    ~medXMLToLUTReader();
    bool read(QIODevice *device);
    QString errorString() const;
private:
    medXMLToLUTReaderPrivate * d;
};


#endif // MEDXMLTOLUTREADER_H
