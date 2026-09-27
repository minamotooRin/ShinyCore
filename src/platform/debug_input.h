#pragma once
#include <span>
// Nonblocking bytes from redirected stdin: zero means pending, -1 means EOF.
int sc_debug_input(std::span<char> destination);
