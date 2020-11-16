#include "wrapping_integers.hh"

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) { return WrappingInt32{isn + n}; }

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    WrappingInt32 wrapped_checkpoint = wrap(checkpoint, isn);

    int32_t seqno_diff = n - wrapped_checkpoint;
    int64_t int64_seqno_diff = seqno_diff;
    int32_t int32_checkpoint = checkpoint;

    if (checkpoint < UINT32_MAX) {
        uint32_t a = int32_checkpoint + seqno_diff;
        return static_cast<uint64_t>(a);
    }
    return checkpoint + int64_seqno_diff;
}
