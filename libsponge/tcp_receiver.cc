#include "tcp_receiver.hh"

#include <iostream>

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // Set the Initial Sequence Number if necessary
    const TCPHeader tcp_header = seg.header();
    WrappingInt32 seqno = tcp_header.seqno;

    if (not ackno().has_value() && not tcp_header.syn) {
        return;
    }

    if (not ackno().has_value() && tcp_header.syn) {
        isn = seqno;
        isn_set = true;
    }

    // Push any data, or end-of-stream marker, to the StreamReassembler.
    const Buffer tcp_payload = seg.payload();
    Buffer data = tcp_payload.copy();
    if (tcp_header.syn) {
        data.remove_prefix(0);
    }
    uint64_t absolute_seqno = unwrap(seqno, isn, stream_out().bytes_written());
    uint64_t stream_index = absolute_seqno - 1;
    if (tcp_header.syn) {
        stream_index = absolute_seqno;
    }

    // only push to reassembler if it is new data
    WrappingInt32 last_seqno = seqno + seg.length_in_sequence_space();

    if (ackno().has_value() and last_seqno - ackno().value() > 0) {
        if (seqno - ackno().value() < 0 or abs(seqno - ackno().value()) < _capacity) {
            _reassembler.push_substring(data.copy(), stream_index, tcp_header.fin);
        }
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (isn_set) {
        // ack SYN
        WrappingInt32 current_ackno = isn + 1;
        if (stream_out().input_ended()) {
            // ack FIN
            return current_ackno + 1 + stream_out().bytes_written();
        }
        return current_ackno + stream_out().bytes_written();
    }
    return {};
}

size_t TCPReceiver::window_size() const { return _capacity - stream_out().buffer_size(); }
