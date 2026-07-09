import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Logos.HashwebBackend 1.0
import Logos.HashwebKb 1.0
import "MathRender.js" as MathJS

// HashWeb knowledge base (the kb.js editor, Qt-native). The backend is a
// thin client over hashweb_module; the document model is kb.js's: a
// workspace kv, pages on containment registers, per-page layout TREES of
// leaf blocks (containers alternate orientation by depth — the split
// layout), blockKind registers for lists, marks for formatting. The layout
// tree replicates as one JSON property that changes ONLY structurally;
// per-keystroke content rides the blockUpdated signal so typing never
// rebuilds delegates. Structural keys and drag-drop call the backend,
// which composes the hub's primitives exactly as kb.js does.
Rectangle {
    id: root
    color: root.th.bg

    // ── theme ─────────────────────────────────────────────────────────────
    // Every color in the view routes through `th`, so the whole UI reskins
    // on this one bool. INK is the original dark scheme; CREAM is warm
    // paper. Values are plain strings: rich-text <font> tags and Canvas
    // fill styles consume them as-is.
    property bool cream: false
    readonly property var th: cream ? themeCream : themeInk

    // ── handle ────────────────────────────────────────────────────────────
    // The alias peers see in presence. Deliberately NEVER derived from the
    // OS account — presence broadcasts to the whole space, and joining a
    // space must not reveal who you are. Defaults to a rolled alias.
    property string handle: ""
    function rollHandle() {
        var adjectives = [
            "plucky", "velvet", "mossy", "amber", "quiet", "zesty", "dapper",
            "breezy", "cosmic", "dusty", "electric", "fuzzy", "gentle",
            "hasty", "ivory", "jolly", "keen", "lunar", "minty", "nimble",
            "opal", "prickly", "rusty", "silky", "tidal", "umber", "vivid",
            "wobbly", "young", "zigzag", "arctic", "bouncy", "crisp",
            "drowsy", "eager", "foggy", "glossy", "humble"]
        var animals = [
            "otter", "lynx", "heron", "badger", "quokka", "newt", "marmot",
            "puffin", "gecko", "wombat", "ibis", "jackal", "kiwi", "lemur",
            "mole", "narwhal", "ocelot", "pika", "raven", "seal", "tapir",
            "urchin", "vole", "walrus", "yak", "zebra", "axolotl", "bison",
            "crane", "dingo", "egret", "ferret", "gull", "hare"]
        return adjectives[Math.floor(Math.random() * adjectives.length)]
               + "-" + animals[Math.floor(Math.random() * animals.length)]
    }
    onHandleChanged: if (d.backend) d.backend.setHandle(handle)
    Component.onCompleted: {
        if (handle === "")
            handle = rollHandle()
        if (d.backend)
            d.backend.setHandle(handle)
    }

    Settings {
        category: "hashweb-ui"
        property alias creamTheme: root.cream
        property alias handle: root.handle
    }
    readonly property var themeInk: ({
        bg: "#0f1216", panel: "#12161b", header: "#151a20", card: "#161c22",
        hover: "#1d242d", border: "#232b34",
        sel: "#2d4a63", selDown: "#24557a", off: "#1a2027",
        text: "#e6edf3", textMid: "#c7d0dc", textSoft: "#aeb7c4",
        dim: "#8a94a6", dimmer: "#5a6472", faint: "#3d4550", handle: "#2a323d",
        green: "#4ec97b", greenBg: "#1f3d2b",
        red: "#e0564f", redBg: "#5a2320", redText: "#ffd7d3",
        amber: "#d8a03d", amberBg: "#2a2314", amberText: "#e8d5a0",
        math: "#9ecbff", codeInk: "#7ee3a0", codeBlockInk: "#a8d0f0",
        kw: "#c586c0", str: "#7ee3a0", cmt: "#5a6472", num: "#d8a03d"
    })
    readonly property var themeCream: ({
        bg: "#f7f2e6", panel: "#f1ebdc", header: "#ede6d3", card: "#ece5d2",
        hover: "#e4dcc6", border: "#d6ccb2",
        sel: "#b7cfdf", selDown: "#a2c3d6", off: "#e7dfcb",
        text: "#2d2a22", textMid: "#4c463a", textSoft: "#5f584a",
        dim: "#7c745f", dimmer: "#9b927c", faint: "#b9af96", handle: "#d0c6ab",
        green: "#2e7d4f", greenBg: "#dcead4",
        red: "#b23a31", redBg: "#f2d5d0", redText: "#7c2620",
        amber: "#9c6b1a", amberBg: "#f0e6c8", amberText: "#6d4d10",
        math: "#345a9e", codeInk: "#2f6b45", codeBlockInk: "#34567e",
        kw: "#7a3f8f", str: "#2f6b45", cmt: "#9b927c", num: "#9c6b1a"
    })
    // Prose reads in the platform's text face; monospace is reserved for
    // code (panels + inline `code` spans, applied by the highlighter).
    readonly property string proseFont: Qt.application.font.family

    // The QtRO replica, from the host's `logos` context object.
    QtObject {
        id: d
        readonly property var backend: typeof logos !== "undefined" && logos
                                       ? logos.module("hashweb_ui") : null
        readonly property var pages: typeof logos !== "undefined" && logos
                                     ? logos.model("hashweb_ui", "pageModel") : null
    }

    readonly property int hubStatus: d.backend ? d.backend.hubStatus : HashwebBackend.Stopped
    readonly property string joinedSpace: d.backend ? d.backend.spaceId : ""
    readonly property string currentPage: d.backend ? d.backend.currentPage : ""
    readonly property var comments: JSON.parse((d.backend && d.backend.commentsJson) || "[]")

    // ── presence (live peers; ephemeral, delivery-layer) ──────────────────
    readonly property var presence: JSON.parse((d.backend && d.backend.presenceJson) || "[]")
    property string announceBlock: ""
    property int announceCaret: 0
    function queueAnnounce(block, caret) {
        announceBlock = block
        announceCaret = caret
        announceTimer.restart()
    }
    Timer {
        id: announceTimer
        interval: 400
        onTriggered: if (d.backend && root.joinedSpace !== "")
                         d.backend.announcePresence(root.currentPage,
                                                    root.announceBlock,
                                                    root.announceCaret)
    }
    readonly property var titleConflicts: JSON.parse((d.backend && d.backend.titleConflictJson) || "[]")

    // ── the layout tree (structure-only updates) ──────────────────────────
    readonly property var layoutTree: JSON.parse((d.backend && d.backend.layoutJson) || "[]")

    // Per-block content authority: blockUpdated pushes text/spans/embeds
    // for live blocks; a structural update carries fresh content in the
    // tree itself, so the authority resets with it.
    property var blockAuthority: ({})
    property int authorityRev: 0
    property var pendingFocusRestore: null
    onLayoutTreeChanged: {
        blockAuthority = {}
        authorityRev++
        // The re-render may hop the focused editor through the stash;
        // restore focus and caret once the new tree settles.
        for (var o in leafPool) {
            var l = leafPool[o]
            if (l && l.editorFocused) {
                pendingFocusRestore = { obj: l.object, caret: l.caretPos() }
                break
            }
        }
        if (pendingFocusRestore)
            Qt.callLater(function() {
                if (root.pendingFocusRestore) {
                    root.focusBlockAt(root.pendingFocusRestore.obj,
                                      root.pendingFocusRestore.caret)
                    root.pendingFocusRestore = null
                }
            })
    }

    // Live leaf editors, keyed by block object id (delegates register).
    property var leafRegistry: ({})

    // kb.js prevLeaves, Qt edition: leaf items PERSIST across structural
    // re-renders, keyed by origin — slots adopt pooled editors instead of
    // recreating them, so focus, caret, and in-flight keystrokes survive
    // splits/joins/kind changes (delegate recreation was eating keystrokes
    // typed during the QtRO round trip).
    property var leafPool: ({})
    // visible:false would strip focus from a stashed editor mid-reparent;
    // a zero-size clipped host keeps focus alive through the hop.
    Item { id: leafStash; width: 0; height: 0; clip: true }

    function adoptLeaf(node, slot) {
        var l = leafPool[node.origin]
        if (!l) {
            l = leafComp.createObject(leafStash, { node: node })
            leafPool[node.origin] = l
        } else {
            l.node = node
        }
        l.parent = slot
        l.width = slot.width
        return l
    }
    function stashLeaf(l) {
        l.parent = leafStash
    }
    // Drop pool entries whose origin left the document.
    onFlatLeavesChanged: {
        var live = {}
        for (var i = 0; i < flatLeaves.length; i++)
            live[flatLeaves[i].origin] = true
        for (var o in leafPool) {
            if (!live[o]) {
                var l = leafPool[o]
                delete leafPool[o]
                if (l) l.destroy()
            }
        }
    }
    // Document-order leaves, for cross-block caret flow.
    readonly property var flatLeaves: {
        var out = []
        function walk(nodes) {
            for (var i = 0; i < nodes.length; i++) {
                if (nodes[i].t === "cont") walk(nodes[i].children)
                else out.push(nodes[i])
            }
        }
        walk(layoutTree)
        return out
    }

    function focusBlockAt(object, caret) {
        var it = leafRegistry[object]
        if (!it) return false
        it.focusAt(caret)
        return true
    }
    function focusNeighbor(object, delta, caret) {
        for (var i = 0; i < flatLeaves.length; i++) {
            if (flatLeaves[i].obj === object) {
                var t = flatLeaves[i + delta]
                if (!t) return false
                return focusBlockAt(t.obj, caret)
            }
        }
        return false
    }

    // The block being dragged by its handle ("" = no drag in flight).
    property string dragOrigin: ""
    // The page being dragged in the sidebar ("" = none).
    property string pageDragObj: ""
    // Current drop-hover target: {kind:"leaf",origin,edge} or
    // {kind:"page",obj,zone} — drives the indicator lines. QML's Drag/
    // DropArea machinery never saw our programmatically-moved ghost, so
    // targeting is manual hit-testing over the registries instead.
    property var hoverDrop: null
    property var pageRowRegistry: ({})

    function leafUnder(x, y) {
        for (var o in leafPool) {
            var l = leafPool[o]
            if (!l || !l.parent || l.parent === leafStash)
                continue
            var pt = l.mapFromItem(root, x, y)
            if (pt.x >= 0 && pt.y >= 0 && pt.x <= l.width && pt.y <= l.height)
                return l
        }
        return null
    }
    function leafEdgeFor(l, x, y) {
        // Rows are wide and short: nearest-edge math makes the sides
        // unreachable. Outer fifths = side drop (split), middle = above/
        // below by half.
        var pt = l.mapFromItem(root, x, y)
        if (pt.x < l.width * 0.2) return "left"
        if (pt.x > l.width * 0.8) return "right"
        return pt.y < l.height / 2 ? "top" : "bottom"
    }
    function updateBlockDragHover(x, y) {
        var l = leafUnder(x, y)
        hoverDrop = (l && l.origin !== dragOrigin)
                    ? { kind: "leaf", origin: l.origin, edge: leafEdgeFor(l, x, y) }
                    : null
    }
    function finishBlockDrag(x, y) {
        var src = dragOrigin
        dragOrigin = ""
        hoverDrop = null
        if (src === "" || !d.backend)
            return
        var l = leafUnder(x, y)
        if (l && l.origin !== src) {
            d.backend.dropBlock(src, l.origin, leafEdgeFor(l, x, y))
            return
        }
        // below the last block, inside the document column → append
        var pt = blocksHost.mapFromItem(root, x, y)
        if (pt.x >= -40 && pt.x <= blocksHost.width + 40 && pt.y > blocksHost.height)
            d.backend.dropToEnd(src)
    }
    function pageRowUnder(x, y) {
        for (var o in pageRowRegistry) {
            var r = pageRowRegistry[o]
            if (!r)
                continue
            var pt = r.mapFromItem(root, x, y)
            if (pt.x >= 0 && pt.y >= 0 && pt.x <= r.width && pt.y <= r.height)
                return r
        }
        return null
    }
    function pageZoneFor(r, x, y) {
        var pt = r.mapFromItem(root, x, y)
        if (pt.y < r.height * 0.3) return "above"
        if (pt.y > r.height * 0.7) return "below"
        return "into"
    }
    function updatePageDragHover(x, y) {
        var r = pageRowUnder(x, y)
        hoverDrop = (r && r.pageObj !== pageDragObj)
                    ? { kind: "page", obj: r.pageObj, zone: pageZoneFor(r, x, y) }
                    : null
    }
    function finishPageDrag(x, y) {
        var src = pageDragObj
        pageDragObj = ""
        hoverDrop = null
        if (src === "" || !d.backend)
            return
        var r = pageRowUnder(x, y)
        if (r && r.pageObj !== src)
            d.backend.dropPage(src, r.pageObj, pageZoneFor(r, x, y))
    }

    function statusColor(status) {
        switch (status) {
        case HashwebBackend.Online:  return root.th.green
        case HashwebBackend.Error:   return root.th.red
        case HashwebBackend.Offline: return root.th.dim
        default:                     return root.th.amber
        }
    }

    // Content-addressed image cache: an artifact's bytes can never change
    // under its id, so entries live forever.
    property var imageCache: ({})
    property int imageCacheRev: 0

    // Rendered math, keyed by theme + ("D:"|"I:") + tex → {url,w,h}. The
    // theme tag keeps the two inks in separate cache slots so flipping the
    // theme re-renders rather than serving the other palette's pixels.
    property var mathCache: ({})
    property int mathCacheRev: 0
    property var mathPending: ({})
    property var mathQueue: []
    function mathKey(tex, display) {
        return (cream ? "L:" : "K:") + (display ? "D:" : "I:") + tex.trim()
    }
    // Called from inside styledHtml BINDINGS — must never synchronously
    // touch anything a binding depends on (that recursed the binding until
    // the stack blew). Everything defers through the zero-interval timer.
    function requestMath(tex, display) {
        var key = mathKey(tex, display)
        if (tex.trim() === "" || mathCache[key] || mathPending[key])
            return
        mathPending[key] = true
        mathQueue.push({ key: key, tex: tex.trim(), display: display, ink: th.math })
        mathKickTimer.start()
    }
    Timer {
        id: mathKickTimer
        interval: 0
        onTriggered: mathCanvas.kick()
    }

    // Offscreen renderer: the plugin's host process has no QGuiApplication
    // (no fonts for QPainter), but the QML scene does — a Canvas measures,
    // draws, and exports each formula as a data URL, one at a time.
    Canvas {
        id: mathCanvas
        x: -100000
        width: 4
        height: 4
        renderStrategy: Canvas.Immediate
        renderTarget: Canvas.Image
        property var job: null
        property var jobLayout: null
        function kick() {
            if (job || root.mathQueue.length === 0 || !available)
                return
            job = root.mathQueue.shift()
            var ctx = getContext("2d")
            if (!ctx) {
                root.mathQueue.unshift(job)
                job = null
                return
            }
            try {
                var size = job.display ? 20 : 15
                jobLayout = MathJS.layout(ctx, job.tex, size * 2, job.display)
            } catch (e) {
                console.warn("mathrender: layout failed for", job.tex, ":", e)
                delete root.mathPending[job.key]
                job = null
                jobLayout = null
                mathKickTimer.start()
                return
            }
            width = jobLayout.w
            height = jobLayout.h
            requestPaint()
        }
        onPaint: {
            if (!job || !jobLayout)
                return
            var ctx = getContext("2d")
            ctx.save()
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = job.ink
            jobLayout.draw(ctx)
            ctx.restore()
        }
        onPainted: {
            if (!job)
                return
            var key = job.key
            var w = Math.ceil(jobLayout.w / 2)
            var h = Math.ceil(jobLayout.h / 2)
            var asc = jobLayout.asc / 2
            job = null
            jobLayout = null
            // toDataURL() from the paint path corrupts the context heap
            // (QQuickContext2D::flush double-free) — grab the item and
            // hand the QImage to the in-memory provider. No filesystem:
            // Basecamp's plugin sandbox blocks file:// outside the plugin
            // roots, and image:// is exempt from the network switch too.
            var hkey = "v3:" + key
            var hash = 5381
            for (var ci = 0; ci < hkey.length; ci++)
                hash = ((hash * 33) ^ hkey.charCodeAt(ci)) >>> 0
            var id = hash.toString(16)
            mathCanvas.grabToImage(function(result) {
                var url = KbMath.saveImage(id, result.image,
                                           Qt.resolvedUrl("."))
                Qt.callLater(function() {
                    delete root.mathPending[key]
                    root.mathCache[key] = { url: url, w: w, h: h, asc: asc }
                    root.mathCacheRev++
                    mathKickTimer.start()
                })
            })
        }
    }

    // Tool button with legible dark-theme colors (Qt's default flat-button
    // palette renders near-invisible on this background).
    component KbTool: Button {
        id: kbTool
        property color tint: root.th.dim
        flat: true
        font.family: "Menlo"
        font.pixelSize: 10
        font.letterSpacing: 1
        leftPadding: 10; rightPadding: 10; topPadding: 5; bottomPadding: 5
        contentItem: Label {
            text: kbTool.text
            color: kbTool.hovered
                   ? (root.cream ? Qt.darker(kbTool.tint, 1.25)
                                 : Qt.lighter(kbTool.tint, 1.4))
                   : kbTool.tint
            font: kbTool.font
            horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle {
            radius: 5
            color: kbTool.hovered ? root.th.hover : "transparent"
            border.color: kbTool.hovered ? root.th.border : "transparent"
        }
    }

    FileDialog {
        id: imageDialog
        property string targetBlock: ""
        nameFilters: ["Images (*.png *.jpg *.jpeg *.gif *.webp)"]
        onAccepted: if (d.backend && targetBlock !== "")
                        d.backend.insertImage(targetBlock, selectedFile.toString())
    }

    // ── code-panel language menu ──────────────────────────────────────────
    Popup {
        id: langMenu
        property var target: null
        padding: 4
        background: Rectangle { color: root.th.card; radius: 6; border.color: root.th.border }
        function openFor(leaf, anchor) {
            target = leaf
            parent = anchor
            x = anchor.width - 110
            y = anchor.height + 2
            open()
        }
        contentItem: Column {
            Repeater {
                model: ["plain", "python", "javascript", "rust", "c"]
                delegate: Rectangle {
                    required property var modelData
                    width: 110; height: 22; radius: 4
                    color: langRowArea.containsMouse ? root.th.hover : "transparent"
                    Label {
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: parent.modelData
                        color: root.th.text
                        font.family: "Menlo"
                        font.pixelSize: 11
                    }
                    MouseArea {
                        id: langRowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (d.backend && langMenu.target)
                                d.backend.setBlockKind(
                                    langMenu.target.origin,
                                    parent.modelData === "plain"
                                        ? "" : "lang:" + parent.modelData)
                            langMenu.close()
                        }
                    }
                }
            }
        }
    }

    // ── slash menu (kb.js SLASH_ITEMS) ────────────────────────────────────
    Popup {
        id: slashMenu
        property var target: null   // the leaf that typed "/"
        property var editor: null
        property string query: ""
        property int active: 0
        readonly property var allItems: [
            { key: "H",  label: "Heading",       act: "heading" },
            { key: "<>", label: "Code block",    act: "codeblock" },
            { key: "∑",  label: "Math equation", act: "eqblock" },
            { key: "•",  label: "Bulleted list", act: "bullet" },
            { key: "1.", label: "Numbered list", act: "number" },
            { key: "#",  label: "Table",         act: "table" },
            { key: "I",  label: "Image",         act: "image" },
            { key: "P",  label: "Page link",     act: "pagelink" },
        ]
        readonly property var filtered: allItems.filter(function(it) {
            return it.label.toLowerCase().indexOf(query.toLowerCase()) !== -1
        })
        onFilteredChanged: { active = 0; if (visible && filtered.length === 0) close() }
        padding: 4
        background: Rectangle { color: root.th.card; radius: 6; border.color: root.th.border }

        // Where the "/" was typed: the query runs from here to the caret,
        // and inline commands insert at this offset.
        property int startPos: 0
        function openFor(leaf, ed, at) {
            target = leaf
            editor = ed
            startPos = at === undefined ? 0 : at
            query = ""
            active = 0
            parent = ed
            y = ed.cursorRectangle.y + ed.cursorRectangle.height + 4
            x = ed.cursorRectangle.x
            open()
        }
        function runActive() { if (filtered.length > 0) run(filtered[active]) }
        function run(it) {
            var del = target
            var ed = editor
            var at = startPos
            close()
            if (!del || !d.backend)
                return
            // Strip the typed "/query" (kb.js runSlash).
            var strip = query.length + 1
            d.backend.applyBlockEdit(del.object, at, strip, "")
            del.applyingRemote = true
            ed.text = ed.text.substring(0, at) + ed.text.substring(at + strip)
            del.shadow = ed.text
            ed.cursorPosition = at
            del.applyingRemote = false
            var empty = ed.text.length === 0
            switch (it.act) {
            case "heading":
            case "codeblock":
            case "eqblock":
            case "bullet":
            case "number":
            case "table":
                if (empty) {
                    if (it.act === "heading") d.backend.makeHeading(del.object)
                    else if (it.act === "table") d.backend.insertTable(del.object, 0)
                    else if (it.act === "bullet" || it.act === "number")
                        d.backend.setBlockKind(del.origin, it.act)
                    else d.backend.makeBlockRegion(del.object, it.act)
                } else {
                    // Block commands from mid-text: a fresh, pre-shaped
                    // sibling right after this block (inline commands
                    // like the page link stay at the slash).
                    d.backend.insertBlockAfter(del.object, it.act)
                }
                break
            case "image":
                imageDialog.targetBlock = del.object
                imageDialog.open()
                break
            case "pagelink":
                pagePicker.targetBlock = del.object
                pagePicker.targetPos = at
                pagePicker.open()
                break
            }
        }

        contentItem: Column {
            spacing: 0
            Repeater {
                model: slashMenu.filtered
                Rectangle {
                    required property var modelData
                    required property int index
                    width: 200; height: 26; radius: 4
                    color: index === slashMenu.active ? root.th.hover : "transparent"
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 8; spacing: 8
                        Label {
                            text: modelData.key
                            color: root.th.dim; font.family: "Menlo"; font.pixelSize: 10
                            width: 22
                        }
                        Label { text: modelData.label; color: root.th.text; font.pixelSize: 12 }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: slashMenu.run(parent.modelData)
                    }
                }
            }
        }
    }

    // ── page-link picker ──────────────────────────────────────────────────
    Popup {
        id: pagePicker
        property string targetBlock: ""
        property int targetPos: 0
        width: 240; height: Math.min(300, pickerList.contentHeight + 16)
        anchors.centerIn: parent
        modal: true
        padding: 8
        background: Rectangle { color: root.th.card; radius: 6; border.color: root.th.border }
        contentItem: ListView {
            id: pickerList
            clip: true
            model: d.pages
            delegate: Rectangle {
                required property string pageObj
                required property string pageTitle
                required property int pageDepth
                width: ListView.view ? ListView.view.width : 0
                height: 26
                color: "transparent"
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 6 + pageDepth * 12
                    text: pageTitle
                    color: root.th.textMid; font.pixelSize: 12
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (d.backend && pagePicker.targetBlock !== "")
                            d.backend.insertPageLink(pagePicker.targetBlock,
                                                     pagePicker.targetPos, parent.pageObj)
                        pagePicker.close()
                    }
                }
            }
        }
    }

    Connections {
        target: d.backend
        function onError(message) { errorBar.show(message) }
        function onImageData(artifactId, dataUrl) {
            if (dataUrl === "")
                return
            root.imageCache[artifactId] = dataUrl
            root.imageCacheRev++
        }
        // Per-block content updates with the structure unchanged.
        function onBlockUpdated(object, text, spans, embeds) {
            root.blockAuthority[object] = {
                text: text,
                spans: JSON.parse(spans || "[]"),
                embeds: JSON.parse(embeds || "[]"),
            }
            root.authorityRev++
        }
        function onFocusBlock(object, caret) {
            focusTimer.targetObject = object
            focusTimer.targetCaret = caret
            focusTimer.restart()
        }
        function onFocusTitle() {
            titleField.forceActiveFocus()
            titleField.selectAll()
        }
    }

    // Delegates are created asynchronously after a layout change; retry briefly.
    Timer {
        id: focusTimer
        property string targetObject: ""
        property int targetCaret: 0
        property int tries: 0
        interval: 16
        repeat: true
        function splitSource() {
            for (var o in root.leafPool) {
                var l = root.leafPool[o]
                if (l && l.splitPending)
                    return l
            }
            return null
        }
        onTriggered: {
            if (root.focusBlockAt(targetObject, targetCaret)) {
                var src = splitSource()
                if (src && src.object !== targetObject) {
                    var target = root.leafRegistry[targetObject]
                    var buf = src.takeSplitBuffer()
                    if (target)
                        target.acceptSplitPrefix(buf)
                }
                tries = 0
                stop()
            } else if (tries > 20) {
                var stale = splitSource()
                if (stale)
                    stale.flushSplitInPlace()
                tries = 0
                stop()
            } else {
                tries++
            }
        }
        onRunningChanged: if (running) tries = 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── header / status bar ──────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: root.th.header

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10

                Rectangle {
                    width: 9; height: 9; radius: 4.5
                    color: root.statusColor(root.hubStatus)
                }
                Label {
                    text: "HASHWEB"
                    color: root.th.text
                    font.family: "Menlo"
                    font.pixelSize: 12
                    font.letterSpacing: 3
                }
                Label {
                    text: d.backend ? d.backend.statusMessage : "Waiting for backend..."
                    color: root.th.dim
                    font.family: "Menlo"
                    font.pixelSize: 11
                }
                // Live peers: one dot per editor in this space.
                Row {
                    spacing: 4
                    visible: root.presence.length > 0
                    Repeater {
                        model: root.presence
                        delegate: Rectangle {
                            required property var modelData
                            width: 9; height: 9; radius: 4.5
                            anchors.verticalCenter: parent.verticalCenter
                            color: modelData.color || root.th.green
                            ToolTip.visible: dotArea.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: modelData.name || "peer"
                            MouseArea {
                                id: dotArea
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }
                    }
                }
                Label {
                    visible: root.presence.length > 0
                    text: root.presence.length === 1 ? "1 peer editing"
                                                     : root.presence.length + " peers editing"
                    color: root.th.dim
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                // The space chip doubles as "change space": click to leave
                // and return to the landing page.
                Rectangle {
                    id: spaceChipHeader
                    visible: root.joinedSpace !== ""
                    implicitWidth: spaceChipRow.implicitWidth + 18
                    implicitHeight: 22
                    radius: 11
                    color: spaceChipArea.containsMouse ? root.th.hover : "transparent"
                    border.color: spaceChipArea.containsMouse ? root.th.sel : root.th.border
                    Row {
                        id: spaceChipRow
                        anchors.centerIn: parent
                        spacing: 5
                        Label {
                            text: "␟ " + root.joinedSpace
                            color: root.th.textMid
                            font.family: "Menlo"
                            font.pixelSize: 11
                        }
                        Label {
                            text: "⏏"
                            visible: spaceChipArea.containsMouse
                            color: root.th.dim
                            font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    MouseArea {
                        id: spaceChipArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (d.backend) d.backend.leaveSpace()
                    }
                    ToolTip.visible: spaceChipArea.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: "change space"
                }
                KbTool {
                    text: root.cream ? "◑" : "◐"
                    font.pixelSize: 13
                    leftPadding: 7; rightPadding: 7
                    onClicked: root.cream = !root.cream
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: root.cream ? "ink theme" : "cream theme"
                }
                Switch {
                    id: networkSwitch
                    text: checked ? "NETWORK" : "OFFLINE"
                    enabled: d.backend && (root.hubStatus === HashwebBackend.Online
                                           || root.hubStatus === HashwebBackend.Offline)
                    checked: d.backend ? !d.backend.offline : true
                    onToggled: if (d.backend) d.backend.toggleOffline(!checked)
                    indicator: Rectangle {
                        implicitWidth: 34
                        implicitHeight: 16
                        x: networkSwitch.leftPadding
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 8
                        color: networkSwitch.checked ? root.th.greenBg : root.th.off
                        border.color: networkSwitch.checked ? root.th.green : root.th.faint
                        Rectangle {
                            x: networkSwitch.checked ? parent.width - width - 2 : 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 12; height: 12; radius: 6
                            color: networkSwitch.checked ? root.th.green : root.th.dim
                            Behavior on x { NumberAnimation { duration: 120 } }
                        }
                    }
                    contentItem: Label {
                        text: networkSwitch.text
                        color: networkSwitch.checked ? root.th.green : root.th.dim
                        font.family: "Menlo"
                        font.pixelSize: 10
                        font.letterSpacing: 2
                        leftPadding: networkSwitch.indicator.width + 8
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        // ── landing: space selection (until a space is joined) ───────────
        Item {
            id: landingPage
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.joinedSpace === ""

            readonly property var recentSpaces:
                JSON.parse((d.backend && d.backend.recentSpacesJson) || "[]")
            readonly property bool ready: root.hubStatus === HashwebBackend.Online
                                          || root.hubStatus === HashwebBackend.Offline

            // faint grid, technical-drawing style
            Canvas {
                anchors.fill: parent
                opacity: 0.35
                // onPaint isn't a binding — repaint explicitly on reskin.
                property var themeDep: root.th
                onThemeDepChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    ctx.strokeStyle = root.th.card
                    ctx.lineWidth = 1
                    for (var x = 0; x < width; x += 48) {
                        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, height); ctx.stroke()
                    }
                    for (var y = 0; y < height; y += 48) {
                        ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(width, y); ctx.stroke()
                    }
                }
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
            }

            ColumnLayout {
                id: landing
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 48)
                spacing: 0

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: "H A S H W E B"
                    color: root.th.text
                    font.family: "Menlo"
                    font.pixelSize: 30
                    font.letterSpacing: 6
                }
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 8
                    text: "FIG. 01 — KNOWLEDGE BASE ON A FLAT STORE OF OBJECTS"
                    color: root.th.dimmer
                    font.family: "Menlo"
                    font.pixelSize: 10
                    font.letterSpacing: 2
                }

                // ── join card ─────────────────────────────────────────────
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 36
                    implicitHeight: joinCard.implicitHeight + 40
                    radius: 8
                    color: root.th.panel
                    border.color: root.th.border

                    ColumnLayout {
                        id: joinCard
                        anchors.fill: parent
                        anchors.margins: 20
                        spacing: 10

                        Label {
                            text: "SPACE"
                            color: root.th.dim
                            font.family: "Menlo"
                            font.pixelSize: 10
                            font.letterSpacing: 3
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: spaceField
                                Layout.fillWidth: true
                                placeholderText: "my-notes"
                                font.family: "Menlo"; font.pixelSize: 15
                                color: root.th.text
                                enabled: landingPage.ready
                                onAccepted: joinButton.clicked()
                                background: Rectangle {
                                    color: root.th.bg; radius: 6
                                    border.color: spaceField.activeFocus ? root.th.sel : root.th.border
                                }
                            }
                            Button {
                                id: joinButton
                                text: "JOIN →"
                                font.family: "Menlo"; font.pixelSize: 12
                                enabled: spaceField.enabled && spaceField.text.trim() !== ""
                                onClicked: if (d.backend) d.backend.joinSpace(spaceField.text)
                                background: Rectangle {
                                    radius: 6
                                    color: joinButton.enabled
                                           ? (joinButton.down ? root.th.selDown : root.th.sel) : root.th.off
                                }
                                contentItem: Label {
                                    text: joinButton.text
                                    color: joinButton.enabled ? root.th.text : root.th.dimmer
                                    font: joinButton.font
                                    horizontalAlignment: Text.AlignHCenter
                                    leftPadding: 10; rightPadding: 10
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "A space is a shared store: everyone who joins the same id "
                                  + "converges on the same workspace. [A-Za-z0-9._-], max 64."
                            wrapMode: Text.Wrap
                            color: root.th.dimmer
                            font.pixelSize: 11
                        }

                        Label {
                            Layout.topMargin: 6
                            text: "HANDLE"
                            color: root.th.dim
                            font.family: "Menlo"
                            font.pixelSize: 10
                            font.letterSpacing: 3
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextField {
                                id: handleField
                                Layout.fillWidth: true
                                text: root.handle
                                onTextEdited: root.handle = text.trim()
                                font.family: "Menlo"; font.pixelSize: 13
                                color: root.th.text
                                background: Rectangle {
                                    color: root.th.bg; radius: 6
                                    border.color: handleField.activeFocus ? root.th.sel
                                                                          : root.th.border
                                }
                            }
                            KbTool {
                                text: "⟳"
                                font.pixelSize: 13
                                onClicked: root.handle = root.rollHandle()
                                ToolTip.visible: hovered
                                ToolTip.delay: 600
                                ToolTip.text: "roll a new handle"
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "How you appear to other editors. A rolled alias — your "
                                  + "real name is never used or revealed."
                            wrapMode: Text.Wrap
                            color: root.th.dimmer
                            font.pixelSize: 11
                        }

                        // recent spaces: one-click rejoin
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            visible: landingPage.recentSpaces.length > 0
                            spacing: 6
                            Label {
                                text: "PREVIOUS SPACES"
                                color: root.th.dim
                                font.family: "Menlo"
                                font.pixelSize: 10
                                font.letterSpacing: 3
                            }
                            Flow {
                                Layout.fillWidth: true
                                spacing: 6
                                Repeater {
                                    model: landingPage.recentSpaces
                                    Rectangle {
                                        id: spaceChip
                                        required property string modelData
                                        width: chipLabel.implicitWidth + 22
                                        height: 26
                                        radius: 13
                                        color: chipArea.containsMouse ? root.th.hover : root.th.card
                                        border.color: chipArea.containsMouse ? root.th.sel : root.th.border
                                        Label {
                                            id: chipLabel
                                            anchors.centerIn: parent
                                            text: "␟ " + spaceChip.modelData
                                            color: root.th.textMid
                                            font.family: "Menlo"
                                            font.pixelSize: 11
                                        }
                                        MouseArea {
                                            id: chipArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: landingPage.ready
                                            onClicked: if (d.backend)
                                                           d.backend.joinSpace(spaceChip.modelData)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── delivery status line ──────────────────────────────────
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 20
                    spacing: 8
                    Rectangle {
                        width: 8; height: 8; radius: 4
                        color: root.statusColor(root.hubStatus)
                        SequentialAnimation on opacity {
                            running: !landingPage.ready
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.3; duration: 700 }
                            NumberAnimation { to: 1.0; duration: 700 }
                        }
                    }
                    Label {
                        text: d.backend ? d.backend.statusMessage : "Waiting for backend..."
                        color: root.th.dim
                        font.family: "Menlo"
                        font.pixelSize: 11
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    text: "syncs peer-to-peer over logos delivery · works offline"
                    color: root.th.faint
                    font.family: "Menlo"
                    font.pixelSize: 10
                }
            }
        }

        // ── workspace: sidebar + document + comments ──────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.joinedSpace !== ""
            spacing: 0

            // ── page tree sidebar ─────────────────────────────────────────
            Rectangle {
                Layout.preferredWidth: 220
                Layout.fillHeight: true
                color: root.th.panel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        Label {
                            text: "PAGES"
                            color: root.th.dim
                            font.family: "Menlo"; font.pixelSize: 10
                            font.letterSpacing: 2
                        }
                        Item { Layout.fillWidth: true }
                        KbTool {
                            text: "+ NEW"
                            onClicked: if (d.backend) d.backend.newPage()
                        }
                    }

                    ListView {
                        id: pageList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: d.pages
                        clip: true

                        delegate: Rectangle {
                            id: pageRow
                            required property string pageObj
                            required property string pageTitle
                            required property int pageDepth
                            required property bool pageOrphan
                            required property bool pageConflicted

                            width: ListView.view ? ListView.view.width : 0
                            height: 28
                            color: pageObj === root.currentPage ? root.th.hover : "transparent"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12 + pageRow.pageDepth * 14
                                anchors.rightMargin: 8
                                spacing: 6
                                Label {
                                    text: pageRow.pageOrphan ? "⚑" : "·"
                                    color: pageRow.pageOrphan ? root.th.amber : root.th.dimmer
                                    font.pixelSize: 11
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: pageRow.pageTitle
                                    elide: Text.ElideRight
                                    color: pageRow.pageObj === root.currentPage ? root.th.text : root.th.textSoft
                                    font.pixelSize: 13
                                }
                            }
                            Component.onCompleted: root.pageRowRegistry[pageObj] = pageRow
                            Component.onDestruction: {
                                if (root.pageRowRegistry[pageObj] === pageRow)
                                    delete root.pageRowRegistry[pageObj]
                            }

                            MouseArea {
                                id: pageRowArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                preventStealing: true
                                property point pressPos
                                property bool wasDrag: false
                                function track(mouse) {
                                    var p = pageRowArea.mapToItem(root, mouse.x, mouse.y)
                                    dragGhost.x = p.x
                                    dragGhost.y = p.y
                                    return p
                                }
                                onPressed: function(mouse) {
                                    pressPos = Qt.point(mouse.x, mouse.y)
                                    wasDrag = false
                                }
                                onPositionChanged: function(mouse) {
                                    if (!pressed)
                                        return
                                    if (!wasDrag
                                            && Math.abs(mouse.x - pressPos.x)
                                               + Math.abs(mouse.y - pressPos.y) > 8) {
                                        wasDrag = true
                                        root.pageDragObj = pageRow.pageObj
                                    }
                                    if (wasDrag) {
                                        var p = track(mouse)
                                        root.updatePageDragHover(p.x, p.y)
                                    }
                                }
                                onReleased: function(mouse) {
                                    if (root.pageDragObj !== "") {
                                        var p = pageRowArea.mapToItem(root, mouse.x, mouse.y)
                                        root.finishPageDrag(p.x, p.y)
                                    }
                                }
                                onCanceled: {
                                    root.pageDragObj = ""
                                    root.hoverDrop = null
                                }
                                onClicked: {
                                    if (!wasDrag && d.backend)
                                        d.backend.openPage(pageRow.pageObj)
                                }
                            }

                            // Three drop zones (kb.js renderTree): top third
                            // = before, bottom third = after, middle = INTO.
                            readonly property bool dropHovered:
                                root.hoverDrop && root.hoverDrop.kind === "page"
                                && root.hoverDrop.obj === pageObj
                            Rectangle {
                                visible: pageRow.dropHovered && root.hoverDrop.zone === "above"
                                anchors.top: parent.top
                                width: parent.width; height: 2; color: root.th.green
                            }
                            Rectangle {
                                visible: pageRow.dropHovered && root.hoverDrop.zone === "below"
                                anchors.bottom: parent.bottom
                                width: parent.width; height: 2; color: root.th.green
                            }
                            Rectangle {
                                visible: pageRow.dropHovered && root.hoverDrop.zone === "into"
                                anchors.fill: parent
                                color: "transparent"
                                border.color: root.th.amber
                                border.width: 1
                                radius: 4
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 8
                        visible: root.currentPage !== ""
                        KbTool {
                            text: "+ SUBPAGE"
                            onClicked: if (d.backend) d.backend.newSubpage()
                        }
                        Item { Layout.fillWidth: true }
                        KbTool {
                            id: deleteButton
                            property bool armed: false
                            text: armed ? "REALLY?" : "DELETE"
                            tint: armed ? root.th.red : root.th.dim
                            onClicked: {
                                if (!armed) {
                                    armed = true
                                    disarmTimer.restart()
                                } else {
                                    armed = false
                                    if (d.backend) d.backend.deletePage()
                                }
                            }
                            Timer {
                                id: disarmTimer
                                interval: 3000
                                onTriggered: deleteButton.armed = false
                            }
                        }
                    }
                }
            }

            Rectangle { width: 1; Layout.fillHeight: true; color: root.th.hover }

            // ── document: title + recursive layout tree ──────────────────
            ScrollView {
                id: docScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                ColumnLayout {
                    width: Math.min(760, docScroll.availableWidth - 48)
                    x: Math.max(24, (docScroll.availableWidth - width) / 2)
                    spacing: 4

                    Item { implicitHeight: 20 }

                    // breadcrumb (kb.js crumbs)
                    Label {
                        visible: root.currentPage !== ""
                        text: "WORKSPACE  /  " + (d.backend ? d.backend.pageTitle.toUpperCase() : "")
                        color: root.th.dimmer
                        font.family: "Menlo"
                        font.pixelSize: 10
                        font.letterSpacing: 2
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // The title is line zero of the document.
                    TextField {
                        id: titleField
                        Layout.fillWidth: true
                        visible: root.currentPage !== ""
                        font.pixelSize: 28
                        font.bold: true
                        color: root.th.text
                        background: null
                        property string backendTitle: d.backend ? d.backend.pageTitle : ""
                        onBackendTitleChanged: if (!activeFocus) text = backendTitle
                        Component.onCompleted: text = backendTitle
                        onActiveFocusChanged: if (!activeFocus) commit()
                        function commit() {
                            if (d.backend && text !== backendTitle)
                                d.backend.renamePage(text)
                        }
                        Timer {
                            id: titleDebounce
                            interval: 350
                            onTriggered: titleField.commit()
                        }
                        onTextEdited: titleDebounce.restart()
                        Keys.onReturnPressed: function(event) {
                            event.accepted = true
                            commit()
                            if (d.backend) d.backend.titleEnter()
                        }
                        Keys.onDownPressed: function(event) {
                            event.accepted = root.flatLeaves.length > 0
                            if (event.accepted)
                                root.focusBlockAt(root.flatLeaves[0].obj, 0)
                        }
                    }

                    Label {
                        visible: root.currentPage !== ""
                        text: "select text to format · type / for blocks · drag ⠿ to split the layout"
                        color: root.th.faint
                        font.family: "Menlo"
                        font.pixelSize: 10
                    }

                    // MVR conflict bar: concurrent titles; click one to agree.
                    Flow {
                        Layout.fillWidth: true
                        visible: root.currentPage !== "" && root.titleConflicts.length > 1
                        spacing: 6
                        Label {
                            text: "concurrent titles:"
                            color: root.th.amber; font.family: "Menlo"; font.pixelSize: 11
                        }
                        Repeater {
                            model: root.titleConflicts
                            Rectangle {
                                required property string modelData
                                width: altLabel.implicitWidth + 16; height: 22; radius: 4
                                color: root.th.amberBg; border.color: root.th.amber
                                Label {
                                    id: altLabel
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    color: root.th.amberText; font.pixelSize: 11
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: if (d.backend) d.backend.renamePage(parent.modelData)
                                }
                            }
                        }
                    }

                    Label {
                        visible: root.currentPage === ""
                        text: "No page selected — create one with + new"
                        color: root.th.dimmer
                        font.pixelSize: 14
                        Layout.margins: 24
                    }

                    // ── the layout tree, top level (vertical) ─────────────
                    ColumnLayout {
                        id: blocksHost
                        Layout.fillWidth: true
                        spacing: 2
                        Repeater {
                            model: root.layoutTree
                            delegate: Loader {
                                required property var modelData
                                Layout.fillWidth: true
                                sourceComponent: modelData.t === "cont" ? containerComp : leafSlotComp
                                onLoaded: item.node = modelData
                            }
                        }
                    }

                    // Add button; drop-below-last-block is hit-tested in
                    // finishBlockDrag.
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 48
                        KbTool {
                            text: "+ BLOCK"
                            visible: root.currentPage !== ""
                            onClicked: if (d.backend) d.backend.addBlockAtEnd()
                        }
                    }

                    Item { implicitHeight: 40 }
                }
            }

            // ── comments panel (kb.js right rail) ─────────────────────────
            Rectangle {
                Layout.preferredWidth: 260
                Layout.fillHeight: true
                visible: root.comments.length > 0
                color: root.th.panel

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    Label {
                        Layout.margins: 10
                        text: "COMMENTS (" + root.comments.length + ")"
                        color: root.th.dim
                        font.family: "Menlo"; font.pixelSize: 10
                        font.letterSpacing: 2
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: root.comments
                        clip: true
                        spacing: 8

                        delegate: Rectangle {
                            id: commentCard
                            required property var modelData
                            width: ListView.view ? ListView.view.width - 16 : 0
                            x: 8
                            implicitHeight: cardCol.implicitHeight + 16
                            radius: 6
                            color: root.th.card
                            border.color: root.th.border

                            ColumnLayout {
                                id: cardCol
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4

                                Label {
                                    Layout.fillWidth: true
                                    text: commentCard.modelData.fragments.length > 0
                                          ? commentCard.modelData.fragments[0].quote : ""
                                    elide: Text.ElideRight
                                    color: root.th.amber
                                    font.pixelSize: 11
                                    font.italic: true
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: {
                                            var f = commentCard.modelData.fragments[0]
                                            if (f) root.focusBlockAt(f.obj, f.start)
                                        }
                                    }
                                }
                                Repeater {
                                    model: commentCard.modelData.messages
                                    Label {
                                        required property string modelData
                                        Layout.fillWidth: true
                                        text: modelData
                                        wrapMode: Text.Wrap
                                        color: root.th.textMid
                                        font.pixelSize: 12
                                    }
                                }
                                TextField {
                                    Layout.fillWidth: true
                                    placeholderText: commentCard.modelData.messages.length
                                                     ? "reply..." : "write a comment..."
                                    font.pixelSize: 11
                                    color: root.th.text
                                    background: Rectangle {
                                        color: root.th.bg; radius: 4; border.color: root.th.border
                                    }
                                    onAccepted: {
                                        if (d.backend && text.trim() !== "") {
                                            d.backend.replyComment(commentCard.modelData.tag, text)
                                            text = ""
                                        }
                                    }
                                }
                                Label {
                                    Layout.alignment: Qt.AlignRight
                                    text: "RESOLVE ✕"
                                    color: root.th.red
                                    font.family: "Menlo"; font.pixelSize: 9
                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: if (d.backend)
                                                       d.backend.resolveComment(commentCard.modelData.tag)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── drag ghost (shared; handles drag it, DropAreas hear it) ──────────
    // The drag chip: purely visual — targeting is manual hit-testing.
    Item {
        id: dragGhost
        width: 1
        height: 1
        visible: root.dragOrigin !== "" || root.pageDragObj !== ""
        z: 1000
        Rectangle {
            x: 14
            y: 18
            width: ghostLabel.implicitWidth + 20
            height: 26
            radius: 6
            color: root.th.hover
            border.color: root.th.sel
            visible: dragGhost.visible
            Label {
                id: ghostLabel
                anchors.centerIn: parent
                text: root.pageDragObj !== "" ? "▸ moving page" : "⠿ moving block"
                color: root.th.textMid; font.family: "Menlo"; font.pixelSize: 11
            }
        }
    }

    // ── recursive layout components ───────────────────────────────────────

    // A leaf SLOT: adopts the pooled leaf for its origin. The slot is the
    // disposable part of the tree; the editor it hosts is not.
    Component {
        id: leafSlotComp
        Item {
            id: slot
            property var node
            property Item leaf
            implicitHeight: leaf ? leaf.implicitHeight : 0
            onNodeChanged: if (node) leaf = root.adoptLeaf(node, slot)
            onWidthChanged: if (leaf && leaf.parent === slot) leaf.width = width
            Component.onDestruction: {
                if (leaf && leaf.parent === slot)
                    root.stashLeaf(leaf)
            }
        }
    }

    // A container: children arrange perpendicular to the parent axis —
    // depth % 2 == 0 lays out vertically, else horizontally (kb.js
    // renderNodeInto flexDirection).
    Component {
        id: containerComp
        GridLayout {
            id: cont
            property var node
            readonly property bool horizontal: node ? node.depth % 2 !== 0 : false
            columns: horizontal ? (node ? node.children.length : 1) : 1
            rowSpacing: 2
            columnSpacing: 10
            Repeater {
                model: cont.node ? cont.node.children : []
                delegate: Loader {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: cont.horizontal ? 100 : -1
                    Layout.alignment: Qt.AlignTop
                    sourceComponent: modelData.t === "cont" ? containerComp : leafSlotComp
                    onLoaded: item.node = modelData
                }
            }
        }
    }

    // A leaf block: gutter (handle + list marker) + editor + styled overlay
    // + embeds, with drag-drop edges.
    Component {
        id: leafComp
        FocusScope {
            id: leafRoot
            property var node
            implicitHeight: leafCol.implicitHeight + 4

            readonly property string object: node ? node.obj : ""
            readonly property string origin: node ? node.origin : ""
            readonly property string blockKind: node ? node.kind : ""
            readonly property bool editorFocused: blockEditor.activeFocus
            function caretPos() { return blockEditor.cursorPosition }

            // Content authority: blockUpdated pushes newer text/spans/embeds
            // than the (structure-frozen) tree carries.
            readonly property var auth: (root.authorityRev, node ? root.blockAuthority[node.obj] : undefined)
            readonly property string authText: auth ? auth.text : (node ? node.text : "")
            readonly property var spansParsed: auth ? auth.spans : (node ? node.spans : [])
            readonly property var embedsParsed: auth ? auth.embeds : (node ? node.embeds : [])

            // Shadow of the last hub-confirmed text; edits diff against it.
            // Set imperatively so nothing slips past onTextChanged unguarded.
            property string shadow: ""
            property bool applyingRemote: false
            // Ops sent since the authority last agreed with the editor.
            // While non-zero, authority can be OLDER than the editor
            // (echoes in flight) and must not touch a focused editor.
            property int localEdits: 0
            property bool droppedEcho: false
            property int pendingCaret: -1

            // Enter's split takes a QtRO round trip; keystrokes typed in
            // that window BUFFER here and flush into the NEW block when
            // its focusBlock arrives — otherwise they tear across the
            // boundary (half in the old block, half in the new).
            property bool splitPending: false
            property string splitBuffer: ""
            Timer {
                id: splitTimeout
                interval: 700
                onTriggered: leafRoot.flushSplitInPlace()
            }
            // The split never answered (or focus never landed): type in place.
            function flushSplitInPlace() {
                if (!splitPending)
                    return
                splitPending = false
                splitTimeout.stop()
                var buf = splitBuffer
                splitBuffer = ""
                if (buf === "" || !d.backend)
                    return
                var at = caretPos()
                applyingRemote = true
                blockEditor.text = blockEditor.text.substring(0, at) + buf
                                   + blockEditor.text.substring(at)
                shadow = blockEditor.text
                blockEditor.cursorPosition = at + buf.length
                applyingRemote = false
                localEdits++
                d.backend.applyBlockEdit(object, at, 0, buf)
            }
            // Called by the focus timer once the NEW editor actually has
            // focus: everything buffered since Enter becomes the new
            // block's prefix — ops to the hub, optimistic seed locally so
            // subsequent diffs compute against the right base.
            function takeSplitBuffer() {
                splitPending = false
                splitTimeout.stop()
                var buf = splitBuffer
                splitBuffer = ""
                return buf
            }
            function acceptSplitPrefix(buf) {
                if (buf === "")
                    return
                // Input rules apply to the buffered prefix too: "- " typed
                // during the split window still makes a list item.
                if (blockKind === "" && d.backend) {
                    var m = buf.match(/^(- |\* )/) ? "bullet"
                          : buf.match(/^\d{1,2}\. /) ? "number" : ""
                    if (m !== "") {
                        buf = buf.replace(/^(- |\* |\d{1,2}\. )/, "")
                        d.backend.setBlockKind(origin, m)
                        if (buf === "")
                            return
                    }
                }
                applyingRemote = true
                blockEditor.text = buf + blockEditor.text
                shadow = blockEditor.text
                blockEditor.cursorPosition = buf.length
                applyingRemote = false
                localEdits++
                if (d.backend)
                    d.backend.applyBlockEdit(object, 0, 0, buf)
            }

            // Heading styling is for BARE text blocks only: a code/equation
            // region starting with a "#" comment is not a title, nor is a
            // list item.
            readonly property bool isHeading: authText.startsWith("# ")
                                              && !isCodePanel
                                              && blockKind === ""
            // An image block: nothing but one image (or still-fetching)
            // embed. Renders as a picture, not an editor.
            readonly property bool isImageBlock: {
                if (embedsParsed.length !== 1)
                    return false
                var k = embedsParsed[0].kind
                if (k !== "image" && k !== "pending")
                    return false
                return authText.replace(/\uFFFC/g, "").trim() === ""
            }
            readonly property bool hasEqblock: eqTex !== ""

            // Is [start,end) entirely covered by `kind`? Drives toggling:
            // fully-marked selections unmark, anything else marks.
            function rangeHasMark(start, end, kind) {
                var off = 0
                var covered = 0
                for (var i = 0; i < spansParsed.length; i++) {
                    var sp = spansParsed[i]
                    var s0 = off
                    var s1 = off + sp.text.length
                    off = s1
                    var has = false
                    for (var m = 0; m < sp.marks.length; m++)
                        if (sp.marks[m].kind === kind)
                            has = true
                    if (has)
                        covered += Math.max(0, Math.min(end, s1) - Math.max(start, s0))
                }
                return covered >= end - start
            }
            function toggleMark(kind) {
                var s = blockEditor.selectionStart
                var e = blockEditor.selectionEnd
                if (s === e || !d.backend)
                    return
                if (rangeHasMark(s, e, kind))
                    d.backend.unmarkBlockRange(object, s, e, kind)
                else
                    d.backend.markBlockRange(object, s, e, kind, "on")
            }

            // Inline `math` spans with editor-layout geometry: text offsets,
            // boundary points, and line box, in EDITOR coordinates. Feeds
            // the math overlay (rendered image over concealed spans; $
            // ghosts on the one being edited). Recomputes when the layout
            // reflows — including highlighter conceal changes (revision).
            readonly property var inlineMathSpans: {
                var reflowDeps = [blockEditor.text, blockEditor.width,
                                  spanHl.revision]
                var out = []
                var off = 0
                for (var i = 0; i < spansParsed.length; i++) {
                    var sp = spansParsed[i]
                    var isMath = false
                    for (var m = 0; m < sp.marks.length; m++)
                        if (sp.marks[m].kind === "math")
                            isMath = true
                    if (isMath && sp.text.length > 0
                        && off + sp.text.length <= blockEditor.length) {
                        var r0 = blockEditor.positionToRectangle(off)
                        var r1 = blockEditor.positionToRectangle(off + sp.text.length)
                        out.push({ start: off, end: off + sp.text.length,
                                   tex: sp.text.trim(),
                                   x0: r0.x, y0: r0.y, x1: r1.x, y1: r1.y,
                                   h: Math.max(r0.height, r1.height),
                                   wraps: Math.abs(r0.y - r1.y) > 1 })
                    }
                    off += sp.text.length
                }
                return out
            }
            readonly property string eqTex: {
                for (var i = 0; i < spansParsed.length; i++)
                    for (var m = 0; m < spansParsed[i].marks.length; m++)
                        if (spansParsed[i].marks[m].kind === "eqblock")
                            return spansParsed[i].text.trim()
                return ""
            }
            // A code / equation region anywhere in the block: render the
            // whole block on a panel, monospace-faithful.
            readonly property bool isCodePanel: {
                for (var i = 0; i < spansParsed.length; i++)
                    for (var m = 0; m < spansParsed[i].marks.length; m++) {
                        var k = spansParsed[i].marks[m].kind
                        if (k === "codeblock" || k === "eqblock") return true
                    }
                return false
            }
            // The mark spans, shaped for SpanHighlighter: kind list plus the
            // conceal width for inline math (rendered image + breathing
            // room). Requests renders as a side effect, so a fresh mark's
            // image lands and re-binds via mathCacheRev.
            readonly property string spanFeed: {
                var rev = root.mathCacheRev
                var arr = []
                for (var i = 0; i < spansParsed.length; i++) {
                    var sp = spansParsed[i]
                    var kinds = []
                    var isMath = false
                    var isEq = false
                    for (var m = 0; m < sp.marks.length; m++) {
                        kinds.push(sp.marks[m].kind)
                        if (sp.marks[m].kind === "math") isMath = true
                        if (sp.marks[m].kind === "eqblock") isEq = true
                    }
                    var concealW = 0
                    if (sp.text.trim() !== "") {
                        if (isEq) {
                            root.requestMath(sp.text, true)
                        } else if (isMath) {
                            // exactly the image width: any spacing around
                            // the equation is the text's own (the spaces
                            // outside the span), not manufactured here
                            var hit = root.mathCache[root.mathKey(sp.text, false)]
                            if (hit) concealW = hit.w
                            else root.requestMath(sp.text, false)
                        }
                    }
                    arr.push({ len: sp.text.length, kinds: kinds,
                               concealW: concealW })
                }
                return JSON.stringify(arr)
            }

            // Inline page-link chips: every U+FFFC atom pairs with its
            // embeds entry in document order; page links conceal to chip
            // width (the highlighter squeezes the atom, the overlay draws
            // the chip). Other embed kinds keep the strip below the block.
            function chipLabel(e) { return "§ " + (e.title || "page") }
            readonly property string embedFeed: {
                var out = []
                var txt = authText
                var n = 0
                for (var i = 0; i < txt.length; i++) {
                    if (txt[i] !== "\uFFFC")
                        continue
                    var e = embedsParsed[n]; n++
                    if (e && e.kind === "page")
                        out.push({ pos: i,
                                   w: chipFm.advanceWidth(chipLabel(e)) + 16 })
                }
                return JSON.stringify(out)
            }
            readonly property var pageChipSpots: {
                var reflowDeps = [blockEditor.text, blockEditor.width,
                                  spanHl.revision]
                var out = []
                var txt = blockEditor.text
                var n = 0
                for (var i = 0; i < txt.length; i++) {
                    if (txt[i] !== "\uFFFC")
                        continue
                    var e = embedsParsed[n]; n++
                    if (!e || e.kind !== "page")
                        continue
                    var r = blockEditor.positionToRectangle(i)
                    out.push({ x: r.x, y: r.y, h: r.height,
                               label: chipLabel(e), target: e.payload,
                               w: chipFm.advanceWidth(chipLabel(e)) + 16 })
                }
                return out
            }

            // The Loader assigns `node` AFTER Component.onCompleted fires
            // (onLoaded ordering), so registration and the editor seed hang
            // off the node assignment — keying them off onCompleted
            // registered every leaf under "" and focus targeting silently
            // found nothing (caught live: title Enter never moved focus
            // into the new block).
            onNodeChanged: {
                if (!node)
                    return
                root.leafRegistry[node.obj] = leafRoot
                applyingRemote = true
                blockEditor.text = authText
                shadow = authText
                applyingRemote = false
            }
            Component.onDestruction: {
                if (node && root.leafRegistry[node.obj] === leafRoot)
                    delete root.leafRegistry[node.obj]
            }

            // Follow the authority whenever it lands — guarded so a stale
            // push never rolls back a focused editor's shadow (re-sending
            // in-flight chars as duplicates was the typing-corruption bug).
            onAuthTextChanged: {
                if (authText === blockEditor.text) {
                    shadow = authText
                    localEdits = 0
                    droppedEcho = false
                    return
                }
                if (!blockEditor.activeFocus || localEdits === 0) {
                    applyRemote(authText)
                    localEdits = 0
                    if (pendingCaret >= 0) {
                        blockEditor.cursorPosition = Math.min(pendingCaret, blockEditor.length)
                        pendingCaret = -1
                    }
                } else {
                    droppedEcho = true
                }
            }

            function focusAt(caret) {
                if (isImageBlock) {
                    leafRoot.forceActiveFocus()
                    return
                }
                blockEditor.forceActiveFocus()
                pendingCaret = caret > blockEditor.length ? caret : -1
                blockEditor.cursorPosition = Math.max(0, Math.min(caret, blockEditor.length))
            }

            // Keyboard on a selected image block: delete + caret flow.
            Keys.onPressed: function(event) {
                if (!isImageBlock)
                    return
                if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                    event.accepted = true
                    if (d.backend) d.backend.joinBlock(object)
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Left) {
                    event.accepted = root.focusNeighbor(object, -1, 1e9)
                } else if (event.key === Qt.Key_Down || event.key === Qt.Key_Right) {
                    event.accepted = root.focusNeighbor(object, +1, 0)
                }
            }

            function applyRemote(t) {
                if (blockEditor.text === t) { shadow = t; return }
                applyingRemote = true
                var caret = blockEditor.cursorPosition
                blockEditor.text = t
                shadow = t
                blockEditor.cursorPosition = Math.min(caret, blockEditor.length)
                applyingRemote = false
            }

            Timer {
                id: blurReconcile
                interval: 250 // > one QtRO round trip
                onTriggered: {
                    if (blockEditor.activeFocus || !leafRoot.droppedEcho)
                        return
                    leafRoot.droppedEcho = false
                    leafRoot.applyRemote(leafRoot.authText)
                }
            }

            // drop-edge indicator lines (driven by root.hoverDrop)
            readonly property bool dropHovered:
                root.hoverDrop && root.hoverDrop.kind === "leaf"
                && root.hoverDrop.origin === origin
            Rectangle {
                visible: leafRoot.dropHovered && root.hoverDrop.edge === "top"
                anchors.top: parent.top; width: parent.width; height: 2; color: root.th.green
            }
            Rectangle {
                visible: leafRoot.dropHovered && root.hoverDrop.edge === "bottom"
                anchors.bottom: parent.bottom; width: parent.width; height: 2; color: root.th.green
            }
            Rectangle {
                visible: leafRoot.dropHovered && root.hoverDrop.edge === "left"
                anchors.left: parent.left; height: parent.height; width: 2; color: root.th.amber
            }
            Rectangle {
                visible: leafRoot.dropHovered && root.hoverDrop.edge === "right"
                anchors.right: parent.right; height: parent.height; width: 2; color: root.th.amber
            }

            Column {
                id: leafCol
                width: parent.width

                RowLayout {
                    id: blockRow
                    width: parent.width
                    spacing: 0

                    // drag handle (kb.js ⠿)
                    Label {
                        id: handle
                        text: "⠿"
                        color: handleArea.containsMouse || handleArea.drag.active
                               ? root.th.dim : root.th.handle
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignTop
                        topPadding: 6
                        rightPadding: 4
                        MouseArea {
                            id: handleArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.OpenHandCursor
                            preventStealing: true
                            function track(mouse) {
                                var p = handleArea.mapToItem(root, mouse.x, mouse.y)
                                dragGhost.x = p.x
                                dragGhost.y = p.y
                                return p
                            }
                            onPressed: function(mouse) {
                                track(mouse)
                                root.dragOrigin = leafRoot.origin
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                var p = track(mouse)
                                root.updateBlockDragHover(p.x, p.y)
                            }
                            onReleased: function(mouse) {
                                var p = handleArea.mapToItem(root, mouse.x, mouse.y)
                                root.finishBlockDrag(p.x, p.y)
                            }
                            onCanceled: {
                                root.dragOrigin = ""
                                root.hoverDrop = null
                            }
                        }
                    }

                    // Contested placement: frozen at last-agreed, badged;
                    // the next drag names both heads and resolves.
                    Label {
                        visible: leafRoot.node ? !!leafRoot.node.conflicted : false
                        text: "⚑"
                        color: root.th.amber
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignTop
                        topPadding: 6
                        rightPadding: 2
                        ToolTip.visible: conflictHover.hovered
                        ToolTip.text: "placement contested by a concurrent move — drag to resolve"
                        HoverHandler { id: conflictHover }
                    }

                    // list bullet / number gutter
                    Label {
                        visible: !leafRoot.isImageBlock
                                 && (leafRoot.blockKind === "bullet"
                                     || leafRoot.blockKind === "number")
                        text: leafRoot.blockKind === "bullet"
                              ? "•" : (leafRoot.node ? leafRoot.node.num : 1) + "."
                        color: root.th.dim
                        font.family: "Menlo"
                        font.pixelSize: 15
                        Layout.alignment: Qt.AlignTop
                        topPadding: 4
                        leftPadding: 2
                        rightPadding: 2
                    }

                    Item {
                        visible: !leafRoot.isImageBlock
                        Layout.fillWidth: true
                        implicitHeight: leafRoot.isImageBlock ? 0
                                        : Math.max(blockEditor.implicitHeight,
                                                   eqFace.visible ? eqFace.height + 12 : 0)
                                          + (leafRoot.isCodePanel ? 8 : 0)

                        // code / equation regions sit on a panel
                        Rectangle {
                            anchors.fill: parent
                            visible: leafRoot.isCodePanel
                            color: root.th.panel
                            radius: 6
                            border.color: root.th.border
                        }

                        TextArea {
                            id: blockEditor
                            width: parent.width
                            wrapMode: TextArea.Wrap
                            font.family: leafRoot.isCodePanel && !leafRoot.hasEqblock
                                         ? "Menlo" : root.proseFont
                            font.pixelSize: leafRoot.isHeading ? 22
                                            : leafRoot.isCodePanel ? 13 : 15
                            font.bold: leafRoot.isHeading
                            // The codeblock region mark anchors on ONE seed
                            // character (typed text lands outside the closed
                            // mark), so code styling is block-level, like the
                            // panel itself — not a per-span format.
                            color: leafRoot.isCodePanel && !leafRoot.hasEqblock
                                   ? root.th.codeBlockInk : root.th.text
                            selectionColor: root.th.sel
                            selectByMouse: true
                            background: null
                            leftPadding: 6; rightPadding: 6; topPadding: 3; bottomPadding: 3
                            // The editor is the block's ONE face: marks are
                            // styled in-place by SpanHighlighter, so nothing
                            // swaps (or reflows) on focus/blur. Only the
                            // display-equation face still replaces it.
                            opacity: eqFace.visible ? 0 : 1
                            placeholderText: root.flatLeaves.length === 1 ? "Type here…"
                                             : (activeFocus ? "type / for blocks" : "")
                            placeholderTextColor: root.th.faint

                            onActiveFocusChanged: {
                                if (!activeFocus)
                                    blurReconcile.restart()
                                else
                                    root.queueAnnounce(leafRoot.origin, cursorPosition)
                            }
                            onCursorPositionChanged: if (activeFocus)
                                root.queueAnnounce(leafRoot.origin, cursorPosition)

                            onTextChanged: {
                                if (leafRoot.applyingRemote || text === leafRoot.shadow || !d.backend)
                                    return
                                var oldText = leafRoot.shadow
                                var newText = text
                                var prefix = 0
                                var maxPrefix = Math.min(oldText.length, newText.length)
                                while (prefix < maxPrefix && oldText[prefix] === newText[prefix])
                                    prefix++
                                var suffix = 0
                                var maxSuffix = Math.min(oldText.length, newText.length) - prefix
                                while (suffix < maxSuffix
                                       && oldText[oldText.length - 1 - suffix] === newText[newText.length - 1 - suffix])
                                    suffix++
                                var removed = oldText.length - prefix - suffix
                                var inserted = newText.substring(prefix, newText.length - suffix)
                                if (leafRoot.splitPending) {
                                    if (removed === 0) {
                                        // typed while the split is in flight:
                                        // buffer for the new block, keep the
                                        // editor at its pre-split face.
                                        leafRoot.splitBuffer += inserted
                                        leafRoot.applyingRemote = true
                                        var c = cursorPosition - inserted.length
                                        text = leafRoot.shadow
                                        cursorPosition = Math.max(0, Math.min(c, length))
                                        leafRoot.applyingRemote = false
                                        return
                                    }
                                    // deletions can't buffer: flush in place.
                                    splitTimeout.stop()
                                    leafRoot.splitPending = false
                                    inserted = leafRoot.splitBuffer + inserted
                                    leafRoot.splitBuffer = ""
                                }
                                leafRoot.shadow = newText
                                leafRoot.localEdits++
                                leafRoot.pendingCaret = -1
                                d.backend.applyBlockEdit(leafRoot.object, prefix, removed, inserted)
                                updateSlash()
                                maybeInputRule(inserted)
                            }

                            function updateSlash() {
                                if (!slashMenu.visible) {
                                    // "/" opens the menu anywhere at a word
                                    // start — not just in an empty block.
                                    var cp = cursorPosition
                                    if (cp > 0 && text[cp - 1] === "/"
                                            && (cp === 1 || text[cp - 2] === " "
                                                || text[cp - 2] === "\n"))
                                        slashMenu.openFor(leafRoot, blockEditor, cp - 1)
                                    return
                                }
                                if (slashMenu.target !== leafRoot
                                        || slashMenu.startPos >= text.length
                                        || text[slashMenu.startPos] !== "/"
                                        || cursorPosition <= slashMenu.startPos) {
                                    slashMenu.close()
                                    return
                                }
                                var q = text.substring(slashMenu.startPos + 1,
                                                       cursorPosition)
                                if (q.indexOf(" ") !== -1 || q.indexOf("\n") !== -1) {
                                    slashMenu.close()
                                    return
                                }
                                slashMenu.query = q
                            }

                            // kb.js input rules: '- ' / '* ' / 'N. ' at
                            // block start make a list item; $tex$ makes
                            // inline math; $$tex$$ makes an equation block.
                            // The typed delimiters tombstone; the mark
                            // carries the meaning.
                            function maybeInputRule(inserted) {
                                if (inserted === "$" && !leafRoot.isCodePanel) {
                                    mathRule()
                                    return
                                }
                                if (inserted !== " " || leafRoot.blockKind !== "")
                                    return
                                var head = text.substring(0, cursorPosition)
                                var kind = ""
                                if (head === "- " || head === "* ") kind = "bullet"
                                else if (/^\d{1,2}\. $/.test(head)) kind = "number"
                                if (kind === "")
                                    return
                                d.backend.applyBlockEdit(leafRoot.object, 0, head.length, "")
                                leafRoot.shadow = text.substring(head.length)
                                leafRoot.applyingRemote = true
                                blockEditor.text = leafRoot.shadow
                                blockEditor.cursorPosition = 0
                                leafRoot.applyingRemote = false
                                d.backend.setBlockKind(leafRoot.origin, kind)
                            }

                            function mathRule() {
                                var p = cursorPosition - 1 // the typed $
                                if (p < 1 || text[p] !== "$")
                                    return
                                function bad(c) {
                                    return c === "" || c.indexOf("\n") !== -1
                                           || c.indexOf("\uFFFC") !== -1
                                }
                                var start, len, content, mkind
                                if (text[p - 1] === "$") {
                                    // $$content$$ → equation block
                                    var k = text.lastIndexOf("$$", p - 2)
                                    if (k === -1)
                                        return
                                    content = text.substring(k + 2, p - 1)
                                    if (bad(content) || content.indexOf("$") !== -1)
                                        return
                                    d.backend.markBlockRange(leafRoot.object, k + 2, p - 1,
                                                             "eqblock", "on")
                                    d.backend.applyBlockEdit(leafRoot.object, p - 1, 2, "")
                                    d.backend.applyBlockEdit(leafRoot.object, k, 2, "")
                                    start = k
                                    len = 2
                                } else {
                                    // $content$ → inline math
                                    var i2 = text.lastIndexOf("$", p - 1)
                                    if (i2 === -1 || (i2 > 0 && text[i2 - 1] === "$"))
                                        return
                                    content = text.substring(i2 + 1, p)
                                    if (bad(content))
                                        return
                                    d.backend.markBlockRange(leafRoot.object, i2 + 1, p,
                                                             "math", "on")
                                    d.backend.applyBlockEdit(leafRoot.object, p, 1, "")
                                    d.backend.applyBlockEdit(leafRoot.object, i2, 1, "")
                                    start = i2
                                    len = 1
                                }
                                leafRoot.applyingRemote = true
                                blockEditor.text = text.substring(0, start) + content
                                                   + text.substring(p + 1)
                                leafRoot.shadow = blockEditor.text
                                blockEditor.cursorPosition = start + content.length
                                leafRoot.applyingRemote = false
                                leafRoot.localEdits++
                            }

                            Keys.onReturnPressed: function(event) {
                                if (slashMenu.visible && slashMenu.target === leafRoot) {
                                    event.accepted = true
                                    slashMenu.runActive()
                                    return
                                }
                                if (event.modifiers & (Qt.ShiftModifier | Qt.ControlModifier | Qt.MetaModifier)) {
                                    // modifier-Enter: newline in a text block,
                                    // BREAK OUT of a code/equation block.
                                    if (!leafRoot.isCodePanel) {
                                        event.accepted = false
                                        return
                                    }
                                } else if (leafRoot.isCodePanel) {
                                    // plain Enter inside code: a newline —
                                    // code flows as one block.
                                    event.accepted = false
                                    return
                                }
                                event.accepted = true
                                if (leafRoot.splitPending)
                                    return // one split at a time; swallow
                                if (d.backend) {
                                    leafRoot.splitPending = true
                                    leafRoot.splitBuffer = ""
                                    splitTimeout.restart()
                                    d.backend.splitBlock(leafRoot.object, cursorPosition)
                                }
                            }
                            Keys.onPressed: function(event) {
                                if (slashMenu.visible && slashMenu.target === leafRoot) {
                                    if (event.key === Qt.Key_Down) {
                                        event.accepted = true
                                        slashMenu.active = (slashMenu.active + 1) % slashMenu.filtered.length
                                        return
                                    }
                                    if (event.key === Qt.Key_Up) {
                                        event.accepted = true
                                        slashMenu.active = (slashMenu.active + slashMenu.filtered.length - 1)
                                                           % slashMenu.filtered.length
                                        return
                                    }
                                    if (event.key === Qt.Key_Escape || event.key === Qt.Key_Tab) {
                                        event.accepted = true
                                        if (event.key === Qt.Key_Tab) slashMenu.runActive()
                                        else slashMenu.close()
                                        return
                                    }
                                }
                                if ((event.modifiers & Qt.AltModifier)
                                        && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
                                    event.accepted = true
                                    if (d.backend)
                                        d.backend.moveBlock(leafRoot.object,
                                                            event.key === Qt.Key_Up ? -1 : 1)
                                    return
                                }
                                if (event.key === Qt.Key_Backspace && cursorPosition === 0
                                        && selectionStart === selectionEnd) {
                                    event.accepted = true
                                    if (d.backend)
                                        d.backend.joinBlock(leafRoot.object)
                                } else if (event.key === Qt.Key_Up && cursorPosition === 0) {
                                    event.accepted = root.focusNeighbor(leafRoot.object, -1, 1e9)
                                    if (!event.accepted && root.flatLeaves.length > 0
                                            && root.flatLeaves[0].obj === leafRoot.object) {
                                        titleField.forceActiveFocus()
                                        event.accepted = true
                                    }
                                } else if (event.key === Qt.Key_Down && cursorPosition === length) {
                                    event.accepted = root.focusNeighbor(leafRoot.object, +1, 0)
                                } else if (event.key === Qt.Key_Left && cursorPosition === 0
                                           && selectionStart === selectionEnd) {
                                    event.accepted = root.focusNeighbor(leafRoot.object, -1, 1e9)
                                } else if (event.key === Qt.Key_Right && cursorPosition === length
                                           && selectionStart === selectionEnd) {
                                    event.accepted = root.focusNeighbor(leafRoot.object, +1, 0)
                                } else if (event.key === Qt.Key_V
                                           && (event.modifiers & Qt.ControlModifier)) {
                                    // Multi-line clipboard text pastes as
                                    // parsed markdown blocks; single-line
                                    // falls through to the plain paste.
                                    var clip = KbClipboard.text()
                                    if (clip.indexOf("\n") !== -1 && d.backend) {
                                        event.accepted = true
                                        d.backend.pasteMarkdown(leafRoot.object,
                                                                cursorPosition, clip)
                                    }
                                } else if (event.key === Qt.Key_B
                                           && (event.modifiers & Qt.ControlModifier)
                                           && selectionStart !== selectionEnd) {
                                    event.accepted = true
                                    leafRoot.toggleMark("bold")
                                } else if (event.key === Qt.Key_I
                                           && (event.modifiers & Qt.ControlModifier)
                                           && selectionStart !== selectionEnd) {
                                    event.accepted = true
                                    leafRoot.toggleMark("italic")
                                } else if (event.key === Qt.Key_E
                                           && (event.modifiers & Qt.ControlModifier)
                                           && selectionStart !== selectionEnd) {
                                    event.accepted = true
                                    leafRoot.toggleMark("code")
                                } else if (event.key === Qt.Key_M
                                           && (event.modifiers & Qt.ControlModifier)
                                           && selectionStart !== selectionEnd) {
                                    event.accepted = true
                                    if (d.backend)
                                        d.backend.addComment(leafRoot.object,
                                                             selectionStart, selectionEnd)
                                }
                            }
                        }

                        // Display equations get a dedicated CENTERED face:
                        // QQuickText's rich text ignores alignment around
                        // <img>, so centering happens at the item level.
                        Image {
                            id: eqFace
                            readonly property var hit:
                                (root.mathCacheRev,
                                 root.mathCache[root.mathKey(leafRoot.eqTex, true)])
                            visible: !blockEditor.activeFocus
                                     && leafRoot.hasEqblock && !!hit
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 6
                            source: hit ? hit.url : ""
                            width: hit ? hit.w : 0
                            height: hit ? hit.h : 0
                            // The editor's raw text is left-aligned while
                            // this face is centered, so clicks on the
                            // RENDERED equation (and only it) would miss
                            // the editor — hand focus over explicitly.
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.IBeamCursor
                                onClicked: {
                                    blockEditor.forceActiveFocus()
                                    blockEditor.cursorPosition = blockEditor.length
                                }
                            }
                        }

                        // Marks styled straight into the editor's document:
                        // character formats only, so text and offsets stay
                        // byte-identical to the CRDT model and the editor is
                        // the block's single face in view AND edit.
                        FontMetrics { id: edFm; font: blockEditor.font }

                        SpanHighlighter {
                            id: spanHl
                            document: blockEditor.textDocument
                            caret: blockEditor.activeFocus
                                   ? blockEditor.cursorPosition : -1
                            codeInk: root.th.codeInk
                            codeBlockInk: root.th.codeBlockInk
                            mathInk: root.th.math
                            kwInk: root.th.kw
                            strInk: root.th.str
                            cmtInk: root.th.cmt
                            numInk: root.th.num
                            language: leafRoot.blockKind.indexOf("lang:") === 0
                                      ? leafRoot.blockKind.substring(5) : ""
                            spansJson: leafRoot.spanFeed
                            embedsJson: leafRoot.embedFeed
                        }
                        FontMetrics { id: chipFm; font.family: root.proseFont; font.pixelSize: 12 }

                        // Inline math: the highlighter conceals a span the
                        // caret is outside of (transparent ink, squeezed to
                        // the image's width) and this overlay draws the
                        // rendered image over it — text flows around the
                        // equation with no gap, focused or not. Clicking the
                        // image lands the caret inside the hidden span,
                        // which lifts the conceal and expands the source in
                        // place, with translucent ghost $ delimiters at the
                        // mark's edges. Offsets can lag the editor by an
                        // in-flight keystroke; they settle with the echo.
                        Repeater {
                            model: leafRoot.inlineMathSpans
                            delegate: Item {
                                id: mathOverlay
                                readonly property bool editing:
                                    blockEditor.activeFocus
                                    && blockEditor.cursorPosition >= modelData.start
                                    && blockEditor.cursorPosition <= modelData.end
                                readonly property var hit:
                                    (root.mathCacheRev,
                                     root.mathCache[root.mathKey(modelData.tex, false)])

                                Image {
                                    visible: !mathOverlay.editing && !!mathOverlay.hit
                                    x: modelData.x0
                                    // renders are tight (no margins); align
                                    // the render's baseline with the line's
                                    y: modelData.y0 + edFm.ascent
                                       - (mathOverlay.hit ? mathOverlay.hit.asc : 0)
                                    source: mathOverlay.hit ? mathOverlay.hit.url : ""
                                    width: mathOverlay.hit ? mathOverlay.hit.w : 0
                                    height: mathOverlay.hit ? mathOverlay.hit.h : 0
                                }

                                Text {
                                    visible: mathOverlay.editing
                                    x: modelData.x0 - implicitWidth
                                    y: modelData.y0
                                    text: "$"
                                    opacity: 0.4
                                    color: root.th.math
                                    font.family: blockEditor.font.family
                                    font.pixelSize: blockEditor.font.pixelSize
                                }
                                Text {
                                    visible: mathOverlay.editing
                                    x: modelData.x1
                                    y: modelData.y1
                                    text: "$"
                                    opacity: 0.4
                                    color: root.th.math
                                    font.family: blockEditor.font.family
                                    font.pixelSize: blockEditor.font.pixelSize
                                }
                            }
                        }

                        // Inline page-link chips over concealed embed atoms.
                        Repeater {
                            model: leafRoot.pageChipSpots
                            delegate: Rectangle {
                                required property var modelData
                                x: modelData.x + 1
                                y: modelData.y + modelData.h / 2 - height / 2
                                width: modelData.w - 2
                                height: 17
                                radius: 8.5
                                color: pageChipArea.containsMouse ? root.th.hover
                                                                  : root.th.card
                                border.color: pageChipArea.containsMouse
                                              ? root.th.sel : root.th.border
                                Label {
                                    anchors.centerIn: parent
                                    text: parent.modelData.label
                                    color: root.th.math
                                    font.family: root.proseFont
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    id: pageChipArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: if (d.backend)
                                                   d.backend.openPage(parent.modelData.target)
                                }
                            }
                        }

                        // Remote carets: where each live peer is editing.
                        Repeater {
                            model: root.presence.filter(function(p) {
                                return p.block === leafRoot.origin
                                       && p.page === root.currentPage
                            })
                            delegate: Item {
                                required property var modelData
                                readonly property var r:
                                    (blockEditor.text, blockEditor.width,
                                     spanHl.revision,
                                     blockEditor.positionToRectangle(
                                         Math.min(modelData.caret || 0,
                                                  blockEditor.length)))
                                Rectangle {
                                    x: parent.r.x
                                    y: parent.r.y
                                    width: 2
                                    height: parent.r.height
                                    color: parent.modelData.color || root.th.green
                                    opacity: 0.85
                                }
                                Rectangle {
                                    x: parent.r.x
                                    y: parent.r.y - 11
                                    implicitWidth: peerName.implicitWidth + 6
                                    implicitHeight: 11
                                    radius: 2
                                    color: parent.modelData.color || root.th.green
                                    Text {
                                        id: peerName
                                        anchors.centerIn: parent
                                        text: parent.parent.modelData.name || "peer"
                                        font.pixelSize: 8
                                        color: "#ffffff"
                                    }
                                }
                            }
                        }

                        // Floating selection toolbar: mark toggles + comment.
                        Rectangle {
                            id: selBar
                            visible: blockEditor.activeFocus
                                     && blockEditor.selectionStart !== blockEditor.selectionEnd
                            readonly property var anchorRect:
                                (blockEditor.selectionStart,
                                 blockEditor.positionToRectangle(
                                     blockEditor.selectionStart))
                            x: Math.max(0, Math.min(anchorRect.x,
                                                    parent.width - implicitWidth))
                            y: anchorRect.y - implicitHeight - 4
                            z: 10
                            radius: 5
                            implicitWidth: selRow.implicitWidth + 8
                            implicitHeight: 24
                            color: root.th.card
                            border.color: root.th.border
                            Row {
                                id: selRow
                                anchors.centerIn: parent
                                spacing: 2
                                Repeater {
                                    model: [
                                        { t: "B",  kind: "bold" },
                                        { t: "I",  kind: "italic" },
                                        { t: "<>", kind: "code" },
                                        { t: "∑",  kind: "math" },
                                        { t: "✎",  kind: "comment" },
                                    ]
                                    delegate: Rectangle {
                                        required property var modelData
                                        width: 26; height: 20; radius: 4
                                        color: selBtnArea.containsMouse ? root.th.hover
                                                                        : "transparent"
                                        Label {
                                            anchors.centerIn: parent
                                            text: parent.modelData.t
                                            color: root.th.textMid
                                            font.pixelSize: 11
                                            font.bold: parent.modelData.t === "B"
                                            font.italic: parent.modelData.t === "I"
                                        }
                                        MouseArea {
                                            id: selBtnArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: {
                                                var k = parent.modelData.kind
                                                if (k === "comment") {
                                                    if (d.backend)
                                                        d.backend.addComment(
                                                            leafRoot.object,
                                                            blockEditor.selectionStart,
                                                            blockEditor.selectionEnd)
                                                } else {
                                                    leafRoot.toggleMark(k)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Language selector chip on code panels.
                        Rectangle {
                            visible: leafRoot.isCodePanel && !leafRoot.hasEqblock
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: 4
                            implicitHeight: 16
                            implicitWidth: langLabel.implicitWidth + 12
                            radius: 8
                            z: 5
                            color: langChipArea.containsMouse ? root.th.hover
                                                              : "transparent"
                            border.color: langChipArea.containsMouse
                                          ? root.th.border : "transparent"
                            Label {
                                id: langLabel
                                anchors.centerIn: parent
                                text: spanHl.language !== "" ? spanHl.language : "plain"
                                color: root.th.dimmer
                                font.family: "Menlo"
                                font.pixelSize: 9
                            }
                            MouseArea {
                                id: langChipArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: langMenu.openFor(leafRoot, parent)
                            }
                        }
                    }

                }

                // Embedded objects (kb.js renderEmbed).
                Column {
                    id: embedStrip
                    x: 22
                    width: parent.width - x
                    spacing: 4

                    Repeater {
                        // Page links render INLINE (chip over the concealed
                        // atom); everything else keeps the strip.
                        model: leafRoot.embedsParsed.filter(function(e) {
                            return e.kind !== "page"
                        })
                        delegate: Loader {
                            required property var modelData
                            sourceComponent: modelData.kind === "image" ? imageEmbed
                                           : modelData.kind === "pending" ? pendingEmbed
                                           : modelData.kind === "page" ? pageEmbed
                                           : modelData.kind === "table" ? tableEmbed
                                           : chipEmbed
                            property string payload: modelData.payload
                            property var embedData: modelData
                        }
                    }

                    Component {
                        id: imageEmbed
                        Item {
                            width: img.width + 8
                            height: img.height + 8
                            Rectangle {
                                anchors.fill: parent
                                radius: 6
                                color: "transparent"
                                border.width: 2
                                border.color: leafRoot.activeFocus && leafRoot.isImageBlock
                                              ? root.th.sel : "transparent"
                            }
                            Image {
                                id: img
                                x: 4; y: 4
                                source: (root.imageCacheRev, root.imageCache[payload] || "")
                                fillMode: Image.PreserveAspectFit
                                width: Math.min(implicitWidth, embedStrip.width - 8)
                                asynchronous: true
                                Component.onCompleted: {
                                    if (!root.imageCache[payload] && d.backend)
                                        d.backend.requestImage(payload)
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: if (leafRoot.isImageBlock) leafRoot.forceActiveFocus()
                            }
                        }
                    }
                    Component {
                        id: pendingEmbed
                        Rectangle {
                            width: 180; height: 60; radius: 6
                            color: root.th.card; border.color: root.th.border
                            Label {
                                anchors.centerIn: parent
                                text: "⧗ fetching…"
                                color: root.th.dim; font.family: "Menlo"; font.pixelSize: 11
                            }
                            Timer {
                                interval: 1500; repeat: true; running: true
                                onTriggered: if (d.backend) d.backend.requestImage(payload)
                            }
                        }
                    }
                    Component {
                        id: pageEmbed
                        KbTool {
                            text: "📄 " + (embedData.title || "page")
                            tint: root.th.math
                            font.pixelSize: 12
                            font.letterSpacing: 0
                            font.family: "Helvetica"
                            onClicked: if (d.backend) d.backend.openPage(payload)
                        }
                    }
                    Component {
                        id: chipEmbed
                        Label {
                            text: "⟨" + payload.substring(0, 8) + "…⟩"
                            color: root.th.dimmer; font.family: "Menlo"; font.pixelSize: 11
                        }
                    }
                    Component {
                        id: tableEmbed
                        Column {
                            spacing: 2
                            Repeater {
                                model: embedData.rows
                                Row {
                                    required property var modelData
                                    spacing: 2
                                    Repeater {
                                        model: modelData
                                        TextField {
                                            required property var modelData
                                            width: 140
                                            text: modelData.text
                                            font.pixelSize: 12
                                            color: root.th.text
                                            background: Rectangle {
                                                color: root.th.card; border.color: root.th.border
                                            }
                                            onEditingFinished: {
                                                var oldT = modelData.text
                                                if (text === oldT || !d.backend)
                                                    return
                                                var p = 0
                                                var maxP = Math.min(oldT.length, text.length)
                                                while (p < maxP && oldT[p] === text[p]) p++
                                                var s = 0
                                                var maxS = Math.min(oldT.length, text.length) - p
                                                while (s < maxS && oldT[oldT.length-1-s] === text[text.length-1-s]) s++
                                                d.backend.applyBlockEdit(
                                                    modelData.obj, p,
                                                    oldT.length - p - s,
                                                    text.substring(p, text.length - s))
                                            }
                                        }
                                    }
                                }
                            }
                            Row {
                                spacing: 6
                                KbTool {
                                    text: "+ ROW"
                                    onClicked: if (d.backend) d.backend.tableAddRow(payload)
                                }
                                KbTool {
                                    text: "+ COL"
                                    onClicked: if (d.backend) d.backend.tableAddCol(payload)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: errorBar
        visible: false
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: errorLabel.implicitHeight + 16
        color: root.th.redBg
        function show(message) { errorLabel.text = message; visible = true; errorTimer.restart() }
        Label {
            id: errorLabel
            anchors.centerIn: parent
            color: root.th.redText; font.family: "Menlo"; font.pixelSize: 12
        }
        Timer { id: errorTimer; interval: 6000; onTriggered: errorBar.visible = false }
    }
}
