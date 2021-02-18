#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) { stream_capacity = capacity; }

size_t ByteStream::write(const string &data) {
    size_t remain = remaining_capacity();
    size_t need_write{0};
    if (remain > data.size())
        need_write = data.size();
    else
        need_write = remain;

    for (size_t i = 0; i < need_write; i++)
        stream.push_back(data.at(i));

    bytes_w += need_write;
    return need_write;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    std::string recv{};
    size_t need_peek{0};
    if (len > buffer_size())
        need_peek = buffer_size();
    else
        need_peek = len;
    for (size_t i = 0; i < need_peek; i++)
        recv.push_back(stream.at(i));
    return recv;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t need_pop{0};
    if (len > buffer_size())
        need_pop = buffer_size();
    else
        need_pop = len;
    for (size_t i = 0; i < need_pop; i++)
        stream.pop_front();
    bytes_r += need_pop;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string recv;
    size_t need_read{0};
    if (len > buffer_size())
        need_read = buffer_size();
    else
        need_read = len;
    for (size_t i = 0; i < need_read; i++) {
        recv.push_back(stream.front());
        stream.pop_front();
    }
    bytes_r += need_read;
    return recv;
}

void ByteStream::end_input() {
    stream_ended = true;
    stream.push_back(EOF);
}

bool ByteStream::input_ended() const { return stream_ended; }

size_t ByteStream::buffer_size() const {
    if (input_ended())
        return stream.size() - 1;  // EOF
    else
        return stream.size();
}

bool ByteStream::buffer_empty() const {
    if (input_ended())
        return eof();
    else
        return stream.empty();
}

bool ByteStream::eof() const { return stream.front() == EOF; }

size_t ByteStream::bytes_written() const { return bytes_w; }

size_t ByteStream::bytes_read() const { return bytes_r; }

size_t ByteStream::remaining_capacity() const {
    if (input_ended())
        return stream_capacity - stream.size() + 1;
    else
        return stream_capacity - stream.size();
}
