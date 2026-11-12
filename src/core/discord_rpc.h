#ifndef DISCORD_RPC_H
#define DISCORD_RPC_H

#include "types.h"

// Discord Rich Presence over the local IPC socket (Win32 named pipe on Windows, Unix socket elsewhere)
void DiscordRpcInit(bool enabled);
void DiscordRpcShutdown(void);
void DiscordRpcUpdate(const Satellite *selected, int sat_count, double epoch);
void DiscordRpcSetEnabled(bool enabled);
bool DiscordRpcIsConnected(void);

#endif // DISCORD_RPC_H
