// A compact, dependency-free LaTeX-subset renderer for QML Canvas: enough
// for note math — greek + common symbols, ^ _ scripts, \frac, \sqrt, big
// operators with limits, \text. Unknown commands render as literal upright
// text, so nothing ever disappears. Pipeline: parse → boxes (w, asc, desc,
// draw) → paint at a baseline.
.pragma library

var SYMBOLS = {
    alpha: "α", beta: "β", gamma: "γ", delta: "δ", epsilon: "ε",
    varepsilon: "ε", zeta: "ζ", eta: "η", theta: "θ", vartheta: "ϑ",
    iota: "ι", kappa: "κ", lambda: "λ", mu: "μ", nu: "ν", xi: "ξ",
    pi: "π", varpi: "ϖ", rho: "ρ", sigma: "σ", varsigma: "ς", tau: "τ",
    upsilon: "υ", phi: "φ", varphi: "φ", chi: "χ", psi: "ψ", omega: "ω",
    Gamma: "Γ", Delta: "Δ", Theta: "Θ", Lambda: "Λ", Xi: "Ξ", Pi: "Π",
    Sigma: "Σ", Upsilon: "Υ", Phi: "Φ", Psi: "Ψ", Omega: "Ω",
    infty: "∞", partial: "∂", nabla: "∇", forall: "∀", exists: "∃",
    neg: "¬", lnot: "¬", emptyset: "∅", varnothing: "∅", hbar: "ℏ",
    ell: "ℓ", Re: "ℜ", Im: "ℑ", aleph: "ℵ", wp: "℘", angle: "∠",
    dots: "…", ldots: "…", cdots: "⋯", vdots: "⋮", ddots: "⋱",
    prime: "′", dagger: "†", langle: "⟨", rangle: "⟩",
    lfloor: "⌊", rfloor: "⌋", lceil: "⌈", rceil: "⌉",
    sin: "sin", cos: "cos", tan: "tan", log: "log", ln: "ln", exp: "exp",
    min: "min", max: "max", lim: "lim", sup: "sup", inf: "inf",
    det: "det", gcd: "gcd", mod: "mod", arg: "arg",
}

var OPS = {
    pm: "±", mp: "∓", times: "×", div: "÷", cdot: "·", ast: "∗",
    le: "≤", leq: "≤", ge: "≥", geq: "≥", ne: "≠", neq: "≠",
    equiv: "≡", approx: "≈", sim: "∼", simeq: "≃", cong: "≅",
    propto: "∝", ll: "≪", gg: "≫",
    "in": "∈", notin: "∉", ni: "∋", subset: "⊂", supset: "⊃",
    subseteq: "⊆", supseteq: "⊇", cup: "∪", cap: "∩", setminus: "∖",
    wedge: "∧", vee: "∨", land: "∧", lor: "∨", oplus: "⊕",
    otimes: "⊗", circ: "∘", bullet: "•", vdash: "⊢", models: "⊨",
    mapsto: "↦", to: "→", gets: "←", rightarrow: "→", leftarrow: "←",
    Rightarrow: "⇒", Leftarrow: "⇐", leftrightarrow: "↔",
    Leftrightarrow: "⇔", implies: "⟹", iff: "⟺",
    perp: "⊥", parallel: "∥", mid: "∣", star: "⋆",
}

var BIGOPS = {
    sum: "∑", prod: "∏", coprod: "∐", int: "∫", oint: "∮", iint: "∬",
    bigcup: "⋃", bigcap: "⋂", bigoplus: "⨁", bigotimes: "⨂",
}

// ── parser ──────────────────────────────────────────────────────────────

function parse(src) {
    var st = { s: src, i: 0 }
    return parseRow(st, null)
}

function peek(st) { return st.i < st.s.length ? st.s[st.i] : "" }
function skipSpace(st) { while (st.i < st.s.length && /\s/.test(st.s[st.i])) st.i++ }

function parseArg(st) {
    skipSpace(st)
    if (peek(st) === "{") {
        st.i++
        var row = parseRow(st, "}")
        if (peek(st) === "}") st.i++
        return row
    }
    return parseAtom(st)
}

