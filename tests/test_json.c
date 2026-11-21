#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "json.h"

int main(void)
{
    const char *j = "{\"type\":\"gameFull\",\"id\":\"abc\",\"sente\":{\"name\":\"Ann\",\"rating\":1500},"
                    "\"gote\":{\"aiLevel\":3},\"x\":[1,{\"a\":2}],\"state\":{\"moves\":\"7g7f 3c3d\","
                    "\"btime\":60000,\"winner\":null,\"s\":\"a\\\"b\\u00e9\\u5c06\"}}";
    char v[64];
    assert(json_get(j, "type", v, sizeof v) && !strcmp(v, "gameFull"));
    assert(json_get(j, "sente.name", v, sizeof v) && !strcmp(v, "Ann"));
    assert(json_long(j, "sente.rating", 0) == 1500);
    assert(json_long(j, "gote.aiLevel", 0) == 3);
    assert(json_get(j, "state.moves", v, sizeof v) && !strcmp(v, "7g7f 3c3d"));
    assert(json_long(j, "state.btime", 0) == 60000);
    assert(!json_get(j, "state.winner", v, sizeof v));
    assert(!json_get(j, "gote.name", v, sizeof v));
    assert(json_get(j, "state.s", v, sizeof v) && !strcmp(v, "a\"b\xc3\xa9\xe5\xb0\x86"));
    assert(json_get(j, "x", v, sizeof v) && !strcmp(v, "[1,{\"a\":2}]"));
    assert(json_long(j, "x.0", 0) == 1 && json_long(j, "x.1.a", 0) == 2);
    assert(!json_get(j, "x.2", v, sizeof v) && !json_get(j, "x.a", v, sizeof v) && !json_get(j, "x.-1", v, sizeof v));
    const char *chat = "[{\"text\":\"Good luck\",\"user\":\"Jefik\"}, {\"text\":\"ola\",\"user\":\"lishogi\"}]";
    assert(json_get(chat, "1.text", v, sizeof v) && !strcmp(v, "ola"));
    assert(json_get(chat, "0.user", v, sizeof v) && !strcmp(v, "Jefik") && !json_get(chat, "2.text", v, sizeof v));
    puts("json tests passed");
    return 0;
}
