// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <torrent/exceptions.h>
#include <torrent/torrent.h>
#include <torrent/utils/thread_base.h>

#include "control.h"
#include "display/frame.h"
#include "display/manager.h"
#include "display/window_log_complete.h"
#include "input/manager.h"

#include "ui/element_log_complete.h"

namespace ui {

ElementLogComplete::ElementLogComplete(torrent::log_buffer* l)
  : m_window(NULL)
  , m_log(l) {

  unsigned int signal_index =
    torrent::main_thread()->signal_bitfield()->add_signal(
      std::bind(&ElementLogComplete::received_update, this));

  m_log->lock_and_set_update_slot(
    std::bind(&torrent::thread_base::send_event_signal,
              torrent::main_thread(),
              signal_index,
              false));
}

void
ElementLogComplete::activate(display::Frame* frame, bool focus) {
  if (is_active())
    throw torrent::internal_error(
      "ui::ElementLogComplete::activate(...) is_active().");

  control->input()->push_back(&m_bindings);

  m_window = new WLogComplete(m_log);
  m_window->set_active(true);

  m_frame = frame;
  m_frame->initialize_window(m_window);
}

void
ElementLogComplete::disable() {
  if (!is_active())
    throw torrent::internal_error(
      "ui::ElementLogComplete::disable(...) !is_active().");

  control->input()->erase(&m_bindings);

  m_frame->clear();
  m_frame = NULL;

  delete m_window;
  m_window = NULL;
}

display::Window*
ElementLogComplete::window() {
  return m_window;
}

void
ElementLogComplete::received_update() {
  if (m_window != NULL)
    m_window->mark_dirty();
}

}
