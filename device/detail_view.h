#pragma once

// Included by the device view after the input, parameter and drawing helpers.
enum DetailKind {
    DETAIL_NONE, DETAIL_MASTER_COMP, DETAIL_LIMITER, DETAIL_MASTER_CLIP,
    DETAIL_INSERT_COMP, DETAIL_INSERT_CLIP, DETAIL_FILTER, DETAIL_AMP, DETAIL_MOD, DETAIL_OPERATOR
};

static struct DetailView {
    DetailKind kind = DETAIL_NONE;
    int origin = -1, track = -1, pattern = 0, stage = 0, focus = 0;
    int params[12], count = 0;
    const char* title = "";
    uint32_t sampleAt = 0;
    static constexpr int HISTORY = 96;
    float reduction[HISTORY] = {};
    int next = 0, samples = 0;
} gDetail;

static DetailKind detailKind(int param) {
    if (U.view != V_TRACK || param < 0) return DETAIL_NONE;
    const char* id = PARAMS[param].id;
    if (U.sel == FXT) {
        if (!masterSetting(param)) return DETAIL_NONE;
        if (!strcmp(id, "master.clip")) return DETAIL_MASTER_CLIP;
        const int heading = fxHeading(id);
        return heading == HDR_DYNAMICS ? DETAIL_MASTER_COMP : heading == HDR_LIMITER ? DETAIL_LIMITER : DETAIL_NONE;
    }
    if (isInsertParam(param)) {
        const int type = scGet(U.sel, insertParamSlot(insertStage(param)));
        return type == INS_COMPRESS ? DETAIL_INSERT_COMP : type == INS_CLIP ? DETAIL_INSERT_CLIP : DETAIL_NONE;
    }
    if (!strncmp(id, "trk.flt.", 8)) return DETAIL_FILTER;
    if (!strncmp(id, "trk.amp.", 8)) return DETAIL_AMP;
    if (!strncmp(id, "trk.menv.", 9) && !isModDestination(PARAMS[param])) return DETAIL_MOD;
    if (!strncmp(id, "trk.fm", 6) && sourceParamVisible(id, machOf(U.sel))) return DETAIL_OPERATOR;
    return DETAIL_NONE;
}

static void openDetail(DetailKind kind, int param) {
    auto& d = gDetail;
    d = DetailView{};
    d.kind = kind;
    d.origin = param;
    d.track = U.sel == FXT ? -1 : U.sel;
    d.pattern = scPattern();
    d.sampleAt = U.now;
    const char* id = PARAMS[param].id;
    const char* prefix = "";
    const char* const* fields = nullptr;
    int n = 0;
    auto select = [&](const char* name, const char* group, const char* const* items, int count) {
        d.title = name; prefix = group; fields = items; n = count;
    };
    static const char* masterComp[] = {"threshold", "ratio", "knee", "attack", "release", "detect", "auto", "makeup", "comp_mix"};
    static const char* limiter[] = {"limit", "ceiling", "limrel", "ab"};
    static const char* masterClip[] = {"clip"};
    static const char* insertComp[] = {"threshold", "ratio", "knee", "attack", "release", "detect", "auto", "source", "sc.hpf", "mix", "trim"};
    static const char* clip[] = {"clip.shape", "drive", "clip.ceil", "trim", "mix"};
    static const char* filter[] = {"envmode", "env", "atk", "dec", "sus", "rel", "freq", "reso", "type"};
    static const char* amp[] = {"atk", "dec", "sus", "rel"};
    static const char* mod[] = {"atk", "dec", "depth", "mode"};
    static const char* fm[] = {"env", "atk", "dec", "sus", "rel", "ratio", "level", "fine"};
    char group[12];
    switch (kind) {
    case DETAIL_MASTER_COMP: select("MASTER COMPRESSOR", "master", masterComp, 9); break;
    case DETAIL_LIMITER: select("MASTER LIMITER", "master", limiter, 4); break;
    case DETAIL_MASTER_CLIP: select("MASTER CLIPPER", "master", masterClip, 1); break;
    case DETAIL_INSERT_COMP:
    case DETAIL_INSERT_CLIP:
        d.stage = insertStage(param);
        snprintf(group, sizeof group, "trk.ins%d", d.stage + 1);
        if (kind == DETAIL_INSERT_COMP) select("TRACK COMPRESSOR", group, insertComp, 11);
        else select("TRACK CLIPPER", group, clip, 5);
        break;
    case DETAIL_FILTER: select("FILTER ENVELOPE", "trk.flt", filter, 9); break;
    case DETAIL_AMP: select("AMP ENVELOPE", "trk.amp", amp, 4); break;
    case DETAIL_MOD: select("MOD ENVELOPE", "trk.menv", mod, 4); break;
    case DETAIL_OPERATOR: {
        static const char* titles[] = {"FM OPERATOR C", "FM OPERATOR A", "FM OPERATOR B1", "FM OPERATOR B2"};
        const int index = id[6] - '1';
        snprintf(group, sizeof group, "trk.fm%d", index + 1);
        select(titles[index], group, fm, 8);
        break;
    }
    default: return;
    }
    for (int i = 0; i < n; i++) {
        char key[32];
        snprintf(key, sizeof key, "%s.%s", prefix, fields[i]);
        const int p = pIndex(key);
        if (p == param) d.focus = d.count;
        d.params[d.count++] = p;
    }
    U.aHeld = false;
    U.modal = M_DETAIL;
}

