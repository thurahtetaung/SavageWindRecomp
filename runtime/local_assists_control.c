#include "local_assists.h"
#include "control.h"
#include <string.h>

static void command(int argc, char **argv, ControlReply *reply) {
    if (argc < 2 || argc > 3 || local_assist_find(argv[1]) < 0 ||
        (argc == 3 && strcmp(argv[2], "on") && strcmp(argv[2], "off"))) {
        control_fail(reply, "Usage: assist <stamina|health|durability|arrows|food> [on|off]");
        return;
    }
    int assist = local_assist_find(argv[1]);
    if (argc == 3) local_assist_set(assist, !strcmp(argv[2], "on"));
    control_field_bool(reply, "enabled", local_assist_enabled(assist));
    control_text(reply, "Infinite %s: %s", local_assist_name(assist), local_assist_enabled(assist) ? "on" : "off");
}

void local_assists_init(void) {
    static const ControlCmd commands[] = {
        {"assist", "<name> [on|off]", "Local experimental assists", command, CONTROL_PLAYER},
    };
    control_register(commands, 1);
}
