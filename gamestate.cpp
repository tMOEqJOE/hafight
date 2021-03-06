#include <stdio.h>
#include <math.h>
#include "include/ggponet.h"
#include "inputreader.h"
#include "gamestate.h"

extern GGPOSession *ggpo;

/*
 * InitGameState --
 *
 * Initialize our game state.
 */

void
GameState::Init(int num_players)
{
   _framenumber = 0;
   _num_fighters = num_players;
   int i;

   for (i = 0; i < _num_fighters; i++) {
      _fighters[i].position.x = 100.0;
      _fighters[i].position.y = 100.0;
      _fighters[i].heading = 0;
      _fighters[i].health = STARTING_HEALTH;
      _fighters[i].radius = FIGHTER_RADIUS;
   }
}

void GameState::GetFighterAI(int i, double *heading, double *thrust, int *fire)
{
   *heading = (_fighters[i].heading + 5) % 360;
   *thrust = 0;
   *fire = 0;
}

void GameState::ParseFighterInputs(int inputs, int i, double *heading, double *thrust, int *fire)
{
   Fighter *fighter = _fighters + i;

   ggpo_log(ggpo, "parsing fighter %d inputs: %d.\n", i, inputs);

   if (inputs & INPUT_ROTATE_RIGHT) {
      *heading = (fighter->heading + ROTATE_INCREMENT) % 360;
   } else if (inputs & INPUT_ROTATE_LEFT) {
      *heading = (fighter->heading - ROTATE_INCREMENT + 360) % 360;
   } else {
      *heading = fighter->heading;
   }

   if (inputs & INPUT_THRUST) {
      *thrust = FIGHTER_THRUST;
   } else if (inputs & INPUT_BREAK) {
      *thrust = -FIGHTER_THRUST;
   } else {
      *thrust = 0;
   }
   *fire = inputs & INPUT_FIRE;
}

void GameState::MoveFighter(int which, double x, double y)
{
   Fighter *fighter = _fighters + which;
   
   ggpo_log(ggpo, "calculation of new fighter coordinates: (thrust:%.4f heading:%.4f).\n", x, y);

   fighter->heading = (int)x;

   printf("%f %f\n", x, y);

   if (y) {
      double dx = x;
      double dy = y;

      fighter->velocity.dx += dx;
      fighter->velocity.dy += dy;
   }
   ggpo_log(ggpo, "new fighter velocity: (dx:%.4f dy:%2.f).\n", fighter->velocity.dx, fighter->velocity.dy);

   fighter->position.x += fighter->velocity.dx;
   fighter->position.y += fighter->velocity.dy;
   ggpo_log(ggpo, "new fighter position: (dx:%.4f dy:%2.f).\n", fighter->position.x, fighter->position.y);
}

void
GameState::Update(int inputs[], int disconnect_flags)
{
   _framenumber++;
   for (int i = 0; i < _num_fighters; i++) {
      double thrust, heading;
      int fire;

      if (disconnect_flags & (1 << i)) {
         GetFighterAI(i, &heading, &thrust, &fire);
      } else {
         ParseFighterInputs(inputs[i], i, &heading, &thrust, &fire);
      }
      MoveFighter(i, heading, thrust);

      if (_fighters[i].cooldown) {
         _fighters[i].cooldown--;
      }
   }
}
