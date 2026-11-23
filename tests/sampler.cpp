#include "../web/bridge.cpp"

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { printf("FAIL %d: %s\n", __LINE__, #condition); failures++; } } while (0)

static void testSlices() {
    scInit(48000);
    scLoad("");
    static float ones[4800] = {};
    for (int i = 960; i < 1200; i++) ones[i] = 0.7f;
    for (int i = 2880; i < 3120; i++) ones[i] = 0.9f;
    CHECK(scSmpSet(0, ones, 4800, 48000, "break", 0, 1));
    CHECK(scSmpSliceTransients(0) >= 1);
    scSmpSliceGrid(0, 4);
    CHECK(scSmpCutCount(0) == 3 && scSmpCut(0, 0) == 1200);
    CHECK(scSmpCutMove(0, 0, 960));
    CHECK(!scSmpCutMove(0, 0, 2450));
    CHECK(scSmpSliceAt(0, 960) == 1);
    CHECK(scSmpCutRemove(0, 1) && scSmpCutCount(0) == 2);
    CHECK(scSmpCutAdd(0, 2700) == 1);

    const int slice = knobSlot(MachChop::C_SLICE);
    const int follow = knobSlot(MachChop::C_FOLLOW);
    scSetMachine(0, 4);
    scSet(0, MachSmpl::slotSmp(), 0);
    scSet(0, follow, 127);
    scStepSet(0, 0, 1);
    scStepSet(0, 2, 2);
    scStepSet(0, 5, 1);
    scStepSet(0, 9, 1);
    scLockSet(0, 5, slice, 2, 1);
    CHECK(scSliceLocks(0, 0) == 3);
    CHECK(scLockGet(0, 0, slice) == 0 && scLockGet(0, 5, slice) == 1 && scLockGet(0, 9, slice) == 2);
    CHECK(scLockGet(0, 2, slice) == -32768 && scLockGet(0, 9, follow) == 0);
    CHECK(scSliceLocksUndo(0) && scLockGet(0, 5, slice) == 2 && scLockSlide(0, 5, 0));
    CHECK(scSliceLocks(0, 987654321u) == 3);
    int random = scLockGet(0, 5, slice);
    CHECK(random >= 0 && random < 4 && scSliceLocksUndo(0));
    CHECK(scSliceLocks(0, 987654321u) == 3 && scLockGet(0, 5, slice) == random);

    float snap[64] = {};
    snap[slice] = 1;
    snap[knobSlot(MachChop::C_TUNE)] = 64;
    MState state{};
    MachChop machine;
    machine.start(state, 261.63f, 1, snap, 48000, gEng.pool);
    CHECK(state.as<MachSmpl::St>().s == 960);
    CHECK(state.as<MachSmpl::St>().e == 2700);

    const char* saved = scSave();
    CHECK(saved != nullptr);
    scSmpClear();
    CHECK(saved && scLoad(saved) && scSmpWanted(0));
    CHECK(scSmpSet(0, ones, 4800, 48000, "break", 0, 1));
    CHECK(scSmpCutCount(0) == 3 && scSmpCut(0, 1) == 2700);
    CHECK(scLockGet(0, 5, slice) == random && !scSliceLocksUndo(0));
}

static void testRecorder() {
    static Engine engine;
    engine.prepare(48000);
    static float out[512 * 2], input[512 * 2];
    for (float& value : input) value = 0.25f;
    CHECK(engine.recStart(1, 0, "input", 2, Engine::REC_INPUT) > 0);
    engine.recWrite(input, 512);
    engine.recStop();
    CHECK(engine.pool.s[1].n == 512 && engine.pool.used == 1024);
    CHECK(engine.pool.data[engine.pool.s[1].at] == 0.25f);
    CHECK(!engine.recStart(1, 0, "duplicate", 2, Engine::REC_INPUT));

    engine.P.pat[0].t[0].s[1].on = 1;
    engine.start();
    CHECK(engine.recStart(2, 0.1f, "trig", 2, 0) == 4800);
    engine.recArmTrig(0, 1);
    engine.render(out, 512);
    CHECK(engine.recWaiting() && engine.pool.s[2].n == 0);
    for (int i = 0; i < 40 && engine.recOn; i++) engine.render(out, 512);
    CHECK(!engine.recOn && engine.pool.s[2].n == 4800);
    const float* take = engine.pool.data + engine.pool.s[2].at;
    float peak = 0;
    for (int i = 0; i < 9600; i++) if (fabsf(take[i]) > peak) peak = fabsf(take[i]);
    CHECK(peak > 0.001f);

    CHECK(engine.recStart(3, 0.01f, "armed", 2, Engine::REC_MASTER, true) == 480);
    engine.render(out, 512);
    CHECK(engine.recWaiting() && engine.pool.s[3].n == 0);
    engine.recStop();
    CHECK(!engine.pool.wanted(3) && engine.pool.used == 10624);
}

int main() {
    testSlices();
    testRecorder();
    if (!failures) puts("OK: slice editor, locks, project round trip and recorder");
    return failures != 0;
}
