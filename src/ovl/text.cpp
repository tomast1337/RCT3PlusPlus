// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/text.hpp"

namespace rct3::ovl {

namespace {

constexpr std::string_view kTextTag = "txt";

void append_utf8(std::string& out, char32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

std::vector<TextItem> read_texts(const Overlay& overlay, const Part& part) {
  const AddressSpace space = overlay.space();
  std::vector<TextItem> items;
  for (const auto& symbol : part.symbols) {
    if (!symbol.is_pointer) continue;
    const SymbolName parts = split_symbol_name(symbol.name);
    if (parts.tag != kTextTag) continue;
    items.push_back({parts.name, space.utf16_at(symbol.data)});
  }
  return items;
}

std::string to_utf8(std::u16string_view text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char16_t unit = text[i];
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
        append_utf8(out, 0x10000 + ((static_cast<char32_t>(unit) - 0xD800) << 10) +
                             (static_cast<char32_t>(text[i + 1]) - 0xDC00));
        ++i;
      } else {
        append_utf8(out, 0xFFFD);
      }
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      append_utf8(out, 0xFFFD);
    } else {
      append_utf8(out, unit);
    }
  }
  return out;
}

}  // namespace rct3::ovl
