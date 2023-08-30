#include "AsyncNetwork.h"

#include <QDir>
#include <QHttpPart>

#include <medShanoir.h>
#include <LocalInfo.h>
#include <Authenticator.h>
#include <RequestManager.h>
#include <SyncNetwork.h>
#include <ShanoirRequestPreparation.h>

#include <FileHelper.h>
#include <JsonHelper.h>


AsyncNetwork::AsyncNetwork(medShanoir * parent, LocalInfo *info, Authenticator * authent, SyncNetwork *syncNet, RequestManager * requester): 
	QObject(parent), m_parent(parent), m_info(info), m_authent(authent), m_syncNet(syncNet), m_medReqId(0)
{
	QObject::connect(this, &AsyncNetwork::asyncGet, requester, &RequestManager::httpGet, Qt::ConnectionType::QueuedConnection);
	QObject::connect(this, &AsyncNetwork::asyncPost, requester, &RequestManager::httpPost, Qt::ConnectionType::QueuedConnection);
	QObject::connect(this, &AsyncNetwork::asyncPostMulti, requester, &RequestManager::httpPostMulti, Qt::ConnectionType::QueuedConnection);
	QObject::connect(this, &AsyncNetwork::asyncPut, requester, &RequestManager::httpPut, Qt::ConnectionType::QueuedConnection);
}

AsyncNetwork::~AsyncNetwork()
{

}


/**
 * 
 * GET DATA methods
 * 
*/

int AsyncNetwork::getAssyncData(unsigned int pi_uiLevel, QString key)
{
 	int medId = -1;

	// checking that the level is appropriate for a processed dataset download
 	QStringList parts = key.split('.');
 	bool dataset_level = pi_uiLevel == 4 && parts.size() == 5;
 	bool ps_dataset_level = pi_uiLevel == 6 && parts.size() == 7;
 	if (dataset_level || ps_dataset_level) // dataset level or processed dataset
 	{
 		int id_ds = parts[4].toInt();
 		if (ps_dataset_level)
 		{
 			id_ds = parts[6].toInt();
 		}
		int medIdRequest = ++m_medReqId;
		QUuid netReqId = getDataset(medIdRequest, id_ds, false);
		medId = m_requestIdMap[netReqId];
 	}

 	return medId;
}


QUuid AsyncNetwork::getDataset(int medId, int id_ds, bool conversion)
{
	// generation of the requests id
	QUuid netReqId = QUuid::createUuid();

	m_requestIdMap[netReqId] = medId;
	// saving temporarily the informations about the request in the result map 
	m_idResultMap[medId] = QString::number(id_ds)+"."+ QString::number(conversion);

	// sending the request
	QNetworkRequest req;
	writeNiftiDatasetRetrievingRequest(req, m_info->getBaseURL(), m_authent->getCurrentAccessToken(), id_ds, conversion);

	emit asyncGet(netReqId, req);

	return netReqId;
}

void AsyncNetwork::getAsyncDataInterpretation(QUuid netReqId, RequestResponse res)
{
	int medId = m_requestIdMap[netReqId];
	if (res.code==0) // an error occured before sending the request
	{
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::faild);
		m_requestIdMap.remove(netReqId);
	}
	else if (res.code == 1 || res.code == 2) // in progress
	{
		int bytesSent = res.headers["bytesSent"].toInt();
		int bytesTotal = res.headers["bytesTotal"].toInt();
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::pending);
	}
	else if (res.code == 200) // finished with success
	{
		// successCode is about success of the conversion of the retrieved data into a file
		int successCode = dataToFile(netReqId, res);
		QString tmpResult = m_idResultMap[medId].toString();
		if(successCode == 1 && tmpResult.size()>0)
		{
			QStringList parts = tmpResult.split('.');
			if(parts.size()==2 && parts[1].toInt() == 0)
			{
				getDataset(medId,parts[0].toInt(), true);
			}
			else
			{
				successCode = 0;
			} 
		}
		else if(successCode == -1) 
		{
			emit m_parent->progress(medId, eRequestStatus::faild);
		}
		if(successCode == 0)
		{
			emit m_parent->progress(medId, eRequestStatus::finish);
		}
		m_requestIdMap.remove(netReqId);
	}
	else // an error occured during the request sending (http error)
	{
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::faild);
		m_requestIdMap.remove(netReqId);
	}
}

