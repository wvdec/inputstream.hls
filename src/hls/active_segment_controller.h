/*
 * active_segment_queue.h Copyright (C) 2017 Anthony Waters <awaters1@gmail.com>
 */

#pragma once

#include <unordered_map>
#include <future>
#include <thread>
#include <vector>
#include <atomic>

#include "active_segment.h"
#include "../downloader/downloader.h"

static const int NUM_RELOAD_TRIES = 10;

enum class SegmentState {
	UNKNOWN,
	DOWNLOADING,
	DOWNLOADED,
	DEMUXING,
	DEMUXED
};

struct SegmentData {
  SegmentState state = SegmentState::UNKNOWN;
  std::string content;
  hls::ActiveSegment* active_segment;
};

struct SegmentHasher {
	std::size_t operator()(hls::Segment segment) const {
		using std::size_t;
		using std::hash;
		using std::string;

		return (hash<string>()(segment.get_url()))
				^ (hash<uint32_t>()(segment.byte_length) >> 1)
				^ (hash<uint32_t>()(segment.byte_offset) << 1);
	}
};

class ActiveSegmentController {
public:
  ActiveSegmentController(Downloader *downloader);
  ~ActiveSegmentController();
  void set_media_playlist(hls::MediaPlaylist media_playlist, hls::Segment active_segment);
  void set_media_playlist(hls::MediaPlaylist media_playlist);
  std::future<std::unique_ptr<hls::ActiveSegment>> get_next_segment();
  void set_current_segment(hls::Segment segment);
  hls::Segment get_current_segment();
  hls::MediaPlaylist get_current_playlist() { return media_playlist; };
  bool is_live() { return media_playlist.live; };
  double get_average_bandwidth() { return downloader->get_average_bandwidth(); };
  double get_current_bandwidth() { return downloader->get_current_bandwidth(); };
  uint32_t get_bandwidth_of_current_playlist() { return media_playlist.bandwidth; };
  double get_percentage_buffer_full() { return segment_data.size() / (double) max_segment_data; };
private:
  bool has_next_demux_segment();
  bool has_next_download_segment();
  void download_next_segment();
  void demux_next_segment();
  void reload_playlist();
  void print_segment_data();
private:
  // This pointer is managed by the session
  Downloader *downloader;
  int max_segment_data;
  FRIEND_TEST(ActiveSegmentController, DownloadSegment);
  std::unordered_map<hls::Segment, SegmentData, SegmentHasher> segment_data;
  std::unordered_map<hls::Segment, std::promise<std::unique_ptr<hls::ActiveSegment>>, SegmentHasher> segment_promises;
  std::unordered_map<std::string, std::string> aes_uri_to_key;

  std::mutex private_data_mutex;
  FRIEND_TEST(ActiveSegmentController, DownloadSegment);
  // Where the download is pointing
  int32_t download_segment_index;
  // Where next segment is pointing
  int32_t current_segment_index;
  // Segment we started at, may be empty
  hls::Segment start_segment;
  FRIEND_TEST(ActiveSegmentController, ReloadPlaylist);
  hls::MediaPlaylist media_playlist;

  std::vector<hls::Segment> last_downloaded_segments;

  // Download thread
  std::condition_variable download_cv;
  std::mutex download_mutex;
  std::thread download_thread;

  // Demux thread
  std::condition_variable demux_cv;
  std::mutex demux_mutex;
  std::thread demux_thread;

  // Reload playlist thread
  std::condition_variable reload_cv;
  std::mutex reload_mutex;
  std::thread reload_thread;
  std::atomic_bool reload_playlist_flag;

  // get_next_segment waiting
  std::condition_variable next_segment_cv;
  std::mutex next_segment_mutex;

  std::atomic_bool quit_processing;
};
