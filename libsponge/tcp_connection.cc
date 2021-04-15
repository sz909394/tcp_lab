#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return {_sender.stream_in().remaining_capacity()}; }

size_t TCPConnection::bytes_in_flight() const { return {_sender.bytes_in_flight()}; }

size_t TCPConnection::unassembled_bytes() const { return {_receiver.unassembled_bytes()}; }

size_t TCPConnection::time_since_last_segment_received() const { return {_time_since_last_segment_received}; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    _time_since_last_segment_received = 0;
    if (seg.header().rst == true) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        while (!_sender.segments_out().empty()) {
            _sender.segments_out().pop();
        }
        _active = false;
        return;
    }
    if (seg.header().ack == false && seg.header().syn == true && (LISTEN() || SYN_SENT())) {
        _receiver.segment_received(seg);
        if (SYN_SENT())
            _sender.send_empty_segment();
        else
            _sender.fill_window();
        if (_sender.segments_out().empty())
            _sender.send_empty_segment();
        TCPSegment &s = _sender.segments_out().front();
        if (_receiver.ackno().has_value()) {
            s.header().ackno = _receiver.ackno().value();
            s.header().ack = true;
        }
        if (LISTEN())
            s.header().syn = true;
        size_t max_win = numeric_limits<uint16_t>().max();
        s.header().win = min(_receiver.window_size(), max_win);
        segments_out().push(s);
        _sender.segments_out().pop();
        return;
    }
    if (seg.header().ack == true && seg.length_in_sequence_space() == 0) {
        // 当 FSM 处于 LAST_ACK 状态时，如果接收到的 ack 就是FIN的ack，则 FSM 转为 CLOSED.
        bool state = LAST_ACK();
        _sender.ack_received(seg.header().ackno, seg.header().win);
        if (state && !bytes_in_flight())
            _active = false;
        return;
    }
    if (seg.header().ack == true && seg.length_in_sequence_space() > 0) {
        _receiver.segment_received(seg);
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
        if (_sender.segments_out().empty()) {
            _sender.send_empty_segment();
            TCPSegment &s = _sender.segments_out().front();
            if (_receiver.ackno().has_value()) {
                s.header().ackno = _receiver.ackno().value();
                s.header().ack = true;
            }
            size_t max_win = numeric_limits<uint16_t>().max();
            s.header().win = min(_receiver.window_size(), max_win);
            segments_out().push(s);
            _sender.segments_out().pop();
        } else
            send_all_segment();
        if (CLOSE_WAIT())
            _linger_after_streams_finish = false;
        return;
    }
}

bool TCPConnection::active() const { return {_active}; }

size_t TCPConnection::write(const string &data) {
    size_t len = _sender.stream_in().write(data);
    if (len > 0) {
        _sender.fill_window();
        send_all_segment();
    }
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received = _time_since_last_segment_received + ms_since_last_tick;
    if (TIME_WAIT()) {
        if (_time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            _active = false;
            return;
        }
    }
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
        rst_tcpconnection();
        return;
    }
    send_all_segment();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_all_segment();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_all_segment();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            rst_tcpconnection();
            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::rst_tcpconnection() {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _sender.send_empty_segment();
    TCPSegment &s = _sender.segments_out().front();
    if (_receiver.ackno().has_value()) {
        s.header().ackno = _receiver.ackno().value();
        s.header().ack = true;
    }
    size_t max_win = numeric_limits<uint16_t>().max();
    s.header().win = min(_receiver.window_size(), max_win);
    s.header().rst = true;
    segments_out().push(s);
    while (!_sender.segments_out().empty()) {
        _sender.segments_out().pop();
    }
    _active = false;
}

void TCPConnection::send_all_segment() {
    while (!_sender.segments_out().empty()) {
        TCPSegment &s = _sender.segments_out().front();
        if (_receiver.ackno().has_value()) {
            s.header().ackno = _receiver.ackno().value();
            s.header().ack = true;
        }
        size_t max_win = numeric_limits<uint16_t>().max();
        s.header().win = min(_receiver.window_size(), max_win);
        segments_out().push(s);
        _sender.segments_out().pop();
    }
}

bool TCPConnection::LISTEN() { return (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0); }
bool TCPConnection::SYN_SENT() {
    return (_sender.next_seqno_absolute() == _sender.bytes_in_flight() && !_receiver.ackno().has_value());
}
bool TCPConnection::RECEIVER_FIN_RECV() {
    return _receiver.ackno().has_value() && _receiver.stream_out().input_ended();
}

bool TCPConnection::SENDER_SYN_ACKED() { return !_sender.stream_in().eof(); }

bool TCPConnection::SENDER_FIN_ACKED() {
    return _sender.stream_in().eof() && (_sender.next_seqno_absolute() >= _sender.stream_in().bytes_written() + 2) &&
           !_sender.bytes_in_flight();
}

bool TCPConnection::SENDER_FIN_SENT() {
    return _sender.stream_in().eof() && (_sender.next_seqno_absolute() >= _sender.stream_in().bytes_written() + 2) &&
           _sender.bytes_in_flight();
}

bool TCPConnection::TIME_WAIT() { return RECEIVER_FIN_RECV() && SENDER_FIN_ACKED() && _linger_after_streams_finish; }

bool TCPConnection::LAST_ACK() { return RECEIVER_FIN_RECV() && SENDER_FIN_SENT() && !_linger_after_streams_finish; }

bool TCPConnection::CLOSE_WAIT() { return RECEIVER_FIN_RECV() && SENDER_SYN_ACKED(); }
