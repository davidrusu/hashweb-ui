#include "PageListModel.h"

PageListModel::PageListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PageListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant PageListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.size())
        return {};
    const PageItem& p = m_items.at(index.row());
    switch (role) {
    case ObjRole:        return p.obj;
    case TitleRole:      return p.title;
    case DepthRole:      return p.depth;
    case OrphanRole:     return p.orphan;
    case ConflictedRole: return p.conflicted;
    default:             return {};
    }
}

QHash<int, QByteArray> PageListModel::roleNames() const
{
    return {
        {ObjRole,        "pageObj"},
        {TitleRole,      "pageTitle"},
        {DepthRole,      "pageDepth"},
        {OrphanRole,     "pageOrphan"},
        {ConflictedRole, "pageConflicted"},
    };
}

bool PageListModel::sameStructure(const QVector<PageItem>& a, const QVector<PageItem>& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i).obj != b.at(i).obj || a.at(i).depth != b.at(i).depth)
            return false;
    return true;
}

void PageListModel::apply(const QVector<PageItem>& next)
{
    if (!sameStructure(m_items, next)) {
        beginResetModel();
        m_items = next;
        endResetModel();
        return;
    }
    for (int i = 0; i < next.size(); ++i) {
        QVector<int> roles;
        if (m_items.at(i).title != next.at(i).title) {
            m_items[i].title = next.at(i).title;
            roles.push_back(TitleRole);
        }
        if (m_items.at(i).conflicted != next.at(i).conflicted) {
            m_items[i].conflicted = next.at(i).conflicted;
            roles.push_back(ConflictedRole);
        }
        m_items[i].atom = next.at(i).atom;
        m_items[i].listObj = next.at(i).listObj;
        m_items[i].rawIdx = next.at(i).rawIdx;
        m_items[i].parent = next.at(i).parent;
        if (!roles.isEmpty()) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, roles);
        }
    }
}

int PageListModel::indexOf(const QString& obj) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).obj == obj)
            return i;
    return -1;
}
