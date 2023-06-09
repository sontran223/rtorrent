// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <algorithm>
#include <functional>

#include <torrent/exceptions.h>
#include <torrent/utils/algorithm.h>

#include "display/frame.h"
#include "display/window.h"

namespace display {

bool
Frame::is_width_dynamic() const {
  switch (m_type) {
    case TYPE_NONE:
      return false;
    case TYPE_WINDOW:
      return m_window->is_active() && m_window->is_width_dynamic();

    case TYPE_ROW:
    case TYPE_COLUMN:
      for (size_type i = 0; i < m_container.size; ++i)
        if (m_container.data[i]->is_width_dynamic())
          return true;

      return false;
  }

  return false;
}

bool
Frame::is_height_dynamic() const {
  switch (m_type) {
    case TYPE_NONE:
      return false;
    case TYPE_WINDOW:
      return m_window->is_active() && m_window->is_height_dynamic();

    case TYPE_ROW:
    case TYPE_COLUMN:
      for (size_type i = 0; i < m_container.size; ++i)
        if (m_container.data[i]->is_height_dynamic())
          return true;

      return false;
  }

  return false;
}

bool
Frame::has_left_frame() const {
  switch (m_type) {
    case TYPE_NONE:
    case TYPE_ROW:
      return false;
    case TYPE_WINDOW:
      return m_window->is_active() && m_window->is_left();

    case TYPE_COLUMN:
      for (size_type i = 0; i < m_container.size; ++i)
        if (m_container.data[i]->has_left_frame())
          return true;

      return false;
  }

  return false;
}

bool
Frame::has_bottom_frame() const {
  switch (m_type) {
    case TYPE_NONE:
    case TYPE_COLUMN:
      return false;
    case TYPE_WINDOW:
      return m_window->is_active() && m_window->is_bottom();

    case TYPE_ROW:
      for (size_type i = 0; i < m_container.size; ++i)
        if (m_container.data[i]->has_bottom_frame())
          return true;

      return false;
  }

  return false;
}

Frame::bounds_type
Frame::preferred_size() const {
  switch (m_type) {
    case TYPE_NONE:
      return bounds_type(0, 0, 0, 0);

    case TYPE_WINDOW:
      if (m_window->is_active())
        return bounds_type(m_window->min_width(),
                           m_window->min_height(),
                           m_window->max_width(),
                           m_window->max_height());
      else
        return bounds_type(0, 0, 0, 0);

    case TYPE_ROW:
    case TYPE_COLUMN: {
      bounds_type accum(0, 0, 0, 0);

      for (size_type i = 0; i < m_container.size; ++i) {
        bounds_type p = m_container.data[i]->preferred_size();

        accum.minWidth += p.minWidth;
        accum.minHeight += p.minHeight;

        if (p.maxWidth == Window::extent_full ||
            accum.maxWidth == Window::extent_full)
          accum.maxWidth = Window::extent_full;
        else
          accum.maxWidth += p.maxWidth;

        if (p.maxHeight == Window::extent_full ||
            accum.maxHeight == Window::extent_full)
          accum.maxHeight = Window::extent_full;
        else
          accum.maxHeight += p.maxHeight;
      }

      return accum;
    }
  }

  return bounds_type(0, 0, 0, 0);
}

void
Frame::set_container_size(size_type size) {
  if ((m_type != TYPE_ROW && m_type != TYPE_COLUMN) || size >= max_size)
    throw torrent::internal_error("Frame::set_container_size(...) Bad state.");

  while (m_container.size > size) {
    delete m_container.data[--m_container.size];
    m_container.data[m_container.size] = nullptr;
  }

  while (m_container.size < size) {
    m_container.data[m_container.size++] = new Frame();
  }
}

void
Frame::initialize_window(Window* window) {
  if (m_type != TYPE_NONE)
    throw torrent::internal_error(
      "Frame::initialize_window(...) m_type != TYPE_NONE.");

  m_type   = TYPE_WINDOW;
  m_window = window;
}

void
Frame::initialize_row(size_type size) {
  if (m_type != TYPE_NONE)
    throw torrent::internal_error(
      "Frame::initialize_container(...) Invalid state.");

  if (size > max_size)
    throw torrent::internal_error(
      "Frame::initialize_container(...) size >= max_size.");

  m_type           = TYPE_ROW;
  m_container.size = size;

  for (size_type i = 0; i < m_container.size; ++i)
    m_container.data[i] = new Frame();
}

void
Frame::initialize_column(size_type size) {
  if (m_type != TYPE_NONE)
    throw torrent::internal_error(
      "Frame::initialize_container(...) Invalid state.");

  if (size > max_size)
    throw torrent::internal_error(
      "Frame::initialize_container(...) size >= max_size.");

  m_type           = TYPE_COLUMN;
  m_container.size = size;

  for (size_type i = 0; i < m_container.size; ++i)
    m_container.data[i] = new Frame();
}

void
Frame::clear() {
  switch (m_type) {
    case TYPE_WINDOW:
      if (m_window != nullptr)
        m_window->set_offscreen(true);

      break;

    case TYPE_ROW:
    case TYPE_COLUMN:
      for (size_type i = 0; i < m_container.size; ++i) {
        m_container.data[i]->clear();
        delete m_container.data[i];
      }
      break;

    default:
      break;
  }

  m_type = TYPE_NONE;
}

void
Frame::refresh() {
  switch (m_type) {
    case TYPE_NONE:
      break;

    case TYPE_WINDOW:
      if (m_window->is_active() && !m_window->is_offscreen())
        m_window->refresh();

      break;

    case TYPE_ROW:
    case TYPE_COLUMN:
      for (Frame **itr  = m_container.data,
                 **last = m_container.data + m_container.size;
           itr != last;
           ++itr)
        (*itr)->refresh();

      break;
  }
}

void
Frame::redraw() {
  switch (m_type) {
    case TYPE_NONE:
      break;

    case TYPE_WINDOW:
      if (m_window->is_active() && !m_window->is_offscreen())
        m_window->redraw();

      break;

    case TYPE_ROW:
    case TYPE_COLUMN:
      for (Frame **itr  = m_container.data,
                 **last = m_container.data + m_container.size;
           itr != last;
           ++itr)
        (*itr)->redraw();

      break;
  }
}

void
Frame::balance(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  m_positionX = x;
  m_positionY = y;
  m_width     = width;
  m_height    = height;

  switch (m_type) {
    case TYPE_NONE:
      break;
    case TYPE_WINDOW:
      balance_window(x, y, width, height);
      break;
    case TYPE_ROW:
      balance_row(x, y, width, height);
      break;
    case TYPE_COLUMN:
      balance_column(x, y, width, height);
      break;
  }
}

inline void
Frame::balance_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  // Ensure that we don't draw windows that are offscreen or have
  // zero extent.
  if (width == 0 || height == 0 || !m_window->is_active()) {
    m_window->set_offscreen(true);
    return;
  }

