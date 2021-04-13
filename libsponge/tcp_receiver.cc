#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    if (!_ISN_FLAG) {
        if (seg.header().syn) {
            _ISN_FLAG = true;
            _ISN = seg.header().seqno;
            _reassembler.push_substring(seg.payload().copy(), 0, seg.header().fin);
        }
    } else {
        if (!_reassembler.stream_out().input_ended()) {
            uint64_t checkpoint = _reassembler.stream_out().bytes_written() - 1;
            uint64_t index = unwrap(seg.header().seqno, _ISN, checkpoint);
            _reassembler.push_substring(seg.payload().copy(), (index - 1), seg.header().fin);
        }
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!_ISN_FLAG)
        return {};
    else {
        if (!_reassembler.stream_out().input_ended())
            return {wrap(_reassembler.stream_out().bytes_written() + 1, _ISN)};
        else
            return {wrap(_reassembler.stream_out().bytes_written() + 2, _ISN)};
    }
}

size_t TCPReceiver::window_size() const { return (_capacity - _reassembler.stream_out().buffer_size()); }
