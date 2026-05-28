#include "ClipModel.h"
#include <QString>

ClipModel::ClipModel(QObject *parent) : QAbstractTableModel(parent) {}

int ClipModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_segments.size());
}

int ClipModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return 3; // Start, End, Duration
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
        case 0: return formatTimestamp(seg.start);
        case 1: return formatTimestamp(seg.end);
        case 2: return formatTimestamp(seg.end - seg.start);
    }
    return QVariant();
}

QVariant ClipModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) return QVariant();

    switch (section) {
        case 0: return "Start";
        case 1: return "End";
        case 2: return "Duration";
    }
    return QVariant();
}

void ClipModel::addSegment(double start, double end) {
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_segments.push_back({start, end});
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
