# SPDX-License-Identifier: GPL-2.0-or-later
"""Opens the host's co-op ports on the home router (UPnP IGD), so friends can join without a VPN
(Kaniel 2026-10-21: players got lost at the Radmin step). Start server asks the router to forward
the ports to this PC and for the router's internet address; Stop server takes the forwards away.

It cannot work everywhere: the router may have UPnP off, and behind carrier-grade NAT the router has
no internet address of its own (it reports a private or 100.64.x one). Then the launcher says so and
the Radmin VPN steps stay the way.

Standard library only: SSDP discovery over UDP multicast, the device description over HTTP, and the
WANIPConnection / WANPPPConnection SOAP actions.
"""
import ipaddress
import re
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ElementTree

SSDP_ADDRESS = ('239.255.255.250', 1900)
SERVICES = ('urn:schemas-upnp-org:service:WANIPConnection:2',
            'urn:schemas-upnp-org:service:WANIPConnection:1',
            'urn:schemas-upnp-org:service:WANPPPConnection:1')
SEARCH_TARGETS = ('urn:schemas-upnp-org:device:InternetGatewayDevice:2',
                  'urn:schemas-upnp-org:device:InternetGatewayDevice:1') + SERVICES
DESCRIPTION = 'Yharnam Together co-op'
# What the guests reach on the host's PC, as its firewall rules (bbport_coop.firewall_commands): the WebAPI
# and the game's backend, and the UDP port of matching and the game traffic it routes. shadNet's own TCP
# port stays on the host's PC.
HOST_PORTS = (('TCP', 31315), ('TCP', 20443), ('UDP', 31314))
# The router has no internet address of its own: carrier-grade NAT (100.64.0.0/10) or a private one.
NOT_PUBLIC = [ipaddress.IPv4Network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16',
                                                  '100.64.0.0/10', '169.254.0.0/16', '0.0.0.0/8')]


class UpnpError(Exception):
    pass


def is_public(address):
    """Whether the router's internet address can be reached from the internet."""
    try:
        ip = ipaddress.IPv4Address(address)
    except ValueError:
        return False
    return not any(ip in network for network in NOT_PUBLIC) and not ip.is_loopback and not ip.is_multicast


# Adapters a router is never behind: VPNs (Radmin VPN 26.0.0.0/8, Tailscale 100.64.0.0/10), link-local.
NOT_LAN = [ipaddress.IPv4Network(n) for n in ('26.0.0.0/8', '100.64.0.0/10', '169.254.0.0/16', '127.0.0.0/8')]


def lan_addresses():
    """This PC's addresses a home router can be behind: every adapter's but the VPNs'. Radmin VPN takes
    the lowest metric and a default route of its own, so a search sent the default way leaves over the VPN
    and no router hears it (kaniel-pc, 2026-10-21)."""
    try:
        addresses = socket.gethostbyname_ex(socket.gethostname())[2]
    except OSError:
        return []
    return [a for a in dict.fromkeys(addresses)
            if not any(ipaddress.IPv4Address(a) in network for network in NOT_LAN)]


def default_gateways(route_print=None):
    """(router, this PC's address towards it) of each default route but the VPNs', from Windows'
    `route print -4 0.0.0.0` (empty elsewhere). A search sent straight to the router comes back through
    Windows Firewall as the answer to it; a multicast one's answer may not."""
    if route_print is None:
        if sys.platform != 'win32':
            return []
        try:
            route_print = subprocess.run(['route', 'print', '-4', '0.0.0.0'], capture_output=True, text=True,
                                         timeout=5, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
        except (OSError, subprocess.SubprocessError):
            return []
    found = []
    for match in re.finditer(r'(?m)^\s*0\.0\.0\.0\s+0\.0\.0\.0\s+(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)\s',
                             route_print):
        router, address = match.groups()
        if not any(ipaddress.IPv4Address(address) in network for network in NOT_LAN) \
                and (router, address) not in found:
            found.append((router, address))
    return found


def search_messages():
    return [('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\n'
             f'MX: 2\r\nST: {target}\r\n\r\n').encode('ascii') for target in SEARCH_TARGETS]


def discover(timeout=2.0, addresses=None):
    """Locations (description URLs) of the gateways that answered an SSDP search, first answer first. The
    search goes out of every LAN adapter, to the multicast group and straight to the adapter's usual router
    address (x.y.z.1: some routers answer only a direct ask)."""
    import select
    routers = {address: router for router, address in default_gateways()} if addresses is None else {}
    sockets = []
    for address in (lan_addresses() if addresses is None else addresses):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(address))
            sock.bind((address, 0))
        except OSError:
            continue
        sockets.append(sock)
        direct = {routers.get(address), address.rsplit('.', 1)[0] + '.1'} - {None}
        for message in search_messages():
            for target in [SSDP_ADDRESS] + [(router, SSDP_ADDRESS[1]) for router in sorted(direct)]:
                try:
                    sock.sendto(message, target)
                except OSError:
                    pass
    found = []
    deadline = time.monotonic() + timeout
    try:
        while sockets:
            left = deadline - time.monotonic()
            if left <= 0:
                break
            ready, _w, _x = select.select(sockets, [], [], left)
            for sock in ready:
                try:
                    data, _sender = sock.recvfrom(4096)
                except OSError:
                    continue
                match = re.search(rb'(?im)^location:\s*(\S+)\s*$', data)
                if match:
                    location = match.group(1).decode('ascii', 'replace')
                    if location.startswith('http://') and location not in found:
                        found.append(location)
    finally:
        for sock in sockets:
            sock.close()
    return found


