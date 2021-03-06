#include <gl/gl.h>
#include <gl/glu.h>
#include <math.h>
#include <stdio.h>
#include "hafight.h"
#include "gamestate.h"
#include "nongamestate.h"
#include "include/ggponet.h"
#include "raylibrenderer.h"
#include "inputreader.h"

//#define SYNC_TEST    // test: turn on synctest
#define MAX_PLAYERS     64

GameState gs = { 0 };
NonGameState ngs = { 0 };
Renderer *renderer = NULL;
GGPOSession *ggpo = NULL;

/* 
 * Simple checksum function stolen from wikipedia:
 *
 *   http://en.wikipedia.org/wiki/Fletcher%27s_checksum
 */
int
fletcher32_checksum(short *data, size_t len)
{
   int sum1 = 0xffff, sum2 = 0xffff;

   while (len) {
      size_t tlen = len > 360 ? 360 : len;
      len -= tlen;
      do {
         sum1 += *data++;
         sum2 += sum1;
      } while (--tlen);
      sum1 = (sum1 & 0xffff) + (sum1 >> 16);
      sum2 = (sum2 & 0xffff) + (sum2 >> 16);
   }

   /* Second reduction step to reduce sums to 16 bits */
   sum1 = (sum1 & 0xffff) + (sum1 >> 16);
   sum2 = (sum2 & 0xffff) + (sum2 >> 16);
   return sum2 << 16 | sum1;
}

/*
 * ha_begin_game_callback --
 *
 * The begin game callback.  We don't need to do anything special here,
 * so just return true.
 */
bool __cdecl
ha_begin_game_callback(const char *)
{
   return true;
}

/*
 * ha_on_event_callback --
 *
 * Notification from GGPO that something has happened.  Update the status
 * text at the bottom of the screen to notify the user.
 */
bool __cdecl
ha_on_event_callback(GGPOEvent *info)
{
   int progress;
   switch (info->code) {
   case GGPO_EVENTCODE_CONNECTED_TO_PEER:
      ngs.SetConnectState(info->u.connected.player, Synchronizing);
      renderer->SetStatusText("Event code connected to peer");
      break;
   case GGPO_EVENTCODE_SYNCHRONIZING_WITH_PEER:
      progress = 100 * info->u.synchronizing.count / info->u.synchronizing.total;
      ngs.UpdateConnectProgress(info->u.synchronizing.player, progress);
      renderer->SetStatusText("Synchronizing...");
      break;
   case GGPO_EVENTCODE_SYNCHRONIZED_WITH_PEER:
      ngs.UpdateConnectProgress(info->u.synchronized.player, 100);
      renderer->SetStatusText("Synchronize Complete");
      break;
   case GGPO_EVENTCODE_RUNNING:
      ngs.SetConnectState(Running);
      renderer->SetStatusText("Running");
      break;
   case GGPO_EVENTCODE_CONNECTION_INTERRUPTED:
      ngs.SetDisconnectTimeout(info->u.connection_interrupted.player,
                               timeGetTime(),
                               info->u.connection_interrupted.disconnect_timeout);
      renderer->SetStatusText("Interrupted");
      break;
   case GGPO_EVENTCODE_CONNECTION_RESUMED:
      ngs.SetConnectState(info->u.connection_resumed.player, Running);
      renderer->SetStatusText("Resumed connection");
      break;
   case GGPO_EVENTCODE_DISCONNECTED_FROM_PEER:
      ngs.SetConnectState(info->u.disconnected.player, Disconnected);
      renderer->SetStatusText("Disconnect");
      break;
   case GGPO_EVENTCODE_TIMESYNC:
      Sleep(1000 * info->u.timesync.frames_ahead / 60);
      renderer->SetStatusText("TimeSync");
      break;
   }
   return true;
}


/*
 * ha_advance_frame_callback --
 *
 * Notification from GGPO we should step foward exactly 1 frame
 * during a rollback.
 */
bool __cdecl
ha_advance_frame_callback(int)
{
   int inputs[MAX_FIGHTERS] = { 0 };
   int disconnect_flags;

   // Make sure we fetch new inputs from GGPO and use those to update
   // the game state instead of reading from the keyboard.
   ggpo_synchronize_input(ggpo, (void *)inputs, sizeof(int) * MAX_FIGHTERS, &disconnect_flags);
   HAFight_AdvanceFrame(inputs, disconnect_flags);
   return true;
}

/*
 * ha_load_game_state_callback --
 *
 * Makes our current state match the state passed in by GGPO.
 */
bool __cdecl
ha_load_game_state_callback(unsigned char *buffer, int len)
{
   memcpy(&gs, buffer, len);
   return true;
}

/*
 * ha_save_game_state_callback --
 *
 * Save the current state to a buffer and return it to GGPO via the
 * buffer and len parameters.
 */
