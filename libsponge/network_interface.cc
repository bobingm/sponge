#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    if (_ip_ethernet_address_mapping.find(next_hop_ip) == _ip_ethernet_address_mapping.end()) {
        // broadcast an ARP request for the next hop’s Ethernet address
        // If the network interface already sent an ARP request about the same IP address
        // in the last five seconds, don’t send a second request—just wait for a reply to the first one.
        if (_frame_holder_timer.find(next_hop_ip) == _frame_holder_timer.end()) {
            // resend the ARP msg
            EthernetFrame arpmsg_frame;
            ARPMessage arpmsg;
            arpmsg.opcode = ARPMessage::OPCODE_REQUEST;
            arpmsg.target_ip_address = next_hop_ip;
            arpmsg.sender_ip_address = _ip_address.ipv4_numeric();
            arpmsg.sender_ethernet_address = _ethernet_address;

            arpmsg_frame.header().type = EthernetHeader::TYPE_ARP;
            arpmsg_frame.header().src = _ethernet_address;
            arpmsg_frame.header().dst = ETHERNET_BROADCAST;
            arpmsg_frame.payload() = arpmsg.serialize();
            frames_out().push(arpmsg_frame);
            _frame_holder_timer[next_hop_ip] = 5000;
        }

        if (_frame_holder.find(next_hop_ip) == _frame_holder.end()) {
            // queue the IP datagram so it can be sent after the ARP reply is received
            // queue the datagram until you learn the destination Ethernet address
            EthernetFrame new_frame;
            new_frame.header().type = EthernetHeader::TYPE_IPv4;
            new_frame.header().src = _ethernet_address;
            new_frame.payload() = dgram.serialize();
            _frame_holder[next_hop_ip] = new_frame;
        }
    } else {
        // knonw address
        EthernetFrame new_frame;
        new_frame.header().type = EthernetHeader::TYPE_IPv4;
        new_frame.header().src = _ethernet_address;
        new_frame.header().dst = _ip_ethernet_address_mapping[next_hop_ip].ethernet_address;
        new_frame.payload() = dgram.serialize();
        frames_out().push(new_frame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    // The code should ignore any frames not destined for the network interface (meaning, the Ethernet destination is
    // either the broadcast address or the interface’s own Ethernet address stored in the ethernet address member
    // variable).
    EthernetAddress frame_dst = frame.header().dst;
    if (frame_dst != _ethernet_address and frame_dst != ETHERNET_BROADCAST) {
        return {};
    }

    // If the inbound frame is IPv4
    if (frame.header().type == EthernetHeader::TYPE_IPv4) {
        InternetDatagram dgram;
        ParseResult result = dgram.parse(frame.payload());
        if (result == ParseResult::NoError) {
            return dgram;
        }
    }

    if (frame.header().type == EthernetHeader::TYPE_ARP) {
        ARPMessage arpmsg;
        ParseResult result = arpmsg.parse(frame.payload());
        if (result == ParseResult::NoError) {
            EthernetAddrTickPair ethernet_addr_tick;
            ethernet_addr_tick.ethernet_address = arpmsg.sender_ethernet_address;
            ethernet_addr_tick.in_memory_ttl = 30000;
            _ip_ethernet_address_mapping[arpmsg.sender_ip_address] = ethernet_addr_tick;

            if (arpmsg.opcode == ARPMessage::OPCODE_REQUEST && arpmsg.target_ip_address == _ip_address.ipv4_numeric()) {
                // if it’s an ARP request asking for our IP address, send an appropriate ARP reply
                EthernetFrame arpmsg_frame;
                ARPMessage reply_arpmsg;
                reply_arpmsg.opcode = ARPMessage::OPCODE_REPLY;
                reply_arpmsg.target_ip_address = arpmsg.sender_ip_address;
                reply_arpmsg.target_ethernet_address = arpmsg.sender_ethernet_address;
                reply_arpmsg.sender_ip_address = _ip_address.ipv4_numeric();
                reply_arpmsg.sender_ethernet_address = _ethernet_address;

                arpmsg_frame.header().type = EthernetHeader::TYPE_ARP;
                arpmsg_frame.header().src = _ethernet_address;
                arpmsg_frame.header().dst = arpmsg.sender_ethernet_address;
                arpmsg_frame.payload() = reply_arpmsg.serialize();
                frames_out().push(arpmsg_frame);
            }

            if (arpmsg.opcode == ARPMessage::OPCODE_REPLY) {
                // if it’s an ARP reply, check queued frames
                uint32_t ip_addr = arpmsg.sender_ip_address;
                if (_frame_holder.find(ip_addr) != _frame_holder.end()) {
                    EthernetFrame new_frame = _frame_holder[ip_addr];
                    new_frame.header().dst = arpmsg.sender_ethernet_address;
                    _frame_holder.erase(ip_addr);
                    _frame_holder_timer.erase(ip_addr);
                    frames_out().push(new_frame);
                }
            }
        }
    }

    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // Expire any IP - to - Ethernet mappings that have expired.
    for (auto it = _ip_ethernet_address_mapping.begin(); it != _ip_ethernet_address_mapping.end();) {
        if (it->second.in_memory_ttl <= ms_since_last_tick) {
            it = _ip_ethernet_address_mapping.erase(it);
        } else {
            it->second.in_memory_ttl -= ms_since_last_tick;
            ++it;
        }
    }

    for (auto it = _frame_holder_timer.begin(); it != _frame_holder_timer.end();) {
        if (it->second <= ms_since_last_tick) {
            it = _frame_holder_timer.erase(it);
        } else {
            it->second -= ms_since_last_tick;
            ++it;
        }
    }
}
