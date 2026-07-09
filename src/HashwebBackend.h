#ifndef HASHWEB_BACKEND_H
#define HASHWEB_BACKEND_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <functional>
#include "rep_HashwebBackend_source.h"
#include "logos_ui_plugin_context.h"
#include "BlockListModel.h"
#include "PageListModel.h"

// Thin client over hashweb_module (see BASECAMP_MODULE.md): the backend
// holds NO replica — the hub one local socket away is the replica. It is the
// kb.js editor's model layer ported to Qt: the workspace kv, the page graph
// on containment registers, per-page layout trees of leaf blocks, blockKind
// registers, and marks. Reads are the hub's composed kb_* JSON walks (one
// call per render); mutations compose the hub's primitives exactly as kb.js
// does. Re-renders are triggered by the hub's `frame` event (deferred out of
// the event callback — a synchronous read inside it stalls QtRO ~20s).
class HashwebBackend : public HashwebBackendSimpleSource,
                       public LogosUiPluginContext
{
    Q_OBJECT
    Q_PROPERTY(BlockListModel* blockModel READ blockModel CONSTANT)
    Q_PROPERTY(PageListModel* pageModel READ pageModel CONSTANT)

public:
    explicit HashwebBackend(QObject* parent = nullptr);
    ~HashwebBackend() override;

    BlockListModel* blockModel() const { return m_blocks; }
    PageListModel* pageModel() const { return m_pages; }

    void onContextReady() override;

public slots:
    void joinSpace(QString spaceId) override;
    void leaveSpace() override;
    void toggleOffline(bool offline) override;

    void openPage(QString pageObj) override;
    void newPage() override;
    void newSubpage() override;
    void deletePage() override;
    void renamePage(QString title) override;
    void dropPage(QString srcObj, QString targetObj, QString zone) override;
    void titleEnter() override;

    void applyBlockEdit(QString object, int pos, int removed, QString inserted) override;
    void splitBlock(QString object, int caret) override;
    void joinBlock(QString object) override;
    void setBlockKind(QString origin, QString kind) override;
    void markBlockRange(QString object, int start, int end, QString kind, QString value) override;
    void unmarkBlockRange(QString object, int start, int end, QString kind) override;
    void insertImage(QString object, QString fileUrl) override;
    void requestImage(QString artifactId) override;
    void addComment(QString object, int start, int end) override;
    void replyComment(QString tag, QString text) override;
    void resolveComment(QString tag) override;
    void insertTable(QString object, int pos) override;
    void tableAddRow(QString tableOrigin) override;
    void tableAddCol(QString tableOrigin) override;
    void insertPageLink(QString object, int pos, QString pageObj) override;
    void moveBlock(QString object, int delta) override;
    void dropBlock(QString srcOrigin, QString targetOrigin, QString dir) override;
    void dropToEnd(QString srcOrigin) override;
    void addBlockAtEnd() override;
    void makeHeading(QString object) override;
    void makeBlockRegion(QString object, QString kind) override;
    void insertBlockAfter(QString object, QString act) override;
    void pasteMarkdown(QString object, int pos, QString md) override;
    void announcePresence(QString page, QString block, int caret) override;
    void setHandle(QString handle) override;

private:
    static QString resolveInstancePath();
    static QString resolvePreset();

    void initialiseModule(int attempt = 0);
    void subscribeToEvents();
    void presenceTick();
    void refreshRecentSpaces();
    void applyDeliveryState(const QString& state, const QString& detail);
    void deferToEventLoop(std::function<void()> work);

    // --- the kb model layer (kb.js ported; see the .cpp section banners) ---
    QString wsObj();                       // open (derive) the workspace kv
    void rebuild();                        // page tree + current page blocks
    void rebuildPages();
    void rebuildBlocks();
    int liveChildIndex(const QString& object) const;
    // kb.js createPage: children-list ref + place-at-birth + title + body.
    QString createPage(const QString& parentObj);
    // kb.js removeNodeFromParent for blocks: register tombstone + raw-atom
    // hygiene in the leaf's own parent.
    void removeLeaf(const BlockItem& leaf);
    // kb.js insertChildAt, atom-anchored: link newOrigin right after
    // `after`'s atom in parentObj (or at raw position 0 when after is null).
    QString insertLeafAfter(const QString& parentObj, const BlockItem* after,
                            const QString& newOrigin);
    QString bodyObjOfCurrent();

    // --- layout tree (split layouts + drag-drop; kb.js dropOnLeaf etc) ---
    // Live children of a layout node, from the last-parsed tree.
    QJsonArray childrenOfNode(const QString& parentOrigin) const;
    // Find a node (leaf or container) anywhere in the tree.
    QJsonObject findNode(const QString& origin) const;
    // kb.js insertChildAt: link + claim child at live index `idx` of parent
    // (idx counts live children; raw position derives from the anchor atom).
    bool insertChildAtIdx(const QString& parentOrigin, const QString& childOrigin, int idx);
    // kb.js makeContainerNode: container seq (mark + child refs), children
    // claimed in. Returns the container's origin.
    QString makeContainer(const QStringList& childOrigins);
    // kb.js removeNodeFromParent: register tombstone + raw-atom hygiene.
    void removeNode(const QJsonObject& node);
    // kb.js normalizeTree: drop empty containers, unwrap single-child ones.
    void normalizeTree();
    bool normalizePass(const QJsonArray& nodes);

    QString m_instancePath;
    bool m_moduleInitialised = false;
    // Presence: this session's identity + the heartbeat that re-announces
    // the last state and polls peers (ephemeral, delivery-layer only).
    QString m_sid;
    QString m_handle;
    QString m_lastPresence;
    QTimer* m_presenceTimer = nullptr;
    QString m_wsObj;                   // workspace kv object id
    QString m_bodyOrigin;              // current page's body origin
    QJsonArray m_tree;                 // last-built layout tree (hub "tree")
    QString m_layoutSignature;         // tree minus text/spans/embeds
    QVector<BlockItem> m_current;      // last-built flattened leaf list
    BlockListModel* m_blocks;
    PageListModel* m_pages;
};

#endif
