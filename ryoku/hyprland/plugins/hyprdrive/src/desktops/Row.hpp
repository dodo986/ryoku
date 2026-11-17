#pragma once

// A MONITOR'S ROW OF DESKTOPS: what its overview strip shows and the only
// numbers Hyprdrive ever gives a desktop on it.
//
// Ryoku groups workspace ids in BLOCKS of ten (modules/workspaces.lua, PER):
// block 0 is 1-10, block 1 is 11-20, and Super+N goes to slot N of the block
// you are in. A monitor's row is the block of the desktop it shows, so the
// strip lists that block's desktops, a new desktop takes a free id in it, and
// a reorder or a fullscreen app's own desktop renumbers inside it and never
// into another block.
//
// A row is SLOTS: slot 1..capacity are the block's own ids (base+1 ..
// base+capacity); slot capacity+1, +2, ... are the row's OVERFLOW, ids
// OVERFLOW_ORIGIN + block*OVERFLOW_SPAN + 1, +2, ... A full block still takes
// an 11th desktop, and it can never take the next block's first id. Once the
// block has room again the overflow desktop moves back into it.
//
// With blocks off (capacity 0) every monitor has the IMPLICIT row: base 0,
// unbounded, slot == id, one global number line.
//
// No Hyprland types: tests/unit/row_test.cpp.

#include <cstdint>

namespace Hyprdrive {
    struct Row {
        using Id = long long;

        // Overflow ids: far above any block a desktop is numbered in, one span
        // per block.
        static constexpr Id OVERFLOW_ORIGIN = 1'000'000;
        static constexpr Id OVERFLOW_SPAN   = 1'000;

        Id   base        = 0;
        Id   capacity    = 0; // the block's own slots; 0 = the implicit, unbounded row
        Id   overflow    = 0; // the id before the first overflow id (bounded rows)
        bool provisional = false; // block guessed: nothing in it is renumbered

        static constexpr Row block(Id index, Id slots, bool provisional = false) {
            return {.base = index * slots, .capacity = slots, .overflow = OVERFLOW_ORIGIN + index * OVERFLOW_SPAN, .provisional = provisional};
        }

        // The block an id belongs to (an overflow id: its row's block).
        static constexpr Id blockOf(Id id, Id slots) {
            if (id > OVERFLOW_ORIGIN)
                return (id - OVERFLOW_ORIGIN - 1) / OVERFLOW_SPAN;
            return slots > 0 && id >= 1 ? (id - 1) / slots : 0;
        }
        // The place of an id inside its own block, as Super+N counts it.
        static constexpr Id slotIn(Id id, Id slots) {
            return slots > 0 && id >= 1 ? (id - 1) % slots + 1 : id;
        }

        constexpr bool bounded() const {
            return capacity > 0;
        }
        constexpr Id first() const {
            return base + 1;
        }
        // The highest slot a row can have (the implicit row: none).
        constexpr Id lastSlot() const {
            return bounded() ? capacity + OVERFLOW_SPAN : INT64_MAX;
        }
        // The desktop's place in the row, 1-based; 0 when the id is not in it.
        constexpr Id slot(Id id) const {
            if (!bounded())
                return id >= 1 ? id - base : 0;
            if (id > base && id <= base + capacity)
                return id - base;
            if (id > overflow && id <= overflow + OVERFLOW_SPAN)
                return capacity + (id - overflow);
            return 0;
        }
        constexpr bool contains(Id id) const {
            return slot(id) > 0;
        }
        // In the block itself (not its overflow).
        constexpr bool inBlock(Id id) const {
            const Id S = slot(id);
            return S > 0 && (!bounded() || S <= capacity);
        }
        constexpr bool inOverflow(Id id) const {
            return bounded() && slot(id) > capacity;
        }
        // The id of a slot. A slot below 1 gives base + slot (below the row:
        // DesktopPlan::check refuses it), one past the overflow gives 0.
        constexpr Id idOf(Id s) const {
            if (!bounded() || s <= capacity)
                return base + s;
            if (s <= capacity + OVERFLOW_SPAN)
                return overflow + (s - capacity);
            return 0;
        }
        // Where an id falls on this row's slot line for a range ("every desktop
        // from `from` on"): its slot. The implicit row also takes ids below 1
        // as they are (shift(0, -1) must stay refusable as BelowOne); a bounded
        // row has no place for an id outside it: nullopt-like 0.
        constexpr Id rank(Id id) const {
            if (!bounded())
                return id;
            return slot(id);
        }
    };

    constexpr bool sameRow(const Row& a, const Row& b) {
        return a.base == b.base && a.capacity == b.capacity && a.overflow == b.overflow;
    }
}
