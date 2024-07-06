#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    if (seg.header().rst) {
        // If rst, set both the inbound and outbound streams to the error
        // state and kills the connection permanently
        _unclean_shundown = true;
        return;
    }
    _receiver.segment_received(seg);
    if (seg.header().ack) {
        _sender.ack_received(seg.header().ackno, _receiver.window_size());
    }

    if (seg.length_in_sequence_space() > 0) {
        // Segment occupied, make sure at least one segment sent in reply
        _sender.fill_window();
        bool isSend = send_segment();
        if (!isSend) {
            _sender.send_empty_segment();
            TCPSegment ackSegment = _sender.segments_out().front();
            _sender.segments_out().pop();
            if (_receiver.ackno().has_value()) {
                ackSegment.header().ackno = _receiver.ackno().value();
                ackSegment.header().ack = true;
            }
            ackSegment.header().win = _receiver.window_size();
            _segments_out.push(ackSegment);
        }
    }
}
bool TCPConnection::check_inbound() {
    // Clean shutdown #preq1
    return unassembled_bytes() == 0 && inbound_stream().input_ended();
}

bool TCPConnection::check_outbound() {
    // Clean shutdown #preq2, #preq3
    return outbound_stream().input_ended() && outbound_stream().buffer_empty() && _sender.bytes_in_flight() == 0 && _sender.next_seqno_absolute() == outbound_stream().bytes_written() + 2;
}
bool TCPConnection::active() const {
    return is_alive;
}

size_t TCPConnection::write(const string &data) {
    if (data.size() == 0) {
        return 0;
    }
    size_t bytes_written = outbound_stream().write(data);
    _sender.fill_window();
    send_segment();
    return bytes_written;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        _sender.send_empty_segment();
        TCPSegment rstSegment = _sender.segments_out().front();
        _sender.segments_out().pop();
        rstSegment.header().rst = true;
        _segments_out.push(rstSegment);
        is_alive = false;
    }
    if (check_outbound() && check_inbound()) {
        if (!_linger_after_streams_finish) {
            is_alive = false;
        } else if (_time_since_last_segment_received > 10 * _cfg.rt_timeout) {
            is_alive = false;
        }
    }
}

void TCPConnection::end_input_stream() {
    if (_unclean_shundown) {
        inbound_stream().set_error();
        outbound_stream().set_error();
        is_alive = false;
    }
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_segment();
}

bool TCPConnection::send_segment() {
    if (_sender.segments_out().empty()) {
        return false;
    }
    while (!_sender.segments_out().empty()) {
        TCPSegment tcpSegment = _sender.segments_out().front();
        _segments_out.push(tcpSegment);
        if (tcpSegment.header().rst) {
            _unclean_shundown = true;
        }
        _sender.segments_out().pop();
    }
    return true;
}
TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
