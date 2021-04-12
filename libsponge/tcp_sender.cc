#include "tcp_sender.hh"

#include "tcp_config.hh"

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
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return _bytes_in_flight; }

void TCPSender::fill_window() {
    // if syn has not sent, sent syn first!, then return;
    if (next_seqno_absolute() == 0) {
        TCPSegment s{};
        s.header().syn = true;
        send_nonempty_segment(s);
        return;
    }

    // if TCPSenderStateSummary::SYN_SENT, just push syn, then return;
    if (next_seqno_absolute() > 0 && next_seqno_absolute() == bytes_in_flight()) {
        return;
    }
    //留给 tcp_connection 来处理，因为 sender 没办法知道是不是接受到了 syn.
    //    {TCPSegment s{}; s.header().syn = true; send_nonempty_segment(s); return;}

    if (!stream_in().input_ended()) {
        while (_remote_win_size != 0 && !stream_in().buffer_empty()) {
            uint16_t limit =
                _remote_win_size > TCPConfig::MAX_PAYLOAD_SIZE ? TCPConfig::MAX_PAYLOAD_SIZE : _remote_win_size;
            TCPSegment s{};
            s.payload() = Buffer(stream_in().read(limit));
            send_nonempty_segment(s);
            _remote_win_size = _remote_win_size - s.length_in_sequence_space();
        }
        return;
    }
    if (stream_in().input_ended() &&
        (next_seqno_absolute() <
         stream_in().bytes_written() + 2)) {  // 这里要注意，如果 win_size 允许，要将 FIN 也放在同一个 segment
                                              // 发出去，不要拆分为两个 segment 来发送.
        while (_remote_win_size != 0 && (next_seqno_absolute() < stream_in().bytes_written() + 2)) {
            uint16_t limit =
                _remote_win_size > TCPConfig::MAX_PAYLOAD_SIZE ? TCPConfig::MAX_PAYLOAD_SIZE : _remote_win_size;
            TCPSegment s{};
            s.payload() = Buffer(stream_in().read(limit));
            // MAX_PAYLOAD_SIZE limits payload only
            if (stream_in().buffer_empty() && s.length_in_sequence_space() < TCPConfig::MAX_PAYLOAD_SIZE + 2 &&
                _remote_win_size > s.length_in_sequence_space()) {
                s.header().fin = true;
            }
            send_nonempty_segment(s);
            _remote_win_size = _remote_win_size - s.length_in_sequence_space();
        }
        return;
    }
    if (stream_in().eof() && (next_seqno_absolute() >= stream_in().bytes_written() + 2) &&
        bytes_in_flight()) {  // FIN have sent; nothing need to sent; if need, just _segment_outstanding's resend;
        return;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
// 边界条件，ackno 可能是个错误离谱的值, 它要符合 0 <= unwrap(ackno, _isn, _recv_seqno_absolute) <
// next_seqno_absolute();
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t ackno_absolute = unwrap(ackno, _isn, _recv_seqno_absolute);
    if (ackno_absolute <= next_seqno_absolute() && ackno_absolute > _recv_seqno_absolute) {
        // now we have receive new ackno, the traffic is open again!
        _rto_initial = _initial_retransmission_timeout;
        _retransmissions = 0;
        // handle _segment_outstanding, Take care syn, fin segment!!!!
        while (!_segment_outstanding.empty()) {
            TCPSegment s = _segment_outstanding.front();
            uint64_t start = unwrap(s.header().seqno, _isn, _recv_seqno_absolute);
            uint64_t end = start + s.length_in_sequence_space() - 1;
            if (ackno_absolute <= start)
                break;
            if (ackno_absolute > end) {
                // 这里，我应该加入异常判断
                if (_recv_seqno_absolute > start)
                    start = _recv_seqno_absolute;
                uint64_t deta = end - start + 1;
                if (_bytes_in_flight >= deta)
                    _bytes_in_flight = _bytes_in_flight - deta;
                else
                    cerr << "\nTCPSender::ack_received: _bytes_in_flight < deta" << endl;
                _segment_outstanding.pop();
                continue;
            }
            // here means start < ackno_absolute <= end
            if (_recv_seqno_absolute > start)
                start = _recv_seqno_absolute;
            int deta = ackno_absolute - start;
            if (deta < 0)
                cerr << "\nTCPSender::ack_received: (ackno_absolute - start) < 0" << endl;
            else {
                uint64_t tmp_deta = deta;
                if (_bytes_in_flight >= tmp_deta)
                    _bytes_in_flight = _bytes_in_flight - deta;
                else
                    cerr << "\nTCPSender::ack_received: ackno_absolute - start" << endl;
            }
            break;
        }
        _recv_seqno_absolute = ackno_absolute;
        // reset timer
        if (!_segment_outstanding.empty())
            _rto_timer = 0;
        else
            _timer_active = false;
    }

    if (window_size > 0) {
        _remote_win_size = window_size;
        _ack_win_zero = false;
    } else {
        _remote_win_size = 1;
        _ack_win_zero = true;
    }
    if (_remote_win_size > _bytes_in_flight)
        _remote_win_size = _remote_win_size - _bytes_in_flight;
    else
        _remote_win_size = 0;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // if _segment_outstanding empty, set _timer_active to false, return;
    if (_segment_outstanding.empty()) {
        _timer_active = false;
        return;
    }
    // accumulate timer
    _rto_timer = _rto_timer + ms_since_last_tick;
    // if timeout, resend the first segment;
    // if win_size is not 0: 增加重传次数，超时时间翻倍; set _rto_timer to 0; return;
    if (_rto_timer >= _rto_initial) {
        segments_out().push(_segment_outstanding.front());
        // 这里要区分: ack 直接传入 win_size 为 0 时，因为超时不是因为网络拥堵引起的，所以 _rto_initial 不用增大;
        // 但如果是 ack 传入的 win_size 被用完了变为 0 时，这个时候的超时就要将 _rto_initial 增大;
        if ((_remote_win_size > 0) || (_remote_win_size == 0 && !_ack_win_zero)) {
            _retransmissions = _retransmissions + 1;
            _rto_initial = (_initial_retransmission_timeout << _retransmissions);
        }
        _rto_timer = 0;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retransmissions; }

void TCPSender::send_empty_segment() {
    // empty_segment.length_in_sequence_space() is 0
    // don't need traced by _segment_outstanding
    // for out segment, sender just care ack, syn, fin, seqno, here ack = true, seqno = next_seqno()
    TCPSegment s{};
    s.header().ack = true;
    s.header().seqno = next_seqno();
    segments_out().push(s);
}

void TCPSender::send_nonempty_segment(TCPSegment &s) {
    if (s.length_in_sequence_space() == 0)
        cerr << "\nTCPSender::send_nonempty_segment: length is zero!!!" << endl;
    else {
        s.header().seqno = next_seqno();
        segments_out().push(s);
        _segment_outstanding.push(s);
        if (!_timer_active) {
            _timer_active = true;
            _rto_timer = 0;
        }
        _bytes_in_flight = _bytes_in_flight + s.length_in_sequence_space();
        _next_seqno = _next_seqno + s.length_in_sequence_space();
    }
}