def _local(tag):
    return tag.rsplit('}', 1)[-1]


def gateway(location, timeout=3.0):
    """(control URL, service type) of the gateway's WAN connection service, from its description."""
    with urllib.request.urlopen(location, timeout=timeout) as response:
        root = ElementTree.fromstring(response.read(1_000_000))
    base = location
    for element in root.iter():
        if _local(element.tag) == 'URLBase' and (element.text or '').strip():
            base = element.text.strip()
    for wanted in SERVICES:
        for service in root.iter():
            if _local(service.tag) != 'service':
                continue
            fields = {_local(child.tag): (child.text or '').strip() for child in service}
            if fields.get('serviceType') == wanted and fields.get('controlURL'):
                return urllib.parse.urljoin(base, fields['controlURL']), wanted
    raise UpnpError('the router has no WAN connection service')


def soap(control, service, action, arguments=(), timeout=3.0):
    """Calls one action; returns its out arguments, or raises UpnpError with the router's error code."""
    body = ''.join(f'<{name}>{value}</{name}>' for name, value in arguments)
    envelope = ('<?xml version="1.0"?>\r\n<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" '
                's:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body>'
                f'<u:{action} xmlns:u="{service}">{body}</u:{action}></s:Body></s:Envelope>')
    request = urllib.request.Request(control, data=envelope.encode('utf-8'), method='POST', headers={
        'Content-Type': 'text/xml; charset="utf-8"', 'SOAPAction': f'"{service}#{action}"'})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            data = response.read(200_000)
    except urllib.error.HTTPError as error:
        code = re.search(rb'<(?:\w+:)?errorCode>\s*(\d+)', error.read(200_000) or b'')
        raise UpnpError(f'{action} refused ({code.group(1).decode() if code else error.code})') from None
    except OSError as error:
        raise UpnpError(f'{action} failed ({error})') from None
    root = ElementTree.fromstring(data)
    for element in root.iter():
        if _local(element.tag) == action + 'Response':
            return {_local(child.tag): (child.text or '').strip() for child in element}
    return {}


def local_address_towards(control):
    """This PC's address on the router's network: where the forwards point."""
    host = urllib.parse.urlparse(control).hostname
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.connect((host, 1900))
        return probe.getsockname()[0]


def find_gateway(timeout=2.0):
    """(control URL, service type) of the first gateway that describes a WAN connection; None for none."""
    for location in discover(timeout):
        try:
            return gateway(location)
        except (OSError, UpnpError, ElementTree.ParseError):
            continue
    return None


def open_ports(ports=HOST_PORTS, found=None):
    """Forwards the host's ports to this PC. Returns {'state': 'open' | 'cgnat' | 'refused' | 'none',
    'public': the router's internet address or '', 'local': this PC's LAN address or '', 'failed':
    the ports the router refused}. 'open' means every port is forwarded and the address is public."""
    found = found or find_gateway()
    if not found:
        return {'state': 'none', 'public': '', 'local': '', 'failed': []}
    control, service = found
    try:
        public = soap(control, service, 'GetExternalIPAddress').get('NewExternalIPAddress', '')
    except (UpnpError, ElementTree.ParseError):
        public = ''
    local = local_address_towards(control)
    if public and not is_public(public):
        return {'state': 'cgnat', 'public': public, 'local': local, 'failed': []}
    failed = []
    for protocol, port in ports:
        try:
            soap(control, service, 'AddPortMapping', (
                ('NewRemoteHost', ''), ('NewExternalPort', port), ('NewProtocol', protocol),
                ('NewInternalPort', port), ('NewInternalClient', local), ('NewEnabled', 1),
                ('NewPortMappingDescription', DESCRIPTION), ('NewLeaseDuration', 0)))
        except (UpnpError, ElementTree.ParseError):
            failed.append(f'{protocol} {port}')
    state = 'refused' if failed or not public else 'open'
    return {'state': state, 'public': public, 'local': local, 'failed': failed}


def close_ports(ports=HOST_PORTS, found=None):
    """Takes the forwards away again; returns how many the router removed."""
    found = found or find_gateway(timeout=1.5)
    if not found:
        return 0
    control, service = found
    local = local_address_towards(control)
    removed = 0
    for protocol, port in ports:
        try:
            entry = soap(control, service, 'GetSpecificPortMappingEntry', (
                ('NewRemoteHost', ''), ('NewExternalPort', port), ('NewProtocol', protocol)))
            if entry.get('NewInternalClient') != local:
                continue  # another PC's forward: not ours to take away
            soap(control, service, 'DeletePortMapping', (
                ('NewRemoteHost', ''), ('NewExternalPort', port), ('NewProtocol', protocol)))
            removed += 1
        except (UpnpError, ElementTree.ParseError):
            pass
    return removed
