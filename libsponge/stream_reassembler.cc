#include "stream_reassembler.hh"

#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // recv data first, merge_substring, write into bytestream
    // how to check eof??
    std::pair<size_t, std::string> recv_substring{};

    if (index < _output_index && index + data.size() > _output_index) {
        recv_substring.first = _output_index;
        recv_substring.second = data.substr(_output_index - index);
    } else {
        recv_substring.first = index;
        recv_substring.second = data;
    }

    if (eof) {
        __eof_flag = eof;
        __eof_index = index + data.size();
    }

    merge_substring(recv_substring);
    write_bytestream();
    shrink_substring_buffer();
}

void StreamReassembler::shrink_substring_buffer(void) {
    signed long size = _output.remaining_capacity();
    std::map<size_t, std::string>::iterator it = substrings_buffer.begin();
    for (; it != substrings_buffer.end(); it++) {
        size = size - it->second.size();
        if (size <= 0)
            break;
    }
    if (it != substrings_buffer.end()) {
        if (size == 0) {
            it++;
            substrings_buffer.erase(it, substrings_buffer.end());
        } else if (size < 0) {
            size_t len = it->second.size() + size;
            it->second = it->second.substr(0, len);
            if (it->second.size() > 0) {
                it++;
            }
            substrings_buffer.erase(it, substrings_buffer.end());
        }
    }
}

void StreamReassembler::write_bytestream(void) {
    for (auto it = substrings_buffer.begin(); it != substrings_buffer.end();) {
        if (it->first == _output_index) {
            size_t len = _output.write(it->second);
            _output_index += len;
            if (len == it->second.size())
                substrings_buffer.erase(it++);
            else {
                it->second = it->second.substr(len);
                break;
            }
        } else
            ++it;
    }
    if (__eof_flag && __eof_index == _output_index) {
        _output.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const {
    size_t size{};
    for (const auto &p : substrings_buffer) {
        size += p.second.size();
    }
    return size;
}

bool StreamReassembler::empty() const { return substrings_buffer.empty(); }
bool StreamReassembler::merge_node_to_end(std::pair<size_t, string> &node, const std::pair<size_t, string> &node_next) {
    size_t dst_start = node.first;
    size_t dst_end = node.first + node.second.size() - 1;
    size_t next_start = node_next.first;
    size_t next_end = node_next.first + node_next.second.size() - 1;
    if (dst_start == next_start) {
        if (dst_end > next_end) {
            return {true};
        }
        if (dst_end == next_end) {
            return {true};
        }
        if (dst_end < next_end) {
            node.second = node_next.second;
            return {true};
        }
    }
    if (dst_start < next_start) {
        if (dst_end < next_start) {
            return {false};
        }
        if (dst_end == next_start) {
            if (node_next.second.size() != 0) {
                node.second.append(node_next.second.substr(1, node_next.second.size()));
            }
            return {true};
        }
        if (dst_end > next_start && dst_end < next_end) {
            if (node_next.second.size() != 0) {
                node.second.append(node_next.second.substr(dst_end - next_start + 1, node_next.second.size()));
            }
            return {true};
        }
        if (dst_end == next_end) {
            return {true};
        }
        if (dst_end > next_end) {
            return {true};
        }
    }
    return {false};
}

bool StreamReassembler::merge_node_to_begin(std::pair<size_t, string> &node,
                                            const std::pair<size_t, string> &node_pre) {
    size_t dst_start = node.first;
    size_t dst_end = node.first + node.second.size() - 1;
    size_t pre_start = node_pre.first;
    size_t pre_end = node_pre.first + node_pre.second.size() - 1;

    if (dst_start > pre_start) {
        if (dst_end > pre_end) {
            node.first = node_pre.first;
            auto string = node_pre.second;
            if (node.second.size() != 0) {
                string.append(node.second.substr(pre_end - dst_start + 1, node.second.size()));
            }
            node.second = string;
            return {true};
        }
        if (dst_end == pre_end) {
            node.first = node_pre.first;
            node.second = node_pre.second;
            return {true};
        }
        if (dst_end < pre_end) {
            node.first = node_pre.first;
            node.second = node_pre.second;
            return {true};
        }
    }

    if (dst_start == pre_end) {
        node.first = node_pre.first;
        auto string = node_pre.second;
        if (node.second.size() != 0) {
            string.append(node.second.substr(1, node.second.size()));
        }
        node.second = string;
        return {true};
    }
    if (dst_start > pre_end) {
        return {false};
    }
    return {false};
}

void StreamReassembler::merge_substring(std::pair<size_t, std::string> &node) {
    auto it = substrings_buffer.lower_bound(node.first);
    for (; it != substrings_buffer.end();) {
        if (it->first > node.first + node.second.size() - 1)
            break;
        bool erase = merge_node_to_end(node, *it);
        if (erase)
            substrings_buffer.erase(it++);
        else
            break;
    }
    if (it != substrings_buffer.begin()) {
        it--;
    }
    for (; it != substrings_buffer.begin();)  // it point to end or first > node.end_index
    {
        if (it->first + it->second.size() - 1 < node.first)
            break;
        bool erase = merge_node_to_begin(node, *it);
        if (erase)
            substrings_buffer.erase(it--);
        else
            break;
    }
    if (!substrings_buffer.empty() && (it == substrings_buffer.begin())) {
        if (it->first + it->second.size() - 1 < node.first) {
        } else {
            bool erase = merge_node_to_begin(node, *it);
            if (erase)
                substrings_buffer.erase(it);
        }
    }
    substrings_buffer.insert(node);
}