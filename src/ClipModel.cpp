#include "ClipModel.h"
#include <QString>
#include <QFileInfo>

ClipModel::ClipModel(QObject *parent) : QAbstractTableModel(parent) {}

int ClipModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_segments.size());
}

int ClipModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return 4; // File, Start, End, Duration
}

static QString formatTimestamp(double seconds) {
    int h = static_cast<int>(seconds / 3600);
    int m = static_cast<int>((seconds - h * 3600) / 60);
    int s = static_cast<int>(seconds - h * 3600 - m * 60);
    int ms = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
    return QString("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

QVariant ClipModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) return QVariant();

    const auto &seg = m_segments[index.row()];
    switch (index.column()) {
        case 0: return QFileInfo(seg.filePath).fileName();
        case 1: return formatTimestamp(seg.start);
        case 2: return formatTimestamp(seg.end);
        case 3: return formatTimestamp(seg.end - seg.start);
    }
    return QVariant();
}

QVariant ClipModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();

    switch (section) {
        case 0: return tr("File");
        case 1: return tr("Start");
        case 2: return tr("End");
        case 3: return tr("Duration");
    }
    return QVariant();
}

void ClipModel::addSegment(const QString &filePath, double start, double end) {
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_segments.push_back({filePath, start, end});
    endInsertRows();
}

void ClipModel::removeSegment(int row) {
    if (row < 0 || row >= static_cast<int>(m_segments.size())) return;
    beginRemoveRows(QModelIndex(), row, row);
    m_segments.erase(m_segments.begin() + row);
    endRemoveRows();
}

void ClipModel::moveUp(int row) {
    if (row <= 0 || row >= static_cast<int>(m_segments.size())) return;
    beginResetModel();
    std::swap(m_segments[row], m_segments[row - 1]);
    endResetModel();
}

void ClipModel::moveDown(int row) {
    if (row < 0 || row >= static_cast<int>(m_segments.size()) - 1) return;
    beginResetModel();
    std::swap(m_segments[row], m_segments[row + 1]);
    endResetModel();
}

void ClipModel::updateHeaders() {
    emit headerDataChanged(Qt::Horizontal, 0, 3);
}

Qt::ItemFlags ClipModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    if (index.column() == 1 || index.column() == 2) {
        return QAbstractTableModel::flags(index) | Qt::ItemIsEditable;
    }
    return QAbstractTableModel::flags(index);
}

bool ClipModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (role != Qt::EditRole || !index.isValid()) return false;
    
    int row = index.row();
    if (row < 0 || row >= static_cast<int>(m_segments.size())) return false;
    
    QString valStr = value.toString().trimmed();
    double seconds = 0.0;
    QStringList parts = valStr.split(':');
    if (parts.size() == 3) {
        bool ok1, ok2, ok3;
        int hours = parts[0].toInt(&ok1);
        int minutes = parts[1].toInt(&ok2);
        QStringList secParts = parts[2].split('.');
        int secs = secParts[0].toInt(&ok3);
        int msecs = 0;
        if (secParts.size() > 1) {
            msecs = secParts[1].left(3).toInt();
        }
        if (ok1 && ok2 && ok3) {
            seconds = hours * 3600 + minutes * 60 + secs + msecs / 1000.0;
        } else {
            return false;
        }
    } else {
        bool ok;
        seconds = valStr.toDouble(&ok);
        if (!ok) return false;
    }
    
    if (index.column() == 1) {
        m_segments[row].start = seconds;
    } else if (index.column() == 2) {
        m_segments[row].end = seconds;
    }
    
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    QModelIndex durationIndex = this->index(row, 3);
    emit dataChanged(durationIndex, durationIndex, {Qt::DisplayRole});
    return true;
}

void ClipModel::updateSegmentStart(int row, double start) {
    if (row < 0 || row >= static_cast<int>(m_segments.size())) return;
    m_segments[row].start = start;
    QModelIndex idx1 = index(row, 1);
    QModelIndex idx3 = index(row, 3);
    emit dataChanged(idx1, idx1, {Qt::DisplayRole, Qt::EditRole});
    emit dataChanged(idx3, idx3, {Qt::DisplayRole});
}

void ClipModel::updateSegmentEnd(int row, double end) {
    if (row < 0 || row >= static_cast<int>(m_segments.size())) return;
    m_segments[row].end = end;
    QModelIndex idx2 = index(row, 2);
    QModelIndex idx3 = index(row, 3);
    emit dataChanged(idx2, idx2, {Qt::DisplayRole, Qt::EditRole});
    emit dataChanged(idx3, idx3, {Qt::DisplayRole});
}

void ClipModel::clearSegments() {
    beginResetModel();
    m_segments.clear();
    endResetModel();
}