bool __cdecl
ha_save_game_state_callback(unsigned char **buffer, int *len, int *checksum, int)
{
   *len = sizeof(gs);
   *buffer = (unsigned char *)malloc(*len);
   if (!*buffer) {
      return false;
   }
   memcpy(*buffer, &gs, *len);
   *checksum = fletcher32_checksum((short *)*buffer, *len / 2);
   return true;
}

/*
 * ha_log_game_state --
 *
 * Log the gamestate.  Used by the synctest debugging tool.
 */
bool __cdecl
ha_log_game_state(char *filename, unsigned char *buffer, int)
{
   FILE* fp = nullptr;
   fopen_s(&fp, filename, "w");
   printf("%s\n", filename);
   if (fp) {
      GameState *gamestate = (GameState *)buffer;
      fprintf(fp, "GameState object.\n");
    //   fprintf(fp, "  bounds: %d,%d x %d,%d.\n", gamestate->_bounds.left, gamestate->_bounds.top,
    //           gamestate->_bounds.right, gamestate->_bounds.bottom);
      fprintf(fp, "  num_fighters: %d.\n", gamestate->_num_fighters);
      for (int i = 0; i < gamestate->_num_fighters; i++) {
         Fighter *fighter = gamestate->_fighters + i;
         fprintf(fp, "  fighter %d position:  %.4f, %.4f\n", i, fighter->position.x, fighter->position.y);
         fprintf(fp, "  fighter %d velocity:  %.4f, %.4f\n", i, fighter->velocity.dx, fighter->velocity.dy);
         fprintf(fp, "  fighter %d radius:    %d.\n", i, fighter->radius);
         fprintf(fp, "  fighter %d heading:   %d.\n", i, fighter->heading);
         fprintf(fp, "  fighter %d health:    %d.\n", i, fighter->health);
         fprintf(fp, "  fighter %d speed:     %d.\n", i, fighter->speed);
         fprintf(fp, "  fighter %d cooldown:  %d.\n", i, fighter->cooldown);
         fprintf(fp, "  fighter %d score:     %d.\n", i, fighter->score);
      }
      fclose(fp);
   }
   return true;
}

/*
 * ha_free_buffer --
 *
 * Free a save state buffer previously returned in ha_save_game_state_callback.
 */
void __cdecl 
ha_free_buffer(void *buffer)
{
   free(buffer);
}

/*
 * HAFight_Init --
 *
 * Initialize the fight game.  This initializes the game state and
 * the video renderer and creates a new network session.
 */
void
HAFight_Init(unsigned short localport, int num_players, GGPOPlayer *players, int num_spectators)
{
   GGPOErrorCode result;
   renderer = new RaylibRenderer();

   // Initialize the game state
   gs.Init(num_players);
   ngs.num_players = num_players;

   // Fill in a ggpo callbacks structure to pass to start_session.
   GGPOSessionCallbacks cb = { 0 };
   cb.begin_game      = ha_begin_game_callback;
   cb.advance_frame	 = ha_advance_frame_callback;
   cb.load_game_state = ha_load_game_state_callback;
   cb.save_game_state = ha_save_game_state_callback;
   cb.free_buffer     = ha_free_buffer;
   cb.on_event        = ha_on_event_callback;
   cb.log_game_state  = ha_log_game_state;

#if defined(SYNC_TEST)
   result = ggpo_start_synctest(&ggpo, &cb, "hafight", num_players, sizeof(int), 1);
#else
   result = ggpo_start_session(&ggpo, &cb, "hafight", num_players, sizeof(int), localport);
#endif

   // automatically disconnect clients after 3000 ms and start our count-down timer
   // for disconnects after 1000 ms.   To completely disable disconnects, simply use
   // a value of 0 for ggpo_set_disconnect_timeout.
   ggpo_set_disconnect_timeout(ggpo, 3000);
   ggpo_set_disconnect_notify_start(ggpo, 1000);

   int i;
   for (i = 0; i < num_players + num_spectators; i++) {
      GGPOPlayerHandle handle;
      printf("\n%d\n", i);
      result = ggpo_add_player(ggpo, players + i, &handle);
      printf("\n%d %d %d\n", result, i, num_players + num_spectators);
      ngs.players[i].handle = handle;
      ngs.players[i].type = players[i].type;
      if (players[i].type == GGPO_PLAYERTYPE_LOCAL) {
         ngs.players[i].connect_progress = 100;
         ngs.local_player_handle = handle;
         ngs.SetConnectState(handle, Connecting);
         ggpo_set_frame_delay(ggpo, handle, FRAME_DELAY);
      } else {
         ngs.players[i].connect_progress = 0;
      }
   }

   renderer->SetStatusText("Connecting to peers.");
}

/*
 * HAFight_InitSpectator --
 *
 * Create a new spectator session
 */
