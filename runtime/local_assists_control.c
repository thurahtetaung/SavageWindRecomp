#include "local_assists.h"
#include "control.h"
#include <string.h>

static void command(int argc, char **argv, ControlReply *reply) {
    if (argc < 2 || argc > 3 || strcmp(argv[1], "stamina") ||
        (argc == 3 && strcmp(argv[2], "on") && strcmp(argv[2], "off"))) {
        control_fail(reply, "Usage: assist stamina [on|off]");
        return;
    }
    if (argc == 3) local_stamina_set(!strcmp(argv[2], "on"));
    control_field_bool(reply, "enabled", local_stamina_enabled());
    control_text(reply, "Infinite stamina: %s", local_stamina_enabled() ? "on" : "off");
}

void local_assists_init(void) {
    static const ControlCmd commands[] = {
        {"assist", "stamina [on|off]", "Local experimental stamina assist", command, CONTROL_PLAYER},
    };
    control_register(commands, 1);
}
