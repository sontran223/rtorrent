// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <torrent/utils/algorithm.h>

#include "core/download.h"
#include "core/view.h"
#include "display/canvas.h"
#include "display/utils.h"
#include "globals.h"
#include "rpc/parse_commands.h"

#include "display/window_download_list.h"

namespace display {

WindowDownloadList::WindowDownloadList()
  : Window(new Canvas, 0, 120, 1, extent_full, extent_full) {}

WindowDownloadList::~WindowDownloadList() {
  if (m_view != nullptr)
    m_view->signal_changed().erase(m_changed_itr);

  m_view = nullptr;
}

void
WindowDownloadList::set_view(core::View* l) {
  if (m_view != nullptr)
    m_view->signal_changed().erase(m_changed_itr);

  m_view = l;

  if (m_view != nullptr)
    m_changed_itr = m_view->signal_changed().insert(
      m_view->signal_changed().begin(), [this] { mark_dirty(); });
}

void
WindowDownloadList::redraw() {
  m_slotSchedule(
    this,
    (cachedTime + torrent::utils::timer::from_seconds(1)).round_seconds());

  m_canvas->erase();

  if (m_view == nullptr)
    return;

  const auto width  = m_canvas->width();
  const auto height = m_canvas->height();
  if (m_view->empty_visible() || width < 5 || height < 2) {
    return;
  }

  m_canvas->print(0,
                  0,
                  "%s",
                  ("[View: " + m_view->name() +
                   (m_view->get_filter_temp().is_empty() ? "" : " (filtered)") +
                   "]")
                    .c_str());

  // show "X of Y"
  if (width > 16 + 8 + m_view->name().length()) {
    int item_idx = m_view->focus() - m_view->begin_visible();
    if (item_idx == int(m_view->size()))
      m_canvas->print(width - 16, 0, "[ none of %-5d]", m_view->size());
    else
      m_canvas->print(
        width - 16, 0, "[%5d of %-5d]", item_idx + 1, m_view->size());
  }

  int               layout_height;
  const std::string layout_name =
    rpc::call_command_string("ui.torrent_list.layout");

  if (layout_name == "full") {
    layout_height = 3;
  } else if (layout_name == "compact") {
    layout_height = 1;
  } else {
    m_canvas->print(
      0, 0, "INVALID ui.torrent_list.layout '%s'", layout_name.c_str());
    return;
  }

  using Range = std::pair<core::View::iterator, core::View::iterator>;

  Range range = torrent::utils::advance_bidirectional(
    m_view->begin_visible(),
    m_view->focus() != m_view->end_visible() ? m_view->focus()
                                             : m_view->begin_visible(),
    m_view->end_visible(),
    height / layout_height);

  // Make sure we properly fill out the last lines so it looks like
  // there are more torrents, yet don't hide it if we got the last one
  // in focus.
  if (range.second != m_view->end_visible())
    ++range.second;

  int   pos    = 1;
  char* buffer = static_cast<char*>(calloc(width + 1, sizeof(char)));
  char* last   = buffer + width - 2 + 1;

  // Add a proper 'column info' method.
  if (layout_name == "compact") {
    print_download_column_compact(buffer, last);

    m_canvas->set_default_attributes(A_BOLD);
    m_canvas->print(0, pos++, "  %s", buffer);
  }

  if (layout_name == "full") {
    while (range.first != range.second) {
      print_download_title(buffer, last, *range.first);
      m_canvas->print(
        0, pos++, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);
      print_download_info_full(buffer, last, *range.first);
      m_canvas->print(
        0, pos++, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);
      print_download_status(buffer, last, *range.first);
      m_canvas->print(
        0, pos++, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);

      range.first++;
    }

  } else {
    while (range.first != range.second) {
      print_download_info_compact(buffer, last, *range.first);
      m_canvas->set_default_attributes(
        range.first == m_view->focus() ? A_REVERSE : A_NORMAL);
      m_canvas->print(
        0, pos++, "%c %s", range.first == m_view->focus() ? '*' : ' ', buffer);

      range.first++;
    }
  }

  free(buffer);
}

}
