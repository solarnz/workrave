// BreakControl.cc
//
// Copyright (C) 2001, 2002, 2003, 2004, 2005 Rob Caelers & Raymond Penners
// All rights reserved.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or (at your option)
// any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//

static const char rcsid[] = "$Id$";

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "nls.h"
#include "debug.hh"

#include <assert.h>
#include <string>

#include "BreakControl.hh"

#include "BreakInterface.hh"
#include "Core.hh"
#include "Statistics.hh"
#include "AppInterface.hh"
#include "ActivityMonitorInterface.hh"
#include "ActivityMonitorListener.hh"
#include "Timer.hh"
#include "Statistics.hh"

#ifdef HAVE_DISTRIBUTION
#include "DistributionManager.hh"
#endif


//! Construct a new Break Controller.
/*!
 *  \param id ID of the break this BreakControl controls.
 *  \param c pointer to control interface.
 *  \param factory pointer to the GUI window factory used to create the break
 *          windows.
 *  \param timer pointer to the interface of the timer that belongs to this break.
 */
BreakControl::BreakControl(BreakId id, Core *c, AppInterface *app, Timer *timer) :
  break_id(id),
  core(c),
  application(app),
  break_timer(timer),
  break_stage(STAGE_NONE),
  reached_max_prelude(false),
  prelude_time(0),
  user_initiated(false),
  prelude_count(0),
  postponable_count(0),
  reached_max_postpone(false),
  max_number_of_preludes(2),
  max_number_of_postpones(2),
  ignorable_break(true),
  config_ignorable_break(true),
  fake_break(false),
  fake_break_count(0),
  user_abort(false),
  delayed_abort(false)
{
  set_ignorable_break(ignorable_break);

  assert(break_timer != NULL);
  assert(application != NULL);
}


//! Destructor.
BreakControl::~BreakControl()
{
  application->hide_break_window();
}


//! Periodic heartbeat.
void
BreakControl::heartbeat()
{
  TRACE_ENTER_MSG("BreakControl::heartbeat", break_id);

  prelude_time++;

  bool is_idle = false;

  if (!break_timer->has_activity_monitor())
    {
      // Prefer the running state of the break timer as input for
      // our current activity.
      TimerInterface::TimerState tstate = break_timer->get_state();
      is_idle = (tstate == TimerInterface::STATE_STOPPED);
    }
  else
    {
      // Unless the timer has its own activity monitor.
      ActivityState activity_state = core->get_current_monitor_state();
      is_idle = (activity_state != ACTIVITY_ACTIVE);
    }

  TRACE_MSG("stage = " << break_stage);
  switch (break_stage)
    {
    case STAGE_NONE:
      break;
      
    case STAGE_SNOOZED:
      break;

    case STAGE_DELAYED:
      {
        if (delayed_abort)
          {
            // User become active during delayed break.
            goto_stage(STAGE_SNOOZED);
          }
        else if (is_idle)
          {
            // User is idle.
            goto_stage(STAGE_TAKING);
          }
      }
      break;
        
    case STAGE_PRELUDE:
      {
        assert(application != NULL);

        TRACE_MSG("prelude time = " << prelude_time);


        update_prelude_window();
        application->refresh_break_window();

        if (is_idle)
          {
            // User is idle.
            if (prelude_time >= 10)
              {
                // User is idle and prelude is visible for at least 10s.
                goto_stage(STAGE_TAKING);
              }
          }
        else if (prelude_time == 30)
          {
            // User is not idle and the prelude is visible for 30s.
            if (reached_max_prelude)
              {
                // Final prelude, force break.
                goto_stage(STAGE_TAKING);
              }
            else 
              {
                // Delay break.
                goto_stage(STAGE_DELAYED);
              }
          }
        else if (prelude_time == 20)
          {
            // Still not idle after 20s. Red alert.
            application->set_prelude_stage(AppInterface::STAGE_ALERT);
            application->refresh_break_window();
          }
        else if (prelude_time == 10)
          {
            // Still not idle after 10s. Yellow alert.
            application->set_prelude_stage(AppInterface::STAGE_WARN);
            application->refresh_break_window();
          }

        if (prelude_time == 4)
          {
            // Move prelude window to top of screen after 4s.
            application->set_prelude_stage(AppInterface::STAGE_MOVE_OUT);
          }
      } 
      break;
        
    case STAGE_TAKING:
      {
        // refresh the break window.
        update_break_window();
        application->refresh_break_window();
      }
      break;
    }

  TRACE_EXIT();
}
  

