#include "BlockListModel.h"

BlockListModel::BlockListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int BlockListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant BlockListModel::data(const QModelIndex& index, int role) const
{
    if (index.row() < 0 || index.row() >= m_items.size())
        return {};
    const BlockItem& b = m_items.at(index.row());
    switch (role) {
    case ObjectRole: return b.object;
    case OriginRole: return b.origin;
    case TextRole:   return b.text;
    case KindRole:   return b.kind;
    case DepthRole:  return b.depth;
    case SpansRole:  return b.spans;
    case EmbedsRole: return b.embeds;
    default:         return {};
    }
}

QHash<int, QByteArray> BlockListModel::roleNames() const
{
    return {
        {ObjectRole, "object"},
        {OriginRole, "origin"},
        {TextRole,   "blockText"},
        {KindRole,   "blockKind"},
        {DepthRole,  "blockDepth"},
        {SpansRole,  "blockSpans"},
        {EmbedsRole, "blockEmbeds"},
    };
}

bool BlockListModel::sameStructure(const QVector<BlockItem>& a, const QVector<BlockItem>& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i).object != b.at(i).object)
            return false;
    return true;
}

QVector<QString> BlockListModel::apply(const QVector<BlockItem>& next)
{
    if (!sameStructure(m_items, next)) {
        beginResetModel();
        m_items = next;
        endResetModel();
        return {}; // full rebuild; delegates re-read everything on creation
    }
    // Same block objects in the same order: update in place, report which
    // blocks' TEXT changed so the backend can push them to live delegates.
    QVector<QString> changed;
    for (int i = 0; i < next.size(); ++i) {
        QVector<int> roles;
        if (m_items.at(i).text != next.at(i).text) {
            m_items[i].text = next.at(i).text;
            changed.push_back(m_items.at(i).object);
            roles.push_back(TextRole);
        }
        if (m_items.at(i).kind != next.at(i).kind) {
            m_items[i].kind = next.at(i).kind;
            roles.push_back(KindRole);
        }
        if (m_items.at(i).spans != next.at(i).spans) {
            m_items[i].spans = next.at(i).spans;
            roles.push_back(SpansRole);
        }
        if (m_items.at(i).embeds != next.at(i).embeds) {
            m_items[i].embeds = next.at(i).embeds;
            roles.push_back(EmbedsRole);
        }
        // Structural anchors refresh silently (no view role bound to them).
        m_items[i].atom = next.at(i).atom;
        m_items[i].parentOrigin = next.at(i).parentOrigin;
        m_items[i].parentObj = next.at(i).parentObj;
        if (!roles.isEmpty()) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx, roles);
        }
    }
    return changed;
}

QString BlockListModel::textOf(const QString& object) const
{
    for (const BlockItem& b : m_items)
        if (b.object == object)
            return b.text;
    return {};
}
