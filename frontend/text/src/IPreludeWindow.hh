// IPreludeWindow.hh --- base class for the break windows
//
// Copyright (C) 2001, 2002, 2003, 2007 Rob Caelers & Raymond Penners
// All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#ifndef IPRELUDEWINDOW_HH
#define IPRELUDEWINDOW_HH

#include <string>
#include "IApp.hh"

using namespace workrave;

class IPreludeWindow
{
public:
  virtual ~IPreludeWindow() {};

  //! Starts (i.e. shows) the prelude window.
  virtual void start() = 0;

  //! Stops (i.e. hides) the prelude window.
  virtual void stop() = 0;

  //! Destroys the prelude window.
  /*! \warn this will 'delete' the window.
   */
  virtual void destroy() = 0;

  //! Refreshes the content of the prelude window.
  virtual void refresh() = 0;

  //! Sets the progress to the specified value and maximum value.
  virtual void set_progress(int value, int max_value) = 0;

  //! Sets the alert stage of the prelude window.
  virtual void set_stage(IApp::PreludeStage stage) = 0;

  //! Sets the progress text of the prelude window.
  virtual void set_progress_text(IApp::PreludeProgressText text) = 0;

  //! Sets the response callback.
  virtual void set_response(IBreakResponse *pri) = 0;
};

#endif // IPRELUDEWINDOW_HH