static int detailValue(int field) {
    return scGet(gDetail.track, slotOf(gDetail.params[field]));
}

static void handleDetail() {
    auto& d = gDetail;
    if (IN.pressed(BT_B) || IN.pressed(BT_SELECT)) { U.modal = M_NONE; return; }
    if ((d.track >= 0 && d.pattern != scPattern()) || detailKind(d.origin) != d.kind) {
        U.modal = M_NONE;
        toast("Detail closed: pattern or effect changed");
        return;
    }
    const int move = IN.repeat(BT_RIGHT, U.now) - IN.repeat(BT_LEFT, U.now);
    d.focus = clampi(d.focus + move, 0, d.count - 1);
    const int p = d.params[d.focus], value = detailValue(d.focus);
    const int fine = IN.repeat(BT_UP, U.now) - IN.repeat(BT_DOWN, U.now);
    const int coarse = IN.repeat(BT_R1, U.now) - IN.repeat(BT_L1, U.now);
    if (fine || coarse) setViewValue(d.track, slotOf(p), stepValue(p, value, fine ? fine : coarse, !fine));
    if (IN.pressed(BT_Y)) setViewValue(d.track, slotOf(p), PARAMS[p].def);
    if (IN.pressed(BT_A) && (d.kind == DETAIL_FILTER || d.kind == DETAIL_OPERATOR))
        setViewValue(d.track, slotOf(d.params[0]), !detailValue(0));
    if (IN.pressed(BT_START)) togglePlayback();
    if (scIsFill() && !IN.down[BT_START]) scFill(0);
}

static constexpr uint32_t DETAIL_ACCENT = 0x58e0bd;
static void curveSegment(int x, int y, int previous, uint32_t color) {
    fill(x, y < previous ? y : previous, 1, abs(y - previous) + 1, color);
}

static void graphGrid(int x, int y, int w, int h) {
    fill(x, y, w, h, 0x111419);
    for (int i = 1; i < 4; i++) {
        fill(x, y + i * h / 4, w, 1, C_LINE);
        fill(x + i * w / 4, y, 1, h, C_LINE);
    }
    frame(x, y, w, h, C_LINE);
}

