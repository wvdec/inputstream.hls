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

#include "hls/HLS.h"

class DemuxContainer {
public:
  DemuxContainer() : demux_packet(0), pcr(0), segment_changed(false), current_time(0) {};
  DemuxPacket *demux_packet;
  uint64_t pcr;
  hls::Segment segment;
  bool segment_changed;
  int32_t current_time;
};
