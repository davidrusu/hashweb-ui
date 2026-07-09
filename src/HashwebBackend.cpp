#include "HashwebBackend.h"

// Generated umbrella: LogosModules (behind modules()) from
// metadata.json#dependencies — the Qt-typed hashweb_module wrapper.
#include "logos_sdk.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <cstdlib>

namespace {

constexpr const char* kInstancePathEnvVar = "HASHWEB_MODULE_INSTANCE_PATH";
constexpr const char* kPresetEnvVar = "HASHWEB_DELIVERY_PRESET";
constexpr const char* kDefaultPreset = "logos.test";

// The host informs this module's capability token to capability_module a
// beat AFTER onContextReady, so the first calls can bounce off "auth token
// not recognized" (empty error). Retry init until it lands.
constexpr int kInitRetryMs = 300;
constexpr int kMaxInitAttempts = 20;

// kb.js WS_ORIGIN: ascii "hashweb-kb.workspace.v1" zero-padded to 32 bytes —
// the workspace kv every kb client in a space converges on.
QString wsOriginHex()
{
    QByteArray seed = QByteArrayLiteral("hashweb-kb.workspace.v1");
    seed.resize(32);
    return QString::fromLatin1(seed.toHex());
}

QString randomOriginHex()
{
    QByteArray b = QUuid::createUuid().toRfc4122() + QUuid::createUuid().toRfc4122();
    b.resize(32);
    return QString::fromLatin1(b.toHex());
}

// Unwrap a `result`-returning hub call: its string value, or empty on failure.
QString sval(const LogosResult& r) { return r.success ? r.getString() : QString(); }
int ival(const LogosResult& r) { return r.success ? r.getValue<int>() : -1; }

bool isListKind(const QString& kind)
{
    return kind == QLatin1String("bullet") || kind == QLatin1String("number");
}

// The layout tree stripped of per-keystroke fields (text/spans/embeds):
// equal signatures = same structure = keep the delegates alive.
QJsonArray structureOf(const QJsonArray& tree)
{
    QJsonArray out;
    for (const QJsonValue& v : tree) {
        QJsonObject n = v.toObject();
        n.remove(QStringLiteral("text"));
        n.remove(QStringLiteral("spans"));
        n.remove(QStringLiteral("embeds"));
        n.remove(QStringLiteral("raw_idx")); // ghosts shift raw positions, not structure
        n.remove(QStringLiteral("atom"));    // moves re-anchor without visual change
        if (n.contains(QStringLiteral("children")))
            n[QStringLiteral("children")] =
                structureOf(n.value(QStringLiteral("children")).toArray());
        out.push_back(n);
    }
    return out;
}

} // namespace

HashwebBackend::HashwebBackend(QObject* parent)
    : HashwebBackendSimpleSource(parent)
    , m_instancePath(resolveInstancePath())
    , m_blocks(new BlockListModel(this))
    , m_pages(new PageListModel(this))
{
    setHubStatus(HashwebBackendSimpleSource::Stopped);
    setStatusMessage(QStringLiteral("Ready"));
    setSpaceId(QString());
    setOffline(false);
    setCurrentPage(QString());
    setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
    setRecentSpacesJson(QStringLiteral("[]"));
    setPresenceJson(QStringLiteral("[]"));

    m_sid = QString::fromLatin1(
        QUuid::createUuid().toRfc4122().toHex().left(12));
    m_presenceTimer = new QTimer(this);
    m_presenceTimer->setInterval(3000);
    connect(m_presenceTimer, &QTimer::timeout, this,
            &HashwebBackend::presenceTick);
    m_presenceTimer->start();
}

HashwebBackend::~HashwebBackend() = default;

void HashwebBackend::onContextReady()
{
    initialiseModule();
}

QString HashwebBackend::resolveInstancePath()
{
    if (const char* env = std::getenv(kInstancePathEnvVar); env && *env) {
        QString path = QString::fromUtf8(env);
        QDir().mkpath(path);
        return path;
    }
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    const QString path = base + QStringLiteral("/hashweb_module");
    QDir().mkpath(path);
    return path;
}

QString HashwebBackend::resolvePreset()
{
    const char* env = std::getenv(kPresetEnvVar);
    return (env && *env) ? QString::fromUtf8(env) : QString::fromLatin1(kDefaultPreset);
}

// ── lifecycle ────────────────────────────────────────────────────────────────

void HashwebBackend::initialiseModule(int attempt)
{
    setHubStatus(HashwebBackendSimpleSource::Initialising);
    setStatusMessage(QStringLiteral("Starting delivery node..."));
    if (attempt == 0)
        qDebug() << "HashwebBackend: init at" << m_instancePath;

    LogosResult res = modules().hashweb_module.init(m_instancePath, resolvePreset(), 0);
    if (!res.success) {
        if (attempt + 1 < kMaxInitAttempts) {
            setStatusMessage(QStringLiteral("Waiting for module authorization..."));
            QTimer::singleShot(kInitRetryMs, this,
                               [this, attempt] { initialiseModule(attempt + 1); });
            return;
        }
        const QString reason = res.getError<QString>();
        setHubStatus(HashwebBackendSimpleSource::Error);
        setStatusMessage(QStringLiteral("init failed: ") + reason);
        emit error(QStringLiteral("Failed to initialise hashweb: ") + reason);
        return;
    }
    m_moduleInitialised = true;

    subscribeToEvents();
    refreshRecentSpaces();

    const QVariantMap status = modules().hashweb_module.status().toMap();
    applyDeliveryState(status.value(QStringLiteral("delivery_state")).toString(),
                       status.value(QStringLiteral("detail")).toString());
}

