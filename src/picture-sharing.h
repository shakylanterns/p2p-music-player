#ifndef PICTURE_SHARING_H
#define PICTURE_SHARING_H

#include "message-type.h"
#include <asio.hpp>
#include <fstream>
#include <iostream>
#include <queue>
#include <vector>

using namespace std::literals;

class PictureSharing {
  public:
    PictureSharing();
    ~PictureSharing();
    void open_file_for_writing();
    void reset_sharing_file();
    void try_writing_segment();
    int get_next_assigned_id();

    void push_segment(ReturnPictureSegment rps);
    int get_next_segment_id();
    bool all_segments_asked();
    int get_segment_count();
    void set_segment_count(int t);
    int new_peer(peer_id id);
    // give the peer 10 seconds to send the data back
    void start_timeout(int assigned_id);
    // if nothing, drop it
    void end_timeout(int assigned_id);
    // if the waiting flag is on for a peer, execute the handler
    void if_waiting(std::function<void(int)> handler);
    int get_peer_id(int assigned_id);

  private:
    std::vector<std::queue<ReturnPictureSegment>> queue_buffer;
    int current_segment_id = -1;
    int current_byte = 0;
    int current_writing_id = 0;
    std::ofstream os;
    int current_assigned_id = -1;
    int total_segment_count;

    void write_segment(const ReturnPictureSegment &rps);
    // this flag will be for peer if it is idling (the queue is full) or
    // the there is no response from the peer
    std::vector<bool> waiting;
    std::vector<asio::high_resolution_timer> timeout_timers;
    asio::io_context ctx;
    std::thread worker;
    std::vector<int> peer_map;
};

#endif