function parseCommand(st) {
    var name = ""
    while (st.i < st.s.length && /[a-zA-Z]/.test(st.s[st.i]))
        name += st.s[st.i++]
    if (name === "") {
        var c = st.i < st.s.length ? st.s[st.i++] : ""
        if (c === "," || c === ";" || c === "!" || c === "\\")
            return { k: "sym", t: " ", it: false, op: false }
        return { k: "sym", t: c, it: false, op: false }
    }
    if (name === "frac") return { k: "frac", num: parseArg(st), den: parseArg(st) }
    if (name === "sqrt") return { k: "sqrt", rad: parseArg(st) }
    if (name === "text" || name === "mathrm" || name === "operatorname") {
        skipSpace(st)
        var t = ""
        if (peek(st) === "{") {
            st.i++
            while (st.i < st.s.length && st.s[st.i] !== "}") t += st.s[st.i++]
            if (st.i < st.s.length) st.i++
        }
        return { k: "text", t: t }
    }
    if (name === "left" || name === "right") {
        skipSpace(st)
        if (st.i < st.s.length) {
            var d = st.s[st.i++]
            if (d === "\\") return parseCommand(st)
            if (d === ".") return { k: "row", kids: [] }
            return { k: "sym", t: d, it: false, op: false }
        }
        return { k: "row", kids: [] }
    }
    if (BIGOPS[name]) return { k: "bigop", t: BIGOPS[name], lo: null, hi: null }
    if (OPS[name]) return { k: "sym", t: OPS[name], it: false, op: true }
    if (SYMBOLS[name]) return { k: "sym", t: SYMBOLS[name], it: false, op: false }
    return { k: "text", t: "\\" + name }
}

function parseAtom(st) {
    skipSpace(st)
    if (st.i >= st.s.length) return null
    var c = st.s[st.i]
    if (c === "\\") { st.i++; return parseCommand(st) }
    if (c === "{") {
        st.i++
        var row = parseRow(st, "}")
        if (peek(st) === "}") st.i++
        return row
    }
    st.i++
    if (/[a-zA-Z]/.test(c)) return { k: "sym", t: c, it: true, op: false }
    if ("+-=<>*/".indexOf(c) !== -1)
        return { k: "sym", t: c === "-" ? "−" : c, it: false, op: true }
    return { k: "sym", t: c, it: false, op: false }
}

function parseRow(st, term) {
    var row = { k: "row", kids: [] }
    while (st.i < st.s.length) {
        skipSpace(st)
        if (st.i >= st.s.length || (term && peek(st) === term)) break
        var c = peek(st)
        if (c === "^" || c === "_") {
            st.i++
            var script = parseArg(st)
            var base = row.kids.length ? row.kids.pop() : { k: "row", kids: [] }
            var sc
            if (base.k === "script" || base.k === "bigop") {
                sc = base
            } else {
                sc = { k: "script", base: base, sub: null, sup: null }
            }
            if (sc.k === "bigop") {
                if (c === "_") sc.lo = script; else sc.hi = script
            } else {
                if (c === "_") sc.sub = script; else sc.sup = script
            }
            row.kids.push(sc)
            continue
        }
        var atom = parseAtom(st)
        if (!atom) break
        row.kids.push(atom)
    }
    return row
}

// ── layout ──────────────────────────────────────────────────────────────

var FONT = "'STIX Two Math', 'Times New Roman', serif"

function setFont(ctx, size, italic) {
    ctx.font = (italic ? "italic " : "") + Math.max(1, Math.round(size)) + "px " + FONT
}

function textBox(ctx, t, size, italic) {
    setFont(ctx, size, italic)
    var w = ctx.measureText(t).width
    return {
        w: w, asc: size * 0.78, desc: size * 0.24,
        draw: function(c, x, y) {
            setFont(c, size, italic)
            c.fillText(t, x, y)
        }
    }
}

function emptyBox() { return { w: 0, asc: 0, desc: 0, draw: function() {} } }

function layoutRow(ctx, kids, size, display) {
    var boxes = [], gaps = []
    for (var i = 0; i < kids.length; i++) {
        var k = kids[i]
        if (!k) continue
        var b = layoutNode(ctx, k, size, display)
        var g = (k.k === "sym" && k.op) ? size * 0.22 : 0
        var prevG = i > 0 && kids[i-1] && kids[i-1].k === "sym" && kids[i-1].op
                    ? size * 0.22 : 0
        gaps.push(boxes.length === 0 ? 0 : Math.max(g, prevG, size * 0.03))
        boxes.push(b)
    }
    var out = { w: 0, asc: 0, desc: 0 }
    for (i = 0; i < boxes.length; i++) {
        out.w += gaps[i] + boxes[i].w
        out.asc = Math.max(out.asc, boxes[i].asc)
        out.desc = Math.max(out.desc, boxes[i].desc)
    }
    out.draw = function(c, x, y) {
        var cx = x
        for (var j = 0; j < boxes.length; j++) {
            cx += gaps[j]
            boxes[j].draw(c, cx, y)
            cx += boxes[j].w
        }
    }
    return out
}

