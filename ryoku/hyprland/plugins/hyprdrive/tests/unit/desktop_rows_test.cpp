// Host test for DesktopPlan.hpp ON ROWS (Row.hpp): every plan Hyprdrive makes
// keeps each desktop inside its monitor's row (a Ryoku block) and never
// touches another row. desktop_plan_test.cpp is the implicit row (blocks off).
#include "DesktopPlan.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>

using namespace Hyprdrive;
using namespace Hyprdrive::DesktopPlan;

static int g_checks = 0, g_failed = 0;
#define CHECK(c)                                                                                                                                                                   \
    do {                                                                                                                                                                           \
        ++g_checks;                                                                                                                                                                \
        if (!(c)) {                                                                                                                                                                \
            ++g_failed;                                                                                                                                                            \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                                                                               \
        }                                                                                                                                                                          \
    } while (0)

static std::vector<Desk> applied(std::vector<Desk> desks, const std::vector<Move>& moves) {
    for (const auto& M : moves)
        for (auto& D : desks)
            if (D.key == M.key)
                D.id = M.to;
    return desks;
}
static bool unique(const std::vector<Desk>& desks) {
    std::set<Id> seen;
    for (const auto& D : desks)
        if (D.id < 1 || !seen.insert(D.id).second)
            return false;
    return true;
}
static Desk desk(Key k, Id id, const Row& row, int windows = 1) {
    return {.key = k, .id = id, .windows = windows, .row = row};
}

