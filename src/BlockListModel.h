#ifndef BLOCK_LIST_MODEL_H
#define BLOCK_LIST_MODEL_H

#include <QAbstractListModel>
#include <QString>
#include <QVector>

// One leaf block of the current page, straight from the hub's
// kb_page_blocks read. `object` is the block's seq object id (the id text
// ops target); `origin` its identity in its parent's children list; `atom`
// its winning ref-atom (the anchor structural edits use to find the RAW
// seq position — live indices diverge from raw positions once ghost atoms
// accumulate); `parentObj` the parent layout node's seq object the atom
// lives in. All 64-hex.
struct BlockItem {
    QString object;
    QString origin;
    QString text;
    QString atom;
    QString parentOrigin;
    QString parentObj;
    QString kind;   // "" | bullet | number (blockKind register)
    int depth = 0;  // container nesting depth (0 = body child)
    QString spans;  // marked-spans JSON for rich rendering
    QString embeds; // embedded atoms JSON [{idx,payload,kind}]
};

// The ordered list of leaf blocks the QML editor renders one delegate each.
// Structure changes (split/join/move) reset the model so ListView rebuilds
// delegates; text-only changes are pushed out-of-band via the backend's
// blockTextChanged signal so a focused editor is never clobbered mid-type;
// kind/spans changes ride dataChanged.
class BlockListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ObjectRole = Qt::UserRole + 1,
        OriginRole,
        TextRole,
        KindRole,
        DepthRole,
        SpansRole,
        EmbedsRole,
    };

    explicit BlockListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the block list. Returns the objects whose text changed while
    // the structure (object order/count) stayed the same — the caller emits
    // blockTextChanged for those; a structure change resets the model.
    QVector<QString> apply(const QVector<BlockItem>& next);

    QString textOf(const QString& object) const;

private:
    static bool sameStructure(const QVector<BlockItem>& a, const QVector<BlockItem>& b);
    QVector<BlockItem> m_items;
};

#endif
