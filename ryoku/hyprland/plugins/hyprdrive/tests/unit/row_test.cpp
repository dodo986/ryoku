// Host test for Row.hpp: a monitor's row of desktops -- the implicit row
// (slot == id, unbounded), a Ryoku block (base+1..base+10) and its overflow
// (an 11th desktop that can never be another block's), and which block and
// slot an id has.
#include "Row.hpp"

#include <cstdio>

using namespace Hyprdrive;

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                                                                                                                   \
    do {                                                                                                                                                                           \
        ++g_checks;                                                                                                                                                                \
        if (!(c)) {                                                                                                                                                                \
            ++g_failed;                                                                                                                                                            \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                                                                               \
        }                                                                                                                                                                          \
    } while (0)

int main() {
    // ---- the implicit row: the global number line from 1
    {
        const Row R{};
        CHECK(!R.bounded());
        CHECK(R.first() == 1);
        CHECK(R.slot(1) == 1 && R.slot(37) == 37 && R.slot(1'000'001) == 1'000'001);
        CHECK(!R.contains(0) && !R.contains(-5));
        CHECK(R.idOf(5) == 5 && R.idOf(0) == 0);
        CHECK(R.rank(0) == 0 && R.rank(-1) == -1); // shift(0, -1) stays refusable
        CHECK(R.inBlock(99) && !R.inOverflow(99));
    }
    // ---- a block: block 2 = 21..30, then its overflow
    {
        const Row R = Row::block(2, 10);
        CHECK(R.bounded() && R.base == 20 && R.first() == 21);
        CHECK(R.slot(21) == 1 && R.slot(30) == 10);
        CHECK(!R.contains(20) && !R.contains(31) && !R.contains(1) && !R.contains(11));
        CHECK(R.idOf(1) == 21 && R.idOf(10) == 30);
        // the 11th desktop is NOT 31 (block 3's first): it is overflow
        CHECK(R.idOf(11) == 1'002'001 && R.idOf(12) == 1'002'002);
        CHECK(R.slot(1'002'001) == 11 && R.inOverflow(1'002'001) && !R.inBlock(1'002'001));
        CHECK(R.idOf(R.lastSlot()) == 1'003'000 && R.idOf(R.lastSlot() + 1) == 0);
        CHECK(R.idOf(0) == 20 && !R.contains(R.idOf(0))); // below the row: outside it
        for (Row::Id s = 1; s <= R.lastSlot(); ++s)
            CHECK(R.slot(R.idOf(s)) == s);
    }
    // ---- rows never overlap: blocks 0..99, blocks and overflows
    {
        int overlaps = 0;
        for (Row::Id a = 0; a < 20; ++a)
            for (Row::Id b = 0; b < 20; ++b) {
                if (a == b)
                    continue;
                const Row A = Row::block(a, 10), B = Row::block(b, 10);
                for (Row::Id s = 1; s <= A.lastSlot(); s += 7)
                    overlaps += B.contains(A.idOf(s));
            }
        CHECK(overlaps == 0);
        // an overflow id is in no block of ten Super+N reaches: (id-1)/10 is huge
        CHECK((Row::block(0, 10).idOf(11) - 1) / 10 >= 100'000);
        CHECK((Row::block(99, 10).idOf(Row::block(99, 10).lastSlot()) - 1) / 10 < 1'000'000'000);
    }
    // ---- sameRow
    {
        CHECK(sameRow(Row{}, Row{}));
        CHECK(sameRow(Row::block(1, 10), Row::block(1, 10, true))); // provisional is the same row
        CHECK(!sameRow(Row{}, Row::block(0, 10)));                   // block 0 is NOT the implicit row
        CHECK(!sameRow(Row::block(1, 10), Row::block(2, 10)));
        CHECK(Row::block(1, 10, true).provisional);
    }
    // ---- Ryoku's blocks: an id's block and its slot, as Super+N counts it
    {
        CHECK(Row::blockOf(1, 10) == 0 && Row::blockOf(10, 10) == 0 && Row::blockOf(11, 10) == 1 && Row::blockOf(24, 10) == 2);
        CHECK(Row::blockOf(Row::block(3, 10).idOf(11), 10) == 3); // an overflow id: its row's block
        CHECK(Row::blockOf(7, 0) == 0);
        CHECK(Row::slotIn(1, 10) == 1 && Row::slotIn(10, 10) == 10 && Row::slotIn(11, 10) == 1 && Row::slotIn(24, 10) == 4);
        CHECK(Row::slotIn(7, 0) == 7);
        CHECK(Row::block(Row::blockOf(13, 10), 10).slot(13) == Row::slotIn(13, 10));
    }
    std::printf("row_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
