/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXRemoteServer.h"
#include "nsCOMPtr.h"
#include "nsICommandLine.h"

#include "nsIWidget.h"
#include "nsAppShellCID.h"
#include "nsPIDOMWindow.h"
#include "mozilla/X11Util.h"

#include "nsCOMPtr.h"
#include "nsString.h"
#include "prenv.h"
#include "nsCRT.h"

#include "nsXULAppAPI.h"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

using namespace mozilla;

#define MOZILLA_VERSION_PROP "_MOZILLA_VERSION"
#define MOZILLA_LOCK_PROP "_MOZILLA_LOCK"
#define MOZILLA_RESPONSE_PROP "_MOZILLA_RESPONSE"
#define MOZILLA_USER_PROP "_MOZILLA_USER"
#define MOZILLA_PROFILE_PROP "_MOZILLA_PROFILE"
#define MOZILLA_PROGRAM_PROP "_MOZILLA_PROGRAM"
#define MOZILLA_COMMANDLINE_PROP "_MOZILLA_COMMANDLINE"

const unsigned char kRemoteVersion[] = "5.1";

// Minimize the roundtrips to the X server by getting all the atoms at once
static const char* XAtomNames[] = {
    MOZILLA_VERSION_PROP,    MOZILLA_LOCK_PROP,    MOZILLA_RESPONSE_PROP,
    MOZILLA_USER_PROP,       MOZILLA_PROFILE_PROP, MOZILLA_PROGRAM_PROP,
    MOZILLA_COMMANDLINE_PROP};
static Atom XAtoms[std::size(XAtomNames)];

Atom nsXRemoteServer::sMozVersionAtom;
Atom nsXRemoteServer::sMozLockAtom;
Atom nsXRemoteServer::sMozResponseAtom;
Atom nsXRemoteServer::sMozUserAtom;
Atom nsXRemoteServer::sMozProfileAtom;
Atom nsXRemoteServer::sMozProgramAtom;
Atom nsXRemoteServer::sMozCommandLineAtom;

nsXRemoteServer::nsXRemoteServer() = default;

void nsXRemoteServer::XRemoteBaseStartup(const char* aAppName,
                                         const char* aProfileName) {
  EnsureAtoms();

  mAppName = aAppName;
  ToLowerCase(mAppName);

  mProfileName = aProfileName;
}

void nsXRemoteServer::HandleCommandsFor(Window aWindowId) {
  // set our version
  XChangeProperty(mozilla::DefaultXDisplay(), aWindowId, sMozVersionAtom,
                  XA_STRING, 8, PropModeReplace, kRemoteVersion,
                  sizeof(kRemoteVersion) - 1);

  // get our username
  unsigned char* logname;
  logname = (unsigned char*)PR_GetEnv("LOGNAME");
  if (logname) {
    // set the property on the window if it's available
    XChangeProperty(mozilla::DefaultXDisplay(), aWindowId, sMozUserAtom,
                    XA_STRING, 8, PropModeReplace, logname,
                    strlen((char*)logname));
  }

  XChangeProperty(mozilla::DefaultXDisplay(), aWindowId, sMozProgramAtom,
                  XA_STRING, 8, PropModeReplace, (unsigned char*)mAppName.get(),
                  mAppName.Length());

  XChangeProperty(mozilla::DefaultXDisplay(), aWindowId, sMozProfileAtom,
                  XA_STRING, 8, PropModeReplace,
                  (unsigned char*)mProfileName.get(), mProfileName.Length());
}

bool nsXRemoteServer::HandleNewProperty(XID aWindowId, Display* aDisplay,
                                        Time aEventTime, Atom aChangedAtom) {
  if (aChangedAtom == sMozCommandLineAtom) {
    // We got a new command atom.
    int result;
    Atom actual_type;
    int actual_format;
    unsigned long len, bytes_after;
    char* data = 0;

    result = XGetWindowProperty(aDisplay, aWindowId, aChangedAtom,
                                0,                      /* long_offset */
                                (65536 / sizeof(long)), /* long_length */
                                X11True,        /* atomic delete after */
                                XA_STRING,      /* req_type */
                                &actual_type,   /* actual_type return */
                                &actual_format, /* actual_format_return */
                                &len,           /* nitems_return */
                                &bytes_after,   /* bytes_after_return */
                                (unsigned char**)&data); /* prop_return
                                                            (we only care
                                                            about the first ) */

    // Failed to get property off the window or
    // got a part only
    if (result != Success || bytes_after != 0) {
      return false;
    }

    // Failed to get the data off the window or it was the wrong type?
    if (!data) {
      return false;
    }

    // cool, we got the property data.
    const char* response = HandleCommandLine(Span(data, len), aEventTime);

    // put the property onto the window as the response
    XChangeProperty(aDisplay, aWindowId, sMozResponseAtom, XA_STRING, 8,
                    PropModeReplace, (const unsigned char*)response,
                    strlen(response));
    XFree(data);
    return true;
  }

  if (aChangedAtom == sMozResponseAtom) {
    // client accepted the response.  party on wayne.
    return true;
  }

  else if (aChangedAtom == sMozLockAtom) {
    // someone locked the window
    return true;
  }

  return false;
}

void nsXRemoteServer::EnsureAtoms(void) {
  if (sMozVersionAtom) return;

  XInternAtoms(mozilla::DefaultXDisplay(), const_cast<char**>(XAtomNames),
               std::size(XAtomNames), X11False, XAtoms);

  int i = 0;
  sMozVersionAtom = XAtoms[i++];
  sMozLockAtom = XAtoms[i++];
  sMozResponseAtom = XAtoms[i++];
  sMozUserAtom = XAtoms[i++];
  sMozProfileAtom = XAtoms[i++];
  sMozProgramAtom = XAtoms[i++];
  sMozCommandLineAtom = XAtoms[i];
}