void
HAFight_InitSpectator(unsigned short localport, int num_players, char *host_ip, unsigned short host_port)
{
   GGPOErrorCode result;
   renderer = new RaylibRenderer();

   // Initialize the game state
   gs.Init(num_players);
   ngs.num_players = num_players;

   // Fill in a ggpo callbacks structure to pass to start_session.
   GGPOSessionCallbacks cb = { 0 };
   cb.begin_game      = ha_begin_game_callback;
   cb.advance_frame	  = ha_advance_frame_callback;
   cb.load_game_state = ha_load_game_state_callback;
   cb.save_game_state = ha_save_game_state_callback;
   cb.free_buffer     = ha_free_buffer;
   cb.on_event        = ha_on_event_callback;
   cb.log_game_state  = ha_log_game_state;

   result = ggpo_start_spectating(&ggpo, &cb, "hafight", num_players, sizeof(int), localport, host_ip, host_port);

   renderer->SetStatusText("Starting new spectator session");
}


/*
 * HAFight_DisconnectPlayer --
 *
 * Disconnects a player from this session.
 */

void
HAFight_DisconnectPlayer(int player)
{
   if (player < ngs.num_players) {
      char logbuf[128];
      GGPOErrorCode result = ggpo_disconnect_player(ggpo, ngs.players[player].handle);
      if (GGPO_SUCCEEDED(result)) {
         sprintf_s(logbuf, ARRAYSIZE(logbuf), "Disconnected player %d.\n", player);
      } else {
         sprintf_s(logbuf, ARRAYSIZE(logbuf), "Error while disconnecting player (err:%d).\n", result);
      }
      renderer->SetStatusText(logbuf);
   }
}


/*
 * HAFight_DrawCurrentFrame --
 *
 * Draws the current frame without modifying the game state.
 */
void
HAFight_DrawCurrentFrame()
{
   if (renderer != nullptr) {
      renderer->Draw(gs, ngs);
   }
}

/*
 * HAFight_AdvanceFrame --
 *
 * Advances the game state by exactly 1 frame using the inputs specified
 * for player 1 and player 2.
 */
void HAFight_AdvanceFrame(int inputs[], int disconnect_flags)
{
   gs.Update(inputs, disconnect_flags);

   // update the checksums to display in the top of the window.  this
   // helps to detect desyncs.
   ngs.now.framenumber = gs._framenumber;
   ngs.now.checksum = fletcher32_checksum((short *)&gs, sizeof(gs) / 2);
   if ((gs._framenumber % 90) == 0) {
      ngs.periodic = ngs.now;
   }

   // Notify ggpo that we've moved forward exactly 1 frame.
   ggpo_advance_frame(ggpo);

   // Update the performance monitor display.
   GGPOPlayerHandle handles[MAX_PLAYERS];
   int count = 0;
   for (int i = 0; i < ngs.num_players; i++) {
      if (ngs.players[i].type == GGPO_PLAYERTYPE_REMOTE) {
         handles[count++] = ngs.players[i].handle;
      }
   }
}

/*
 * HAFight_RunFrame --
 *
 * Run a single frame of the game.
 */
void
HAFight_RunFrame()
{
  GGPOErrorCode result = GGPO_OK;
  int disconnect_flags;
  int inputs[MAX_FIGHTERS] = { 0 };

  if (ngs.local_player_handle != GGPO_INVALID_HANDLE) {
     int input = ReadInputs();
#if defined(SYNC_TEST)
     input = rand(); // test: use random inputs to demonstrate sync testing
#endif
     result = ggpo_add_local_input(ggpo, ngs.local_player_handle, &input, sizeof(input));
  }

   // synchronize these inputs with ggpo.  If we have enough input to proceed
   // ggpo will modify the input list with the correct inputs to use and
   // return 1.
  if (GGPO_SUCCEEDED(result)) {
     result = ggpo_synchronize_input(ggpo, (void *)inputs, sizeof(int) * MAX_FIGHTERS, &disconnect_flags);
     if (GGPO_SUCCEEDED(result)) {
         // inputs[0] and inputs[1] contain the inputs for p1 and p2.  Advance
         // the game by 1 frame using those inputs.
         HAFight_AdvanceFrame(inputs, disconnect_flags);
     }
  }
  HAFight_DrawCurrentFrame();
}

/*
 * HAFight_Idle --
 *
 * Spend our idle time in ggpo so it can use whatever time we have left over
 * for its internal bookkeeping.
 */
void
HAFight_Idle(int time)
{
   ggpo_idle(ggpo, time);
}

void
HAFight_Exit()
{
   memset(&gs, 0, sizeof(gs));
   memset(&ngs, 0, sizeof(ngs));

   if (ggpo) {
      ggpo_close_session(ggpo);
      ggpo = NULL;
   }
   delete renderer;
   renderer = NULL;
}