//! Initiates the specified break stage.
void
BreakControl::goto_stage(BreakStage stage)
{
  TRACE_ENTER_MSG("BreakControl::goto_stage", break_id << " " << stage);
  switch (stage)
    {
    case STAGE_DELAYED:
      {
        delayed_abort = false;
        ActivityMonitorInterface *monitor = core->get_activity_monitor();
        monitor->set_listener(this);
      }
      break;
      
    case STAGE_NONE:
      {
        // Teminate the break.
        application->hide_break_window();
        core->defrost();

        if (break_stage == STAGE_TAKING)
          {
            // Update statistics and play sound if the break end
            // was "natural"
            time_t idle = break_timer->get_elapsed_idle_time();
            time_t reset = break_timer->get_auto_reset();

            if (idle >= reset && !user_abort)
              {
                // natural break end.

                // Update stats.
                Statistics *stats = core->get_statistics();
                stats->increment_break_counter(break_id, Statistics::STATS_BREAKVALUE_TAKEN);

                // Play sound
                switch (break_id)
                  {
                  case BREAK_ID_REST_BREAK:
                    post_event(CORE_EVENT_SOUND_REST_BREAK_ENDED);
                    break;
                  case BREAK_ID_MICRO_BREAK:
                    post_event(CORE_EVENT_SOUND_MICRO_BREAK_ENDED);
                    break;
                  default:
                    break;
                  }
              }
          }
      }
      break;
      
    case STAGE_SNOOZED:
      {
        application->hide_break_window();
        if (!user_initiated)
          {
            post_event(CORE_EVENT_SOUND_BREAK_IGNORED);
          }
        core->defrost();
      }
      break;

    case STAGE_PRELUDE:
      {
        prelude_count++;
        postponable_count++;
        prelude_time = 0;
        application->hide_break_window();

        prelude_window_start();
        application->refresh_break_window();
        post_event(CORE_EVENT_SOUND_BREAK_PRELUDE);
      }
      break;
        
    case STAGE_TAKING:
      {
        // Remove the prelude window, if necessary.
        application->hide_break_window();

        // "Innocent until proven guilty".
        ActivityMonitorInterface *monitor = core->get_activity_monitor();
        monitor->force_idle();
        break_timer->stop_timer();

	// Check if we have reached the maximum number of preludes and force
	// break
	if (reached_max_postpone)
	  {
	    ignorable_break = false;
	  }
	else
	  {
	    ignorable_break = config_ignorable_break;
	  }

        // Start the break.
        break_window_start();
        application->refresh_break_window();

        // Play sound
        if (!user_initiated)
          {
            CoreEvent event = CORE_EVENT_NONE;
            switch (break_id)
              {
              case BREAK_ID_REST_BREAK:
                event = CORE_EVENT_SOUND_REST_BREAK_STARTED;
                break;
              case BREAK_ID_MICRO_BREAK:
                event = CORE_EVENT_SOUND_MICRO_BREAK_STARTED;
                break;
              case BREAK_ID_DAILY_LIMIT:
                event = CORE_EVENT_SOUND_DAILY_LIMIT;
                break;
              default:
                break;
              }
            post_event(event);
          }

        core->freeze();
      }
      break;
    }

  break_stage = stage;
  TRACE_EXIT();
}


//! Updates the contents of the prelude window.
void
BreakControl::update_prelude_window()
{
  application->set_break_progress(prelude_time, 29);
}


//! Updates the contents of the break window.
void
BreakControl::update_break_window()
{
  assert(break_timer != NULL);
  time_t duration = break_timer->get_auto_reset();
  time_t idle = 0;
  
  if (fake_break)
    {
      idle = duration - fake_break_count;

      if (fake_break_count <= 0)
        {
          stop_break(false);
        }

      fake_break_count--;
    }
  else
    {
      idle = break_timer->get_elapsed_idle_time();
    }

  if (idle > duration)
    {
      idle = duration;
    }

  application->set_break_progress(idle, duration);
}


//! Starts the break.
void
BreakControl::start_break()
{
  TRACE_ENTER_MSG("BreakControl::start_break", break_id);
  fake_break = false;
  user_initiated = false;
  prelude_time = 0;
  user_abort = false;
  
  reached_max_prelude = max_number_of_preludes >= 0 && prelude_count + 1 >= max_number_of_preludes;
  reached_max_postpone = max_number_of_postpones >= 0 && postponable_count + 1 >= max_number_of_postpones;

  if ((max_number_of_preludes >= 0 && prelude_count >= max_number_of_preludes) ||
      (max_number_of_postpones >= 0 && postponable_count >= max_number_of_postpones))
    {
      // Forcing break without prelude.
      goto_stage(STAGE_TAKING);
    }
  else
    {
      // Starting break with prelude.
      
      // Idle until proven guilty.
      ActivityMonitorInterface *monitor = core->get_activity_monitor();
      monitor->force_idle();
      break_timer->stop_timer();

      // Update statistics.
      Statistics *stats = core->get_statistics();
      stats->increment_break_counter(break_id, Statistics::STATS_BREAKVALUE_PROMPTED);
      
      if (prelude_count == 0)
        {
          stats->increment_break_counter(break_id, Statistics::STATS_BREAKVALUE_UNIQUE_BREAKS);
        }

      // Start prelude.
      goto_stage(STAGE_PRELUDE);
    }

  TRACE_EXIT();
}


