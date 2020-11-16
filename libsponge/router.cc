#include "router.hh"

#include <algorithm>
#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";

    RoutePrefix route_prefix_obj;
    route_prefix_obj.route_prefix = route_prefix;
    route_prefix_obj.prefix_length = prefix_length;
    route_prefix_address_mapping[route_prefix_obj] = std::make_tuple(next_hop, interface_num);
}

uint8_t Router::match_prefix_length(const RoutePrefix route_prefix_obj, const uint32_t ip_addr) {
    uint8_t mismatch_length = 0;
    uint32_t address_match = route_prefix_obj.route_prefix ^ ip_addr;

    for (int i = 31; i >= 0; i--) {
        bool mismatch = (address_match >> i) & 1;  // 0 match, 1 mismatch
        if (mismatch) {
            mismatch_length = i + 1;
            break;
        }
    }

    if (32 - mismatch_length < route_prefix_obj.prefix_length) {
        return 0;
    }

    return 32 - mismatch_length;
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // The Router searches the routing table to find the routes that match the datagram’s destination address.
    uint8_t longest_prefix = 0;
    optional<Address> target_address;
    size_t target_interface_num;

    for (auto &route_entry : route_prefix_address_mapping) {
        uint8_t matched_prefix_len = match_prefix_length(route_entry.first, dgram.header().dst);

        if (matched_prefix_len > longest_prefix || route_entry.first.prefix_length == 0) {
            longest_prefix = matched_prefix_len;
            std::tie(target_address, target_interface_num) = route_entry.second;
        }
    }

    // decrease TTL
    if (dgram.header().ttl > 0) {
        dgram.header().ttl -= 1;
        if (dgram.header().ttl > 0) {
            interface(target_interface_num)
                .send_datagram(dgram, target_address.value_or(Address::from_ipv4_numeric(dgram.header().dst)));
        }
    }
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}
