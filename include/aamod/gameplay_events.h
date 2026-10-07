#ifndef AAMOD_GAMEPLAY_EVENTS_H
#define AAMOD_GAMEPLAY_EVENTS_H
#include <stdint.h>
#define AAMOD_GAMEPLAY_RUN_START 1u
#define AAMOD_GAMEPLAY_DAMAGE 2u
#define AAMOD_GAMEPLAY_PICKUP 4u
#define AAMOD_GAMEPLAY_RUN_END 8u
#define AAMOD_GAMEPLAY_CONTENT_CURRENCY 3u
#define AAMOD_GAMEPLAY_CURRENCY_CRYSTALS 1u
#define AAMOD_GAMEPLAY_CONTENT_RUN_RESULT 4u
#define AAMOD_GAMEPLAY_RUN_DEFEAT 0u
#define AAMOD_GAMEPLAY_RUN_VICTORY 1u
#define AAMOD_GAMEPLAY_RUN_ABANDONED 2u
/* 只发布已确认原生入口的事件。supported_kinds 描述本次适配的实际范围。
 * 不以快照变化模拟伤害、拾取或开局。无原生来源的字段为零。
 * run_key 是引擎本轮计数，不是跨进程唯一标识；player 从零开始。
 */
typedef struct AAModGameplayEvent {
    uint64_t id;
    uint32_t kind,player;
    uint32_t source_rva,update_thread_id;
    double run_key,before,after,amount;
    uint64_t tick_ms;
    uint32_t content_kind,content_id;
} AAModGameplayEvent;
typedef struct AAModGameplayInfo {
    uint32_t size,status,supported_kinds,reserved;
    uint64_t oldest,newest;
} AAModGameplayInfo;
/* 保留最近 128 条。游标错误时 count=0，info 含当前边界，events 不修改。
 * after=0 从最早保留项读取；capacity=0 可查询边界。推进到最后复制项 ID。
 * status 使用 AAMOD_STATE_*；不支持的宿主返回 UNSUPPORTED 和空流。
 */
typedef uint32_t (*AAModGameplayEventsFn)(uint64_t after,AAModGameplayEvent* events,
    uint32_t capacity,uint32_t event_size,uint32_t* count,AAModGameplayInfo* info,uint32_t info_size);
#endif