static void drawDetailDynamics() {
    auto& d = gDetail;
    const bool clip = d.kind == DETAIL_INSERT_CLIP || d.kind == DETAIL_MASTER_CLIP;
    const bool trackClip = d.kind == DETAIL_INSERT_CLIP;
    const bool limiter = d.kind == DETAIL_LIMITER;
    const bool audible = d.track >= 0 || (masterValue(MasterChain::ON) && !masterValue(MasterChain::AB));
    const int x = 36, y = 108, w = clip ? 568 : 286, h = 174;
    float threshold = 0, slope = 0, knee = 0, mixValue = 1, trim = 1, drive = 1, ceiling = 1;
    if (clip) {
        drive = StereoInsert::dbGain(detailValue(trackClip ? 1 : 0));
        if (trackClip) {
            ceiling = StereoInsert::dbGain(detailValue(2));
            trim = StereoInsert::dbGain(detailValue(3));
            mixValue = detailValue(4) * 0.01f;
        } else if (!detailValue(0)) mixValue = 0;
    } else if (!limiter) {
        threshold = detailValue(0);
        slope = 1.0f - 1.0f / detailValue(1);
        knee = detailValue(2);
        mixValue = detailValue(d.track >= 0 ? 9 : 8) * 0.01f;
        trim = StereoInsert::dbGain(detailValue(d.track >= 0 ? 10 : 7));
    }
    text(x, 84, clip ? "TRANSFER / LINEAR" : "TRANSFER / dB", C_TXT);
    graphGrid(x, y, w, h);
    int prev = y + h - 2;
    const int shape = trackClip ? detailValue(0) : CLIP_HARD;
    for (int i = 1; i < w - 1; i++) {
        const float f = (i - 1) / (float)(w - 3);
        float out = f;
        if (clip) {
            const float in = 4.0f * f - 2.0f;
            const float wet = trackClip ? trim * ceiling * clipTransfer(in * drive / ceiling, shape) : softClip(in * drive);
            out = 0.5f + (in + mixValue * (wet - in)) * 0.25f;
        } else {
            const float in = f * 66 - 60.0f;
            if (limiter) {
                const float cap = detailValue(1) * 0.1f + 20.0f * log10f(0.8f);
                out = (60.0f + (detailValue(0) ? fminf(cap, in) : in)) / 66.0f;
            } else {
                const float gain = (1.0f - mixValue) + mixValue * trim * StereoInsert::dbGain(-compressorReduction(in, threshold, knee, slope));
                out = (60.0f + in + 20.0f * log10f(gain)) / 66.0f;
            }
        }
        const int yy = clampi(y + h - 1 - (int)lroundf(out * h), y + 1, y + h - 2);
        const int ref = y + h - 2 - (int)lroundf(f * (h - 3));
        if (i % 4 < 2) fill(x + i, ref, 1, 1, C_DIM);
        curveSegment(x + i, yy, i > 1 ? prev : yy, audible ? DETAIL_ACCENT : C_DIM);
        prev = yy;
    }
    text(x, y + h + 8, clip ? "-2" : "-60", C_DIM);
    textC(x + w / 2, y + h + 8, clip ? "INPUT  0" : "INPUT  -27", C_DIM);
    textR(x + w, y + h + 8, clip ? "+2" : "+6", C_DIM);
    if (clip) return;

    if (U.now - d.sampleAt >= 50) {
        d.sampleAt = U.now;
        float reduction = 0.0f;
        lockA();
        if (audible && limiter && detailValue(0)) reduction = -20.0f * log10f(fmaxf(1e-6f, gEng.mastering.gain));
        if (audible && !limiter) reduction = d.track >= 0 ? scInsertReduction(d.track, d.stage) : gEng.mastering.compressor.reductionDb();
        unlockA();
        d.reduction[d.next] = reduction;
        d.next = (d.next + 1) % DetailView::HISTORY;
        if (d.samples < DetailView::HISTORY) d.samples++;
    }
    const int hx = 350, hw = 254;
    char label[40];
    snprintf(label, sizeof label, "GAIN REDUCTION   %.1f dB", d.reduction[(d.next + DetailView::HISTORY - 1) % DetailView::HISTORY]);
    text(hx, 84, label, C_TXT);
    graphGrid(hx, y, hw, h);
    prev = y + 1;
    for (int i = 0; i < d.samples; i++) {
        const int index = (d.next + DetailView::HISTORY - d.samples + i) % DetailView::HISTORY;
        const int at = hx + hw - 2 - (d.samples - 1 - i) * (hw - 3) / (DetailView::HISTORY - 1);
        const int yy = y + 1 + (int)(fminf(24.0f, d.reduction[index]) * (h - 3) / 24.0f);
        const int gap = i ? (hw - 3) / (DetailView::HISTORY - 1) : 0;
        for (int k = 0; k < gap; k++) fill(at - gap + k, prev, 1, 1, DETAIL_ACCENT);
        curveSegment(at, yy, i ? prev : yy, DETAIL_ACCENT);
        prev = yy;
    }
    text(hx, y + h + 8, "-4.8 s", C_DIM);
    textR(hx + hw, y + h + 8, "NOW", C_DIM);
}

