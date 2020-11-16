#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <algorithm>
#include <iostream>
#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , outstanding_segments() {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - unwrap(current_ackno, _isn, 0); }

void TCPSender::fill_window() {
    size_t window_size_to_fill = 0;
    if (current_win_size == 0) {
        window_size_to_fill = 1 - bytes_in_flight();
    } else {
        window_size_to_fill = current_win_size - bytes_in_flight();
    }

    // read ByteStream
    size_t total_bytes_read = 0;
    while ((not syn_sent || stream_in().buffer_size() > 0 || (stream_in().eof() && not fin_sent)) &&
           (total_bytes_read < window_size_to_fill)) {
        // create a new segment
        TCPSegment new_seg;

        // set seqno
        new_seg.header().seqno = next_seqno();
        uint64_t absolute_seqno = next_seqno_absolute();

        // set syn bit
        if (not syn_sent) {
            // syn bit
            new_seg.header().syn = true;
            syn_sent = true;
            total_bytes_read += 1;
            _next_seqno += 1;
        }

        // read data
        size_t stream_size = stream_in().buffer_size();
        std::string data = stream_in().read(
            std::min(std::min(window_size_to_fill - total_bytes_read, TCPConfig::MAX_PAYLOAD_SIZE), stream_size));

        // set payload
        new_seg.payload() = std::string(data);
        total_bytes_read += data.size();
        _next_seqno += data.size();

        // set fin bit
        if (stream_in().eof() && total_bytes_read < window_size_to_fill) {
            new_seg.header().fin = true;
            fin_sent = true;
            total_bytes_read += 1;
            _next_seqno += 1;
        }

        // push to _segments_out
        _segments_out.push(new_seg);

        // push to outstangind segments (because it has not been ack yet)
        outstanding_segments[absolute_seqno] = new_seg;

        // check retransimission timer
        if (new_seg.length_in_sequence_space() > 0 && not _timer.has_start()) {
            _timer.start();
        }
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // When the receiver gives the sender an ackno that acknowledges the successful receipt of new data
    if (ackno - next_seqno() > 0) {
        return;
    }
    if (ackno - current_ackno > 0 && current_ackno != _isn) {
        _timer.stop();
        _timer.reset(_initial_retransmission_timeout);

        if (outstanding_segments.size() > 0) {
            _timer.start();
        }
        consecutive_retransmission_count = 0;
    }

    if (ackno - current_ackno > 0) {
        current_ackno = ackno;
    }
    uint64_t absolute_ackno = unwrap(current_ackno, _isn, 0);
    current_win_size = window_size;

    // scan outstanding segments, remove any that have been fully acknowledged
    auto it = outstanding_segments.cbegin();
    while (it != outstanding_segments.cend()) {
        uint64_t segment_upper = it->first + it->second.payload().size();
        if (it->second.header().fin) {
            segment_upper += 1;
        }
        if (segment_upper <= absolute_ackno) {
            it = outstanding_segments.erase(it);
        } else {
            ++it;
        }
    }

    // When all outstanding data has been acknowledged, stop the retransmission timer
    if (outstanding_segments.size() == 0) {
        _timer.stop();
    }

    // fill window if there is new space
    if (current_win_size > 0) {
        fill_window();
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // If tick is called and the retransmission timer has expired:
    if (_timer.has_expired(ms_since_last_tick)) {
        // Retransmit the earliest (lowest sequence number)
        // segment that hasn’t been fully acknowledged by the TCP receiver.
        uint64_t lower = unwrap(current_ackno, _isn, 0);
        uint64_t earliest_seqno = unwrap(current_ackno + current_win_size, _isn, 0);
        TCPSegment earliest;
        bool find_earliest = false;

        auto it = outstanding_segments.cbegin();
        while (it != outstanding_segments.cend()) {
            uint64_t segment_upper = it->first + it->second.payload().size();
            if (segment_upper < lower) {
                ++it;
                continue;
            } else {
                // not fully acknowledged yet
                // find the earliest segment
                if (it->first <= earliest_seqno) {
                    earliest_seqno = it->first;
                    earliest = it->second;
                    find_earliest = true;
                }
                ++it;
            }
        }

        if (find_earliest) {
            _segments_out.push(earliest);
            if (current_win_size > 0) {
                consecutive_retransmission_count += 1;
            }
        }

        // double the init rto
        if (current_win_size > 0) {
            _timer.double_rto();
        }
        _timer.start();

    } else {
        _timer.update_rto(ms_since_last_tick);
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return consecutive_retransmission_count; }

void TCPSender::send_empty_segment() {
    // create a new segment
    TCPSegment new_seg;

    // set seqno
    new_seg.header().seqno = next_seqno();

    _segments_out.push(new_seg);
}