int main() {
    const Row B0 = Row::block(0, 10), B1 = Row::block(1, 10), B2 = Row::block(2, 10);

    // ---- check: a move out of the row is refused; nothing else is
    {
        const std::vector<Desk> D{desk(1, 11, B1), desk(2, 12, B1), desk(3, 21, B2)};
        CHECK(check(D, {{1, 13}}) == Verdict::Ok);
        CHECK(check(D, {{1, 21}}) == Verdict::OutsideRow); // another monitor's block
        CHECK(check(D, {{1, 3}}) == Verdict::OutsideRow);
        CHECK(check(D, {{1, 10}}) == Verdict::OutsideRow); // below the row
        CHECK(check(D, {{2, 21}}) == Verdict::OutsideRow);
        CHECK(check(D, {{1, B1.idOf(11)}}) == Verdict::Ok); // its own overflow
        CHECK(check(D, {{1, B2.idOf(11)}}) == Verdict::OutsideRow);
        // a desktop numbered in the row is a member like any other
        const std::vector<Desk> P{desk(1, 21, B2), desk(2, 22, B2)};
        CHECK(check(P, {{1, 23}}) == Verdict::Ok);
        CHECK(check(P, {{1, 22}}) == Verdict::Collision); // a real collision still is refused
    }
    // ---- shift in a block: only that row; the 10th goes to overflow, never to 21
    {
        std::vector<Desk> D;
        for (Id i = 11; i <= 20; ++i)
            D.push_back(desk(Key(i), i, B1));
        D.push_back(desk(100, 21, B2));
        D.push_back(desk(101, 1, B0));
        const auto M = shiftMoves(D, B1, 15, +1);
        CHECK(M.size() == 6);
        CHECK(check(D, M) == Verdict::Ok);
        const auto A = applied(D, M);
        CHECK(unique(A));
        CHECK(A[9].id == 1'001'001); // was 20
        CHECK(A[10].id == 21 && A[11].id == 1);
        CHECK(shiftMoves(D, B1, 25, +1).empty()); // not in the row
        CHECK(shiftMoves(D, B1, 5, +1).empty());
        // and back: the overflow desktop returns to 20
        const auto BACK = shiftMoves(A, B1, 16, -1);
        CHECK(check(A, BACK) == Verdict::Ok);
        const auto A2 = applied(A, BACK);
        for (size_t i = 0; i < 10; ++i)
            CHECK(i == 4 || A2[i].id == Id(11 + i)); // (15 itself went to 16 and back to 15: the -1 from 16 skips it)
        CHECK(A2[9].id == 20);
    }
    // ---- payMoves in a row: the empty desk goes to the row's top
    {
        const std::vector<Desk> D{desk(1, 21, B2), desk(2, 22, B2, 0), desk(3, 23, B2), desk(4, 1'002'001, B2), desk(5, 11, B1)};
        const auto              M = payMoves(D, B2, 22, 2);
        CHECK(check(D, M) == Verdict::Ok);
        const auto A = applied(D, M);
        CHECK(A[2].id == 22 && A[3].id == 30 && A[1].id == 1'002'001 && A[4].id == 11); // the overflow desk comes back into the block
    }
    // ---- compact in a block with a desktop at 2 and an overflow desktop: all
    // of them from the row's first place, the shown empty one after
    {
        const std::vector<Room> R{{1, 22, true}, {2, 24, true}, {3, 27, true}, {4, 1'002'001, true}, {5, 29, false, true}};
        const auto              M = compactMoves(R, B2);
        std::map<Key, Id>       to{{1, 22}, {2, 24}, {3, 27}, {4, 1'002'001}, {5, 29}};
        for (const auto& X : M)
            to[X.key] = X.to;
        CHECK(to[1] == 21 && to[2] == 22 && to[3] == 23 && to[4] == 24 && to[5] == 25);
        // idempotent
        std::vector<Room> R2 = R;
        for (auto& X : R2)
            X.id = to[X.key];
        CHECK(compactMoves(R2, B2).empty());
    }
    // ---- heal in a row: the loser gets the first free place in ITS row
    {
        const std::vector<Desk> D{desk(1, 21, B2, 2), desk(2, 21, B2, 1), desk(3, 22, B2), desk(4, 11, B1)};
        const auto              M = healMoves(D);
        CHECK(M.size() == 1 && M[0].key == 2 && M[0].to == 23);
        CHECK(check(D, M) == Verdict::Ok);
        // a full block: the loser goes to overflow, not to 31
        std::vector<Desk> F;
        for (Id i = 21; i <= 30; ++i)
            F.push_back(desk(Key(i), i, B2));
        F.push_back(desk(99, 25, B2, 0));
        const auto MF = healMoves(F);
        CHECK(MF.size() == 1 && MF[0].to == 1'002'001);
    }

    // ---- PROPERTIES on random rows: every operation stays in its row
    std::mt19937 rng(20261111);
    for (int round = 0; round < 3000; ++round) {
        const bool        IMPLICIT = rng() % 4 == 0;
        std::vector<Row>  rows;
        if (IMPLICIT)
            rows.push_back(Row{});
        else {
            std::set<Id> used;
            const int    N = 1 + int(rng() % 3);
            while (int(rows.size()) < N) {
                const Id B = rng() % 4;
                if (used.insert(B).second)
                    rows.push_back(Row::block(B, 10));
            }
        }
        std::vector<Desk> D;
        std::set<Id>      taken;
        Key               k = 1;
        for (const auto& R : rows) {
            const int N = int(rng() % 13);
            for (int i = 0; i < N; ++i) {
                const Id S  = 1 + Id(rng() % (R.bounded() ? 14 : 30));
                const Id ID = R.idOf(S);
                if (!ID || !taken.insert(ID).second)
                    continue;
                D.push_back(desk(k++, ID, R, int(rng() % 3)));
            }
        }
        const auto IN_ROW = [&](const std::vector<Desk>& before, const std::vector<Move>& moves) {
            for (const auto& M : moves)
                for (const auto& X : before)
                    if (X.key == M.key && !X.row.contains(M.to))
                        return false;
            return true;
        };
        const auto OTHERS_UNTOUCHED = [&](const std::vector<Move>& moves, const Row& row) {
            for (const auto& M : moves)
                for (const auto& X : D)
                    if (X.key == M.key && !sameRow(X.row, row))
                        return false;
            return true;
        };
        const Row& ROW  = rows[rng() % rows.size()];
        const Id   FROM = ROW.idOf(1 + Id(rng() % 12));
        for (const int DELTA : {+1, -1}) {
            const auto M = shiftMoves(D, ROW, FROM, DELTA);
            CHECK(OTHERS_UNTOUCHED(M, ROW));
            const auto V = check(D, M);
            CHECK(V != Verdict::Unknown && V != Verdict::Twice);
            if (V == Verdict::Ok) {
                CHECK(IN_ROW(D, M));
                CHECK(unique(applied(D, M)));
            }
        }
        // pay
        {
            std::vector<Key> members;
            for (const auto& X : D)
                if (member(X, ROW))
                    members.push_back(X.key);
            if (!members.empty()) {
                const Key  E = members[rng() % members.size()];
                Id         at = 0;
                for (const auto& X : D)
                    if (X.key == E)
                        at = X.id;
                const auto M = payMoves(D, ROW, at, E);
                CHECK(OTHERS_UNTOUCHED(M, ROW));
                if (check(D, M) == Verdict::Ok)
                    CHECK(IN_ROW(D, M) && unique(applied(D, M)));
            }
        }
        // permute (a strip drag): the row's own ids, reordered
        {
            std::vector<Desk*> mem;
            std::vector<Id>    pool;
            for (auto& X : D)
                if (member(X, ROW)) {
                    mem.push_back(&X);
                    pool.push_back(X.id);
                }
            std::ranges::shuffle(pool, rng);
            std::vector<Move> M;
            for (size_t i = 0; i < mem.size(); ++i)
                M.push_back({mem[i]->key, pool[i]});
            CHECK(check(D, M) == Verdict::Ok);
            CHECK(OTHERS_UNTOUCHED(M, ROW));
        }
        // heal (on a copy with one duplicate)
        {
            auto H = D;
            if (H.size() >= 2) {
                const size_t A = rng() % H.size(), B = rng() % H.size();
                if (A != B && sameRow(H[A].row, H[B].row))
                    H[B].id = H[A].id;
            }
            const auto M = healMoves(H);
            CHECK(check(H, M) == Verdict::Ok);
            CHECK(IN_ROW(H, M) || !unique(H));
            CHECK(unique(applied(H, M)));
        }
        // compact, per row
        {
            std::vector<Move> plan;
            for (const auto& R : rows) {
                std::vector<Room> group;
                for (const auto& X : D)
                    if (member(X, R))
                        group.push_back({X.key, X.id, X.windows > 0, false});
                const auto M = compactMoves(group, R);
                plan.insert(plan.end(), M.begin(), M.end());
                // populated first..: places 1..n, in their old order
                const auto      A = applied(D, M);
                std::vector<Id> oldOrder;
                for (const auto& X : D)
                    if (member(X, R) && X.windows > 0)
                        oldOrder.push_back(R.slot(X.id));
                std::ranges::sort(oldOrder);
                Id   s       = 0;
                bool ordered = true;
                for (const Id OLD : oldOrder) {
                    ++s;
                    bool found = false;
                    for (size_t i = 0; i < D.size(); ++i)
                        if (member(D[i], R) && D[i].windows > 0 && R.slot(D[i].id) == OLD)
                            found = R.slot(A[i].id) == s;
                    ordered &= found;
                }
                CHECK(ordered);
                // idempotent
                std::vector<Room> again;
                for (size_t i = 0; i < D.size(); ++i)
                    if (member(D[i], R))
                        again.push_back({A[i].key, A[i].id, A[i].windows > 0, false});
                CHECK(compactMoves(again, R).empty());
            }
            CHECK(check(D, plan) == Verdict::Ok);
            CHECK(IN_ROW(D, plan));
            CHECK(unique(applied(D, plan)));
        }
    }

    std::printf("desktop_rows_test: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
