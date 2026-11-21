#ifndef UPNP_H
#define UPNP_H

#include <glib.h>

/* Asks the router (UPnP Internet Gateway Device) to forward a TCP port to this device, so a game
 * hosted here can be joined from the internet. Asynchronous; `done` comes in the main loop
 * with the public address ("203.0.113.5"), or NULL when there is no such router or it refused. */
typedef void (*UpnpDone)(const char *external_ip, void *data);

void upnp_open(int port, const char *description, UpnpDone done, void *data);
/* Removes the forwarding made by upnp_open, if any (asynchronous; the router drops it anyway when
 * its two-hour lease ends). */
void upnp_close(void);

#endif
