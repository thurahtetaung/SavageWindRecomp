#include "local_assists.h"
#include "control.h"
#include "settings.h"
#include <string.h>

static void command(int argc, char **argv, ControlReply *reply) {
    if (argc < 2 || argc > 3 || local_assist_find(argv[1]) < 0 ||
        (argc == 3 && strcmp(argv[2], "on") && strcmp(argv[2], "off"))) {
        control_fail(reply, "Usage: assist <stamina|health|durability|arrows|food|horse> [on|off]");
        return;
    }
    int assist = local_assist_find(argv[1]);
    if (argc == 3) local_assist_set(assist, !strcmp(argv[2], "on"));
    control_field_bool(reply, "enabled", local_assist_enabled(assist));
    control_text(reply, "Infinite %s: %s", local_assist_name(assist), local_assist_enabled(assist) ? "on" : "off");
}

// Upstream's Options > Accessibility settings behind this fork's stamina, arrows and health toggles
static void upstream_set(int assist, int value) {
    if (assist == LOCAL_STAMINA) g_settings.infinite_stamina = value;
    else if (assist == LOCAL_ARROWS) g_settings.infinite_arrows = value;
    else if (assist == LOCAL_HEALTH) g_settings.god_mode = value;
}
static int upstream_get(int assist) {
    return assist == LOCAL_STAMINA ? g_settings.infinite_stamina : assist == LOCAL_ARROWS ? g_settings.infinite_arrows
         : assist == LOCAL_HEALTH ? g_settings.god_mode : -1;
}

void local_assists_init(void) {
    local_assists_bind_upstream(upstream_set, upstream_get);
    static const ControlCmd commands[] = {
        {"assist", "<name> [on|off]", "Local experimental assists", command, CONTROL_PLAYER},
    };
    control_register(commands, 1);
}
