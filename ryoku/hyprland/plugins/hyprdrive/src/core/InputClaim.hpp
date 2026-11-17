#pragma once

// The answer a surface gives when the pointer hooks offer it a button: whether
// it took the press, and whether it still owes a frame for what it started.
namespace Hyprdrive::Input {

enum class EButton { Menu, Launch, Main, Other };

struct SClaim {
    bool claimed    = false;
    bool needsFrame = false;
};

}