void HashwebBackend::refreshRecentSpaces()
{
    // The hub's persisted spaces: everything joined before, on any run.
    const QStringList spaces = modules().hashweb_module.list_spaces().toList();
    QJsonArray arr;
    for (const QString& s : spaces)
        arr.push_back(s);
    setRecentSpacesJson(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void HashwebBackend::subscribeToEvents()
{
    auto& hub = modules().hashweb_module;
    hub.on(QStringLiteral("frame"), [this](const QVariantList& a) {
        const QString space = a.value(0).toString();
        deferToEventLoop([this, space] {
            if (space == spaceId())
                rebuild();
        });
    });
    hub.on(QStringLiteral("delivery_state_changed"), [this](const QVariantList& a) {
        const QString state = a.value(0).toString();
        const QString detail = a.value(1).toString();
        deferToEventLoop([this, state, detail] { applyDeliveryState(state, detail); });
    });
}

void HashwebBackend::applyDeliveryState(const QString& state, const QString& detail)
{
    setOffline(state == QStringLiteral("offline"));
    if (state == QStringLiteral("online")) {
        setHubStatus(HashwebBackendSimpleSource::Online);
        setStatusMessage(QStringLiteral("Online"));
    } else if (state == QStringLiteral("offline")) {
        setHubStatus(HashwebBackendSimpleSource::Offline);
        setStatusMessage(QStringLiteral("Offline — edits stay local until you reconnect"));
    } else if (state == QStringLiteral("initialising")) {
        setHubStatus(HashwebBackendSimpleSource::Initialising);
        setStatusMessage(QStringLiteral("Connecting to delivery..."));
    } else {
        setHubStatus(HashwebBackendSimpleSource::Error);
        setStatusMessage(detail.isEmpty() ? QStringLiteral("Delivery error") : detail);
    }
}

// ── workspace / pages (kb.js page graph) ────────────────────────────────────

void HashwebBackend::joinSpace(QString spaceId)
{
    if (!isContextReady() || !m_moduleInitialised) {
        emit error(QStringLiteral("Hub not initialised"));
        return;
    }
    const QString trimmed = spaceId.trimmed();
    if (trimmed.isEmpty()) {
        emit error(QStringLiteral("Space id cannot be empty"));
        return;
    }
    if (!modules().hashweb_module.join_space(trimmed).success) {
        emit error(QStringLiteral("join_space failed"));
        return;
    }
    setSpaceId(trimmed);
    refreshRecentSpaces();
    m_wsObj = wsObj();
    setCurrentPage(QString());
    setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
    rebuild();
    // Open the first page if the workspace already has content.
    if (currentPage().isEmpty() && !m_pages->items().isEmpty())
        openPage(m_pages->items().first().obj);
    setStatusMessage(QStringLiteral("Joined ") + trimmed);
}

void HashwebBackend::leaveSpace()
{
    if (spaceId().isEmpty())
        return;
    // The hub persists the space's state file and unsubscribes its topic;
    // rejoining later reloads from disk and resyncs.
    modules().hashweb_module.leave_space(spaceId());
    setSpaceId(QString());
    setCurrentPage(QString());
    setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
    m_wsObj.clear();
    m_bodyOrigin.clear();
    m_current.clear();
    m_blocks->apply({});
    m_pages->apply({});
    m_lastPresence.clear();
    setPresenceJson(QStringLiteral("[]"));
    refreshRecentSpaces();
    setStatusMessage(QStringLiteral("Online"));
}

QString HashwebBackend::wsObj()
{
    // Idempotent open (create ≠ open): every peer derives the same kv.
    return sval(modules().hashweb_module.create_kv(spaceId(), wsOriginHex()));
}

void HashwebBackend::rebuild()
{
    rebuildPages();
    rebuildBlocks();
}

void HashwebBackend::rebuildPages()
{
    if (spaceId().isEmpty())
        return;
    const QString json = sval(modules().hashweb_module.kb_page_tree(spaceId()));
    if (json.isEmpty())
        return;
    const QJsonArray pages =
        QJsonDocument::fromJson(json.toUtf8()).object().value(QStringLiteral("pages")).toArray();
    QVector<PageItem> next;
    next.reserve(pages.size());
    bool currentAlive = false;
    for (const QJsonValue& v : pages) {
        const QJsonObject o = v.toObject();
        PageItem p;
        p.obj = o.value(QStringLiteral("obj")).toString();
        p.origin = o.value(QStringLiteral("origin")).toString();
        p.title = o.value(QStringLiteral("title")).toString();
        p.parent = o.value(QStringLiteral("parent")).toString();
        p.depth = o.value(QStringLiteral("depth")).toInt();
        p.orphan = o.value(QStringLiteral("orphan")).toBool();
        p.conflicted = o.value(QStringLiteral("conflicted")).toBool();
        p.atom = o.value(QStringLiteral("atom")).toString();
        p.listObj = o.value(QStringLiteral("list_obj")).toString();
        p.rawIdx = o.value(QStringLiteral("raw_idx")).toInt(-1);
        if (p.obj == currentPage())
            currentAlive = true;
        next.push_back(p);
    }
    m_pages->apply(next);
    if (!currentPage().isEmpty() && !currentAlive) {
        // The open page was deleted (possibly remotely).
        setCurrentPage(QString());
        setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
        m_current.clear();
        m_blocks->apply({});
    }
}

void HashwebBackend::rebuildBlocks()
{
    if (spaceId().isEmpty() || currentPage().isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString json = sval(hub.kb_page_blocks(spaceId(), currentPage()));
    if (json.isEmpty())
        return;
    const QJsonObject pb = QJsonDocument::fromJson(json.toUtf8()).object();
    setPageTitle(pb.value(QStringLiteral("title")).toString());
    setTitleConflictJson(QString::fromUtf8(
        QJsonDocument(pb.value(QStringLiteral("title_conflict")).toArray())
            .toJson(QJsonDocument::Compact)));
    m_bodyOrigin = pb.value(QStringLiteral("body_origin")).toString();

    // The layout tree replicates ONLY on structural change; per-keystroke
    // content rides blockUpdated so delegates survive typing.
    m_tree = pb.value(QStringLiteral("tree")).toArray();
    const QString signature = QString::fromUtf8(
        QJsonDocument(structureOf(m_tree)).toJson(QJsonDocument::Compact));
    if (signature != m_layoutSignature) {
        m_layoutSignature = signature;
        setLayoutJson(QString::fromUtf8(QJsonDocument(m_tree).toJson(QJsonDocument::Compact)));
    }

    QVector<BlockItem> next;
    const QJsonArray leaves = pb.value(QStringLiteral("leaves")).toArray();
    next.reserve(leaves.size());
    for (const QJsonValue& v : leaves) {
        const QJsonObject o = v.toObject();
        BlockItem b;
        b.object = o.value(QStringLiteral("obj")).toString();
        b.origin = o.value(QStringLiteral("origin")).toString();
        b.text = o.value(QStringLiteral("text")).toString();
        b.atom = o.value(QStringLiteral("atom")).toString();
        b.parentOrigin = o.value(QStringLiteral("parent_origin")).toString();
        b.parentObj = o.value(QStringLiteral("parent_obj")).toString();
        b.kind = o.value(QStringLiteral("kind")).toString();
        b.depth = o.value(QStringLiteral("depth")).toInt();
        b.spans = QString::fromUtf8(
            QJsonDocument(o.value(QStringLiteral("spans")).toArray()).toJson(QJsonDocument::Compact));
        b.embeds = QString::fromUtf8(
            QJsonDocument(o.value(QStringLiteral("embeds")).toArray()).toJson(QJsonDocument::Compact));
        next.push_back(b);
    }

    // Content changes with the structure unchanged: push per block.
    QHash<QString, const BlockItem*> old;
    for (const BlockItem& b : m_current)
        old.insert(b.object, &b);
    for (const BlockItem& b : next) {
        const BlockItem* o = old.value(b.object, nullptr);
        if (o && (o->text != b.text || o->spans != b.spans || o->embeds != b.embeds))
            emit blockUpdated(b.object, b.text, b.spans, b.embeds);
    }

    m_current = next;
    m_blocks->apply(next);

    setCommentsJson(sval(hub.kb_comments(spaceId(), currentPage())));
}

int HashwebBackend::liveChildIndex(const QString& object) const
{
    for (int i = 0; i < m_current.size(); ++i)
        if (m_current.at(i).object == object)
            return i;
    return -1;
}

void HashwebBackend::openPage(QString pageObj)
{
    if (pageObj == currentPage())
        return;
    setCurrentPage(pageObj);
    setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
    m_current.clear();
    m_blocks->apply({});
    if (pageObj.isEmpty())
        return;
    // ensureTreeSchema, flag-only port: kb.js's rows→tree migration guards
    // against registered children; new-era pages are always 'tree'. Pre-tree
    // rows data (legacy web-only docs) renders flat until migrated by kb.js.
    auto& hub = modules().hashweb_module;
    const QJsonObject schema = QJsonDocument::fromJson(
        sval(hub.read_key(spaceId(), pageObj, QStringLiteral("bodySchema"))).toUtf8()).object();
    if (schema.value(QStringLiteral("kind")).toString() == QLatin1String("absent"))
        hub.put_string(spaceId(), pageObj, QStringLiteral("bodySchema"), QStringLiteral("tree"));
    rebuildBlocks();
}

// kb.js createPage: children-list ref + place-at-birth + title + body ref +
// first leaf + bodySchema flag.
QString HashwebBackend::createPage(const QString& parentObj)
{
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString origin = randomOriginHex();

    // childrenListOf(parentObj): the seq whose ORIGIN is seqId(parentObj).
    const QString listOrigin = sval(hub.seq_id(parentObj));
    const QString listObj = sval(hub.create_seq(space, listOrigin));
    if (listObj.isEmpty()) {
        emit error(QStringLiteral("createPage: children list open failed"));
        return {};
    }
    LogosResult lenRes = hub.text_len(space, listObj);
    const int at = lenRes.success ? lenRes.getValue<int>() : 0;
    const QString elem = sval(hub.seq_insert_ref(space, listObj, at, origin));
    const QString page = sval(hub.create_kv(space, origin));
    if (page.isEmpty() || elem.isEmpty()) {
        emit error(QStringLiteral("createPage failed"));
        return {};
    }
    hub.place_at(space, page, elem); // claimed at birth
    hub.put_string(space, page, QStringLiteral("title"), QStringLiteral("Untitled"));
    const QString bodyOrigin = randomOriginHex();
    hub.put_ref(space, page, QStringLiteral("body"), bodyOrigin);
    const QString body = sval(hub.create_seq(space, bodyOrigin));
    {
        const QString leafOrigin = randomOriginHex();
        hub.create_seq(space, leafOrigin);
        const QString leaf = sval(hub.seq_id(leafOrigin));
        const QString leafElem = sval(hub.seq_insert_ref(space, body, 0, leafOrigin));
        if (!leaf.isEmpty() && !leafElem.isEmpty())
            hub.place_at(space, leaf, leafElem);
    }
    hub.put_string(space, page, QStringLiteral("bodySchema"), QStringLiteral("tree"));
    return page;
}

void HashwebBackend::newPage()
{
    if (m_wsObj.isEmpty())
        return;
    const QString page = createPage(m_wsObj);
    if (page.isEmpty())
        return;
    rebuildPages();
    openPage(page);
    emit focusTitle();
}

void HashwebBackend::newSubpage()
{
    if (currentPage().isEmpty())
        return;
    const QString page = createPage(currentPage());
    if (page.isEmpty())
        return;
    rebuildPages();
    openPage(page);
    emit focusTitle();
}

void HashwebBackend::deletePage()
{
    const int i = m_pages->indexOf(currentPage());
    if (i < 0)
        return;
    const PageItem p = m_pages->items().at(i);
    auto& hub = modules().hashweb_module;
    // Delete = the register records detachment (unresurrectable by any
    // fallback); the atom is tombstoned as hygiene (kb.js delete-page).
    hub.place_at(spaceId(), p.obj, sval(hub.tombstone_id()));
    if (!p.listObj.isEmpty() && !p.atom.isEmpty()) {
        const int rawPos = ival(hub.seq_position_of(spaceId(), p.listObj, p.atom));
        if (rawPos >= 0)
            hub.text_remove(spaceId(), p.listObj, rawPos, 1);
    }
    setCurrentPage(QString());
    setPageTitle(QString());
    setCommentsJson(QStringLiteral("[]"));
    setTitleConflictJson(QStringLiteral("[]"));
    m_layoutSignature.clear();
    m_tree = QJsonArray();
    setLayoutJson(QStringLiteral("[]"));
    m_current.clear();
    m_blocks->apply({});
    rebuildPages();
    if (!m_pages->items().isEmpty())
        openPage(m_pages->items().first().obj);
}

void HashwebBackend::renamePage(QString title)
{
    if (currentPage().isEmpty())
        return;
    const QString t = title.isEmpty() ? QStringLiteral("Untitled") : title;
    if (t == pageTitle())
        return;
    modules().hashweb_module.put_string(spaceId(), currentPage(), QStringLiteral("title"), t);
    setPageTitle(t);
    rebuildPages(); // sidebar title updates immediately
}

void HashwebBackend::dropPage(QString srcObj, QString targetObj, QString zone)
{
    if (srcObj.isEmpty() || targetObj.isEmpty() || srcObj == targetObj)
        return;
    const int si = m_pages->indexOf(srcObj);
    const int ti = m_pages->indexOf(targetObj);
    if (si < 0 || ti < 0)
        return;
    const PageItem src = m_pages->items().at(si);
    const PageItem tgt = m_pages->items().at(ti);
    // Dropping into your own subtree would orphan the subtree.
    for (QString anc = targetObj; !anc.isEmpty();) {
        if (anc == srcObj)
            return;
        const int ai = m_pages->indexOf(anc);
        if (ai < 0)
            break;
        anc = m_pages->items().at(ai).parent;
    }

    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    // Live positions re-derived at drop time (the sidebar's cached raw
    // indices can lag remote edits).
    const int srcIdx = (src.listObj.isEmpty() || src.atom.isEmpty())
                           ? -1
                           : ival(hub.seq_position_of(space, src.listObj, src.atom));

    if (zone == QLatin1String("into")) {
        // Become a subpage — the target's children list already exists by
        // derivation, even if it has never been touched.
        const QString targetList =
            sval(hub.create_seq(space, sval(hub.seq_id(targetObj))));
        if (targetList.isEmpty())
            return;
        LogosResult lenRes = hub.text_len(space, targetList);
        const int len = lenRes.success ? lenRes.getValue<int>() : 0;
        if (src.listObj == targetList && srcIdx >= 0) {
            hub.seq_move(space, src.listObj, srcIdx, len); // order only: ONE Move op
        } else {
            const QString elem = sval(hub.seq_insert_ref(space, targetList, len, src.origin));
            if (!elem.isEmpty())
                hub.place_at(space, srcObj, elem); // fresh link + claim
        }
    } else {
        if (tgt.orphan || tgt.listObj.isEmpty() || tgt.atom.isEmpty())
            return; // no ordered slot next to an unplaced page
        const int tgtIdx = ival(hub.seq_position_of(space, tgt.listObj, tgt.atom));
        if (tgtIdx < 0)
            return;
        const int slot = tgtIdx + (zone == QLatin1String("above") ? 0 : 1);
        if (src.listObj == tgt.listObj && srcIdx >= 0) {
            if (slot == srcIdx || slot == srcIdx + 1)
                return; // dropping onto its own slot
            hub.seq_move(space, src.listObj, srcIdx, slot);
        } else {
            // Reparent = link + claim in the destination; the page's own
            // register decides membership, so nothing is removed and
            // concurrent reparents freeze instead of duplicating.
            const QString elem = sval(hub.seq_insert_ref(space, tgt.listObj, slot, src.origin));
            if (!elem.isEmpty())
                hub.place_at(space, srcObj, elem);
        }
    }
    rebuildPages();
}

void HashwebBackend::titleEnter()
{
    // kb.js: Enter in the title opens a fresh FIRST block of the body.
    if (currentPage().isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString newOrigin = randomOriginHex();
    hub.create_seq(spaceId(), newOrigin);
    const QString newLeaf =
        insertLeafAfter(bodyObjOfCurrent(), nullptr, newOrigin);
    if (newLeaf.isEmpty())
        return;
    rebuildBlocks();
    emit focusBlock(newLeaf, 0);
}

// ── blocks ───────────────────────────────────────────────────────────────────

void HashwebBackend::applyBlockEdit(QString object, int pos, int removed, QString inserted)
{
    if (object.isEmpty() || pos < 0 || removed < 0)
        return;
    const QString space = spaceId();
    auto& hub = modules().hashweb_module;
    bool ok = true;
    if (removed > 0) {
        LogosResult r = hub.text_remove(space, object, pos, removed);
        if (!r.success) {
            qWarning() << "hashweb: text_remove failed:" << r.getError<QString>();
            ok = false;
        }
    }
    if (ok && !inserted.isEmpty()) {
        LogosResult r = hub.text_insert(space, object, pos, inserted);
        if (!r.success) {
            qWarning() << "hashweb: text_insert failed:" << r.getError<QString>();
            ok = false;
        }
    }
    if (!ok) {
        // The delegate's shadow advanced but the hub didn't; resync the model
        // so the delegate reconciles (on blur, or immediately if unfocused).
        rebuildBlocks();
        return;
    }
    // No local rebuild on success: the hub echoes our authored frame back,
    // refreshing via the same path remote edits take.
}

QString HashwebBackend::insertLeafAfter(const QString& parentObj, const BlockItem* after,
                                        const QString& newOrigin)
{
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    hub.create_seq(space, newOrigin);
    const QString newLeaf = sval(hub.seq_id(newOrigin));
    if (newLeaf.isEmpty() || parentObj.isEmpty())
        return {};
    // Insert into the RAW parent seq. A live index is not a raw position
    // once ghost atoms accumulate (kb.js insertChildAt's hard-won rule), so
    // anchor on the preceding sibling's winning atom — or raw slot 0.
    int rawPos = 0;
    if (after) {
        const int p = ival(hub.seq_position_of(space, parentObj, after->atom));
        if (p < 0) {
            qWarning() << "hashweb: sibling atom not found in parent seq";
            return {};
        }
        rawPos = p + 1;
    }
    const QString atom = sval(hub.seq_insert_ref(space, parentObj, rawPos, newOrigin));
    if (atom.isEmpty()) {
        qWarning() << "hashweb: seq_insert_ref failed";
        return {};
    }
    hub.place_at(space, newLeaf, atom);
    return newLeaf;
}

void HashwebBackend::removeLeaf(const BlockItem& leaf)
{
    auto& hub = modules().hashweb_module;
    // Register detachment first (no fallback can resurrect it), then the
    // raw atom as hygiene (kb.js removeNodeFromParent, in this order).
    hub.place_at(spaceId(), leaf.object, sval(hub.tombstone_id()));
    const int rawPos = ival(hub.seq_position_of(spaceId(), leaf.parentObj, leaf.atom));
    if (rawPos >= 0)
        hub.text_remove(spaceId(), leaf.parentObj, rawPos, 1);
}

QString HashwebBackend::bodyObjOfCurrent()
{
    if (currentPage().isEmpty())
        return {};
    auto& hub = modules().hashweb_module;
    const QString json = sval(hub.kb_page_blocks(spaceId(), currentPage()));
    return QJsonDocument::fromJson(json.toUtf8())
        .object()
        .value(QStringLiteral("body_obj"))
        .toString();
}

void HashwebBackend::splitBlock(QString object, int caret)
{
    const int ci = liveChildIndex(object);
    if (ci < 0 || caret < 0)
        return;
    const QString space = spaceId();
    auto& hub = modules().hashweb_module;
    const BlockItem cur = m_current.at(ci);

    // The hub, not m_current, is the authoritative text: applyBlockEdit
    // skips the local rebuild, so the cache lags in-flight keystrokes. Slot
    // calls are ordered, so every preceding edit has already landed.
    LogosResult textRes = hub.text(space, object);
    if (!textRes.success) {
        qWarning() << "hashweb: split: text() failed:" << textRes.getError<QString>();
        return;
    }
    const QString text = textRes.getString();

    // Enter on an EMPTY list item exits the list (the universal idiom).
    if (isListKind(cur.kind) && text.isEmpty()) {
        hub.del_key(space, currentPage(), QStringLiteral("blockKind:") + cur.origin);
        rebuildBlocks();
        emit focusBlock(object, 0);
        return;
    }

    const int at = qMin(caret, int(text.size()));
    QString tail = text.mid(at);
    // A tail containing embeds (U+FFFC atoms) stays put (kb.js tailPlain).
    if (tail.contains(QChar(0xFFFC)))
        tail.clear();

    const QString newOrigin = randomOriginHex();
    const QString newLeaf = insertLeafAfter(cur.parentObj, &cur, newOrigin);
    if (newLeaf.isEmpty()) {
        rebuildBlocks();
        return;
    }
    if (!tail.isEmpty()) {
        hub.text_remove(space, object, at, tail.size());
        hub.text_insert(space, newLeaf, 0, tail);
    }
    // The list continues: the new item inherits the flavor (kb.js Enter).
    if (isListKind(cur.kind)) {
        hub.put_string(space, currentPage(), QStringLiteral("blockKind:") + newOrigin,
                       cur.kind);
    }

    rebuildBlocks();
    emit focusBlock(newLeaf, 0);
}

void HashwebBackend::joinBlock(QString object)
{
    const int ci = liveChildIndex(object);
    if (ci < 0)
        return;
    const QString space = spaceId();
    auto& hub = modules().hashweb_module;
    const BlockItem cur = m_current.at(ci);

    // A list item unwraps to a plain block first; the next press merges.
    if (isListKind(cur.kind)) {
        hub.del_key(space, currentPage(), QStringLiteral("blockKind:") + cur.origin);
        rebuildBlocks();
        emit focusBlock(object, 0);
        return;
    }

    LogosResult curTextRes = hub.text(space, cur.object);
    if (!curTextRes.success) {
        qWarning() << "hashweb: join: text() failed";
        return;
    }
    QString curText = curTextRes.getString();
    // Embeds are blocks of their own (no inline images): atoms never merge
    // into the previous block — a U+FFFC inserted as TEXT would corrupt it.
    curText.remove(QChar(0xFFFC));

    // Empty (or embed-only) block: the delete deletes the block, focus a
    // neighbor (kb.js empty-block delete).
    if (curText.isEmpty()) {
        if (m_current.size() <= 1)
            return; // never delete the only block
        removeLeaf(cur);
        rebuildBlocks();
        if (ci > 0) {
            const QString prev = m_current.size() > ci - 1 ? m_current.at(ci - 1).object : QString();
            LogosResult lenRes = hub.text_len(space, prev);
            emit focusBlock(prev, lenRes.success ? lenRes.getValue<int>() : 0);
        } else if (!m_current.isEmpty()) {
            emit focusBlock(m_current.first().object, 0);
        }
        return;
    }

    if (ci == 0)
        return; // nothing above to merge into
    const BlockItem prev = m_current.at(ci - 1);

    LogosResult lenRes = hub.text_len(space, prev.object);
    if (!lenRes.success) {
        qWarning() << "hashweb: join: text_len() failed";
        return;
    }
    const int joinAt = lenRes.getValue<int>();
    hub.text_insert(space, prev.object, joinAt, curText);
    removeLeaf(cur);

    rebuildBlocks();
    emit focusBlock(prev.object, joinAt);
}

void HashwebBackend::setBlockKind(QString origin, QString kind)
{
    if (currentPage().isEmpty() || origin.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString key = QStringLiteral("blockKind:") + origin;
    if (kind.isEmpty())
        hub.del_key(spaceId(), currentPage(), key);
    else
        hub.put_string(spaceId(), currentPage(), key, kind);
    rebuildBlocks();
}

void HashwebBackend::markBlockRange(QString object, int start, int end, QString kind,
                                    QString value)
{
    if (object.isEmpty() || start < 0 || end <= start)
        return;
    LogosResult r = modules().hashweb_module.mark_range_closed(spaceId(), object, start, end,
                                                               kind, value);
    if (!r.success)
        qWarning() << "hashweb: mark_range failed:" << r.getError<QString>();
    rebuildBlocks();
}

void HashwebBackend::unmarkBlockRange(QString object, int start, int end, QString kind)
{
    if (object.isEmpty() || start < 0 || end <= start)
        return;
    LogosResult r = modules().hashweb_module.unmark_range(spaceId(), object, start, end, kind);
    if (!r.success)
        qWarning() << "hashweb: unmark_range failed:" << r.getError<QString>();
    rebuildBlocks();
}

// ── images (content-addressed artifacts) ────────────────────────────────────

namespace {
// kb.js sniffImage: magic-number probe.
QString sniffImageMime(const QByteArray& b)
{
    if (b.size() < 12)
        return {};
    const auto u = reinterpret_cast<const unsigned char*>(b.constData());
    if (u[0] == 0x89 && u[1] == 0x50) return QStringLiteral("image/png");
    if (u[0] == 0xff && u[1] == 0xd8) return QStringLiteral("image/jpeg");
    if (u[0] == 0x47 && u[1] == 0x49) return QStringLiteral("image/gif");
    if (u[8] == 0x57 && u[9] == 0x45 && u[10] == 0x42 && u[11] == 0x50)
        return QStringLiteral("image/webp");
    return {};
}
constexpr qsizetype kMaxImageBytes = 1'500'000; // whole snapshots sync: cap hard
} // namespace

void HashwebBackend::insertImage(QString object, QString fileUrl)
{
    const int ci = liveChildIndex(object);
    if (object.isEmpty() || ci < 0)
        return;
    const QUrl url(fileUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : fileUrl;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        emit error(QStringLiteral("cannot read ") + path);
        return;
    }
    const QByteArray bytes = f.readAll();
    if (sniffImageMime(bytes).isEmpty()) {
        emit error(QStringLiteral("not a recognized image (png/jpeg/gif/webp)"));
        return;
    }
    if (bytes.size() > kMaxImageBytes) {
        emit error(QStringLiteral("image too large (max 1.5 MB)"));
        return;
    }
    auto& hub = modules().hashweb_module;
    const QString artifactId = sval(hub.provide_bytes(spaceId(), bytes));
    if (artifactId.isEmpty()) {
        emit error(QStringLiteral("provide_bytes failed"));
        return;
    }
    // An image is always its own BLOCK: reuse `object` if it's empty,
    // otherwise a fresh sibling leaf right below it.
    QString target = object;
    LogosResult textRes = hub.text(spaceId(), object);
    if (!textRes.success || !textRes.getString().isEmpty()) {
        const BlockItem cur = m_current.at(ci);
        target = insertLeafAfter(cur.parentObj, &cur, randomOriginHex());
        if (target.isEmpty()) {
            rebuildBlocks();
            return;
        }
    }
    hub.seq_insert_ref(spaceId(), target, 0, artifactId);
    rebuildBlocks();
}

void HashwebBackend::requestImage(QString artifactId)
{
    if (artifactId.isEmpty())
        return;
    const QByteArray bytes =
        modules().hashweb_module.resolve_bytes(spaceId(), artifactId).toByteArray();
    const QString mime = sniffImageMime(bytes);
    if (bytes.isEmpty() || mime.isEmpty()) {
        emit imageData(artifactId, QString());
        return;
    }
    emit imageData(artifactId,
                   QStringLiteral("data:") + mime + QStringLiteral(";base64,")
                       + QString::fromLatin1(bytes.toBase64()));
}

// ── layout tree (split layouts + drag-drop; kb.js dropOnLeaf) ────────────────

namespace {
QJsonObject findNodeIn(const QJsonArray& nodes, const QString& origin)
{
    for (const QJsonValue& v : nodes) {
        const QJsonObject n = v.toObject();
        if (n.value(QStringLiteral("origin")).toString() == origin)
            return n;
        if (n.contains(QStringLiteral("children"))) {
            const QJsonObject hit =
                findNodeIn(n.value(QStringLiteral("children")).toArray(), origin);
            if (!hit.isEmpty())
                return hit;
        }
    }
    return {};
}

QJsonArray childrenIn(const QJsonArray& nodes, const QString& parentOrigin)
{
    for (const QJsonValue& v : nodes) {
        const QJsonObject n = v.toObject();
        if (n.value(QStringLiteral("origin")).toString() == parentOrigin)
            return n.value(QStringLiteral("children")).toArray();
        if (n.contains(QStringLiteral("children"))) {
            const QJsonArray hit =
                childrenIn(n.value(QStringLiteral("children")).toArray(), parentOrigin);
            if (!hit.isEmpty())
                return hit;
        }
    }
    return {};
}
} // namespace

QJsonObject HashwebBackend::findNode(const QString& origin) const
{
    return findNodeIn(m_tree, origin);
}

QJsonArray HashwebBackend::childrenOfNode(const QString& parentOrigin) const
{
    if (parentOrigin == m_bodyOrigin)
        return m_tree;
    return childrenIn(m_tree, parentOrigin);
}

bool HashwebBackend::insertChildAtIdx(const QString& parentOrigin, const QString& childOrigin,
                                      int idx)
{
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString parentObj = sval(hub.create_seq(space, parentOrigin));
    const QString childObj = sval(hub.create_seq(space, childOrigin));
    if (parentObj.isEmpty() || childObj.isEmpty())
        return false;
    // `idx` counts LIVE children; ghost atoms make raw positions diverge, so
    // translate through the membership walk's anchors (kb.js insertChildAt).
    const QJsonArray kids = childrenOfNode(parentOrigin);
    int raw = -1;
    if (idx >= 0 && idx < kids.size()) {
        const QString anchorAtom =
            kids.at(idx).toObject().value(QStringLiteral("atom")).toString();
        raw = ival(hub.seq_position_of(space, parentObj, anchorAtom));
    }
    if (raw < 0) {
        LogosResult lenRes = hub.text_len(space, parentObj);
        raw = lenRes.success ? lenRes.getValue<int>() : 0;
    }
    const QString atom = sval(hub.seq_insert_ref(space, parentObj, raw, childOrigin));
    if (atom.isEmpty())
        return false;
    hub.place_at(space, childObj, atom);
    return true;
}

QString HashwebBackend::makeContainer(const QStringList& childOrigins)
{
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString contOrigin = randomOriginHex();
    const QString contObj = sval(hub.create_seq(space, contOrigin));
    const QString mark = sval(hub.kb_container_mark());
    if (contObj.isEmpty() || mark.isEmpty())
        return {};
    hub.seq_insert_ref(space, contObj, 0, mark);
    for (int i = 0; i < childOrigins.size(); ++i) {
        const QString childObj = sval(hub.create_seq(space, childOrigins.at(i)));
        const QString atom =
            sval(hub.seq_insert_ref(space, contObj, 1 + i, childOrigins.at(i)));
        if (!childObj.isEmpty() && !atom.isEmpty())
            hub.place_at(space, childObj, atom); // the children now live here
    }
    return contOrigin;
}

void HashwebBackend::removeNode(const QJsonObject& node)
{
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString obj = node.value(QStringLiteral("obj")).toString();
    const QString parentObj = node.value(QStringLiteral("parent_obj")).toString();
    const QString atom = node.value(QStringLiteral("atom")).toString();
    if (obj.isEmpty())
        return;
    hub.place_at(space, obj, sval(hub.tombstone_id()));
    if (!parentObj.isEmpty() && !atom.isEmpty()) {
        const int raw = ival(hub.seq_position_of(space, parentObj, atom));
        if (raw >= 0)
            hub.text_remove(space, parentObj, raw, 1);
    }
}

// One normalize pass over the freshly-fetched tree; true if it fixed
// something (the caller loops — trees are tiny, kb walks the live store).
bool HashwebBackend::normalizePass(const QJsonArray& nodes)
{
    for (const QJsonValue& v : nodes) {
        const QJsonObject n = v.toObject();
        if (!n.contains(QStringLiteral("children")))
            continue;
        const QJsonArray kids = n.value(QStringLiteral("children")).toArray();
        if (kids.isEmpty()) {
            removeNode(n);
            return true;
        }
        if (kids.size() == 1) {
            // Unwrap: hoist the child to the container's slot (a move — a
            // fresh claim), then delete the container (kb.js normalizeTree).
            const QString parentOrigin =
                n.value(QStringLiteral("parent_origin")).toString();
            const QString contOrigin = n.value(QStringLiteral("origin")).toString();
            const QJsonArray siblings = childrenOfNode(parentOrigin);
            int ci = -1;
            for (int i = 0; i < siblings.size(); ++i)
                if (siblings.at(i).toObject().value(QStringLiteral("origin")).toString()
                    == contOrigin)
                    ci = i;
            insertChildAtIdx(parentOrigin,
                             kids.first().toObject().value(QStringLiteral("origin")).toString(),
                             ci < 0 ? siblings.size() : ci);
            removeNode(n);
            return true;
        }
        if (normalizePass(kids))
            return true;
    }
    return false;
}

void HashwebBackend::normalizeTree()
{
    // Empty containers are deleted; single-child wrappers unwrap. Bounded
    // loop: each pass fixes one container, trees are tiny.
    for (int pass = 0; pass < 16; ++pass) {
        // Refetch: earlier fixes moved membership.
        auto& hub = modules().hashweb_module;
        const QString json = sval(hub.kb_page_blocks(spaceId(), currentPage()));
        const QJsonObject pb = QJsonDocument::fromJson(json.toUtf8()).object();
        m_tree = pb.value(QStringLiteral("tree")).toArray();
        m_bodyOrigin = pb.value(QStringLiteral("body_origin")).toString();
        if (!normalizePass(m_tree))
            break;
    }
}

void HashwebBackend::dropBlock(QString srcOrigin, QString targetOrigin, QString dir)
{
    if (srcOrigin.isEmpty() || targetOrigin.isEmpty() || srcOrigin == targetOrigin)
        return;
    const QJsonObject target = findNode(targetOrigin);
    if (target.isEmpty()) {
        rebuildBlocks();
        return;
    }
    const QString tParent = target.value(QStringLiteral("parent_origin")).toString();
    const int tDepth = target.value(QStringLiteral("depth")).toInt();
    // Parallel to the target's parent axis = sibling insert; perpendicular =
    // wrap target in a new container, one level deeper — arranges the other
    // way (kb.js dropOnLeaf; body children sit at depth 1, parent axis V).
    const bool parentVertical = (tDepth - 1) % 2 == 0;
    const bool dirHorizontal =
        dir == QLatin1String("left") || dir == QLatin1String("right");
    const bool before = dir == QLatin1String("left") || dir == QLatin1String("top");
    const QJsonArray siblings = childrenOfNode(tParent);
    int ti = -1;
    for (int i = 0; i < siblings.size(); ++i)
        if (siblings.at(i).toObject().value(QStringLiteral("origin")).toString()
            == targetOrigin)
            ti = i;
    if (ti < 0) {
        normalizeTree();
        rebuildBlocks();
        return;
    }
    if (parentVertical != dirHorizontal) {
        // Parallel: a move is never a delete — the fresh claim supersedes,
        // the old atom stays a dead ghost.
        insertChildAtIdx(tParent, srcOrigin, ti + (before ? 0 : 1));
    } else {
        // Wrap: the container claims both children, then takes the target's
        // old slot. Order matters — ti predates the membership move.
        const QStringList kids = before ? QStringList{srcOrigin, targetOrigin}
                                        : QStringList{targetOrigin, srcOrigin};
        const QString cont = makeContainer(kids);
        if (!cont.isEmpty())
            insertChildAtIdx(tParent, cont, ti);
    }
    normalizeTree();
    rebuild();
    emit focusBlock(findNode(srcOrigin).value(QStringLiteral("obj")).toString(), 0);
}

void HashwebBackend::dropToEnd(QString srcOrigin)
{
    if (srcOrigin.isEmpty() || m_bodyOrigin.isEmpty())
        return;
    insertChildAtIdx(m_bodyOrigin, srcOrigin, m_tree.size());
    normalizeTree();
    rebuild();
}

void HashwebBackend::addBlockAtEnd()
{
    if (m_bodyOrigin.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString origin = randomOriginHex();
    hub.create_seq(spaceId(), origin);
    if (!insertChildAtIdx(m_bodyOrigin, origin, m_tree.size()))
        return;
    rebuildBlocks();
    emit focusBlock(sval(hub.seq_id(origin)), 0);
}

// ── comments (comment:<tag> marks; the tag is the thread's origin) ──────────

void HashwebBackend::addComment(QString object, int start, int end)
{
    if (object.isEmpty() || start < 0 || end <= start)
        return;
    auto& hub = modules().hashweb_module;
    const QString tag = randomOriginHex();
    hub.create_seq(spaceId(), tag); // the thread
    // Expanding-end mark, like kb.js applyInlineMark for comments.
    LogosResult r = hub.mark_range(spaceId(), object, start, end,
                                   QStringLiteral("comment:") + tag, QStringLiteral("on"));
    if (!r.success) {
        qWarning() << "hashweb: comment mark failed:" << r.getError<QString>();
        return;
    }
    rebuildBlocks();
}

void HashwebBackend::replyComment(QString tag, QString text)
{
    const QString trimmed = text.trimmed();
    if (tag.isEmpty() || trimmed.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString thread = sval(hub.create_seq(spaceId(), tag));
    if (thread.isEmpty())
        return;
    LogosResult lenRes = hub.text_len(spaceId(), thread);
    const int len = lenRes.success ? lenRes.getValue<int>() : 0;
    hub.text_insert(spaceId(), thread, len, trimmed + QStringLiteral("\n"));
    rebuildBlocks();
}

void HashwebBackend::resolveComment(QString tag)
{
    if (tag.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QJsonArray comments =
        QJsonDocument::fromJson(commentsJson().toUtf8()).array();
    for (const QJsonValue& v : comments) {
        const QJsonObject c = v.toObject();
        if (c.value(QStringLiteral("tag")).toString() != tag)
            continue;
        const QString kind = QStringLiteral("comment:") + tag;
        for (const QJsonValue& fv : c.value(QStringLiteral("fragments")).toArray()) {
            const QJsonObject f = fv.toObject();
            hub.unmark_range(spaceId(), f.value(QStringLiteral("obj")).toString(),
                             f.value(QStringLiteral("start")).toInt(),
                             f.value(QStringLiteral("end")).toInt(), kind);
        }
    }
    rebuildBlocks();
}

// ── tables (kb.js insertTableAt / objTableNode tools) ────────────────────────

void HashwebBackend::insertTable(QString object, int pos)
{
    if (object.isEmpty() || pos < 0)
        return;
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    // A table is a seq of row refs; a row a seq of cell refs; a cell a text
    // seq — a hashseq of hashseqs, embedded as a link atom.
    const QString tableOrigin = randomOriginHex();
    const QString tableObj = sval(hub.create_seq(space, tableOrigin));
    for (int r = 0; r < 2; ++r) {
        const QString rowOrigin = randomOriginHex();
        const QString rowObj = sval(hub.create_seq(space, rowOrigin));
        for (int c = 0; c < 2; ++c) {
            const QString cellOrigin = randomOriginHex();
            hub.create_seq(space, cellOrigin);
            hub.seq_insert_ref(space, rowObj, c, cellOrigin);
        }
        hub.seq_insert_ref(space, tableObj, r, rowOrigin);
    }
    LogosResult lenRes = hub.text_len(space, object);
    const int len = lenRes.success ? lenRes.getValue<int>() : 0;
    hub.seq_insert_ref(space, object, qMin(pos, len), tableOrigin);
    rebuildBlocks();
}

void HashwebBackend::tableAddRow(QString tableOrigin)
{
    if (tableOrigin.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString tableObj = sval(hub.create_seq(space, tableOrigin));
    LogosResult lenRes = hub.text_len(space, tableObj);
    const int rows = lenRes.success ? lenRes.getValue<int>() : 0;
    int cols = 1;
    if (rows > 0) {
        const QString firstRow = sval(hub.payload_at(space, tableObj, 0));
        if (!firstRow.isEmpty()) {
            LogosResult c = hub.text_len(space, sval(hub.create_seq(space, firstRow)));
            cols = qMax(1, c.success ? c.getValue<int>() : 1);
        }
    }
    const QString rowOrigin = randomOriginHex();
    const QString rowObj = sval(hub.create_seq(space, rowOrigin));
    for (int c = 0; c < cols; ++c) {
        const QString cellOrigin = randomOriginHex();
        hub.create_seq(space, cellOrigin);
        hub.seq_insert_ref(space, rowObj, c, cellOrigin);
    }
    hub.seq_insert_ref(space, tableObj, rows, rowOrigin);
    rebuildBlocks();
}

void HashwebBackend::tableAddCol(QString tableOrigin)
{
    if (tableOrigin.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const QString tableObj = sval(hub.create_seq(space, tableOrigin));
    LogosResult lenRes = hub.text_len(space, tableObj);
    const int rows = lenRes.success ? lenRes.getValue<int>() : 0;
    for (int r = 0; r < rows; ++r) {
        const QString rowOrigin = sval(hub.payload_at(space, tableObj, r));
        if (rowOrigin.isEmpty())
            continue;
        const QString rowObj = sval(hub.create_seq(space, rowOrigin));
        LogosResult c = hub.text_len(space, rowObj);
        const QString cellOrigin = randomOriginHex();
        hub.create_seq(space, cellOrigin);
        hub.seq_insert_ref(space, rowObj, c.success ? c.getValue<int>() : 0, cellOrigin);
    }
    rebuildBlocks();
}

// ── structure ────────────────────────────────────────────────────────────────

void HashwebBackend::insertPageLink(QString object, int pos, QString pageObj)
{
    if (object.isEmpty() || pos < 0 || pageObj.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    LogosResult lenRes = hub.text_len(spaceId(), object);
    const int len = lenRes.success ? lenRes.getValue<int>() : 0;
    // Payload = the page's OBJECT id: a pure name, navigates (kb.js).
    hub.seq_insert_ref(spaceId(), object, qMin(pos, len), pageObj);
    rebuildBlocks();
}

void HashwebBackend::moveBlock(QString object, int delta)
{
    const int ci = liveChildIndex(object);
    if (ci < 0 || delta == 0)
        return;
    const int ti = ci + delta;
    if (ti < 0 || ti >= m_current.size())
        return;
    const BlockItem cur = m_current.at(ci);
    const BlockItem target = m_current.at(ti);
    // Stage: moves within one parent only (columns are a later stage).
    if (target.parentObj != cur.parentObj)
        return;
    // kb.js: a move is insertChildAt — a fresh link + claim; the old atom
    // stays as a dead ghost, never tombstoned.
    const BlockItem* after = nullptr;
    if (delta > 0) {
        after = &target; // land right after the block we hop over
    } else if (ti > 0 && m_current.at(ti - 1).parentObj == cur.parentObj) {
        after = &m_current.at(ti - 1);
    } // else: raw slot 0 of the parent
    const QString newLeaf = insertLeafAfter(cur.parentObj, after, cur.origin);
    if (newLeaf.isEmpty()) {
        rebuildBlocks();
        return;
    }
    rebuildBlocks();
    emit focusBlock(object, 0);
}

void HashwebBackend::makeHeading(QString object)
{
    if (object.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString text = sval(hub.text(spaceId(), object));
    if (!text.startsWith(QLatin1String("# ")))
        hub.text_insert(spaceId(), object, 0, QStringLiteral("# "));
    rebuildBlocks();
}

void HashwebBackend::makeBlockRegion(QString object, QString kind)
{
    if (object.isEmpty() || kind.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    LogosResult lenRes = hub.text_len(spaceId(), object);
    int len = lenRes.success ? lenRes.getValue<int>() : 0;
    if (len == 0) {
        // The region mark needs content to anchor on (closed ends: text
        // typed after the region lands OUTSIDE it). Equations seed
        // editable placeholder TeX so /math renders something to click
        // into; code keeps kb.js's seed space.
        const QString seed = kind == QLatin1String("eqblock")
                                 ? QStringLiteral("a^2 + b^2 = c^2")
                                 : QStringLiteral(" ");
        hub.text_insert(spaceId(), object, 0, seed);
        len = seed.size();
    }
    hub.mark_range_closed(spaceId(), object, 0, len, kind, QString());
    rebuildBlocks();
}

void HashwebBackend::insertBlockAfter(QString object, QString act)
{
    const int ci = liveChildIndex(object);
    if (ci < 0)
        return;
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const BlockItem cur = m_current.at(ci);

    const QString newOrigin = randomOriginHex();
    const QString newLeaf = insertLeafAfter(cur.parentObj, &cur, newOrigin);
    if (newLeaf.isEmpty()) {
        rebuildBlocks();
        return;
    }
    if (act == QLatin1String("bullet") || act == QLatin1String("number")) {
        hub.put_string(space, currentPage(), QStringLiteral("blockKind:") + newOrigin, act);
    } else if (act == QLatin1String("codeblock") || act == QLatin1String("eqblock")) {
        rebuildBlocks();
        makeBlockRegion(newLeaf, act);
    } else if (act == QLatin1String("heading")) {
        hub.text_insert(space, newLeaf, 0, QStringLiteral("# "));
    } else if (act == QLatin1String("table")) {
        rebuildBlocks(); // insertTable resolves the block via m_current
        insertTable(newLeaf, 0);
    }
    rebuildBlocks();
    emit focusBlock(newLeaf, act == QLatin1String("heading") ? 2 : 0);
}

// ── markdown paste ───────────────────────────────────────────────────────────

namespace {

struct MdMark {
    int start;
    int end;
    QString kind;
};

struct MdBlock {
    QString text;
    QString listKind;   // "bullet" / "number" / ""
    QString region;     // "codeblock" / "eqblock" / ""
    QString lang;       // fence info string, for codeblock regions
    QVector<MdMark> marks;
};

// Inline markdown: **bold**, *italic* / _italic_, `code`, $math$ become
// plain text + closed marks. Single pass, innermost-first is not needed —
// delimiters don't nest in practice; unterminated delimiters stay literal.
void parseInlineMd(const QString& in, QString* outText, QVector<MdMark>* outMarks)
{
    QString out;
    out.reserve(in.size());
    int i = 0;
    const int n = in.size();
    auto tryDelim = [&](const QString& delim, const QString& kind) -> bool {
        if (!QStringView(in).mid(i).startsWith(delim))
            return false;
        const int close = in.indexOf(delim, i + delim.size());
        if (close < 0 || close == i + delim.size())
            return false; // unterminated or empty: literal
        const QString inner = in.mid(i + delim.size(), close - i - delim.size());
        outMarks->append({int(out.size()), int(out.size() + inner.size()), kind});
        out += inner;
        i = close + delim.size();
        return true;
    };
    while (i < n) {
        if (tryDelim(QStringLiteral("**"), QStringLiteral("bold")))
            continue;
        if (tryDelim(QStringLiteral("`"), QStringLiteral("code")))
            continue;
        if (tryDelim(QStringLiteral("$"), QStringLiteral("math")))
            continue;
        if (tryDelim(QStringLiteral("*"), QStringLiteral("italic")))
            continue;
        if (tryDelim(QStringLiteral("_"), QStringLiteral("italic")))
            continue;
        // [text](url): keep the text, drop the url (no link entity yet).
        if (in[i] == QLatin1Char('[')) {
            const int closeBracket = in.indexOf(QLatin1Char(']'), i + 1);
            if (closeBracket > 0 && closeBracket + 1 < n
                && in[closeBracket + 1] == QLatin1Char('(')) {
                const int closeParen = in.indexOf(QLatin1Char(')'), closeBracket + 2);
                if (closeParen > 0) {
                    out += in.mid(i + 1, closeBracket - i - 1);
                    i = closeParen + 1;
                    continue;
                }
            }
        }
        out += in[i];
        i++;
    }
    *outText = out;
}

// Split a markdown document into kb blocks: fenced code, headings, list
// items (one block each — kb lists are per-item blockKind registers),
// display math, and blank-line-separated paragraphs.
QVector<MdBlock> parseMarkdown(const QString& md)
{
    QVector<MdBlock> blocks;
    QStringList para; // pending paragraph lines
    auto flushPara = [&]() {
        if (para.isEmpty())
            return;
        MdBlock b;
        parseInlineMd(para.join(QLatin1Char(' ')), &b.text, &b.marks);
        blocks.append(b);
        para.clear();
    };

    const QStringList lines = md.split(QLatin1Char('\n'));
    for (int li = 0; li < lines.size(); li++) {
        const QString& line = lines[li];
        const QString trimmed = line.trimmed();

        if (trimmed.startsWith(QLatin1String("```"))) {
            flushPara();
            MdBlock b;
            b.region = QStringLiteral("codeblock");
            b.lang = trimmed.mid(3).trimmed();
            QStringList code;
            for (li++; li < lines.size() && !lines[li].trimmed().startsWith(QLatin1String("```")); li++)
                code << lines[li];
            b.text = code.join(QLatin1Char('\n'));
            if (!b.text.isEmpty())
                blocks.append(b);
            continue;
        }
        if (trimmed.startsWith(QLatin1String("$$"))) {
            flushPara();
            MdBlock b;
            b.region = QStringLiteral("eqblock");
            // single-line $$...$$ or a fenced block until the closing $$
            QString tex = trimmed.mid(2);
            if (tex.endsWith(QLatin1String("$$"))) {
                tex.chop(2);
            } else {
                QStringList texLines{tex};
                for (li++; li < lines.size() && !lines[li].trimmed().endsWith(QLatin1String("$$")); li++)
                    texLines << lines[li];
                if (li < lines.size()) {
                    QString last = lines[li].trimmed();
                    last.chop(2);
                    texLines << last;
                }
                tex = texLines.join(QLatin1Char(' '));
            }
            b.text = tex.trimmed();
            if (!b.text.isEmpty())
                blocks.append(b);
            continue;
        }
        if (trimmed.isEmpty()) {
            flushPara();
            continue;
        }

        static const QRegularExpression headingRe(QStringLiteral("^(#{1,6})\\s+(.*)$"));
        const auto hm = headingRe.match(trimmed);
        if (hm.hasMatch()) {
            flushPara();
            MdBlock b;
            parseInlineMd(hm.captured(2), &b.text, &b.marks);
            // kb headings are the "# " prefix on a bare block; offsets shift.
            for (auto& m : b.marks) { m.start += 2; m.end += 2; }
            b.text.prepend(QStringLiteral("# "));
            blocks.append(b);
            continue;
        }

        static const QRegularExpression bulletRe(QStringLiteral("^[-*+]\\s+(.*)$"));
        static const QRegularExpression numberRe(QStringLiteral("^\\d{1,3}[.)]\\s+(.*)$"));
        const auto bm = bulletRe.match(trimmed);
        const auto nm = numberRe.match(trimmed);
        if (bm.hasMatch() || nm.hasMatch()) {
            flushPara();
            MdBlock b;
            b.listKind = bm.hasMatch() ? QStringLiteral("bullet") : QStringLiteral("number");
            parseInlineMd((bm.hasMatch() ? bm : nm).captured(1), &b.text, &b.marks);
            blocks.append(b);
            continue;
        }

        para << trimmed;
    }
    flushPara();
    return blocks;
}

} // namespace

void HashwebBackend::pasteMarkdown(QString object, int pos, QString md)
{
    const int ci = liveChildIndex(object);
    if (ci < 0)
        return;
    const QVector<MdBlock> blocks = parseMarkdown(md);
    if (blocks.isEmpty())
        return;
    auto& hub = modules().hashweb_module;
    const QString space = spaceId();
    const BlockItem cur = m_current.at(ci);

    // Shape one hub block from an MdBlock (text + kind + region + marks).
    auto shape = [&](const QString& leaf, const QString& origin, const MdBlock& b) {
        if (!b.text.isEmpty())
            hub.text_insert(space, leaf, 0, b.text);
        if (!b.listKind.isEmpty())
            hub.put_string(space, currentPage(),
                           QStringLiteral("blockKind:") + origin, b.listKind);
        if (!b.region.isEmpty() && !b.text.isEmpty()) {
            hub.mark_range_closed(space, leaf, 0, b.text.size(), b.region, QString());
            if (b.region == QLatin1String("codeblock") && !b.lang.isEmpty())
                hub.put_string(space, currentPage(),
                               QStringLiteral("blockKind:") + origin,
                               QStringLiteral("lang:") + b.lang);
        }
        for (const MdMark& m : b.marks)
            hub.mark_range_closed(space, leaf, m.start, m.end, m.kind, QString());
    };

    int first = 0;
    const QString curText = sval(hub.text(space, object));
    if (curText.trimmed().isEmpty() && !curText.contains(QChar(0xFFFC))) {
        // Empty block: the first parsed block takes its place.
        if (!curText.isEmpty())
            hub.text_remove(space, object, 0, curText.size());
        shape(object, cur.origin, blocks[0]);
        first = 1;
    } else {
        Q_UNUSED(pos); // non-empty target: parsed blocks land after it whole
    }

    // Chain the rest as fresh siblings, each anchored on the previous atom.
    QString prevAtom = cur.atom;
    const QString parentObj = cur.parentObj;
    QString lastLeaf = object;
    for (int i = first; i < blocks.size(); i++) {
        const QString newOrigin = randomOriginHex();
        hub.create_seq(space, newOrigin);
        const QString newLeaf = sval(hub.seq_id(newOrigin));
        if (newLeaf.isEmpty())
            break;
        const int p = ival(hub.seq_position_of(space, parentObj, prevAtom));
        const QString atom = sval(hub.seq_insert_ref(space, parentObj,
                                                     p < 0 ? 0 : p + 1, newOrigin));
        if (atom.isEmpty())
            break;
        hub.place_at(space, newLeaf, atom);
        shape(newLeaf, newOrigin, blocks[i]);
        prevAtom = atom;
        lastLeaf = newLeaf;
    }

    rebuildBlocks();
    emit focusBlock(lastLeaf, 0);
}

// ── presence (ephemeral, delivery-layer) ─────────────────────────────────────

void HashwebBackend::announcePresence(QString page, QString block, int caret)
{
    if (spaceId().isEmpty() || !m_moduleInitialised)
        return;
    // Identity is announced with every packet: peers need no directory.
    // The name is the user's chosen HANDLE, never an OS identity — presence
    // is broadcast to the whole space and must not dox anyone.
    static const QStringList kPresencePalette = {
        QStringLiteral("#e0564f"), QStringLiteral("#4ec97b"), QStringLiteral("#d8a03d"),
        QStringLiteral("#6a9fe8"), QStringLiteral("#c586c0"), QStringLiteral("#3fbfb0"),
    };
    uint h = qHash(m_sid);
    QJsonObject state{
        { QStringLiteral("sid"), m_sid },
        { QStringLiteral("name"),
          m_handle.isEmpty() ? QStringLiteral("anon") : m_handle },
        { QStringLiteral("color"), kPresencePalette[h % kPresencePalette.size()] },
        { QStringLiteral("page"), page },
        { QStringLiteral("block"), block },
        { QStringLiteral("caret"), caret },
    };
    m_lastPresence = QString::fromUtf8(
        QJsonDocument(state).toJson(QJsonDocument::Compact));
    modules().hashweb_module.announce_presence(spaceId(), m_lastPresence);
}

void HashwebBackend::setHandle(QString handle)
{
    m_handle = handle.trimmed().left(24);
    // A heartbeat replays m_lastPresence verbatim — refresh the baked-in
    // name so a rename shows up without waiting for a caret move.
    if (!m_lastPresence.isEmpty()) {
        QJsonObject state =
            QJsonDocument::fromJson(m_lastPresence.toUtf8()).object();
        state[QStringLiteral("name")] =
            m_handle.isEmpty() ? QStringLiteral("anon") : m_handle;
        m_lastPresence = QString::fromUtf8(
            QJsonDocument(state).toJson(QJsonDocument::Compact));
        if (!spaceId().isEmpty() && m_moduleInitialised)
            modules().hashweb_module.announce_presence(spaceId(), m_lastPresence);
    }
}

void HashwebBackend::presenceTick()
{
    if (spaceId().isEmpty() || !m_moduleInitialised) {
        if (presenceJson() != QLatin1String("[]"))
            setPresenceJson(QStringLiteral("[]"));
        return;
    }
    auto& hub = modules().hashweb_module;
    // Heartbeat: keep our last state alive past the hub's presence TTL.
    if (!m_lastPresence.isEmpty())
        hub.announce_presence(spaceId(), m_lastPresence);
    // Peers, minus our own echo.
    const QString raw = sval(hub.presence_json(spaceId()));
    QJsonArray peers;
    for (const QJsonValue& v : QJsonDocument::fromJson(raw.toUtf8()).array()) {
        if (v.toObject().value(QStringLiteral("sid")).toString() != m_sid)
            peers.append(v);
    }
    setPresenceJson(QString::fromUtf8(
        QJsonDocument(peers).toJson(QJsonDocument::Compact)));
}

// ── network toggle ───────────────────────────────────────────────────────────

void HashwebBackend::toggleOffline(bool offline)
{
    if (!isContextReady() || !m_moduleInitialised)
        return;
    if (!modules().hashweb_module.set_offline(offline).success)
        emit error(QStringLiteral("set_offline failed"));
}

void HashwebBackend::deferToEventLoop(std::function<void()> work)
{
    QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection);
}
