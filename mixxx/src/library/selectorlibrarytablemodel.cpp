// Created 3/17/2012 by Keith Salisbury (keithsalisbury@gmail.com)

#include <QObject>

#include "selectorlibrarytablemodel.h"
#include "library/trackcollection.h"
#include "basetrackplayer.h"
#include "playerinfo.h"

#include "controlobjectthreadmain.h"
#include "controlobject.h"
#include "trackinfoobject.h"


SelectorLibraryTableModel::SelectorLibraryTableModel(QObject* parent,
                                                   TrackCollection* pTrackCollection)
        : LibraryTableModel(parent, pTrackCollection,
                            "mixxx.db.model.prepare") {

    connect(this, SIGNAL(doSearch(const QString&)),
            this, SLOT(slotSearch(const QString&)));

    // Getting info on current decks playing etc
    connect(&PlayerInfo::Instance(), SIGNAL(currentPlayingDeckChanged(int)),
           this, SLOT(slotPlayingDeckChanged(int)));

    m_bFilterGenre = true;
    m_bFilterBpm = false;
    m_bFilterYear = false;

}


SelectorLibraryTableModel::~SelectorLibraryTableModel() {
}

bool SelectorLibraryTableModel::isColumnInternal(int column) {
    return LibraryTableModel::isColumnInternal(column);
}

void SelectorLibraryTableModel::slotPlayingDeckChanged(int deck) {
    TrackPointer loaded_track = PlayerInfo::Instance(
        ).getTrackInfo(QString("[Channel%1]").arg(deck));
    if (loaded_track) {

        // Genre
        QString TrackGenre = loaded_track->getGenre();
        m_pFilterGenre = (TrackGenre != "") ? QString(
            "Genre == '%1'").arg(TrackGenre) : QString();

        // Bpm
        float TrackBpm = loaded_track->getBpm();
        m_pFilterBpm = (TrackBpm > 0) ? QString(
            "Bpm > %1 AND Bpm < %2").arg(
            TrackBpm - 1).arg(TrackBpm + 1) : QString();

        // Year
        QString TrackYear = loaded_track->getYear();
        m_pFilterYear = (TrackYear!="") ? QString(
            "Year == '%1'").arg(TrackYear) : QString();

    }
    //qDebug() << "slotPlayingDeckChanged(" << deck;
    emit(doSearch(""));
}

void SelectorLibraryTableModel::search(const QString& searchText) {
    // qDebug() << "SelectorLibraryTableModel::search()" << searchText
    //          << QThread::currentThread();
    emit(doSearch(searchText));
}

void SelectorLibraryTableModel::slotSearch(const QString& searchText) {
    QStringList filters;
    if (m_bFilterGenre && m_pFilterGenre != "") { filters << m_pFilterGenre; }
    if (m_bFilterBpm && m_pFilterBpm != "") { filters << m_pFilterBpm; }
    if (m_bFilterYear && m_pFilterYear != "") { filters << m_pFilterYear; }

    //qDebug() << "slotSearch()m_pFilterGenre = [" << m_pFilterGenre << "] " << (m_pFilterGenre != "");   
    //qDebug() << "slotSearch()m_pFilterBpm = [" << m_pFilterBpm << "] " << (m_pFilterBpm != "");   
    //qDebug() << "slotSearch()m_pFilterYear = [" << m_pFilterYear << "] " << (m_pFilterYear != "");   

    QString filterText = filters.join(" AND ");
 
    qDebug() << "slotSearch()" << filterText;
    BaseSqlTableModel::search(searchText, filterText);
}

void SelectorLibraryTableModel::filterByGenre(bool value) {
    m_bFilterGenre = value;
    emit(doSearch(QString()));
}

void SelectorLibraryTableModel::filterByBpm(bool value) {
    m_bFilterBpm = value;
    emit(doSearch(QString()));
}

void SelectorLibraryTableModel::filterByYear(bool value) {
    m_bFilterYear = value;
    emit(doSearch(QString()));
}
