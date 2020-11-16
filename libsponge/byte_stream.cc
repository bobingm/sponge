#include "byte_stream.hh"

#include <algorithm>  // std::reverse
#include <iostream>
#include <stdexcept>

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity)
    : buffer(), buff_capacity(capacity), total_written(0), total_read(0), end_input_called(0) {}

size_t ByteStream::write(const string &data) {
    size_t written_length = min(data.size(), remaining_capacity());
    buffer.append(data.substr(0, written_length));

    total_written += written_length;
    return written_length;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    if (len > buffer_size()) {
        throw std::invalid_argument("len cannot be long than " + buffer_size());
    }
    return buffer.substr(0, len);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    if (len > buffer_size()) {
        throw std::invalid_argument("len cannot be long than " + buffer_size());
    }
    buffer.erase(0, len);
    total_read += len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string read_output = peek_output(len);
    pop_output(len);
    return read_output;
}

void ByteStream::end_input() { end_input_called = true; }

bool ByteStream::input_ended() const { return end_input_called; }

size_t ByteStream::buffer_size() const { return buffer.size(); }

bool ByteStream::buffer_empty() const { return buffer_size() == 0; }

bool ByteStream::eof() const { return input_ended() && buffer_empty(); }

size_t ByteStream::bytes_written() const { return total_written; }

size_t ByteStream::bytes_read() const { return total_read; }

size_t ByteStream::remaining_capacity() const { return buff_capacity - buffer_size(); }
