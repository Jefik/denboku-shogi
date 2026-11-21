#ifndef RELEASE_H
#define RELEASE_H

/* The game's page, linked from About and Preferences → Updates. */
#define REPO_HOST "github.com/Jefik/denboku-shogi"
#define REPO_URL "https://" REPO_HOST
/* Where updates come from and who signs them (src/core/updater.c): the latest GitHub release's
 * update.txt and update.txt.sig, and the packages beside them. A fork for another game changes these. */
#define RELEASE_URL REPO_URL "/releases/latest/download/"
/* The packages come from the release the manifest names (its Tag), not from the latest one, which may
 * have changed since the manifest was read. */
#define RELEASE_TAG_URL REPO_URL "/releases/download/"
/* The key in use and a reserve kept off the computer, to move to if the first is lost or leaks. */
#define RELEASE_KEYS                                                                                        \
    {                                                                                                       \
        {0xe5, 0x7c, 0x21, 0x94, 0x71, 0x63, 0xe8, 0x21, 0x38, 0x14, 0x67, 0x6a, 0x1b, 0xff, 0x4c, 0xd0,  \
         0xb3, 0x36, 0x5c, 0x3c, 0x0d, 0xc4, 0x34, 0x7f, 0x54, 0x2b, 0x47, 0x7e, 0x51, 0x29, 0xcf, 0x24}, \
        {0xff, 0xb4, 0x19, 0x5b, 0xb4, 0xab, 0xb4, 0x8f, 0xc8, 0xec, 0x18, 0xf3, 0x43, 0x42, 0x60, 0xa8,  \
         0x65, 0x59, 0x2f, 0xbc, 0x75, 0x0d, 0x68, 0x20, 0x8b, 0xdb, 0x97, 0x78, 0x70, 0x96, 0xab, 0x8c}  \
    }

#endif
