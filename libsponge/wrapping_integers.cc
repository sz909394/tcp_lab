#include "wrapping_integers.hh"

#include <iostream>

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t n_32 = (isn.raw_value() + n) % (1ul << 32);
    return WrappingInt32{n_32};
}

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
    WrappingInt32 checkpoint_seqno = wrap(checkpoint, isn);
    long midway = (1ul << 31);
    int32_t offset = (n - checkpoint_seqno);
    if (offset > 0 && offset <= midway)
        return {checkpoint + offset};

    if (offset > 0 && offset > midway) {
        uint64_t offset_0 = 2 * midway - offset;
        if (checkpoint < offset_0)
            return {checkpoint + offset};
        else
            return {checkpoint - offset_0};
    }

    if (offset == 0)
        return {checkpoint};

    if (offset < 0 && (offset * -1) > midway) {
        return {checkpoint + 2 * midway + offset};
    }

    if (offset < 0 && (offset * -1) <= midway) {
        uint64_t offset_1 = offset * -1;
        if (checkpoint < offset_1) {
            return {checkpoint + 2 * midway + offset};
        } else {
            return {checkpoint + offset};
        }
    }
    return {checkpoint};
}