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
# What the host's co-op server listens on (bbport_coop: shadnet, WebAPI, backend; matching and the
# game traffic it routes).
HOST_PORTS = (('TCP', 31313), ('TCP', 31315), ('TCP', 20443), ('UDP', 31314))
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


def discover(timeout=2.0):
    """Locations (description URLs) of the gateways that answered an SSDP search, first answer first."""
    found = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) as sock:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)
        sock.settimeout(timeout)
        for target in SEARCH_TARGETS:
            message = ('M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: "ssdp:discover"\r\n'
                       f'MX: 2\r\nST: {target}\r\n\r\n')
            try:
                sock.sendto(message.encode('ascii'), SSDP_ADDRESS)
            except OSError:
                return found
        while True:
            try:
                data, _sender = sock.recvfrom(4096)
            except OSError:  # the timeout ends the search
                return found
            match = re.search(rb'(?im)^location:\s*(\S+)\s*$', data)
            if match:
                location = match.group(1).decode('ascii', 'replace')
                if location.startswith('http://') and location not in found:
                    found.append(location)


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
