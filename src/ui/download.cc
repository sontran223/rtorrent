// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <torrent/chunk_manager.h>
#include <torrent/connection_manager.h>
#include <torrent/data/file_list.h>
#include <torrent/exceptions.h>
#include <torrent/peer/connection_list.h>
#include <torrent/throttle.h>
#include <torrent/torrent.h>
#include <torrent/tracker_list.h>
#include <torrent/utils/string_manip.h>

#include "control.h"
#include "core/download.h"
#include "core/manager.h"
#include "display/text_element_string.h"
#include "display/window_download_statusbar.h"
#include "display/window_title.h"
#include "input/manager.h"
#include "rpc/parse_commands.h"
#include "ui/download.h"
#include "ui/element_chunks_seen.h"
#include "ui/element_file_list.h"
#include "ui/element_menu.h"
#include "ui/element_peer_list.h"
#include "ui/element_text.h"
#include "ui/element_tracker_list.h"
#include "ui/element_transfer_list.h"
#include "ui/root.h"

namespace ui {

Download::Download(core::Download* d)
  : m_download(d)
  , m_state(DISPLAY_MAX_SIZE)
  , m_focusDisplay(false) {

  m_windowDownloadStatus = new WDownloadStatus(d);
  m_windowDownloadStatus->set_bottom(true);

  m_uiArray[DISPLAY_MENU]          = create_menu();
  m_uiArray[DISPLAY_PEER_LIST]     = new ElementPeerList(d);
  m_uiArray[DISPLAY_INFO]          = create_info();
  m_uiArray[DISPLAY_FILE_LIST]     = new ElementFileList(d);
  m_uiArray[DISPLAY_TRACKER_LIST]  = new ElementTrackerList(d);
  m_uiArray[DISPLAY_CHUNKS_SEEN]   = new ElementChunksSeen(d);
  m_uiArray[DISPLAY_TRANSFER_LIST] = new ElementTransferList(d);

  m_uiArray[DISPLAY_MENU]->slot_exit(
    [m_slot_exit = &m_slot_exit] { (*m_slot_exit)(); });
  m_uiArray[DISPLAY_PEER_LIST]->slot_exit(
    [this] { activate_display_menu(DISPLAY_PEER_LIST); });
  m_uiArray[DISPLAY_INFO]->slot_exit(
    [this] { activate_display_menu(DISPLAY_INFO); });
  m_uiArray[DISPLAY_FILE_LIST]->slot_exit(
    [this] { activate_display_menu(DISPLAY_FILE_LIST); });
  m_uiArray[DISPLAY_TRACKER_LIST]->slot_exit(
    [this] { activate_display_menu(DISPLAY_TRACKER_LIST); });
  m_uiArray[DISPLAY_CHUNKS_SEEN]->slot_exit(
    [this] { activate_display_menu(DISPLAY_CHUNKS_SEEN); });
  m_uiArray[DISPLAY_TRANSFER_LIST]->slot_exit(
    [this] { activate_display_menu(DISPLAY_TRANSFER_LIST); });

  bind_keys();
}

Download::~Download() {
  if (is_active()) {
    torrent::destruct_error(
      "ui::Download::~Download() called on an active object.");
    return;
  }

  std::for_each(
    m_uiArray, m_uiArray + DISPLAY_MAX_SIZE, [](ElementBase* b) { delete b; });

  delete m_windowDownloadStatus;
}

inline ElementBase*
Download::create_menu() {
  ElementMenu* element = new ElementMenu;

  element->push_back(
    "Peer list",
    [this] { activate_display_focus(DISPLAY_PEER_LIST); },
    [this] { activate_display_menu(DISPLAY_PEER_LIST); });
  element->push_back(
    "Info",
    [this] { activate_display_focus(DISPLAY_INFO); },
    [this] { activate_display_menu(DISPLAY_INFO); });
  element->push_back(
    "File list",
    [this] { activate_display_focus(DISPLAY_FILE_LIST); },
    [this] { activate_display_menu(DISPLAY_FILE_LIST); });
  element->push_back(
    "Tracker list",
    [this] { activate_display_focus(DISPLAY_TRACKER_LIST); },
    [this] { activate_display_menu(DISPLAY_TRACKER_LIST); });
  element->push_back(
    "Chunks seen",
    [this] { activate_display_focus(DISPLAY_CHUNKS_SEEN); },
    [this] { activate_display_menu(DISPLAY_CHUNKS_SEEN); });
  element->push_back(
    "Transfer list",
    [this] { activate_display_focus(DISPLAY_TRANSFER_LIST); },
    [this] { activate_display_menu(DISPLAY_TRANSFER_LIST); });

  element->set_entry(0, false);

  m_bindings['p'] = [element] { element->set_entry_trigger(0); };
  m_bindings['o'] = [element] { element->set_entry_trigger(1); };
  m_bindings['i'] = [element] { element->set_entry_trigger(2); };
  m_bindings['u'] = [element] { element->set_entry_trigger(3); };

  return element;
}

inline ElementBase*
Download::create_info() {
  using namespace display::helpers;

  ElementText* element = new ElementText(rpc::make_target(m_download));

  element->set_column(1);
  element->set_interval(1);

  // Get these bindings with some kind of string map.

  element->push_column("Name:", te_command("d.name="));
  element->push_column("Local id:", te_command("d.local_id_html="));
  element->push_column("Info hash:", te_command("d.hash="));
  element->push_column("Created:",
                       te_command("cat=$convert.date=$d.creation_date=,\" "
                                  "\",$convert.time=$d.creation_date="));

  element->push_back("");
  element->push_column("Directory:", te_command("d.directory="));
  element->push_column("Base Path:", te_command("d.base_path="));
  element->push_column("Tied to file:", te_command("d.tied_to_file="));
  element->push_column("File stats:",
                       te_command("cat=$if=$d.is_multi_file=\\,multi\\,single,"
                                  "\" \",$d.size_files=,\" files\""));

  element->push_back("");
  element->push_column(
    "Chunks:",
    te_command("cat=(d.completed_chunks),\" / \",(d.size_chunks),\" * "
               "\",(d.chunk_size),\" (\",(d.wanted_chunks),\")\""));
  element->push_column("Priority:", te_command("d.priority="));
  element->push_column(
    "Peer exchange:",
    te_command(
      "cat=$if=$d.peer_exchange=\\,enabled\\,disabled,\\ ,"
      "$if=$d.is_pex_active=\\,active\\,$d.is_private=\\,private\\,inactive,"
      "\\ (,$d.size_pex=,/,$d.max_size_pex=,)"));

  element->push_column("State changed:",
                       te_command("convert.elapsed_time=$d.state_changed="));

  element->push_back("");
  element->push_column(
    "Memory usage:",
    te_command("cat=$convert.mb=$pieces.memory.current=,\" MB\""));
  element->push_column(
    "Max memory usage:",
    te_command("cat=$convert.mb=$pieces.memory.max=,\" MB\""));
  element->push_column(
    "Free diskspace:",
    te_command("cat=$convert.mb=$d.free_diskspace=,\" MB\""));
  element->push_column(
    "Safe diskspace:",
    te_command("cat=$convert.mb=$pieces.sync.safe_free_diskspace=,\" MB\""));

  element->push_back("");
  element->push_column(
    "Connection type:",
    te_command("cat=(d.connection_current),\" \",(if,(d.accepting_seeders),"
               ",\"no_seeders\")"));
  element->push_column(
    "Choke heuristic:",
    te_command("cat=(d.up.choke_heuristics),\", "
               "\",(d.down.choke_heuristics),\", \",(d.group)"));
  element->push_column("Safe sync:",
                       te_command("if=$pieces.sync.always_safe=,yes,no"));
  element->push_column(
    "Send buffer:",
    te_command("cat=$convert.kb=$network.send_buffer.size=,\" KB\""));
  element->push_column(
    "Receive buffer:",
    te_command("cat=$convert.kb=$network.receive_buffer.size=,\" KB\""));

  // TODO: Define a custom command for this and use $argument.0 instead of
  // looking up the name multiple times?
  element->push_column(
    "Throttle:",
    te_command(
      "branch=d.throttle_name=,\""
      "cat=$d.throttle_name=,\\\"  [Max \\\","
      "$convert.throttle=$throttle.up.max=$d.throttle_name=,\\\"/\\\","
      "$convert.throttle=$throttle.down.max=$d.throttle_name=,\\\" KB]  [Rate "
      "\\\","
      "$convert.kb=$throttle.up.rate=$d.throttle_name=,\\\"/\\\","
      "$convert.kb=$throttle.down.rate=$d.throttle_name=,\\\" KB]\\\"\","
      "cat=\"global\""));

  element->push_back("");
  element->push_column(
    "Upload:",
    te_command(
      "cat=$convert.kb=$d.up.rate=,\" KB / \",$convert.xb=$d.up.total="));
  element->push_column(
    "Download:",
    te_command(
      "cat=$convert.kb=$d.down.rate=,\" KB / \",$convert.xb=$d.down.total="));
  element->push_column(
    "Skipped:",
    te_command(
      "cat=$convert.kb=$d.skip.rate=,\" KB / \",$convert.xb=$d.skip.total="));
  element->push_column(
    "Preload:",
    te_command("cat=$pieces.preload.type=,\" / \",$pieces.stats_preloaded=,\" "
               "/ \",$pieces.stats_not_preloaded="));

  element->set_column_width(element->column_width() + 1);

  return element;
}

void
Download::activate(display::Frame* frame, bool) {
  if (is_active())
    throw torrent::internal_error(
      "ui::Download::activate() called on an already activated object.");

  control->input()->push_back(&m_bindings);

  m_frame = frame;
  m_frame->initialize_row(2);

  m_frame->frame(1)->initialize_window(m_windowDownloadStatus);
  m_windowDownloadStatus->set_active(true);

  activate_display_menu(DISPLAY_PEER_LIST);
}

void
Download::disable() {
  if (!is_active())
    throw torrent::internal_error(
      "ui::Download::disable() called on an already disabled object.");

  control->input()->erase(&m_bindings);

  activate_display_focus(DISPLAY_MAX_SIZE);

  m_windowDownloadStatus->set_active(false);
  m_frame->clear();

  m_frame = nullptr;
}

void
Download::activate_display(Display displayType, bool focusDisplay) {
  if (!is_active())
    throw torrent::internal_error(
      "ui::Download::activate_display(...) !is_active().");

  if (displayType > DISPLAY_MAX_SIZE)
    throw torrent::internal_error(
      "ui::Download::activate_display(...) out of bounds");

  if (focusDisplay == m_focusDisplay && displayType == m_state)
    return;

  display::Frame* frame = m_frame->frame(0);

  // Cleanup previous state.
  switch (m_state) {
    case DISPLAY_PEER_LIST:
    case DISPLAY_INFO:
    case DISPLAY_FILE_LIST:
    case DISPLAY_TRACKER_LIST:
    case DISPLAY_CHUNKS_SEEN:
    case DISPLAY_TRANSFER_LIST:
      m_uiArray[DISPLAY_MENU]->disable();
      m_uiArray[m_state]->disable();

      frame->clear();
      break;

    case DISPLAY_MENU:
    case DISPLAY_MAX_SIZE:
      break;
  }

  m_state        = displayType;
  m_focusDisplay = focusDisplay;

  // Initialize new state.
  switch (displayType) {
    case DISPLAY_PEER_LIST:
    case DISPLAY_INFO:
    case DISPLAY_FILE_LIST:
    case DISPLAY_TRACKER_LIST:
    case DISPLAY_CHUNKS_SEEN:
    case DISPLAY_TRANSFER_LIST:
      frame->initialize_column(2);

      m_uiArray[DISPLAY_MENU]->activate(frame->frame(0), !focusDisplay);
      m_uiArray[displayType]->activate(frame->frame(1), focusDisplay);
      break;

    case DISPLAY_MENU:
    case DISPLAY_MAX_SIZE:
      break;
  }

  // Set title.
  switch (displayType) {
    case DISPLAY_MAX_SIZE:
      break;
    default:
      control->ui()->window_title()->set_title(m_download->info()->name());
      break;
  }

  control->display()->adjust_layout();
}

void
Download::receive_max_uploads(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->set_uploads_max(
    std::max<int32_t>(m_download->download()->uploads_max() + t, 0));
}

void
Download::receive_min_uploads(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->set_uploads_min(
    std::max<int32_t>(m_download->download()->uploads_min() + t, 0));
}

void
Download::receive_max_downloads(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->set_downloads_max(
    std::max<int32_t>(m_download->download()->downloads_max() + t, 0));
}

void
Download::receive_min_downloads(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->set_downloads_min(
    std::max<int32_t>(m_download->download()->downloads_min() + t, 0));
}

void
Download::receive_min_peers(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->connection_list()->set_min_size(std::max<int32_t>(
    m_download->download()->connection_list()->min_size() + t, (int32_t)5));
}

void
Download::receive_max_peers(int t) {
  m_windowDownloadStatus->mark_dirty();

  m_download->download()->connection_list()->set_max_size(std::max<int32_t>(
    m_download->download()->connection_list()->max_size() + t, (int32_t)5));
}

void
Download::receive_next_priority() {
  m_download->set_priority((m_download->priority() + 1) % 4);
}

void
Download::receive_prev_priority() {
  m_download->set_priority((m_download->priority() - 1) % 4);
}

void
Download::adjust_down_throttle(int throttle) {
  core::ThrottleMap::iterator itr = control->core()->throttles().find(
    m_download->bencode()->get_key("rtorrent").get_key_string("throttle_name"));

  if (itr == control->core()->throttles().end() ||
      itr->second.second == nullptr || itr->first == "NULL")
    control->ui()->adjust_down_throttle(throttle);
  else
    itr->second.second->set_max_rate(std::max<int>(
      (itr->second.second->is_throttled() ? itr->second.second->max_rate()
                                          : 0) +
        throttle * 1024,
      0));

  if (m_uiArray[DISPLAY_INFO]->is_active())
    m_uiArray[DISPLAY_INFO]->mark_dirty();
}

void
Download::adjust_up_throttle(int throttle) {
  core::ThrottleMap::iterator itr = control->core()->throttles().find(
    m_download->bencode()->get_key("rtorrent").get_key_string("throttle_name"));

  if (itr == control->core()->throttles().end() ||
      itr->second.first == nullptr || itr->first == "NULL")
    control->ui()->adjust_up_throttle(throttle);
  else
    itr->second.first->set_max_rate(std::max<int>(
      (itr->second.first->is_throttled() ? itr->second.first->max_rate() : 0) +
        throttle * 1024,
      0));

  if (m_uiArray[DISPLAY_INFO]->is_active())
    m_uiArray[DISPLAY_INFO]->mark_dirty();
}

void
Download::bind_keys() {
  m_bindings['1'] = [this] { receive_min_uploads(-1); };
  m_bindings['2'] = [this] { receive_min_uploads(1); };
  m_bindings['3'] = [this] { receive_max_uploads(-1); };
  m_bindings['4'] = [this] { receive_max_uploads(1); };
  m_bindings['!'] = [this] { receive_min_downloads(-1); };
  m_bindings['@'] = [this] { receive_min_downloads(1); };
  m_bindings['#'] = [this] { receive_max_downloads(-1); };
  m_bindings['$'] = [this] { receive_max_downloads(1); };
  m_bindings['5'] = [this] { receive_min_peers(-5); };
  m_bindings['6'] = [this] { receive_min_peers(5); };
  m_bindings['7'] = [this] { receive_max_peers(-5); };
  m_bindings['8'] = [this] { receive_max_peers(5); };
  m_bindings['+'] = [this] { receive_next_priority(); };
  m_bindings['-'] = [this] { receive_prev_priority(); };

  m_bindings['t'] = [download = m_download->download()] {
    download->manual_request(false);
  };
  m_bindings['T'] = [download = m_download->download()] {
    download->manual_request(true);
  };

  const char* keys = control->ui()->get_throttle_keys();

  m_bindings[keys[0]] = [this] {
    adjust_up_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.small"));
  };
  m_bindings[keys[1]] = [this] {
    adjust_up_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.small"));
  };
  m_bindings[keys[2]] = [this] {
    adjust_down_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.small"));
  };
  m_bindings[keys[3]] = [this] {
    adjust_down_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.small"));
  };

  m_bindings[keys[4]] = [this] {
    adjust_up_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.medium"));
  };
  m_bindings[keys[5]] = [this] {
    adjust_up_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.medium"));
  };
  m_bindings[keys[6]] = [this] {
    adjust_down_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.medium"));
  };
  m_bindings[keys[7]] = [this] {
    adjust_down_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.medium"));
  };

  m_bindings[keys[8]] = [this] {
    adjust_up_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.large"));
  };
  m_bindings[keys[9]] = [this] {
    adjust_up_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.large"));
  };
  m_bindings[keys[10]] = [this] {
    adjust_down_throttle(
      (int)rpc::call_command_value("ui.throttle.global.step.large"));
  };
  m_bindings[keys[11]] = [this] {
    adjust_down_throttle(
      (int)-rpc::call_command_value("ui.throttle.global.step.large"));
  };
}

}
