#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return time_pass; }

void TCPConnection::_send_outbound_segments() {
    while (not _sender.segments_out().empty()) {
        WrappingInt32 receiver_ackno{0};
        size_t receiver_win_size{0};
        bool ack_flag = false;
        if (_receiver.ackno().has_value()) {
            receiver_ackno = _receiver.ackno().value();
            receiver_win_size = _receiver.window_size();
            ack_flag = true;
        }

        TCPSegment new_seg = _sender.segments_out().front();
        new_seg.header().ackno = receiver_ackno;
        new_seg.header().win = receiver_win_size;
        new_seg.header().ack = ack_flag;

        if (timeout or destruct) {
            new_seg.header().rst = true;
        }

        _segments_out.push(new_seg);
        _sender.segments_out().pop();
        if (new_seg.header().fin) {
            outbound_fully_sent = true;
            fin_sequence_no = new_seg.header().seqno;
        }
    }
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // check RST flag
    if (seg.header().rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        // kill connection
        kill_connection = true;
    } else {
        _receiver.segment_received(seg);

        if (seg.header().ack) {
            if (_sender.next_seqno_absolute() == 0) {
                return;
            }

            _sender.ack_received(seg.header().ackno, seg.header().win);

            if (outbound_fully_sent and _receiver.ackno().has_value() and seg.header().ackno - 1 == fin_sequence_no) {
                outbound_fully_ack = true;
            }
        }

        if (seg.length_in_sequence_space() > 0) {
            // if the incoming segment occupied any sequence numbers,
            // the TCPConnection makes sure that at least one segment is sent in reply,
            // to reflect an update in the ackno and window size.
            if (not connect_called) {
                connect();
            } else {
                _sender.send_empty_segment();
            }
        }

        if (_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and
            seg.header().seqno == _receiver.ackno().value() - 1) {
            _sender.send_empty_segment();
        }
    }

    _send_outbound_segments();

    if (inbound_stream().input_ended()) {
        inbound_end = true;
        // inbound end but outbound haven't EOF
        if (not _sender.stream_in().eof()) {
            _linger_after_streams_finish = false;
        }
    }

    time_pass = 0;
}

bool TCPConnection::active() const {
    // return true until TCP connection is total done
    // totally done: shut down cleanly  or it recieved a RST

    bool shut_down =
        inbound_end && outbound_end && outbound_fully_sent && outbound_fully_ack && not _linger_after_streams_finish;

    return not kill_connection && not timeout && not shut_down;
}

size_t TCPConnection::write(const string &data) {
    // tell sender to do something
    size_t data_written = _sender.stream_in().write(data);

    _sender.fill_window();

    // push to TCP connection_segments_out
    _send_outbound_segments();
    return data_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    time_pass += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        // abort the connection
        timeout = true;

        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
    }

    if (inbound_end && outbound_end && time_pass >= 10 * _cfg.rt_timeout) {
        _linger_after_streams_finish = false;
    }
    _send_outbound_segments();
}

void TCPConnection::end_input_stream() {
    // outbound
    _sender.stream_in().end_input();

    _sender.fill_window();
    _send_outbound_segments();
    outbound_end = true;
}

void TCPConnection::connect() {
    // send a SYN segment
    connect_called = true;
    _sender.fill_window();

    _send_outbound_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            destruct = true;
            _sender.send_empty_segment();
            _send_outbound_segments();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