void AsyncNetwork::asyncGetSlot(QUuid netReqId, QByteArray payload, QJsonObject headers, int statusOrHttpCode)
{
	if (m_requestIdMap.contains(netReqId))
	{
		getAsyncDataInterpretation(netReqId , { statusOrHttpCode, headers, payload });
	}
}

QVariant AsyncNetwork::getAsyncResults(int pi_iRequest)
{
	QVariant qRes;
	if (m_idResultMap.contains(pi_iRequest)) qRes = m_idResultMap[pi_iRequest];
	return qRes;
}



int AsyncNetwork::dataToFile(QUuid netReqId, RequestResponse res)
{
	int successCode = (res.payload.size() < 100)? 1 :-1;
	int medId = m_requestIdMap[netReqId];

	QString fileName;

	if (successCode!=1 && verifyJsonKeys(res.headers, { "Content-Disposition" }))
	{
		fileName = res.headers.value("Content-Disposition").toString().split("filename=")[1].split(";").first();
	}

	if (successCode!=1 && !fileName.isEmpty())
	{
		QString filePath = m_info->getStoragePath() + QString::number(medId) + "/" + fileName;
		QString zipPath = saveFileData(res.payload, filePath);
		QString extractionPath = extractZipFile(zipPath);
		QDir folder(extractionPath);

		// Find the nifti file in the folder
		QStringList filters;
		filters << "*.nii" << "*.nii.gz";
		QStringList files = folder.entryList(filters, QDir::Files | QDir::NoDotAndDotDot);
		if (files.size() > 0)
		{
			QString dataPath = folder.absoluteFilePath(files[0]);

			m_idResultMap[medId] = dataPath;
		
			m_requestIdMap.remove(netReqId);

			successCode = 0; 

			m_filesToRemove.push_back(QPair<qint64, QString>({ QDateTime::currentSecsSinceEpoch(), extractionPath }));
			//TODO: delete the file from the filesystem in a reasonnable time and DON'T FORGET clean m_idResultMap (call cleaner)
		}
	}
	return successCode;
}


/**
 * 
 * ADD DATA methods
 * 
*/

int AsyncNetwork::addAssyncData(QVariant data, levelMinimalEntries & pio_minimalEntries, unsigned int pi_uiLevel, QString parentKey)
{
	int medId = -1;
	QString path = data.toString(); 

	// checking that the level is appropriate for a processed dataset creation
	int parent_level = pi_uiLevel - 1; // working with parent of the data to create (easier for understanding)
	QStringList parts = parentKey.split('.');
	bool psing_dataset_level = parent_level == 5 && parts.size() == 6;
	if(psing_dataset_level && path.endsWith(".nii.gz"))
	{
		QVariant sending = sendProcessedDataset(path, pio_minimalEntries.name, parts[4].toInt(), parts[5].toInt());
		if(sending.isValid() && sending.canConvert<QUuid>())
		{
			QUuid netReqId = sending.value<QUuid>();
			medId =  m_requestIdMap[netReqId];
		}
	}
	return medId;
}

QVariant AsyncNetwork::sendProcessedDataset(QString &filepath, QString name, int idDataset, int idProcessing)
{
	QString distant_path;

	// construction of the processed dataset to upload
	QFile *file = new QFile(filepath);
	file->open(QIODevice::ReadOnly);
	QFileInfo fileInfo(filepath);

	if (fileInfo.exists())
	{
		// generation of the requests ids 
		int medId = ++m_medReqId;
		QUuid netReqId = QUuid::createUuid();

		m_requestIdMap[netReqId] = medId;
		// saving temporarily the informations about the request in the result map 
		m_idResultMap[medId] =  QString::number(idDataset) +"."+ QString::number(idProcessing)+"."+ name;

		// sending the request
		QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
		QNetworkRequest req;
		writeProcessedDatasetUploadRequest(req, multiPart, m_info->getBaseURL(), m_authent->getCurrentAccessToken(), file, fileInfo);

		emit asyncPostMulti(netReqId, req, multiPart);

		return netReqId;
	}
	else 
	{
		return false;
	}
}

