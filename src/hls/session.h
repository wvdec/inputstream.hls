#pragma once
/*
 *      Copyright (C) 2017 Anthony Waters <awaters1@gmail.com>
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include <unordered_map>
#include <vector>
#include <future>
#include <thread>

#include "HLS.h"
#include "../downloader/downloader.h"
#include "../demuxer/demux.h"
#include "stream.h"

namespace hls {
  const int SEGMENTS_BEFORE_SWITCH = 5;

  class Session {
  public:
    Session(MasterPlaylist master_playlist, Downloader *downloader, int min_bandwidth, int max_bandwidth, bool manual_streams);
    virtual ~Session();
    Session(const Session& other) = delete;
    Session & operator= (const Session & other) = delete;
    uint64_t get_total_time();

    INPUTSTREAM_IDS get_streams();
    INPUTSTREAM_INFO get_stream(uint32_t stream_id);

    DemuxContainer get_current_pkt();
    void read_next_pkt();
    uint64_t get_current_time();
    bool seek_time(double time, bool backwards, double *startpts);
    void demux_abort();
    void demux_flush();
  protected:
    virtual MediaPlaylist download_playlist(std::string url);
    // Downloader has to be deleted last
    std::unique_ptr<Downloader> downloader;
  private:
    int min_bandwidth;
    int max_bandwidth;
    bool manual_streams;
  private:
    void switch_streams(uint32_t media_sequence);
    uint32_t last_switch_sequence;

    uint32_t stall_counter;


    MasterPlaylist master_playlist;

    std::unique_ptr<StreamContainer> active_stream;
    // For when we want to switch streams
    std::unique_ptr<StreamContainer> future_stream;
    bool switch_demux;

    DemuxContainer current_pkt;

    double m_startpts;          ///< start PTS for the program chain
    double m_startdts;          ///< start DTS for the program chain
    uint64_t last_total_time;
    uint64_t last_current_time;
  };
}
