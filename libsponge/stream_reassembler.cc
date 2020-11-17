#include "stream_reassembler.hh"

#include <iostream>
#include <queue>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _output(capacity)
    , _capacity(capacity)
    , data_container({})
    , index_queue()
    , next_expected_index(0)
    , end_index(0)
    , ended(0)
    , current_unassembled_bytes(0) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // check if substring (starts with index, ends with new_upper_bound) is already received.
    size_t new_upper_bound = index + data.size() - 1;
    if (data.size() == 0) {
        new_upper_bound = index;
    }
    if (new_upper_bound < next_expected_index) {
        return;
    }

    // check eof
    if (eof) {
        end_index = index + data.size();
        ended = true;
    }

    // truncate the part that we already recieved
    size_t valid_index = max(index, next_expected_index);
    const string &valid_data = data.substr(valid_index - index);
    data_container[valid_index] = valid_data;
    index_queue.push(valid_index);

    // check capacity
    size_t remaining_capacity_for_unassemble = _capacity - _output.buffer_size();
    size_t current_upper_index = 0;
    size_t unassembled_bytes = 0;
    bool overload = false;
    bool load_first_byte = false;

    while (not index_queue.empty()) {
        size_t next_lower_index = index_queue.top();
        size_t next_upper_index = next_lower_index + data_container[next_lower_index].size() - 1;
        index_queue.pop();

        // duplicate substring stored in re-assembler, remove it
        if (next_upper_index < current_upper_index) {
            data_container.erase(next_lower_index);
            continue;
        }

        // exceed capacity, remove all substrings
        if (overload) {
            data_container.erase(next_lower_index);
            continue;
        }

        if (load_first_byte && next_lower_index <= current_upper_index) {
            // overlapping

            // if exceed capacity, we need to truncate the data to fit the remaining capacity
            if (unassembled_bytes + next_upper_index - current_upper_index + 1 > remaining_capacity_for_unassemble) {
                data_container[current_upper_index + 1] = data_container[next_lower_index].substr(
                    next_upper_index - current_upper_index - 1, remaining_capacity_for_unassemble - unassembled_bytes);
                data_container.erase(next_lower_index);
                overload = true;
                unassembled_bytes += data_container[current_upper_index + 1].size();
                current_upper_index = current_upper_index + data_container[current_upper_index + 1].size();
            } else {
                // no need to truncate
                unassembled_bytes += next_upper_index - current_upper_index + 1;
                current_upper_index = next_lower_index + data_container[next_lower_index].size() - 1;
            }
        } else {
            // no overlapping

            // if exceed capacity, we need to truncate the data to fit the remaining capacity
            if (unassembled_bytes + data_container[next_lower_index].size() > remaining_capacity_for_unassemble) {
                data_container[next_lower_index] =
                    data_container[next_lower_index].substr(0, remaining_capacity_for_unassemble - unassembled_bytes);
                overload = true;
            }
            unassembled_bytes += data_container[next_lower_index].size();
            current_upper_index = next_lower_index + data_container[next_lower_index].size() - 1;
        }
        load_first_byte = true;
    }

    for (auto &p : data_container) {
        index_queue.push(p.first);
    }

    // read data
    size_t min_index = index_queue.top();
    while (not index_queue.empty() && min_index <= next_expected_index) {
        const string &next_reassembled_data_candidate = data_container[min_index];

        // already assembled the data, remove it from re-assembler
        if ((min_index + next_reassembled_data_candidate.size()) < next_expected_index) {
            data_container.erase(min_index);
            index_queue.pop();
        } else {
            const string &next_reassembled_data =
                next_reassembled_data_candidate.substr(next_expected_index - min_index);

            if (next_reassembled_data.size() <= _output.remaining_capacity()) {
                // can read into output ByteStream
                data_container.erase(min_index);
                index_queue.pop();
                _output.write(next_reassembled_data);
                next_expected_index += next_reassembled_data.size();
            } else {
                // longer than the remain capacity of output BytesStream
                data_container.erase(min_index);
                index_queue.pop();
                // only writes the portion can fit into the capacity of output BytesStream
                _output.write(next_reassembled_data.substr(0, _output.remaining_capacity()));
                next_expected_index += _output.remaining_capacity();
                // push the left portion back
                data_container[next_expected_index] = next_reassembled_data.substr(next_expected_index - min_index);
                index_queue.push(next_expected_index);
            }
        }

        if (_output.remaining_capacity() == 0) {
            break;
        }

        min_index = index_queue.top();
    }

    // check end
    if (ended && next_expected_index >= end_index) {
        _output.end_input();
    }

    // calculate number of unassembled bytes
    size_t upper_index = 0;
    current_unassembled_bytes = 0;
    while (not index_queue.empty()) {
        size_t next_index = index_queue.top();
        size_t new_upper_index = next_index + data_container[next_index].size() - 1;
        index_queue.pop();

        if (new_upper_index < upper_index) {
            data_container.erase(next_index);
            continue;
        }
        if (next_index <= upper_index) {
            // overlapping
            current_unassembled_bytes += new_upper_index - upper_index;
        } else {
            current_unassembled_bytes += data_container[next_index].size();
        }
        upper_index = next_index + data_container[next_index].size() - 1;
    }

    // push all left data index back to queue
    for (auto &p : data_container) {
        index_queue.push(p.first);
    }
}

size_t StreamReassembler::unassembled_bytes() const { return current_unassembled_bytes; }

bool StreamReassembler::empty() const { return _output.buffer_empty(); }
