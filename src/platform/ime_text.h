#pragma once
#include "shiny/input.h"
#include <span>
#include <string_view>
// Fixed-capacity UTF-16 -> UTF-8 preedit conversion, shared with platform tests.
// Attributes use SC_COMPOSITION_KINDS indices per UTF-16 unit; clause offsets
// include zero and the UTF-16 length. Missing/invalid clauses fall back to attribute runs.
bool sc_ime_text(ScDeviceInput&,std::u16string_view,int cursor,std::span<const unsigned char> attributes,std::span<const std::uint32_t> clauses={}) noexcept;
