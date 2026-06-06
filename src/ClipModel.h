#pragma once
#include <QAbstractTableModel>
#include <vector>

struct Segment {
    QString filePath;
    double start;
    double end;
};

class ClipModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ClipModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void addSegment(const QString &filePath, double start, double end);
    void removeSegment(int row);
    void moveUp(int row);
    void moveDown(int row);
    void updateHeaders();

    const std::vector<Segment>& segments() const { return m_segments; }

private:
    std::vector<Segment> m_segments;
};