function layoutNode(ctx, n, size, display) {
    if (!n) return emptyBox()
    switch (n.k) {
    case "row": return layoutRow(ctx, n.kids, size, display)
    case "sym": return textBox(ctx, n.t, size, n.it && n.t.length === 1)
    case "text": return textBox(ctx, n.t, size, false)
    case "frac": {
        var inner = display ? size : size * 0.85
        var num = layoutNode(ctx, n.num, inner, false)
        var den = layoutNode(ctx, n.den, inner, false)
        var pad = size * 0.12
        var rule = Math.max(1, size * 0.055)
        var axis = size * 0.30
        var w = Math.max(num.w, den.w) + pad * 2
        return {
            w: w,
            asc: axis + rule + pad + num.asc + num.desc,
            desc: -axis + pad + den.asc + den.desc,
            draw: function(c, x, y) {
                var ry = y - axis
                c.fillRect(x, ry - rule / 2, w, rule)
                num.draw(c, x + (w - num.w) / 2, ry - rule / 2 - pad - num.desc)
                den.draw(c, x + (w - den.w) / 2, ry + rule / 2 + pad + den.asc)
            }
        }
    }
    case "sqrt": {
        var rad = layoutNode(ctx, n.rad, size, display)
        var h = rad.asc + rad.desc
        var hookW = size * 0.55
        var sp = size * 0.10
        var line = Math.max(1, size * 0.055)
        var b = {
            w: hookW + rad.w + sp * 2,
            asc: rad.asc + sp + line * 2,
            desc: rad.desc,
        }
        b.draw = function(c, x, y) {
            var top = y - rad.asc - sp - line
            var bot = y + rad.desc
            c.save()
            c.lineWidth = line
            c.lineJoin = "round"
            c.lineCap = "round"
            c.strokeStyle = c.fillStyle
            c.beginPath()
            c.moveTo(x, bot - h * 0.42)
            c.lineTo(x + hookW * 0.35, bot - h * 0.5)
            c.lineTo(x + hookW * 0.62, bot)
            c.lineTo(x + hookW, top)
            c.lineTo(x + hookW + rad.w + sp * 2, top)
            c.stroke()
            c.restore()
            rad.draw(c, x + hookW + sp, y)
        }
        return b
    }
    case "script": {
        var base = layoutNode(ctx, n.base, size, display)
        var ss = size * 0.68
        var sub = layoutNode(ctx, n.sub, ss, false)
        var sup = layoutNode(ctx, n.sup, ss, false)
        var supRaise = base.asc * 0.62 + size * 0.05
        var subDrop = base.desc * 0.4 + size * 0.16
        return {
            w: base.w + Math.max(sub.w, sup.w) + size * 0.04,
            asc: Math.max(base.asc, supRaise + sup.asc),
            desc: Math.max(base.desc, subDrop + sub.desc),
            draw: function(c, x, y) {
                base.draw(c, x, y)
                var sx = x + base.w + size * 0.04
                sup.draw(c, sx, y - supRaise)
                sub.draw(c, sx, y + subDrop)
            }
        }
    }
    case "bigop": {
        var op = textBox(ctx, n.t, size * (display ? 1.6 : 1.25), false)
        var s2 = size * 0.68
        var lo = layoutNode(ctx, n.lo, s2, false)
        var hi = layoutNode(ctx, n.hi, s2, false)
        if (display) {
            var gap = size * 0.12
            var bw = Math.max(op.w, lo.w, hi.w)
            return {
                w: bw,
                asc: op.asc + (hi.w > 0 ? gap + hi.asc + hi.desc : 0),
                desc: op.desc + (lo.w > 0 ? gap + lo.asc + lo.desc : 0),
                draw: function(c, x, y) {
                    var cx = x + bw / 2
                    op.draw(c, cx - op.w / 2, y)
                    if (hi.w > 0)
                        hi.draw(c, cx - hi.w / 2, y - op.asc - gap - hi.desc)
                    if (lo.w > 0)
                        lo.draw(c, cx - lo.w / 2, y + op.desc + gap + lo.asc)
                }
            }
        }
        var supRaise2 = op.asc * 0.55
        var subDrop2 = op.desc + size * 0.12
        return {
            w: op.w + Math.max(lo.w, hi.w) + size * 0.04,
            asc: Math.max(op.asc, supRaise2 + hi.asc),
            desc: Math.max(op.desc, subDrop2 + lo.desc),
            draw: function(c, x, y) {
                op.draw(c, x, y)
                var sx = x + op.w + size * 0.04
                hi.draw(c, sx, y - supRaise2)
                lo.draw(c, sx, y + subDrop2)
            }
        }
    }
    }
    return emptyBox()
}

// Measure + draw entry point. Returns {w, h, asc} in canvas px for the
// given base size; draw paints into a 2d context.
//
// TIGHT bounds, no margins: spacing and alignment belong to the QML side,
// which places the image by its baseline (`asc` = distance from the top
// edge to the baseline). A 1px guard absorbs antialiasing bleed.
function layout(ctx, tex, size, display) {
    var root = parse(tex)
    var box = layoutRow(ctx, root.kids, size, display)
    var pad = 1
    return {
        w: Math.ceil(box.w + pad * 2),
        h: Math.ceil(box.asc + box.desc + pad * 2),
        asc: box.asc + pad,
        draw: function(c) {
            box.draw(c, pad, pad + box.asc)
        }
    }
}
