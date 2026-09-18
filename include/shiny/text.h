#pragma once
#include "shiny/core.h"
#include "shiny/state.h"
ScResult<void> sc_font_load(ScResource&,const std::string& root);
// Bounded lazy metrics shared by headless layout and native glyph pages.
const ScGlyph* sc_font_glyph(const ScResource&,int codepoint);
struct ScLetter { int codepoint{}; float x{},y{}; const ScResource* font{}; };
struct ScTextLayout { float width{},height{}; std::vector<ScLetter> letters; bool missing{}; };
ScTextLayout sc_text_layout(const ScWorld*,std::string_view text,float size,std::string_view font={},float wrap=0,int align=0);
ScResult<std::vector<int>> sc_utf8(std::string_view);
