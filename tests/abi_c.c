#include "aamod/aamod.h"
_Static_assert(offsetof(AAModAPI, capabilities) == 128, "ABI 1 prefix changed");
_Static_assert(offsetof(AAModAPI, game_info) == 136, "0.1.2 prefix changed");
_Static_assert(sizeof(AAModGameInfo) == 200, "game identity layout changed");
_Static_assert(offsetof(AAModAPI, game_state) == 144, "0.1.3 prefix changed");
_Static_assert(offsetof(AAModAPI, state_events) == 152, "0.1.4 prefix changed");
_Static_assert(sizeof(AAModStateEvent) == 272, "event layout changed");
_Static_assert(offsetof(AAModAPI, command_owner) == 160, "event prefix changed");
_Static_assert(sizeof(AAModCommand) == 32, "command layout changed");
_Static_assert(sizeof(AAModCommandResult) == 48, "command result layout changed");
_Static_assert(offsetof(AAModAPI, resource_owner) == 184, "command prefix changed");
_Static_assert(sizeof(AAModImage) == 40, "image layout changed");
_Static_assert(offsetof(AAModAPI, native_image_info) == 208, "0.1.5 prefix changed");
_Static_assert(offsetof(AAModAPI, data_owner) == 248, "native image API prefix changed");
_Static_assert(offsetof(AAModAPI, equipment) == 280, "plugin data API prefix changed");
_Static_assert(offsetof(AAModAPI, combat_state) == 288, "equipment API prefix changed");
_Static_assert(offsetof(AAModAPI, content_catalog) == 296, "combat API prefix changed");
_Static_assert(offsetof(AAModAPI, equipment_submit) == 304, "catalog API prefix changed");
_Static_assert(offsetof(AAModAPI, gameplay_events) == 320, "equipment commands API prefix changed");
_Static_assert(sizeof(AAModAPI) == 328, "native events API layout changed");
_Static_assert(sizeof(AAModGameplayEvent) == 72, "native event copied layout changed");
_Static_assert(sizeof(AAModGameplayInfo) == 32, "native event stream metadata changed");
_Static_assert(sizeof(AAModEquipmentCommand) == 32, "equipment command layout changed");
_Static_assert(sizeof(AAModEquipmentResult) == 48, "equipment result layout changed");
_Static_assert(sizeof(AAModCatalogEntry) == 208, "catalog entry layout changed");
_Static_assert(sizeof(AAModCombatState) == 48, "combat copied layout changed");
_Static_assert(sizeof(AAModEquipment) == 64, "equipment copied layout changed");
_Static_assert(sizeof(AAModNativeImageInfo) == 40, "native image metadata layout changed");
_Static_assert(sizeof(AAModImageReplacementStatus) == 40, "native image result layout changed");
_Static_assert(sizeof(AAModPlayerState) == 40, "player state layout changed");
_Static_assert(sizeof(AAModGameState) == 256, "game state layout changed");
AAMOD_EXPORT uint32_t AAMOD_Init(const AAModAPI* api, uint32_t size) {
    if (!api || size < offsetof(AAModAPI, capabilities) || api->api_version != 1)
        return AAMOD_ERR_ABI;
    return AAMOD_OK;
}