QVariant AsyncNetwork::sentDatasetFileInterpretation(QUuid netReqIdFile, RequestResponse res)
{
	int medId = m_requestIdMap[netReqIdFile];

	QString distant_path;
	if (res.code == 200 && !res.payload.isNull())
	{
		distant_path = QString::fromUtf8(res.payload);
	}

	QStringList parts = m_idResultMap[medId].toString().split('.');  // format idDs.idPsingDs.psDsname

	if(distant_path.length()>0 && parts.size()==3)
	{
		// generation of the request id
		QUuid netReqIdContext = QUuid::createUuid();

		m_requestIdMap[netReqIdContext] = medId;

		// preparation of the request
		int dsId = parts[0].toInt();
		int psingId = parts[1].toInt();
		QString name = parts[2];

		// building the context of the processed dataset
		DatasetDetails ds_details = m_syncNet->getDatasetDetails(dsId);
		Study s = m_syncNet->getStudyDetails(ds_details.study_id);
		StudyOverview study = {s.id, s.name};
		QString processedDatasetType = "RECONSTRUCTEDDATASET";
		QString processedDatasetName = name;
		QJsonObject parentDatasetProcessing = m_syncNet->getDatasetProcessing(psingId);

		// sending the request
		QNetworkRequest req;
		QByteArray postData;
		writeProcessedDatasetUploadContextRequest(req, postData, m_info->getBaseURL(), m_authent->getCurrentAccessToken(), ds_details, study, distant_path, processedDatasetType, processedDatasetName, parentDatasetProcessing);

		emit asyncPost(netReqIdContext, req, postData);

		return netReqIdContext;
	}
	return false;
}

void AsyncNetwork::sentDatasetContextInterpretation(QUuid netReqId, RequestResponse res)
{
	int medId = m_requestIdMap[netReqId];
	m_idResultMap[medId] = true;
	emit m_parent->progress(medId, eRequestStatus::finish);
}


void AsyncNetwork::addAsyncDataInterpretation(QUuid netReqId, RequestResponse res)
{
	int medId = m_requestIdMap[netReqId];
	if (res.code==0) // an error occured before sending the request
	{
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::faild);
		m_requestIdMap.remove(netReqId);
	}
	else if (res.code == 1 || res.code == 2) // in progress
	{
		int bytesSent = res.headers["bytesSent"].toInt();
		int bytesTotal = res.headers["bytesTotal"].toInt();
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::pending);
	}
	else if (res.code == 200) // finished with success
	{
		// checking the add data step 
		bool justSentDatasetFile = QString::fromUtf8(res.payload).startsWith("/tmp/");
		if(justSentDatasetFile)
		{
			sentDatasetFileInterpretation(netReqId, res);
		}
		else 
		{
			sentDatasetContextInterpretation(netReqId, res);
		}
	}
	else // an error occured during the request sending (http error)
	{
		emit m_parent->progress(m_requestIdMap[netReqId], eRequestStatus::faild);
		m_requestIdMap.remove(netReqId);
		m_idResultMap[medId] = false;
	}
}

void AsyncNetwork::asyncPostSlot(QUuid netReqId, QByteArray payload, QJsonObject headers, int statusOrHttpCode)
{
	if(m_requestIdMap.contains(netReqId))
	{
		addAsyncDataInterpretation(netReqId , { statusOrHttpCode, headers, payload });
	}
}

void AsyncNetwork::asyncPutSlot(QUuid netReqId, QByteArray payload, QJsonObject headers, int statusOrHttpCode)
{
	if(m_requestIdMap.contains(netReqId))
	{
		// HERE call the interpretation(s) function that is/are based on PUT request 
		// for now, none of them exist
	}
}