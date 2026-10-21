"""The host's co-op ports opened on the router by UPnP (launcher/bbport_upnp.py), against a fake
router: the forwards point at this PC, carrier-grade NAT is recognised, Stop takes only ours away
(Kaniel 2026-10-21: friends join without a VPN when the router allows it)."""
import http.server
import re
import sys
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))

import bbport_upnp as upnp  # noqa: E402

SERVICE = 'urn:schemas-upnp-org:service:WANIPConnection:1'
DESCRIPTION = f'''<?xml version="1.0"?>
<root xmlns="urn:schemas-upnp-org:device-1-0"><device>
 <deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1</deviceType>
 <serviceList><service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>
  <controlURL>/l3f</controlURL></service></serviceList>
 <deviceList><device><deviceType>urn:schemas-upnp-org:device:WANDevice:1</deviceType><deviceList><device>
  <serviceList><service><serviceType>{SERVICE}</serviceType><controlURL>/ctl/IPConn</controlURL></service></serviceList>
 </device></deviceList></device></deviceList>
</device></root>'''


class Router:
    """A fake gateway: its description, GetExternalIPAddress, Add/Delete/GetSpecificPortMappingEntry."""

    def __init__(self, public='203.0.113.7', refuse=()):
        self.public, self.refuse, self.mappings, self.calls = public, set(refuse), {}, []
        router = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass

            def reply(self, status, body):
                data = body.encode()
                self.send_response(status)
                self.send_header('Content-Type', 'text/xml')
                self.send_header('Content-Length', str(len(data)))
                self.end_headers()
                self.wfile.write(data)

            def do_GET(self):
                self.reply(200, DESCRIPTION)

            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length'])).decode()
                action = self.headers['SOAPAction'].strip('"').split('#')[1]
                args = dict(re.findall(r'<(New\w+)>([^<]*)</\1>', body))
                router.calls.append((action, args))
                key = (args.get('NewProtocol'), args.get('NewExternalPort'))
                out = ''
                if action == 'GetExternalIPAddress':
                    out = f'<NewExternalIPAddress>{router.public}</NewExternalIPAddress>'
                elif action == 'AddPortMapping':
                    if f'{key[0]} {key[1]}' in router.refuse:
                        return self.reply(500, '<errorCode>718</errorCode>')
                    router.mappings[key] = args['NewInternalClient']
                elif action == 'GetSpecificPortMappingEntry':
                    if key not in router.mappings:
                        return self.reply(500, '<errorCode>714</errorCode>')
                    out = f'<NewInternalClient>{router.mappings[key]}</NewInternalClient>'
                elif action == 'DeletePortMapping':
                    router.mappings.pop(key, None)
                self.reply(200, f'<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"><s:Body>'
                                f'<u:{action}Response xmlns:u="{SERVICE}">{out}</u:{action}Response>'
                                '</s:Body></s:Envelope>')

        self.httpd = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        self.location = f'http://127.0.0.1:{self.httpd.server_address[1]}/desc.xml'

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()


class UpnpTests(unittest.TestCase):
    def router(self, **options):
        router = Router(**options)
        self.addCleanup(router.stop)
        return router, upnp.gateway(router.location)

    def test_the_wan_service_is_found_in_the_description(self):
        router, found = self.router()
        self.assertEqual(found, (router.location.replace('/desc.xml', '/ctl/IPConn'), SERVICE))

    def test_the_host_ports_are_forwarded_to_this_pc(self):
        router, found = self.router()
        result = upnp.open_ports(found=found)
        self.assertEqual(result['state'], 'open')
        self.assertEqual(result['public'], '203.0.113.7')
        self.assertEqual(result['local'], '127.0.0.1')
        self.assertEqual(set(router.mappings), {('TCP', '31313'), ('TCP', '31315'), ('TCP', '20443'),
                                                ('UDP', '31314')})
        self.assertEqual(set(router.mappings.values()), {'127.0.0.1'})
        added = [args for action, args in router.calls if action == 'AddPortMapping']
        self.assertTrue(all(a['NewInternalPort'] == a['NewExternalPort'] for a in added))
        self.assertTrue(all(a['NewLeaseDuration'] == '0' for a in added), 'routers that take no lease too')

    def test_carrier_grade_nat_is_told_apart_and_nothing_is_forwarded(self):
        for address in ('100.72.1.9', '10.20.0.5', '192.168.0.2'):
            router, found = self.router(public=address)
            result = upnp.open_ports(found=found)
            self.assertEqual(result['state'], 'cgnat', address)
            self.assertEqual(router.mappings, {})

    def test_a_refused_port_is_named(self):
        router, found = self.router(refuse={'UDP 31314'})
        result = upnp.open_ports(found=found)
        self.assertEqual(result['state'], 'refused')
        self.assertEqual(result['failed'], ['UDP 31314'])

    def test_stop_takes_only_our_forwards_away(self):
        router, found = self.router()
        upnp.open_ports(found=found)
        router.mappings[('TCP', '31313')] = '192.168.1.50'  # another PC's own forward
        self.assertEqual(upnp.close_ports(found=found), 3)
        self.assertEqual(router.mappings, {('TCP', '31313'): '192.168.1.50'})

    def test_no_router_answers(self):
        with mock.patch.object(upnp, 'find_gateway', return_value=None):
            self.assertEqual(upnp.open_ports()['state'], 'none')
            self.assertEqual(upnp.close_ports(), 0)

    def test_public_addresses(self):
        self.assertTrue(upnp.is_public('203.0.113.7'))
        self.assertTrue(upnp.is_public('26.1.2.3'))
        for address in ('100.64.0.1', '172.16.4.4', '127.0.0.1', '0.0.0.0', 'not an ip', ''):
            self.assertFalse(upnp.is_public(address), address)


if __name__ == '__main__':
    unittest.main()