  if (width > m_window->max_width()) {
    if (m_window->is_left())
      x += width - m_window->max_width();

    width = m_window->max_width();
  }

  if (height > m_window->max_height()) {
    if (m_window->is_bottom())
      y += height - m_window->max_height();

    height = m_window->max_height();
  }

  m_window->set_offscreen(false);
  m_window->resize(x, y, width, height);
  m_window->mark_dirty();
}

inline Frame::extent_type
dynamic_min_height(const Frame::dynamic_type& value) {
  return value.second.min_height();
}
inline Frame::extent_type
dynamic_min_width(const Frame::dynamic_type& value) {
  return value.second.min_width();
}

inline void
Frame::balance_row(uint32_t x, uint32_t y, uint32_t, uint32_t height) {
  // Find the size of the static frames. The dynamic frames are added
  // to a temporary list for the second pass. Each frame uses the
  // m_width and m_height as temporary storage for width and height in
  // this algorithm.
  size_type    dynamicSize = 0;
  dynamic_type dynamicFrames[max_size];

  int remaining = height;

  for (Frame **itr  = m_container.data,
             **last = m_container.data + m_container.size;
       itr != last;
       ++itr) {
    bounds_type bounds = (*itr)->preferred_size();

    if ((*itr)->is_height_dynamic()) {
      (*itr)->m_height             = 0;
      dynamicFrames[dynamicSize++] = std::make_pair(*itr, bounds);

    } else {
      (*itr)->m_height = bounds.minHeight;
      remaining -= bounds.minHeight;
    }
  }

  // Sort the dynamic frames by the min size in the direction we are
  // interested in. Then try to satisfy the largest first, and if we
  // have any remaining space we can use that to extend it and any
  // following frames.
  //
  // Else if we're short, only give each what they require.
  std::stable_sort(dynamicFrames,
                   dynamicFrames + dynamicSize,
                   [](const auto& l, const auto& r) {
                     return dynamic_min_height(l) > dynamic_min_height(r);
                   });

  bool retry;

  do {
    retry = false;

    for (dynamic_type *itr = dynamicFrames, *last = dynamicFrames + dynamicSize;
         itr != last;
         ++itr) {
      uint32_t adjust =
        (std::max(remaining, 0) + std::distance(itr, last) - 1) /
        std::distance(itr, last);

      adjust += itr->first->m_height;
      adjust = std::max(adjust, itr->second.minHeight);
      adjust = std::min(adjust, itr->second.maxHeight);

      remaining -= adjust - itr->first->m_height;

      retry = retry || itr->first->m_height != adjust;

      itr->first->m_height = adjust;
    }

  } while (retry && remaining > 0);

  // Use the pre-calculated frame sizes to balance the sub-frames. If
  // the frame is too small, it will set the remaining windows to zero
  // extent which will flag them as offscreen.

  for (Frame **itr  = m_container.data,
             **last = m_container.data + m_container.size;
       itr != last;
       ++itr) {
    // If there is any remaining space, check if we want to shift
    // the subsequent frames to the other side of this frame.
    if (remaining > 0 && (*itr)->has_bottom_frame()) {
      (*itr)->m_height += remaining;
      remaining = 0;
    }

    (*itr)->balance(x, y, m_width, std::min((*itr)->m_height, height));

    y += (*itr)->m_height;
    height -= (*itr)->m_height;
  }
}