static void drawDetailEnvelope() {
    const DetailKind kind = gDetail.kind;
    const bool mod = kind == DETAIL_MOD, amp = kind == DETAIL_AMP, op = kind == DETAIL_OPERATOR;
    const bool linked = (kind == DETAIL_FILTER || op) && !detailValue(0);
    const int idx = mod || amp ? 0 : op ? 1 : 2;
    const float a = pTime(detailValue(idx)), decay = pTime(detailValue(idx + 1));
    const float sus = mod ? 0.0f : amp ? pLevel(detailValue(idx + 2)) : detailValue(idx + 2) * 0.01f;
    const float release = mod ? 0.0f : pTime(detailValue(idx + 3));
    const int x = 34, y = 108, w = 572, h = 174;
    const float weights[] = {1.0f + log10f(1.0f + 1000 * a), 1.0f + log10f(1.0f + 1000 * decay),
                             mod ? 0.0f : 1.8f, mod ? 0.0f : 1.0f + log10f(1.0f + 1000 * release)};
    const float total = weights[0] + weights[1] + weights[2] + weights[3];
    const char* labels[] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
    text(x, 84, linked ? (op ? "LEGACY ENVELOPE ACTIVE" : "AMP LINK ACTIVE") : mod ? "AD ENVELOPE" : "ADSR ENVELOPE",
         linked ? C_LOCK : C_TXT);
    graphGrid(x, y, w, h);
    const uint32_t accent = linked ? C_DIM : DETAIL_ACCENT;
    const float scale = sus > 1 ? sus : 1;
    const int segments = mod ? 2 : 4;
    int start = x + 1, prev = y + h - 2;
    float extent = 0.0f;
    for (int s = 0; s < segments; s++) {
        extent += weights[s];
        const int end = x + 1 + (int)(extent * (w - 3) / total);
        if (s > 0) fill(start, y + 1, 1, h - 2, C_LINE);
        textC((start + end) / 2, y + h + 8, labels[s], C_DIM);
        for (int i = start; i <= end; i++) {
            const float t = (i - start) / (float)(end - start);
            float value = sus;
            if (s == 0) value = mod ? t : 1.3f * (1.0f - expf(-1.4663371f * t));
            if (s == 1) value = sus + (1.0f - sus) * expf(-7.5f * t);
            if (s == 3) value = sus * expf(-9.2f * t);
            const int yy = clampi(y + h - 2 - (int)lroundf(value * (h - 3) / scale), y + 1, y + h - 2);
            curveSegment(i, yy, prev, accent);
            prev = yy;
        }
        start = end;
    }
}

static void drawDetail() {
    const auto& d = gDetail;
    text(22, 45, d.title, C_TXT);
    char status[40];
    if (d.track >= 0) {
        if (d.kind == DETAIL_INSERT_COMP || d.kind == DETAIL_INSERT_CLIP)
            snprintf(status, sizeof status, "TRACK %d / INSERT %d", d.track + 1, d.stage + 1);
        else snprintf(status, sizeof status, "TRACK %d", d.track + 1);
    } else {
        snprintf(status, sizeof status, "%s", !masterValue(MasterChain::ON) ? "CHAIN OFF" :
                 masterValue(MasterChain::AB) ? "A/B DRY" : "CHAIN ON");
    }
    textR(616, 45, status, DETAIL_ACCENT);
    if (d.kind >= DETAIL_FILTER) drawDetailEnvelope();
    else drawDetailDynamics();
    constexpr int page = 8;
    const int first = d.focus / page * page;
    for (int i = first; i < d.count && i < first + page; i++) {
        const int x = 22 + (i - first) % 4 * 150, y = 321 + (i - first) / 4 * 51;
        const bool focus = i == d.focus;
        fill(x, y, 144, 45, focus ? C_SEL : C_PANEL);
        frame(x, y, 144, 45, focus ? DETAIL_ACCENT : C_LINE);
        text(x + 7, y + 5, rowName(d.params[i]), focus ? DETAIL_ACCENT : C_DIM, 16);
        text(x + 7, y + 24, valueText(d.params[i], detailValue(i)), C_TXT, 16);
    }
    fill(0, 433, SW, SH - 433, C_PANEL);
    text(22, 437, "LEFT/RIGHT Select   UP/DOWN Edit   L1/R1 Coarse", C_TXT);
    if (d.count > page) {
        snprintf(status, sizeof status, "%d/%d", d.focus + 1, d.count);
        textR(616, 437, status, C_DIM);
    }
    text(22, 457, d.kind == DETAIL_FILTER || d.kind == DETAIL_OPERATOR
         ? "A Envelope Source   Y Reset   START Play/Stop   B Close"
         : "Y Reset   START Play/Stop   B Close", C_DIM);
}