//! Starts the break without preludes.
void
BreakControl::force_start_break(bool initiated_by_user)
{
  TRACE_ENTER_MSG("BreakControl::force_start_break", break_id);

  fake_break = false;
  user_initiated = initiated_by_user;
  prelude_time = 0;
  user_abort = false;
  
  if (break_timer->is_auto_reset_enabled())
    {
      TRACE_MSG("auto reset enabled");
      time_t idle = break_timer->get_elapsed_idle_time();
      TRACE_MSG(idle << " " << break_timer->get_auto_reset());
      if (idle >= break_timer->get_auto_reset())
        {
          TRACE_MSG("Faking break");
          fake_break = true;
          fake_break_count = break_timer->get_auto_reset();
        }
    }

  if (!break_timer->get_activity_sensitive())
    {
      break_timer->force_idle();
    }
  
  goto_stage(STAGE_TAKING);

  TRACE_EXIT();
}


//! Stops the break.
/*!
 *  Stopping a break will reset the "number of presented preludes" counter. So,
 *  wrt, "max-preludes", the break will start over when it comes back.
 */
void
BreakControl::stop_break(bool forced_stop)
{
  TRACE_ENTER_MSG("BreakControl::stop_break", break_id);

  TRACE_MSG(" forced stop = " << forced_stop);

  suspend_break();
  prelude_count = 0;
  if (!forced_stop)
    {
      postponable_count = 0;
    }

  TRACE_EXIT();
}


//! Suspend the break.
/*!
 *  A suspended break will come back after snooze time. The number of times the
 *  break will come back can be defined with set_max_preludes.
 */
void
BreakControl::suspend_break()
{
  TRACE_ENTER_MSG("BreakControl::suspend_break", break_id);

  goto_stage(STAGE_NONE);
  core->defrost();
  
  TRACE_EXIT();
}


//! Does the controller need a heartbeat?
bool
BreakControl::need_heartbeat()
{
  return ( break_stage != STAGE_NONE && break_stage != STAGE_SNOOZED );
}


//! Is the break active ?
BreakControl::BreakState
BreakControl::get_break_state()
{
  BreakState ret = BREAK_INACTIVE;

  if (break_stage == STAGE_NONE)
    {
      ret = BREAK_INACTIVE;
    }
  else if (break_stage == STAGE_SNOOZED)
    {
      ret = BREAK_INACTIVE; // FIXME: actually: SUSPENDED;
    }
  else
    {
      ret = BREAK_ACTIVE;
    }
  return ret;
}


//! Postpones the active break.
/*!
 *  Postponing a break does not reset the break timer. The break prelude window
 *  will re-appear after snooze time.
 */
void
BreakControl::postpone_break()
{
  if (!user_initiated)
    {
      if (!fake_break)
        {
          // Snooze the timer.
          break_timer->snooze_timer();
        }
      
      // Update stats.
      Statistics *stats = core->get_statistics();
      stats->increment_break_counter(break_id, Statistics::STATS_BREAKVALUE_POSTPONED);
    }

  // This is to avoid a skip sound...
  user_abort = true;

  // and stop the break.
  stop_break(true);
}


//! Skips the active break.
/*!
 *  Skipping a break resets the break timer.
 */
void
BreakControl::skip_break()
{
  // This is to avoid a skip sound...
  user_abort = true;

  if (break_id == BREAK_ID_DAILY_LIMIT)
    {
      // Make sure the daily limit remains silent after skipping.
      break_timer->inhibit_snooze();
    }
  else
    {
      // Reset the restbreak timer.
      break_timer->reset_timer();
    }
  
  // Update stats.
  Statistics *stats = core->get_statistics();
  stats->increment_break_counter(break_id, Statistics::STATS_BREAKVALUE_SKIPPED);

  // and stop the break.
  stop_break(false);
}

void
BreakControl::stop_prelude()
{
  if (break_stage == STAGE_DELAYED)
    {
      delayed_abort = true;
    }
}

//! Sets the maximum number of preludes. 
/*!
 *  After the maximum number of preludes, the break either stops bothering the
 *  user, or forces a break. This can be set with set_force_after_preludes.
 */
void
BreakControl::set_max_preludes(int m)
{
  max_number_of_preludes = m;
}

//! Sets the maximum number of preludes before making the break not ignorable
void
BreakControl::set_max_postpone(int m)
{
  max_number_of_postpones = m;
}


