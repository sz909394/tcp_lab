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
    std::set<IP_TO_ETHER_SET>::iterator it;
    for (it = IP_TO_ETHER_SETS.begin(); it != IP_TO_ETHER_SETS.end(); it++) {
        if (it->hop_ip == next_hop_ip) {
            EthernetFrame s{};
            s.header().dst = it->hop_mac_addr;
            s.header().src = _ethernet_address;
            s.header().type = s.header().TYPE_IPv4;
            s.payload() = BufferList(dgram.serialize());
            _frames_out.push(s);
            break;
        }
    }
    if (it != IP_TO_ETHER_SETS.end())
        return;

    std::set<ARP_REQ_SET>::iterator ARP_it;
    for (ARP_it = ARP_REQ_SETS.begin(); ARP_it != ARP_REQ_SETS.end(); ARP_it++) {
        if (ARP_it->target_ip_address == next_hop_ip)
            break;
    }

    if (ARP_it == ARP_REQ_SETS.end()) {
        ARPMessage arp_m{};
        arp_m.sender_ethernet_address = _ethernet_address;
        arp_m.sender_ip_address = _ip_address.ipv4_numeric();
        arp_m.opcode = arp_m.OPCODE_REQUEST;
        arp_m.target_ip_address = next_hop_ip;

        EthernetFrame arp_m_eth_frame{};
        arp_m_eth_frame.header().dst = ETHERNET_BROADCAST;
        arp_m_eth_frame.header().src = arp_m.sender_ethernet_address;
        arp_m_eth_frame.header().type = arp_m_eth_frame.header().TYPE_ARP;
        arp_m_eth_frame.payload() = Buffer(arp_m.serialize());

        _frames_out.push(arp_m_eth_frame);

        ARP_REQ_SETS.insert({next_hop_ip, arp_m_eth_frame});
        IP_Datagrams_outstanding.insert({next_hop_ip, dgram});
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if ((frame.header().dst == _ethernet_address) || (frame.header().dst == ETHERNET_BROADCAST)) {
        if (frame.header().type == frame.header().TYPE_IPv4) {
            InternetDatagram s{};
            if (s.parse(frame.payload()) == ParseResult::NoError)
                return s;
            else
                return {};
        }
        if (frame.header().type == frame.header().TYPE_ARP) {
            ARPMessage s{};
            if (s.parse(frame.payload()) == ParseResult::NoError) {
                IP_TO_ETHER_SET map_0;
                map_0.hop_ip = s.sender_ip_address;
                map_0.hop_mac_addr = s.sender_ethernet_address;
                IP_TO_ETHER_SETS.insert(map_0);

                for (auto it = IP_Datagrams_outstanding.begin(); it != IP_Datagrams_outstanding.end(); it++) {
                    Address dst{"0", 0};
                    if (it->next_hop == s.sender_ip_address)
                        send_datagram(it->Datagram, dst.from_ipv4_numeric(s.sender_ip_address));
                    IP_Datagrams_outstanding.erase(it);
                }

                if (s.opcode == s.OPCODE_REPLY) {
                    for (auto it = ARP_REQ_SETS.begin(); it != ARP_REQ_SETS.end(); it++) {
                        if (it->target_ip_address == s.sender_ip_address)
                            ARP_REQ_SETS.erase(it);
                    }
                }

                if (s.opcode == s.OPCODE_REQUEST && s.target_ip_address == _ip_address.ipv4_numeric()) {
                    ARPMessage reply{};
                    reply.target_ethernet_address = s.sender_ethernet_address;
                    reply.target_ip_address = s.sender_ip_address;
                    reply.sender_ethernet_address = _ethernet_address;
                    reply.sender_ip_address = _ip_address.ipv4_numeric();
                    reply.opcode = reply.OPCODE_REPLY;

                    EthernetFrame frame_arp_reply{};
                    frame_arp_reply.header().dst = s.sender_ethernet_address;
                    frame_arp_reply.header().src = _ethernet_address;
                    frame_arp_reply.header().type = frame_arp_reply.header().TYPE_ARP;
                    frame_arp_reply.payload() = Buffer(reply.serialize());
                    _frames_out.push(frame_arp_reply);
                }
            } else
                return {};
        }
        return {};
    } else
        return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    for (auto it = ARP_REQ_SETS.begin(); it != ARP_REQ_SETS.end(); it++) {
        it->timer = it->timer + ms_since_last_tick;
        if (it->timer > ARP_MESSAGE_TIMEOUT) {
            it->timer = 0;
            _frames_out.push(it->ARP_REQ_FRAME);
        }
    }
    for (auto it = IP_TO_ETHER_SETS.begin(); it != IP_TO_ETHER_SETS.end(); it++) {
        it->timer = it->timer + ms_since_last_tick;
        if (it->timer > IP_TO_ETHER_TIMEOUT)
            IP_TO_ETHER_SETS.erase(it);
    }
}
