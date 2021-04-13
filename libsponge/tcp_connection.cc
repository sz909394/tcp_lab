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

void TCPConnection::segment_received(const TCPSegment &seg) 
{ 
    DUMMY_CODE(seg); 
}

bool TCPConnection::active() const { return {_active}; }

size_t TCPConnection::write(const string &data) {
    size_t len = _sender.stream_in().write(data);
    if(len > 0){
        _sender.fill_window();
        send_all_segment();
    }
    return len;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) 
{ 
    DUMMY_CODE(ms_since_last_tick); 
}

void TCPConnection::end_input_stream() 
{
    if(SYN_SENT() || LISTEN())
    {
        _active = false;
    }
    else
    {
        _sender.stream_in().end_input();
        _sender.fill_window();
        send_all_segment();
    }
}

void TCPConnection::connect()
{
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

void TCPConnection::rst_tcpconnection()
{
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _sender.send_empty_segment();
    TCPSegment& s = _sender.segments_out().front();
    if(_receiver.ackno().has_value()){
        s.header().ackno = _receiver.ackno().value();
        s.header().ack = true;
    }
    size_t max_win = numeric_limits<uint16_t>().max();
    s.header().win = min(_receiver.window_size(), max_win);
    s.header().rst = true;
    segments_out().push(s);
    while(!_sender.segments_out().empty())
    {
        _sender.segments_out().pop();
    }
    _active = false;
}

void TCPConnection::send_all_segment()
{
    while(!_sender.segments_out().empty())
        {
            TCPSegment& s = _sender.segments_out().front();
            if(_receiver.ackno().has_value()){
                s.header().ackno = _receiver.ackno().value();
                s.header().ack = true;
            }
            size_t max_win = numeric_limits<uint16_t>().max();
            s.header().win = min(_receiver.window_size(), max_win);
            segments_out().push(s);
            _sender.segments_out().pop();
        }
}

bool TCPConnection::LISTEN()
{
   return (!_receiver.ackno().has_value() && _sender.next_seqno_absolute() == 0);
}
bool TCPConnection::SYN_SENT()
{
   return (_sender.next_seqno_absolute() == _sender.bytes_in_flight() && !_receiver.ackno().has_value());
}