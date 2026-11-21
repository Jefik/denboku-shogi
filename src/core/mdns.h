#ifndef MDNS_H
#define MDNS_H

/* The LAN discovery's fallback: the host's announcement as an mDNS (RFC 6762) response for the
 * service _denboku._udp.local, for routers that drop the subnet broadcast but pass multicast.
 * Only the packet is made and read here (no sockets, no GTK). */

#define MDNS_GROUP "224.0.0.251"
#define MDNS_PORT 5353
/* The longest room name carried: a TXT string holds 255 bytes. */
#define MDNS_NAME_BYTES 200

/* Writes the response (PTR, SRV and TXT records) for a host on `port` called `name`; `id` tells
 * one host's instance from another's. Returns its size, 0 when `size` is too small. */
int mdns_announce(unsigned char *out, int size, unsigned id, int port, const char *name);
/* Reads a packet; on a _denboku._udp announcement fills `port` and `name` (UTF-8, whole
 * characters) and returns 1, otherwise 0. Whatever the bytes, nothing is read past `len`. */
int mdns_parse(const unsigned char *pkt, int len, int *port, char name[MDNS_NAME_BYTES + 1]);

#endif
