#ifndef PAGE_LIST_MODEL_H
#define PAGE_LIST_MODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// One page of the workspace graph, straight from the hub's kb_page_tree
// read: the tree flattened pre-order (depth carries the indent), with the
// unplaced-orphans strip appended. `atom`/`listObj`/`rawIdx` are the
// structural anchors deletePage needs.
struct PageItem {
    QString obj;
    QString origin;
    QString title;
    QString parent; // parent page obj ("" = workspace root)
    int depth = 0;
    bool orphan = false;
    bool conflicted = false;
    QString atom;
    QString listObj;
    int rawIdx = -1;
};

class PageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ObjRole = Qt::UserRole + 1,
        TitleRole,
        DepthRole,
        OrphanRole,
        ConflictedRole,
    };

    explicit PageListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the page list; resets on structure change, dataChanged on
    // title-only changes.
    void apply(const QVector<PageItem>& next);

    const QVector<PageItem>& items() const { return m_items; }
    int indexOf(const QString& obj) const;

private:
    static bool sameStructure(const QVector<PageItem>& a, const QVector<PageItem>& b);
    QVector<PageItem> m_items;
};

#endif
