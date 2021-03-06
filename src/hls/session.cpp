/*
 * session.cpp Copyright (C) 2017 Anthony Waters <awaters1@gmail.com>
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>
#include <climits>

#include "decrypter.h"

#include "session.h"
#include "../globals.h"

#define LOGTAG                  "[SESSION] "


uint64_t hls::Session::get_current_time() {
  uint64_t current_time = current_pkt.current_time;
  if (current_time == 0) {
    return last_current_time;
  }
  last_current_time = current_time;
  return current_time;
}

DemuxContainer hls::Session::get_current_pkt() {
  if (!current_pkt.demux_packet) {
    read_next_pkt();
  }
  DemuxPacket *pkt = current_pkt.demux_packet;
  if (pkt && pkt->iStreamId != DMX_SPECIALID_STREAMCHANGE) {
    // When we have a discontinuity we have to modify the PTS values
    // of the incoming packets to match the existing stream
    // But we have to keep track of which times have a different start value
    // to take into account seeking
    bool discontinuity = current_pkt.discontinuity;
    if (discontinuity) {
      xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Detected a discontinuity at pts %f",
                            pkt->pts);
      m_startpts = DVD_NOPTS_VALUE;
      m_startdts = DVD_NOPTS_VALUE;
    }
    if (m_startpts == DVD_NOPTS_VALUE && pkt->pts != DVD_NOPTS_VALUE &&
        m_startdts == DVD_NOPTS_VALUE && pkt->dts != DVD_NOPTS_VALUE) {
      double desired_pts = current_pkt.segment.time_in_playlist * DVD_TIME_BASE;
      double diff = pkt->pts - desired_pts;
      m_startpts = diff;
      m_startdts = diff;
    }

    if (pkt->pts != DVD_NOPTS_VALUE && m_startpts != DVD_NOPTS_VALUE) {
      pkt->pts = pkt->pts - m_startpts;
    }
    if (pkt->dts != DVD_NOPTS_VALUE && m_startdts != DVD_NOPTS_VALUE) {
      pkt->dts = pkt->dts - m_startdts;
    }
  }

  return current_pkt;
}

void hls::Session::read_next_pkt() {
  if (active_stream) {
    current_pkt = active_stream->get_demux()->Read();
    if (current_pkt.segment_changed) {
      // So we decide to switch in one segment, then the next segment we switch, this gives the demuxer
      // one segment's worth of time to prepare packets
        xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Changed to segment %d", current_pkt.segment.media_sequence);
      if (switch_demux && future_stream) {
        // Should only switch if future_demux is at the next segment we are looking for
        if (future_stream->get_demux()->get_current_media_sequence() == current_pkt.segment.media_sequence) {
          xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Switched stream at segment %d", current_pkt.segment.media_sequence);
          active_stream.swap(future_stream);
          delete future_stream.release();
          ipsh->FreeDemuxPacket(current_pkt.demux_packet);
          current_pkt = active_stream->get_demux()->Read();
          switch_demux = false;

          last_switch_sequence = current_pkt.segment.media_sequence;
        } else {
            xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Delaying stream switch at %d waiting for %d",
                      current_pkt.segment.media_sequence, future_stream->get_demux()->get_current_media_sequence());
            if (current_pkt.segment.media_sequence > future_stream->get_demux()->get_current_media_sequence()) {
              xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Cancelling switch");
              delete future_stream.release();
            }
            switch_streams(current_pkt.segment.media_sequence + 1);
            switch_demux = !(future_stream == nullptr);
        }
      } else {
        switch_streams(current_pkt.segment.media_sequence + 1);
        switch_demux = !(future_stream == nullptr);
      }
    }
  } else {
    xbmc->Log(ADDON::LOG_ERROR, LOGTAG "No active demux, unable to get data");
    current_pkt = DemuxContainer();
  }
}

hls::MediaPlaylist hls::Session::download_playlist(std::string url) {
  FileMediaPlaylist media_playlist;
  media_playlist.open(url.c_str());
  return media_playlist;
}


// Switch streams up and down based on
// 1. current bandwidth
// 2. If we able to keep our buffer full in active_segment_controller
// 3. If we stalled at all in get next segment
void hls::Session::switch_streams(uint32_t media_sequence) {
  if ((media_sequence != 0 && media_sequence < last_switch_sequence + SEGMENTS_BEFORE_SWITCH)) {
    // Skip stream switch if we are in the middle of one
    return;
  }
  // Bits per second
  uint32_t bandwith_of_current_stream = 0;
  double average_bandwidth = downloader->get_average_bandwidth();
  bool switch_up = true;
  if (active_stream) {
    bandwith_of_current_stream = active_stream->get_stream()->get_playlist().bandwidth;
    // TODO: Also update this to detect stalls in the demux waiting for packets
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Switch Stream stalls: %d buffer: %f bandwidth: %f media sequence: %d current: %d",
        stall_counter, active_stream->get_demux()->get_percentage_packet_buffer_full(), average_bandwidth, media_sequence,
        bandwith_of_current_stream);
    if (average_bandwidth <= (bandwith_of_current_stream * .80)) {
      switch_up = false;
      bandwith_of_current_stream = 0;
    }
  }
  if (average_bandwidth < min_bandwidth) {
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Setting average bandwidth to %d", min_bandwidth);
    average_bandwidth = min_bandwidth;
  }
  if (average_bandwidth > max_bandwidth && max_bandwidth != 0) {
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Setting average bandwidth to %d", max_bandwidth);
    average_bandwidth = max_bandwidth;
  }
  std::vector<MediaPlaylist> &media_playlists = master_playlist.get_media_playlists();
  auto next_active_playlist = media_playlists.end();
  for(auto it = media_playlists.begin(); it != media_playlists.end(); ++it) {
    if (switch_up && it->bandwidth > bandwith_of_current_stream && it->bandwidth < average_bandwidth && it->bandwidth >= min_bandwidth &&
         (it->bandwidth <= max_bandwidth || max_bandwidth == 0)) {
       bandwith_of_current_stream = it->bandwidth;
       next_active_playlist = it;
       xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "(Up) Variant stream bandwidth: %d url: %s", it->bandwidth, it->get_url().c_str());
    } else if (!switch_up && it->bandwidth >= bandwith_of_current_stream && it->bandwidth < average_bandwidth && it->bandwidth >= min_bandwidth &&
        (it->bandwidth <= max_bandwidth || max_bandwidth == 0)) {
      // Switch down
       bandwith_of_current_stream = it->bandwidth;
       next_active_playlist = it;
       xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "(Down) Variant stream bandwidth: %d url: %s", it->bandwidth, it->get_url().c_str());
    }
  }

  if (active_stream && next_active_playlist != media_playlists.end() &&
      *next_active_playlist != active_stream->get_stream()->get_playlist() && !manual_streams) {
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Switching to playlist %d %s", next_active_playlist->bandwidth, next_active_playlist->get_url().c_str());
    if (future_stream && *next_active_playlist == future_stream->get_stream()->get_playlist()) {
      // Ignore switch to the current playlist
    } else {
      if (next_active_playlist->live) {
        next_active_playlist->clear_segments();
      }
      future_stream = std::unique_ptr<StreamContainer>(new StreamContainer(*next_active_playlist, downloader.get(), media_sequence));
    }
  } else if (!active_stream) {
    if (next_active_playlist == media_playlists.end()) {
      next_active_playlist = media_playlists.begin();
    }
    active_stream = std::unique_ptr<StreamContainer>(new StreamContainer(*next_active_playlist, downloader.get(), 0));
  } else if (next_active_playlist != media_playlists.end() && *next_active_playlist == active_stream->get_stream()->get_playlist() && future_stream){
      xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Cancelling playlist switch because it is the current one");
      delete future_stream.get();
  } else {
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Not switching playlist manual: %d, min: %d, max: %d", manual_streams, min_bandwidth, max_bandwidth);
  }
}

INPUTSTREAM_IDS hls::Session::get_streams() {
  // Load the first segment of the active playlactive_segmentist to obtain the streams
  // from the mpeg2ts
  if (!active_stream) {
    INPUTSTREAM_IDS ids = INPUTSTREAM_IDS();
    ids.m_streamCount = 0;
    return ids;
  }
  return active_stream->get_demux()->GetStreamIds();
}

INPUTSTREAM_INFO hls::Session::get_stream(uint32_t stream_id) {
  for(size_t i = 0; i < get_streams().m_streamCount; ++i) {
    if (active_stream->get_demux()->GetStreams()[i].m_pID == stream_id) {
      return active_stream->get_demux()->GetStreams()[i];
    }
  }
  return INPUTSTREAM_INFO();
}

bool hls::Session::seek_time(double time, bool backwards, double *startpts) {
  if (active_stream) {
    // time is in MSEC
    double desired = time / 1000.0;

    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "%s: bw:%d desired:%+6.3f", __FUNCTION__, backwards, desired);
    if (active_stream->get_stream()->empty()) {
      std::promise<void> promise;
      std::future<void> future = promise.get_future();
      active_stream->get_stream()->wait_for_playlist(std::move(promise));
      future.wait();
      xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "%s: Playlist is ready", __FUNCTION__);
    }

    if (active_stream->get_stream()->empty()) {
        return false;
    }


    hls::Segment seek_to = active_stream->get_stream()->find_segment_at_time(desired);
    double new_time = seek_to.time_in_playlist;
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "seek to %+6.3f", new_time);

    hls::MediaPlaylist &active_playlist = active_stream->get_stream()->get_updated_playlist();
    xbmc->Log(ADDON::LOG_DEBUG, LOGTAG "Using playlist %s", active_playlist.get_url().c_str());
    active_stream = std::unique_ptr<StreamContainer>(
        new StreamContainer(active_playlist, downloader.get(), seek_to.media_sequence));


    if (current_pkt.demux_packet) {
      ipsh->FreeDemuxPacket(current_pkt.demux_packet);
      current_pkt.demux_packet = 0;
    }

    m_startdts = m_startpts = DVD_NOPTS_VALUE;

    *startpts = (new_time * DVD_TIME_BASE);

    // Cancel any stream switches
    switch_demux = false;
    if (future_stream) {
      delete future_stream.release();
    }
    return true;
  }
  return false;
}

hls::Session::Session(MasterPlaylist master_playlist, Downloader *downloader,
    int min_bandwidth, int max_bandwidth, bool manual_streams) :
    min_bandwidth(min_bandwidth),
    max_bandwidth(max_bandwidth),
    manual_streams(manual_streams),
    master_playlist(master_playlist),
    active_stream(nullptr),
    future_stream(nullptr),
    downloader(downloader),
    switch_demux(false),
    m_startpts(DVD_NOPTS_VALUE),
    m_startdts(DVD_NOPTS_VALUE),
    last_total_time(0),
    last_current_time(0),
    last_switch_sequence(0),
    stall_counter(0) {
  switch_streams(0);
}

uint64_t hls::Session::get_total_time() {
  uint64_t current_total_time = active_stream->get_stream()->get_total_duration();
  if (current_total_time == 0) {
    return last_total_time;
  }
  last_total_time = current_total_time;
  return current_total_time;
}

void hls::Session::demux_abort() {
  // TODO: Causes issues so disable for now
  // in order to properly implement we need a lock on them
  // in case other threads are using them
//  if (active_demux) {
//    delete active_demux.get();
//  }
//  if (future_demux) {
//    delete future_demux.get();
//  }
}

void hls::Session::demux_flush() {
  if (active_stream) {
    active_stream->get_demux()->Flush();
  }
  if (future_stream) {
    future_stream->get_demux()->Flush();
  }
}

hls::Session::~Session() {
  if (current_pkt.demux_packet) {
    ipsh->FreeDemuxPacket(current_pkt.demux_packet);
  }
}