inline void
Frame::balance_column(uint32_t x, uint32_t y, uint32_t width, uint32_t) {
  // Find the size of the static frames. The dynamic frames are added
  // to a temporary list for the second pass. Each frame uses the
  // m_width and m_height as temporary storage for width and height in
  // this algorithm.
  size_type    dynamicSize = 0;
  dynamic_type dynamicFrames[max_size];

  int remaining = width;

  for (Frame **itr  = m_container.data,
             **last = m_container.data + m_container.size;
       itr != last;
       ++itr) {
    bounds_type bounds = (*itr)->preferred_size();

    if ((*itr)->is_width_dynamic()) {
      (*itr)->m_width              = 0;
      dynamicFrames[dynamicSize++] = std::make_pair(*itr, bounds);

    } else {
      (*itr)->m_width = bounds.minWidth;
      remaining -= bounds.minWidth;
    }
  }

  // Sort the dynamic frames by the min size in the direction we are
  // interested in. Then try to satisfy the largest first, and if we
  // have any remaining space we can use that to extend it and any
  // following frames.
  //
  // Else if we're short, only give each what they require.
  std::stable_sort(dynamicFrames,
                   dynamicFrames + dynamicSize,
                   [](const auto& l, const auto& r) {
                     return dynamic_min_width(l) > dynamic_min_width(r);
                   });

  bool retry;

  do {
    retry = false;

    for (dynamic_type *itr = dynamicFrames, *last = dynamicFrames + dynamicSize;
         itr != last;
         ++itr) {
      uint32_t adjust =
        (std::max(remaining, 0) + std::distance(itr, last) - 1) /
        std::distance(itr, last);

      adjust += itr->first->m_width;
      adjust = std::max(adjust, itr->second.minWidth);
      adjust = std::min(adjust, itr->second.maxWidth);

      remaining -= adjust - itr->first->m_width;

      retry = retry || itr->first->m_width != adjust;

      itr->first->m_width = adjust;
    }

  } while (retry && remaining > 0);

  // Use the pre-calculated frame sizes to balance the sub-frames. If
  // the frame is too small, it will set the remaining windows to zero
  // extent which will flag them as offscreen.

  for (Frame **itr  = m_container.data,
             **last = m_container.data + m_container.size;
       itr != last;
       ++itr) {
    // If there is any remaining space, check if we want to shift
    // the subsequent frames to the other side of this frame.
    if (remaining > 0 && (*itr)->has_left_frame()) {
      (*itr)->m_width += remaining;
      remaining = 0;
    }

    (*itr)->balance(x, y, std::min((*itr)->m_width, width), m_height);

    x += (*itr)->m_width;
    width -= (*itr)->m_width;
  }
}

}