//! Sets the ignorable-break flags
/*!
 *  A break that is ignorable has a skip/postpone button.
 */
void
BreakControl::set_ignorable_break(bool i)
{
  config_ignorable_break = i;
}


//! Creates and shows the break window.
void
BreakControl::break_window_start()
{
  TRACE_ENTER_MSG("BreakControl::break_window_start", break_id);

  application->start_break_window(break_id,
                                  user_initiated ? true : ignorable_break);

  update_break_window();
  TRACE_EXIT();
}

//! Creates and shows the prelude window.
void
BreakControl::prelude_window_start()
{
  TRACE_ENTER_MSG("BreakControl::prelude_window_start", break_id);

  application->start_prelude_window(break_id);

  application->set_prelude_stage(AppInterface::STAGE_INITIAL);

  if (!reached_max_prelude)
    {
      application->set_prelude_progress_text(AppInterface::PROGRESS_TEXT_DISAPPEARS_IN);
    }
  else 
    {
      application->set_prelude_progress_text(AppInterface::PROGRESS_TEXT_BREAK_IN);
    }
  
  update_prelude_window();
  
  TRACE_EXIT();
}




bool
BreakControl::action_notify()
{
  TRACE_ENTER("GUI::action_notify");
  core->stop_prelude(break_id);
  TRACE_EXIT();
  return false;   // false: kill listener.
}


//! Initializes this control to the specified state.
void
BreakControl::set_state_data(bool active, const BreakStateData &data)
{
  TRACE_ENTER_MSG("BreakStateData::set_state_data", active);

  TRACE_MSG("forced = " << data.user_initiated <<
            " prelude = " << data.prelude_count <<
            " stage = " <<  data.break_stage <<
            " final = " << reached_max_prelude <<
	    " total preludes = " << postponable_count <<
	    " force ignorable break = " << reached_max_postpone <<
            " time = " << data.prelude_time);
  
  // TODO: check application->hide_break_window();

  user_initiated = data.user_initiated;
  prelude_count = data.prelude_count;
  prelude_time = data.prelude_time;
  postponable_count = data.postponable_count;

  //TODO:
  return;
    
  BreakStage new_break_stage = (BreakStage) data.break_stage;
  
  if (new_break_stage == STAGE_TAKING)
    {
      TRACE_MSG("TAKING -> PRELUDE");
      new_break_stage = STAGE_PRELUDE;
      prelude_count = max_number_of_preludes - 1;
    }
  
  //TODO: check if this can/must be removed  if (active)
    {
      if (user_initiated && new_break_stage == STAGE_TAKING)
        {
          TRACE_MSG("User inflicted break -> TAKING");

          prelude_time = 0;
          goto_stage(STAGE_TAKING);
        }
      else if (new_break_stage == STAGE_TAKING) // && !user_initiated
        {
          TRACE_MSG("Break active");

          // Idle until proven guilty.
          ActivityMonitorInterface *monitor = core->get_activity_monitor();
          monitor->force_idle();
          break_timer->stop_timer();
      
          goto_stage(STAGE_TAKING);
        }
      else if (new_break_stage == STAGE_SNOOZED || new_break_stage == STAGE_PRELUDE)
        {
          TRACE_MSG("Snooze/Prelude");
  
          user_initiated = false;
          prelude_time = 0;
          reached_max_prelude = max_number_of_preludes >= 0 && prelude_count + 1 >= max_number_of_preludes;

          if (max_number_of_preludes >= 0 && prelude_count >= max_number_of_preludes)
            {
              TRACE_MSG("TAKING");
              goto_stage(STAGE_TAKING);
            }
          else
            {
              TRACE_MSG("PRELUDE");

              // Idle until proven guilty.
              ActivityMonitorInterface *monitor = core->get_activity_monitor();
              monitor->force_idle();
              break_timer->stop_timer();
  
              goto_stage(STAGE_PRELUDE);
            }
        }
      else
        {
          TRACE_MSG("NONE");
          goto_stage(STAGE_NONE);
        }
    }

  TRACE_EXIT();
}


//! Returns the state of this control.
void
BreakControl::get_state_data(BreakStateData &data)
{
  data.user_initiated = user_initiated;
  data.prelude_count = prelude_count;
  data.break_stage = break_stage;
  data.reached_max_prelude = reached_max_prelude;
  data.prelude_time = prelude_time;
  data.postponable_count = postponable_count;
  data.reached_max_postpone = reached_max_postpone;
}


//! Plays the specified sound unless action is user initiated.
void
BreakControl::post_event(CoreEvent event)
{
  TRACE_ENTER_MSG("BreakControl::post_event", break_id);
  if (event >= 0)
    {
      core->post_event(event);
    }
  TRACE_EXIT();
}
